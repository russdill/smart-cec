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


static signed char timeouts[5];
#define repeat_timeout		timeouts[0]
#define tv_query_timeout	timeouts[1]
#define new_source_timeout	timeouts[2]
#define routing_change_timeout	timeouts[3]
#define serial_timeout		timeouts[4]

static unsigned char serial_key_code;
static unsigned char cec_ui_command;

static unsigned char tv_state;

static unsigned short source_present;
static unsigned char next_source;
register unsigned char tv_logical_source asm("r4");

register unsigned char new_source_state asm("r3");
static unsigned short tv_phys_source;

static unsigned short new_routing_phys;

static unsigned char deck_cmd;

static unsigned char serial_pos;
static unsigned char serial_code;
static unsigned char serial_ack;
static unsigned char serial_resp;

#define TODO_SEND_PHYS_SOURCE_SER	_BV(0)
#define TODO_SEND_PHYS_SOURCE_CEC	_BV(1)
#define TODO_SEND_PHYS_SOURCE		(TODO_SEND_PHYS_SOURCE_CEC|TODO_SEND_PHYS_SOURCE_SER)
#define TODO_ROUTING_CHANGE		_BV(2)

#define TODO_CEC_RELEASE			_BV(3)
#define TODO_CEC_UI_COMMAND		_BV(4)
#define TODO_ACTIVE_SOURCE		_BV(5)
#define TODO_KEY_REPEAT			_BV(6)
#define TODO_KEY_ONCE			_BV(7)

register unsigned char todo asm("r2");

static bool usi_uart_process_byte(void)
{
	unsigned char byte;
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		if (!ser_recv_ready)
			return false;

		byte = ser_recv_byte;
		ser_recv_ready = false;
	}

	if (serial_timeout < 0)
		serial_pos = 0;

	serial_timeout = MS_TO_LJIFFIES_UP(100);

	if (byte == 'x') {
		if (serial_pos == 7)
			serial_resp = '0';
		serial_pos = 0;
		return true;
	}

	/*
	 * [Command2][ ][Set ID][ ][OK][Data][x]
	 * m 01 OK00x
	 * 012345678
	 */
	if (serial_pos == 0)
		serial_code = byte;
	else if (serial_pos == 5)
		serial_ack = byte;
	else if (serial_pos == 8)
		serial_resp = byte;

	serial_pos++;

	return true;
}

static void lg_response(void)
{
	if (serial_code != 'm')
		return;

	/*
	 * The TV always responds to a power query with zero. However, it will
	 * respond to a remotelock query with NG when off and OK when on.
	 */
	if (serial_ack == 'O') {
		/* OK, TV is on */
#ifdef CEC_TV_LOCK
		/* Lock check */
		if (tv_state == TV_POWERING_UP)
			tv_state = serial_resp == '1' ? TV_SCAN : TV_DO_LOCK;
#endif
		if (tv_state != TV_POWERING_OFF && tv_state != TV_POWER_OFF) {
			if (tv_state == TV_OFF && tv_logical_source)
				todo |= TODO_SEND_PHYS_SOURCE;
			tv_state = TV_ON;
		}
	} else {
		/* NG, TV is off */
		if (tv_state != TV_POWERING_UP && tv_state != TV_POWER_UP) {
			if (tv_state == TV_ON)
				todo |= TODO_ACTIVE_SOURCE;
			tv_state = TV_OFF;
		}
	}
}

IR_NEC_PUBLIC void ir_nec_release(void)
{
	if (todo & TODO_CEC_UI_COMMAND) {
		todo &= ~TODO_CEC_UI_COMMAND;
		todo |= TODO_CEC_RELEASE;
	}
	todo &= ~(TODO_KEY_ONCE|TODO_KEY_REPEAT);
}

