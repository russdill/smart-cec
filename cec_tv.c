/*
 * Copyright (C) 2016 Russ Dill <russ.dill@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stddef.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>

#include "avr-cec/time.h"
#include "avr-cec/cec_msg.h"
#include "avr-cec/cec.h"
#include "avr-cec/cec_spec.h"
#include "usi_uart.h"
#include "lgtv_keys.h"

enum tv_state {
	TV_OFF,
	TV_POWER_OFF,
	TV_POWERING_OFF,
	TV_POWER_UP,
	TV_POWERING_UP,
	TV_ON,
};

enum new_source_state {
	NEW_SOURCE_IDLE,
	NEW_SOURCE_PING,
	NEW_SOURCE_PICK,
	NEW_SOURCE_LOGICAL,
	NEW_SOURCE_PHYS,
};

static signed char timeouts[6];

/* Key repeart timeout, initial repeat 500ms, subsequent 100ms */
#define repeat_timeout		timeouts[0]

/* Controls how often to send serial commands/queries to the TV */
#define tv_query_timeout	timeouts[1]

/* New source state machine timeout */
#define new_source_timeout	timeouts[2]

/*
 * How long to wait for all routing change messages to be received before
 * acting on them.
 */
#define routing_change_timeout	timeouts[3]

/* Discard partial messages from the TV after the serial timeout */
#define serial_rx_timeout	timeouts[4]

#define serial_tx_timeout	timeouts[5]

/* Queue of messages that require direct replies */
static unsigned char recv_pend_cnt;
static unsigned char recv_pend[8*2];

/* Remote keycode to send to the TV */
static unsigned char serial_key_code;

/* Translated CEC keycode to send to the active source */
static unsigned char cec_ui_command;

/* Current power state of the TV */
static unsigned char tv_state;

/* Bitmap of present CEC addresses */
static unsigned short source_present;

/* Next source to test */
static unsigned char next_source;

/* The CEC address of our current source */
register unsigned char tv_logical_source asm("r4");

/* State machine for searching for a new source */
register unsigned char new_source_state asm("r3");

/* The physical address of our current source */
static unsigned short tv_phys_source;

/*
 * The physical address of the new source, subject to change as switches
 * react to the routing change messages.
 */
static unsigned short new_routing_phys;

/* Deck command to send to the current active source */
static unsigned char deck_cmd;

/* Tracking of serial reply from TV */
static unsigned char serial_pos;
static unsigned char serial_code;
static unsigned char serial_ack1;
static unsigned char serial_ack2;
static unsigned char serial_resp;

/* We need to send a message to the TV to indicate the current input */
#define FLAG0_SEND_PHYS_SOURCE_SER	0

/*
 * We need send a CEC set stream path message to indicate a new physical
 * source
 */
#define FLAG0_SEND_PHYS_SOURCE_CEC	1

/*
 * A routing change message was received, process it after the routing
 * change timeout
 */
#define FLAG0_ROUTING_CHANGE		2

/* Send a CEC button press release message */
#define FLAG0_CEC_RELEASE		3

/* Send a CEC button press message */
#define FLAG0_CEC_UI_COMMAND		4

/* Notify active source that TV is turning off */
#define FLAG0_ACTIVE_SOURCE		5

/* Current key for TV should repeat every 100ms */
#define FLAG0_KEY_REPEAT		6

/* Current key for TV should send just once */
#define FLAG0_KEY_ONCE			7

/* Broadcast a CEC menu language message */
#define FLAG1_MENU_LANG			0

/* Broadcast a current physical address message */
#define FLAG1_GIVE_PHYS			1

/* Pause a little bit between serial messages */
#define FLAG1_NEEDS_TX_PAUSE		2

/* New serial byte from the TV */
static bool usi_uart_process_byte(void)
{
	unsigned char byte;
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		if (!ser_recv_ready)
			return false;

		byte = ser_recv_byte;
		ser_recv_ready = false;
	}

	/* Been to long since last byte, this is a new message */
	if (serial_rx_timeout < 0)
		serial_pos = 0;

	serial_rx_timeout = MS_TO_LJIFFIES_UP(100);

	/* Final byte is always x */
	if (byte == 'x') {
		if (serial_pos == 7)
			serial_resp = '0';
		serial_pos = 0;
		return true;
	}

	/*
	 * Format of LG messages:
	 *
	 * [Command2][ ][Set ID][ ][OK][Data][x]
	 * [Command2][ ][Set ID][ ][NG][x]
	 *
	 * m 01 OK00x
	 * m 01 NGx
	 * 012345678
	 */
	if (serial_pos == 0)
		serial_code = byte;
	else if (serial_pos == 5)
		serial_ack1 = byte;
	else if (serial_pos == 6)
		serial_ack2 = byte;
	else if (serial_pos == 8)
		serial_resp = byte;

	serial_pos++;

	return true;
}

/* Process complete serial message from TV */
static void lg_response(void)
{
	if (serial_code != 'm')
		return;

	/*
	 * The TV always responds to a power query with zero. However, it will
	 * respond to a remotelock query with NG when off and OK when on.
	 */
	if (serial_ack1 == 'O' && serial_ack2 == 'K') {
		/* OK, TV is on */
#ifdef CEC_TV_LOCK
		/* Lock check */
		if (tv_state == TV_POWERING_UP)
			tv_state = serial_resp == '1' ? TV_SCAN : TV_DO_LOCK;
#endif
		if (tv_state != TV_POWERING_OFF && tv_state != TV_POWER_OFF) {
			if (tv_state == TV_OFF && tv_logical_source) {
				GPIOR0 |= _BV(FLAG0_SEND_PHYS_SOURCE_SER);
				GPIOR0 |= _BV(FLAG0_SEND_PHYS_SOURCE_CEC);
			}
			tv_state = TV_ON;
		}
	} else if (serial_ack1 == 'N' && serial_ack2 == 'G') {
		/* NG, TV is off */
		if (tv_state != TV_POWERING_UP && tv_state != TV_POWER_UP) {
			if (tv_state == TV_ON)
				GPIOR0 |= _BV(FLAG0_ACTIVE_SOURCE);
			tv_state = TV_OFF;
		}
	}
}

/* Periodic things that need to send on the serial port */
static bool cec_tv_periodic_serial_tx(void)
{
	unsigned char cmd1, cmd2, code;

	if (!usi_uart_write_empty())
		return false;

	if (GPIOR1 & _BV(FLAG1_NEEDS_TX_PAUSE))
		serial_tx_timeout = MS_TO_LJIFFIES_UP(5);
	GPIOR1 &= ~_BV(FLAG1_NEEDS_TX_PAUSE);

	if (serial_tx_timeout >= 0)
		return false;

	/* Pass through keypresses */
	if (GPIOR0 & _BV(FLAG0_KEY_ONCE)) {
		GPIOR0 &= ~_BV(FLAG0_KEY_ONCE);
		goto send_key;
	}

	/* Volume up/down repeat */
	if ((GPIOR0 & _BV(FLAG0_KEY_REPEAT)) && repeat_timeout < 0) {
		repeat_timeout = MS_TO_LJIFFIES_UP(100);
send_key:
		cmd1 = 'm';
		cmd2 = 'c';
		code = serial_key_code;

		/*
		 * Argh....sometimes sending mute twice pops up a menu.
		 * Sending an extra "any" key between mute presses avoids
		 * this.
		 */
		if (code == KEY_MUTE) {
			serial_key_code = KEY_RIGHT;
			GPIOR0 |= _BV(FLAG0_KEY_ONCE);
		}
		goto send1;
	}

	/* Change the TV input */
	if ((GPIOR0 & _BV(FLAG0_SEND_PHYS_SOURCE_SER)) && tv_state == TV_ON) {
		GPIOR0 &= ~_BV(FLAG0_SEND_PHYS_SOURCE_SER);

		/* Input select */
		cmd1 = 'x';
		cmd2 = 'b';
		/* 4 nibble physical address, we want the first nibble */
		code = tv_phys_source >> 12;
		code += 0x90 - 1;
		goto send1;
	}

	/* Periodic requests to TV */
	if (tv_query_timeout > 0)
		return false;

	cmd1 = 'k';
	cmd2 = 'a';
	switch (tv_state) {
	case TV_POWER_UP:
		/*
		 * On powerup, send a request active source message
		 * (if one isn't received?)
		 */
		code = 1;
		tv_state = TV_POWERING_UP;
		break;

	case TV_POWER_OFF:
		code = 0;
		tv_state = TV_POWERING_OFF;
		break;

#if CEC_TV_LOCK
	case TV_DO_LOCK:
		/* Need to lock */
		usi_uart_write("km 01 01\r", 9);
		tv_state = TV_POWERING_UP;
		break;
#endif
	default:
		if (tv_state == TV_POWERING_UP)
			tv_state = TV_POWER_UP;
		else if (tv_state == TV_POWERING_OFF)
			tv_state = TV_POWER_OFF;
		cmd2 = 'm';
		code = 0xff;
	}

	tv_query_timeout = MS_TO_LJIFFIES_UP(1000);
send1:
	usi_uart_put(cmd1);
	usi_uart_put(cmd2);
	usi_uart_put(' ');
	usi_uart_put('0');
	usi_uart_put('1');
	usi_uart_put(' ');
	usi_uart_num(code >> 4);
	usi_uart_num(code & 0xf);
	usi_uart_put('\r');
	GPIOR1 |= _BV(FLAG1_NEEDS_TX_PAUSE);
	return true;
}