/* Needs serial port */
static bool cec_tv_periodic_serial_tx(void)
{
	unsigned char cmd1, cmd2, code;

	if (!usi_uart_write_empty())
		return false;

	if (todo & TODO_KEY_ONCE) {
		todo &= ~TODO_KEY_ONCE;
		goto send_key;
	}

	/* Volume up/down repeat */
	if ((todo & TODO_KEY_REPEAT) && repeat_timeout < 0) {
		repeat_timeout = MS_TO_LJIFFIES_UP(100);
send_key:
		cmd1 = 'm';
		cmd2 = 'c';
		code = serial_key_code;
		goto send1;
	}

	if ((todo & TODO_SEND_PHYS_SOURCE_SER) && tv_state == TV_ON) {
		todo &= ~TODO_SEND_PHYS_SOURCE_SER;

		/* Input select */
		cmd1 = 'x';
		cmd2 = 'b';
		/* 4 nibble physical address, we want the first nibble */
		code = tv_phys_source >> 12;
		code += 0x90 - 1;
		goto send1;
	}

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

	tv_query_timeout = MS_TO_LJIFFIES_UP(250);
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
	return true;
}

/* Needs CEC */
static bool cec_tv_periodic_cec_tx(void)
{
	if (transmit_state >= TRANSMIT_PEND)
		return false;

	/* Pending button release */
	if (todo & TODO_CEC_RELEASE) {
		todo &= ~TODO_CEC_RELEASE;
		if (tv_logical_source) {
			transmit_buf[0] = tv_logical_source;
			transmit_buf[1] = CEC_MSG_USER_CONTROL_RELEASED;
			transmit_buf_end = 1;
			goto xmit;
		}
	}

	/* Pending button repeat */
	if ((todo & TODO_CEC_UI_COMMAND) && repeat_timeout <= 0) {
		if (!tv_logical_source)
			todo &= ~TODO_CEC_UI_COMMAND;
		else {
			repeat_timeout = MS_TO_LJIFFIES_UP(400);

			transmit_buf[0] = tv_logical_source;
			transmit_buf[1] = CEC_MSG_USER_CONTROL_PRESSED;
			transmit_buf[2] = cec_ui_command;
			transmit_buf_end = 2;
			goto xmit;
		}
	}

	if (deck_cmd) {
		if (tv_logical_source) {
			transmit_buf[0] = tv_logical_source;
			if (deck_cmd > CEC_MSG_DECK_CONTROL_MODE_EJECT)
				transmit_buf[1] = CEC_MSG_DECK_CONTROL;
			else
				transmit_buf[1] = CEC_MSG_PLAY;
			transmit_buf[2] = deck_cmd;
		}
		deck_cmd = 0;
		goto xmit;
	}

	if (todo & TODO_ACTIVE_SOURCE) {
		/* Notify current active source tv is turning off */
		todo &= ~TODO_ACTIVE_SOURCE;

		transmit_buf[0] = CEC_ADDR_BROADCAST;
		transmit_buf[1] = CEC_MSG_ACTIVE_SOURCE;
		transmit_buf[2] = 0;
		transmit_buf[3] = 0;
		transmit_buf_end = 3;
		goto xmit;

	} else if (todo & TODO_SEND_PHYS_SOURCE_CEC) {
		todo &= ~TODO_SEND_PHYS_SOURCE_CEC;
		transmit_buf[0] = CEC_ADDR_BROADCAST;
		transmit_buf[1] = CEC_MSG_SET_STREAM_PATH;
		transmit_buf[2] = tv_phys_source >> 8;
		transmit_buf[3] = tv_phys_source;
		transmit_buf_end = 3;
		goto xmit;

	} else if (new_source_state == NEW_SOURCE_LOGICAL) {
		next_source++;
		if (next_source == CEC_ADDR_BROADCAST)
			next_source = 1;
		if (source_present & (1 << next_source)) {
			new_source_timeout = MS_TO_LJIFFIES_UP(300);
			new_source_state = NEW_SOURCE_PHYS;

			transmit_buf[0] = next_source;
			transmit_buf[1] = CEC_MSG_GIVE_PHYSICAL_ADDRESS;
			transmit_buf_end = 1;
			goto xmit;
		}

	} else if (new_source_state == NEW_SOURCE_PING) {
		new_source_timeout = MS_TO_LJIFFIES_UP(1000);
		new_source_state = NEW_SOURCE_IDLE;

		transmit_buf[0] = next_source;
		transmit_buf_end = 0;
		goto xmit;
	}
	return true;

xmit:
	transmit_state = TRANSMIT_PEND;
	return true;
}

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
			todo |= TODO_ACTIVE_SOURCE;
		} else if (tv_state == TV_OFF) {
			tv_state = TV_POWER_UP;
			if (tv_logical_source)
				todo |= TODO_SEND_PHYS_SOURCE;
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
		todo |= TODO_KEY_REPEAT;
		/* Fall-through for KEY_ONCE */

	case KEY_MUTE:
		todo |= TODO_KEY_ONCE;
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

		EEAR = code + 0x10;
		EECR |= _BV(EERE);
		cec_ui_command = EEDR;
		if (cec_ui_command == 0xff)
			return true;
		todo |= TODO_CEC_UI_COMMAND;
		repeat_timeout = 0;
	}

	return true;
}