/* Called by IR subsystem when a button release occurs */
IR_NEC_PUBLIC void ir_nec_release(void)
{
	if (GPIOR0 & _BV(FLAG0_CEC_UI_COMMAND)) {
		GPIOR0 &= ~_BV(FLAG0_CEC_UI_COMMAND);
		GPIOR0 |= _BV(FLAG0_CEC_RELEASE);
	}
	GPIOR0 &= ~_BV(FLAG0_KEY_ONCE);
	GPIOR0 &= ~_BV(FLAG0_KEY_REPEAT);
}

/* Periodically check if there is a new IR button press */
static bool ir_nec_press_periodic(void)
{
	unsigned char code;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		if (!ir_nec_ready)
			return false;
		ir_nec_ready = false;
		if (ir_nec_output[0] != 4)
			return false;
		code = ir_nec_output[1];
	}

	ir_nec_release();

	switch (code) {
	case KEY_POWER:
		if (tv_state == TV_ON) {
			tv_state = TV_POWER_OFF;
			GPIOR0 |= _BV(FLAG0_ACTIVE_SOURCE);
		} else if (tv_state == TV_OFF) {
			tv_state = TV_POWER_UP;
			if (tv_logical_source) {
				GPIOR0 |= _BV(FLAG0_SEND_PHYS_SOURCE_SER);
				GPIOR0 |= _BV(FLAG0_SEND_PHYS_SOURCE_CEC);
			}
		}
		break;
	}

	/* Ignore other remote keys when off */
	if (tv_state != TV_ON)
		return true;

	serial_key_code = code;

	switch (code) {
	case KEY_INPUT:
		/* Next input */
		if (new_source_state == NEW_SOURCE_IDLE)
			new_source_state = NEW_SOURCE_PICK;
		break;

	case KEY_VOL_UP:
	case KEY_VOL_DOWN:
		repeat_timeout = MS_TO_LJIFFIES_UP(500);
		GPIOR0 |= _BV(FLAG0_KEY_REPEAT);
		/* Fall-through for KEY_ONCE */

	case KEY_MUTE:
		GPIOR0 |= _BV(FLAG0_KEY_ONCE);
		break;

	/* Deck Control */
	case KEY_PLAY:
		deck_cmd = CEC_MSG_PLAY_MODE_PLAY_FORWARD;
		break;

	case KEY_PAUSE:
		deck_cmd = CEC_MSG_PLAY_MODE_PLAY_STILL;
		break;

	case KEY_FF:
		deck_cmd = CEC_MSG_PLAY_MODE_FAST_FORWARD_MEDIUM_SPEED;
		break;

	case KEY_FR:
		deck_cmd = CEC_MSG_PLAY_MODE_FAST_REVERSE_MEDIUM_SPEED;
		break;

	case KEY_STOP:
		deck_cmd = CEC_MSG_DECK_CONTROL_MODE_STOP;
		break;

	case KEY_GOTO_PREV:
		deck_cmd = CEC_MSG_DECK_CONTROL_MODE_SKIP_REVERSE;
		break;

	case KEY_GOTO_NEXT:
		deck_cmd = CEC_MSG_DECK_CONTROL_MODE_SKIP_FORWARD;
		break;

	case KEY_MC_EJECT:
		deck_cmd = CEC_MSG_DECK_CONTROL_MODE_EJECT;
		break;

	default:
		/* Remote Control Pass Through */
		if (!tv_logical_source)
			break;

		/* Lookup the translated CEC key in the EEPROM */
		EEAR = code + 0x10;
		EECR |= _BV(EERE);
		cec_ui_command = EEDR;
		if (cec_ui_command == 0xff)
			return true;
		GPIOR0 |= _BV(FLAG0_CEC_UI_COMMAND);
		repeat_timeout = 0;
	}

	return true;
}

/* Periodic things that need to send CEC messages */
static bool cec_tv_periodic_cec_tx(void)
{
	unsigned char end;
	unsigned char *buf;

	/*
	 * Convince GCC to let us use indirect addressing. This lets us use 2
	 * byte opcodes to access memory rather than 4.
	 */
	asm("ldi %A0, lo8(transmit_buf)\n"
	    "ldi %B0, hi8(transmit_buf)\n" : "=b"(buf));

	if (transmit_state >= TRANSMIT_PEND)
		return false;

	/* Handle message replies that get sent back to the initiator */
	if (recv_pend_cnt) {
		unsigned char *pend_buf;
		unsigned char source;
		unsigned char opcode;

		recv_pend_cnt -= 2;
		pend_buf = recv_pend + recv_pend_cnt;
		source = *pend_buf++;
		opcode = *pend_buf++;

		buf[0] = source;
		switch (opcode) {
		case CEC_MSG_GET_CEC_VERSION:
			/* Send CEC_MSG_CEC_VERSION <version> */
			buf[1] = CEC_MSG_CEC_VERSION;
			buf[2] = CEC_MSG_CEC_VERSION_1_4;
			end = 2;
			break;

		case CEC_MSG_GIVE_DEVICE_POWER_STATUS:
			buf[1] = CEC_MSG_REPORT_POWER_STATUS;
			if (tv_state == TV_ON)
				buf[2] = CEC_MSG_POWER_STATUS_ON;
			else if (tv_state == TV_OFF)
				buf[2] = CEC_MSG_POWER_STATUS_STANDBY;
			else if (tv_state >= TV_POWER_UP)
				buf[2] = CEC_MSG_POWER_STATUS_2ON;
			else
				buf[2] = CEC_MSG_POWER_STATUS_2STANDBY;
			end = 2;
			break;

		default:
			/* Send CEC_MSG_ABORT <unk opcode> */
			buf[1] = CEC_MSG_FEATURE_ABORT;
			buf[2] = opcode;
			buf[3] = CEC_MSG_ABORT_REASON_OPCODE;
			end = 3;
		}

		goto xmit;
	}

	buf[0] = CEC_ADDR_BROADCAST;

	/* Broadcast message replies */
	if (GPIOR1 & _BV(FLAG1_MENU_LANG)) {
		GPIOR1 &= ~_BV(FLAG1_MENU_LANG);

		buf[1] = CEC_MSG_SET_MENU_LANGUAGE;
		buf[2] = 'e';
		buf[3] = 'n';
		buf[4] = 'g';
		end = 4;
		goto xmit;
	}

	if (GPIOR1 & _BV(FLAG1_GIVE_PHYS)) {
		GPIOR1 &= ~_BV(FLAG1_GIVE_PHYS);

		buf[1] = CEC_MSG_REPORT_PHYSICAL_ADDRESS;
		buf[2] = 0;
		buf[3] = 0;
		buf[4] = CEC_MSG_DEVICE_TYPE_TV;
		end = 4;
		goto xmit;
	}

	/* Pending button release */
	if (GPIOR0 & _BV(FLAG0_CEC_RELEASE)) {
		GPIOR0 &= ~_BV(FLAG0_CEC_RELEASE);
		if (tv_logical_source) {
			buf[0] = tv_logical_source;
			buf[1] = CEC_MSG_USER_CONTROL_RELEASED;
			end = 1;
			goto xmit;
		}
	}

	/* Pending button repeat */
	if ((GPIOR0 & _BV(FLAG0_CEC_UI_COMMAND)) && repeat_timeout <= 0) {
		if (!tv_logical_source)
			GPIOR0 &= ~_BV(FLAG0_CEC_UI_COMMAND);
		else {
			repeat_timeout = MS_TO_LJIFFIES_UP(400);

			buf[0] = tv_logical_source;
			buf[1] = CEC_MSG_USER_CONTROL_PRESSED;
			buf[2] = cec_ui_command;
			end = 2;
			goto xmit;
		}
	}

	if (deck_cmd) {
		buf[0] = tv_logical_source;
		if (deck_cmd > CEC_MSG_DECK_CONTROL_MODE_EJECT)
			buf[1] = CEC_MSG_PLAY;
		else
			buf[1] = CEC_MSG_DECK_CONTROL;
		buf[2] = deck_cmd;
		deck_cmd = 0;
		end = 2;
		if (tv_logical_source)
			goto xmit;
	}

	if (GPIOR0 & _BV(FLAG0_ACTIVE_SOURCE)) {
		/* Notify current active source tv is turning off */
		GPIOR0 &= ~_BV(FLAG0_ACTIVE_SOURCE);

		buf[1] = CEC_MSG_ACTIVE_SOURCE;
		buf[2] = 0;
		buf[3] = 0;
		end = 3;
		goto xmit;

	} else if (GPIOR0 & _BV(FLAG0_SEND_PHYS_SOURCE_CEC)) {
		GPIOR0 &= ~_BV(FLAG0_SEND_PHYS_SOURCE_CEC);

		buf[1] = CEC_MSG_SET_STREAM_PATH;
		buf[2] = tv_phys_source >> 8;
		buf[3] = tv_phys_source;
		end = 3;
		goto xmit;

	} else if (new_source_state == NEW_SOURCE_LOGICAL) {
		next_source++;
		if (next_source == CEC_ADDR_BROADCAST)
			next_source = 1;
		if (source_present & (1 << next_source)) {
			new_source_timeout = MS_TO_LJIFFIES_UP(300);
			new_source_state = NEW_SOURCE_PHYS;

			buf[0] = next_source;
			buf[1] = CEC_MSG_GIVE_PHYSICAL_ADDRESS;
			end = 1;
			goto xmit;
		}

	} else if (new_source_state == NEW_SOURCE_PING) {
		new_source_timeout = MS_TO_LJIFFIES_UP(1000);
		new_source_state = NEW_SOURCE_IDLE;

		buf[0] = next_source;
		end = 0;
		goto xmit;
	}
	return true;

xmit:
	transmit_state = TRANSMIT_PEND;
	transmit_buf_end = end;
	return true;
}