static bool cec_tv_process_cec_rx_direct(unsigned char source, unsigned char len)
{
	/* Directly addressed messages */
	switch (cec_receive_buf[2]) {
	/* One Touch Play */
	case CEC_MSG_IMAGE_VIEW_ON:
	case CEC_MSG_TEXT_VIEW_ON:
		/* Make sure TV is on */
		if (tv_state < TV_POWER_UP)
			tv_state = TV_POWER_UP;
		return false;

	/* Routing Control */
	case CEC_MSG_INACTIVE_SOURCE:
		/* Physical address */
		/* Current source has entered standby */
		/* Set stream path for next source */
		if (tv_logical_source == source)
			new_source_state = NEW_SOURCE_PICK;

		return false;

	case CEC_MSG_REPORT_POWER_STATUS:
		return false;

	/* Protocol */
	case CEC_MSG_FEATURE_ABORT:
		/* Opcode, reason */
		return false;
	}

	if (transmit_state >= TRANSMIT_PEND)
		/* Wait for TX hardware to be free */
		return true;

	/* Messages that get replied to on the broadcast address */
	transmit_buf[0] = CEC_ADDR_BROADCAST;
	switch (cec_receive_buf[2]) {
	case CEC_MSG_GET_MENU_LANGUAGE:
		transmit_buf[1] = CEC_MSG_SET_MENU_LANGUAGE;
		transmit_buf[2] = 'e';
		transmit_buf[3] = 'n';
		transmit_buf[4] = 'g';
		transmit_buf_end = 4;
		goto done;

	case CEC_MSG_GIVE_PHYSICAL_ADDRESS:
		transmit_buf[1] = CEC_MSG_REPORT_PHYSICAL_ADDRESS;
		transmit_buf[2] = 0;
		transmit_buf[3] = 0;
		transmit_buf[4] = CEC_MSG_DEVICE_TYPE_TV;
		transmit_buf_end = 4;
		goto done;
	}

	/* Don't go any further if the initiator is unassigned */
	if (source == CEC_ADDR_UNREGISTERED)
		return false;

	/* Messages that get sent back to the initiator */
	transmit_buf[0] = source;
	switch (cec_receive_buf[2]) {
	case CEC_MSG_GET_CEC_VERSION:
		/* Send CEC_MSG_CEC_VERSION <version> */
		transmit_buf[1] = CEC_MSG_CEC_VERSION;
		transmit_buf[2] = CEC_MSG_CEC_VERSION_1_3A;
		transmit_buf_end = 2;
		break;

	case CEC_MSG_GIVE_DEVICE_POWER_STATUS:
		transmit_buf[1] = CEC_MSG_REPORT_POWER_STATUS;
		if (tv_state == TV_ON)
			transmit_buf[2] = CEC_MSG_POWER_STATUS_ON;
		else if (tv_state == TV_OFF)
			transmit_buf[2] = CEC_MSG_POWER_STATUS_STANDBY;
		else if (tv_state >= TV_POWER_UP)
			transmit_buf[2] = CEC_MSG_POWER_STATUS_2ON;
		else
			transmit_buf[2] = CEC_MSG_POWER_STATUS_2STANDBY;
		transmit_buf_end = 2;
		break;

	case CEC_MSG_VENDOR_COMMAND:
		if (len == 16 && cec_receive_buf[16] == 0xb1) {
			/* Enter bootloader */
			wdt_enable(0);
			for(;;);
		}
		/* Fall throught to feature abort */

	default:
		/* Send CEC_MSG_ABORT <unk opcode> */
		transmit_buf[1] = CEC_MSG_FEATURE_ABORT;
		transmit_buf[2] = cec_receive_buf[2];
		transmit_buf[3] = CEC_MSG_ABORT_REASON_OPCODE;
		transmit_buf_end = 3;
	}

done:
	transmit_state = TRANSMIT_PEND;
	return false;
}