/* Messages directly addressed to us */
static void cec_tv_process_cec_rx_direct(unsigned char source, unsigned char len)
{
	switch (cec_receive_buf[2]) {
	/* One Touch Play */
	case CEC_MSG_IMAGE_VIEW_ON:
	case CEC_MSG_TEXT_VIEW_ON:
		/* Make sure TV is on */
		if (tv_state < TV_POWER_UP)
			tv_state = TV_POWER_UP;
		break;

	/* Routing Control */
	case CEC_MSG_INACTIVE_SOURCE:
		/* Physical address */
		/* Current source has entered standby */
		/* Set stream path for next source */
		if (tv_logical_source == source)
			new_source_state = NEW_SOURCE_PICK;
		break;

	case CEC_MSG_REPORT_POWER_STATUS:
		break;

	case CEC_MSG_GET_MENU_LANGUAGE:
		GPIOR1 |= _BV(FLAG1_MENU_LANG);
		break;

	case CEC_MSG_GIVE_PHYSICAL_ADDRESS:
		GPIOR1 |= _BV(FLAG1_GIVE_PHYS);
		break;

	case CEC_MSG_FEATURE_ABORT:
		break;

	case CEC_MSG_VENDOR_COMMAND:
		if (len == 16 && cec_receive_buf[16] == 0xb1) {
			/* Enter bootloader */
			wdt_enable(0);
			for(;;);
		}
		/* Fall-through */

	default:
		/* Don't go any further if the initiator is unassigned */
		if (source != CEC_ADDR_UNREGISTERED) {
			unsigned char *buf;
			buf = recv_pend + recv_pend_cnt;
			recv_pend_cnt += 2;
			*buf++ = source;
			*buf++ = cec_receive_buf[2];
		}
	}
}

/* Messages sent to the broadcast address */
static void cec_tv_process_cec_rx_bcast(unsigned char source, unsigned char len)
{
	switch (cec_receive_buf[2]) {
	case CEC_MSG_ROUTING_CHANGE:
		/* bcast, Old physical address, new physical address */
		/* CEC switch changed due to button press */

		new_routing_phys = cec_receive_buf[6] |
					(cec_receive_buf[5] << 8);
		routing_change_timeout = MS_TO_LJIFFIES_UP(200);
		GPIOR0 |= _BV(FLAG0_ROUTING_CHANGE);
		break;

	case CEC_MSG_ROUTING_INFORMATION:
		/* bcast, New physical address */
		/* Lower CEC switch responded to routing change */
		/* After routing change/routing formation is complete, set stream path */
		/* Must wait 7 nominal data bit periods, up to 500ms  */

		new_routing_phys = cec_receive_buf[4] |
					(cec_receive_buf[3] << 8);
		routing_change_timeout = MS_TO_LJIFFIES_UP(200);
		GPIOR0 |= _BV(FLAG0_ROUTING_CHANGE);
		break;

	case CEC_MSG_REPORT_PHYSICAL_ADDRESS:
		/* Bcast, physical address, device type */
		/* Store physical address of device */
		/* May poll with CEC_MSG_GIVE_PHYSICAL_ADDRESS */
		if (len < 5)
			break;
		if (next_source != source || new_source_state != NEW_SOURCE_PHYS)
			break;

		tv_logical_source = source;
		GPIOR0 &= ~_BV(FLAG0_ROUTING_CHANGE);

		if (tv_state >= TV_POWER_UP) {
			GPIOR0 |= _BV(FLAG0_SEND_PHYS_SOURCE_SER);
			GPIOR0 |= _BV(FLAG0_SEND_PHYS_SOURCE_CEC);
		}

		new_source_state = NEW_SOURCE_IDLE;
		tv_phys_source = cec_receive_buf[4] | (cec_receive_buf[3] << 8);
		break;

	case CEC_MSG_ACTIVE_SOURCE:
		/* bcast, Physical address */
		/* Perform switch to given source */
		if (len < 4)
			/* Abort */
			break;

		tv_logical_source = source;
		GPIOR0 |= _BV(FLAG0_SEND_PHYS_SOURCE_SER);
		GPIOR0 &= ~_BV(FLAG0_ROUTING_CHANGE);
		new_source_state = NEW_SOURCE_IDLE;
		tv_phys_source = cec_receive_buf[4] | (cec_receive_buf[3] << 8);

		if (tv_state < TV_POWER_UP)
			tv_state = TV_POWER_UP;
		break;
	}
}