static bool cec_tv_process_cec_rx_bcast(unsigned char source, unsigned char len)
{
	/* Broadcast messages */
	switch (cec_receive_buf[2]) {
	case CEC_MSG_ROUTING_CHANGE:
		/* bcast, Old physical address, new physical address */
		/* CEC switch changed due to button press */

		new_routing_phys = cec_receive_buf[6] |
					(cec_receive_buf[5] << 8);
		routing_change_timeout = MS_TO_LJIFFIES_UP(50);
		todo |= TODO_ROUTING_CHANGE;
		break;

	case CEC_MSG_ROUTING_INFORMATION:
		/* bcast, New physical address */
		/* Lower CEC switch responded to routing change */
		/* After routing change/routing formation is complete, set stream path */
		/* Must wait 7 nominal data bit periods, up to 500ms  */

		new_routing_phys = cec_receive_buf[4] |
					(cec_receive_buf[3] << 8);
		routing_change_timeout = MS_TO_LJIFFIES_UP(50);
		todo |= TODO_ROUTING_CHANGE;
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
		todo &= ~TODO_ROUTING_CHANGE;
		todo |= TODO_SEND_PHYS_SOURCE;
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
		todo |= TODO_SEND_PHYS_SOURCE_SER;
		todo &= ~TODO_ROUTING_CHANGE;
		new_source_state = NEW_SOURCE_IDLE;
		tv_phys_source = cec_receive_buf[4] | (cec_receive_buf[3] << 8);

		if (tv_state < TV_POWER_UP)
			tv_state = TV_POWER_UP;
		break;
	}

	return false;
}

static bool cec_tv_process_cec_rx(void)
{
	unsigned char len;
	unsigned char source;
	unsigned char target;
	bool again = false;

	if (!cec_receive_buf[0])
		return false;

	/* Ignore packets with errors */
	if (cec_receive_buf[0] & 0xc0)
		goto done;

	len = cec_receive_buf[0];

	source = cec_receive_buf[1] >> 4;
	target = cec_receive_buf[1] & 0xf;

	/* We now know this source is present */
	source_present |= 1 << source;

	/* Ignore empty messages and messages from us */
	if (len < 2 || cec_addr_match(source))
		goto done;

	if (target == CEC_ADDR_BROADCAST)
		again = cec_tv_process_cec_rx_bcast(source, len);
	else if (cec_addr_match(target))
		again = cec_tv_process_cec_rx_direct(source, len);

	if (!again) {
done:
		cec_receive_buf[0] = 0;
	}

	return true;
}

CEC_TV_PUBLIC void cec_tv_periodic(unsigned char delta_long)
{
	unsigned int i;
	for (i = 0; i < sizeof(timeouts); i++) {
		if (timeouts[i] >= 0)
			timeouts[i] -= delta_long;
	}

	/* Check for nacks/acks */
	if (transmit_buf[0] && transmit_state < TRANSMIT_PEND) {
		unsigned char target = transmit_buf[0] & 0xf;
		if (transmit_state == TRANSMIT_FAILED) {
			source_present &= ~(1 << target);
			if (target == tv_logical_source)
				new_source_state = NEW_SOURCE_PICK;
		} else
			source_present |= 1 << target;
		transmit_buf[0] = 0;
		return;
	}

	if (routing_change_timeout < 0 && (todo & TODO_ROUTING_CHANGE)) {
		if (tv_phys_source != new_routing_phys) {
			tv_phys_source = new_routing_phys;
			tv_logical_source = 0;
			todo |= TODO_SEND_PHYS_SOURCE;
		}

		todo &= ~TODO_ROUTING_CHANGE;
		new_source_state = NEW_SOURCE_IDLE;
		return;
	}

	if (new_source_timeout < 0) {
		if (new_source_state == NEW_SOURCE_PHYS)
			/* Our query never came back */
			new_source_state = NEW_SOURCE_LOGICAL;
		else if (new_source_state == NEW_SOURCE_IDLE) {
			next_source++;
			if (next_source == CEC_ADDR_BROADCAST)
				next_source = 1;
			new_source_state = NEW_SOURCE_PING;
		}
	}

	if (new_source_state == NEW_SOURCE_PICK) {
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

	/* Routing change information expiration */
	/* After routing change/routing formation is complete, set stream path */
}