/* Process received CEC messages */
static bool cec_tv_process_cec_rx(void)
{
	unsigned char len;
	unsigned char source;
	unsigned char target;

	len = cec_receive_buf[0];
	cec_receive_buf[0] = 0;

	if (!len)
		return false;

	/* Ignore packets with errors */
	if (len & 0xc0)
		return false;

	/* Buffer full, start dropping things */
	if (recv_pend_cnt == sizeof(recv_pend))
		return false;

	source = cec_receive_buf[1] >> 4;
	target = cec_receive_buf[1] & 0xf;

	/* We now know this source is present */
	source_present |= 1 << source;

	/* Ignore empty messages and messages from us */
	if (len < 2 || cec_addr_match(source))
		return false;

	if (target == CEC_ADDR_BROADCAST)
		cec_tv_process_cec_rx_bcast(source, len);
	else if (cec_addr_match(target))
		cec_tv_process_cec_rx_direct(source, len);

	return true;
}

CEC_TV_PUBLIC void cec_tv_periodic(unsigned char delta_long)
{
	unsigned char i;

	/* Update timeouts */
	for (i = 0; i < sizeof(timeouts); i++) {
		if (timeouts[i] >= 0)
			timeouts[i] -= delta_long;
	}

	/* Check for nacks/acks */
	if (transmit_buf[0] && transmit_state < TRANSMIT_PEND) {
		unsigned char target = transmit_buf[0] & 0xf;
		if (transmit_state == TRANSMIT_FAILED) {
			/* This source clearly isn't there */
			source_present &= ~(1 << target);
			if (target == tv_logical_source)
				/* It was our active source, pick a new one */
				new_source_state = NEW_SOURCE_PICK;
		} else
			source_present |= 1 << target;
		transmit_buf[0] = 0;
		return;
	}

	/* We waited long enough for all routing changes messages, act on them */
	if (routing_change_timeout < 0 && (GPIOR0 & _BV(FLAG0_ROUTING_CHANGE))) {
		GPIOR0 &= ~_BV(FLAG0_ROUTING_CHANGE);

		if (tv_phys_source != new_routing_phys) {
			tv_phys_source = new_routing_phys;
			tv_logical_source = 0;
			GPIOR0 |= _BV(FLAG0_SEND_PHYS_SOURCE_SER);
			GPIOR0 |= _BV(FLAG0_SEND_PHYS_SOURCE_CEC);
		}

		new_source_state = NEW_SOURCE_IDLE;
		return;
	}

	if (new_source_timeout < 0) {
		/* Advance the new source state machine */
		if (new_source_state == NEW_SOURCE_PHYS) {
			/* Our query never came back */
			new_source_state = NEW_SOURCE_LOGICAL;
			source_present &= ~(1 << next_source);
		} else if (new_source_state == NEW_SOURCE_IDLE) {
			next_source++;
			if (next_source == CEC_ADDR_BROADCAST)
				next_source = 1;
			new_source_state = NEW_SOURCE_PING;
		}
	}

	if (new_source_state == NEW_SOURCE_PICK) {
		/* Restart the new source state machine */
		next_source = tv_logical_source;
		tv_logical_source = 0;
		new_source_state = NEW_SOURCE_LOGICAL;
	}

	/* Check for complete message from TV */
	if (serial_resp) {
		lg_response();
		serial_resp = 0;
		return;
	}

	/* Handle received serial bytes */
	if (usi_uart_process_byte())
		return;

	/* Handle received IR button presses */
	if (ir_nec_press_periodic())
		return;

	/* Handle incoming CEC messages */
	if (cec_tv_process_cec_rx())
		return;

	/* Handle any events that require us to send serial bytes */
	if (cec_tv_periodic_serial_tx())
		return;

	/* Handle any events that require us to send CEC messages */
	if (cec_tv_periodic_cec_tx())
		return;
}

