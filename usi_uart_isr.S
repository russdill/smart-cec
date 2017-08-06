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

#define __SFR_OFFSET 0

#include <avr/io.h>
#include <avr/iotn45.h>

#include "time.h"

#define __zero_reg__ r1

#define SEND_IDLE	0
#define SEND_STOP	1
#define SEND_DATA7	2
#define SEND_DATA1	8
#define SEND_DATA0	9
#define SEND_START	10

#define BAUD            9600

/* Get the USI to cycle 4 times per baud division */
#define TCNT_TOT	HZ_TO_JIFFIES_RND(BAUD * 4)
#define TCNT_TOP	(TCNT_TOT - 1)


#if TCNT0_PRESCALER != 8
#warning "jiffies busy wait likely inefficient"
#endif

	.section	.bss.usi_uart_isr, "aw", @nobits
_jiffies:
	.zero	3
usi_uart_next_br:
	.zero	1
.global ser_overflow
ser_overflow:
	.zero	1
send_state:
	.zero	1
send_byte:
	.zero	1
recv_byte:
	.zero	1
recv_tick:
	.zero	1
last_bit:
	.zero	1
.global ser_recv_byte
ser_recv_byte:
	.zero	1
.global ser_recv_ready
ser_recv_ready:
	.zero	1
send_consumer:
	.zero	1

/* Clobbers: r19-r24, returns r22-r24, requires r1 = 0 */

	.section	.text.jiffies, "ax", @progbits
.global jiffies
	.type	jiffies, @function
jiffies:

	/* Atomic lock */
	in r19, SREG
	cli

	/*
	 * Avoid a rollover race. If TCNT0 is at the end of it's count, wait
	 * until it rolls over. This will take up to PRESCALER cycles. If
	 * PRESCALER is 8, then this is short, otherwise it may need to be
	 * modified.
	 */
1:	in r20, TCNT0
	in r21, USISR
	cpi r20, TCNT_TOP
	breq 1b

	lds r22, _jiffies
	lds r23, _jiffies+1
	lds r24, _jiffies+2

	/* Release lock/restore flags */
	out SREG, r19

	/* ret = TCNT + _jiffies */
	add r22, r20
	adc r23, __zero_reg__
	adc r24, __zero_reg__

	/* add = 8 * TCNT_TOT; */
	ldi r19, lo8(TCNT_TOT * 8)
	ldi r20, hi8(TCNT_TOT * 8)

	/* count = (count << 4) ^ 0x80 */
	swap r21
	andi r21, 0xf0
	subi r21, 0x80

mul_loop:
	/* If (count & 0x80) ret += add */
	brpl 1f
	add r22, r19
	adc r23, r20
	adc r24, __zero_reg__

	/* add >>= 1, count <<= 1 */
1:	lsr r20
	ror r19
	lsl r21
	brne mul_loop

done:	ret


	.section	.text.__vector_14,"ax",@progbits
.global	__vector_14
	.type	__vector_14, @function
__vector_14:
	push r1
	in r1, SREG
	push r1
	clr __zero_reg__
	push r24
	push r25
	push r26
	push r30
	push r31

	/* Make sure we don't re-enter */
	cbi USICR, USIOIE

	/* Update jiffies count */
	/* r30 USIBR, 24-26 _jiffies */

	in r30, USIBR
	lds r24, usi_uart_next_br
	out USIBR, r24

	/* If (USISR & 8) ser_overflow = true */
	ldi r24, 1
	sbic USISR, 3
	sts ser_overflow, r24

	/* USISR |= 8 */
1:	sbi USISR, 3

	/* jiffies += 8 * TCNT_TOT */
	lds r24, _jiffies
	lds r25, _jiffies+1
	lds r26, _jiffies+2
	subi r24, lo8(0x10000 - TCNT_TOT * 8)
	sbci r25, hi8(0x10000 - TCNT_TOT * 8)
	sbci r26, -1
	sts _jiffies, r24
	sts _jiffies+1, r25
	sts _jiffies+2, r26

	/* Allow other interrupts to run */
	sei

	/* UART input process bit loop */
	lds r24, last_bit
	lds r25, recv_tick
	lds r26, recv_byte

	/* r30 usibr, r31 bit */

	/* Set carry bit, shifts out when we are done */
	sec
uart_process_bit_loop:
	/* Shift out input bits, C is current bit */
	rol r30
	breq uart_process_done

	/* Roll into r31 (bit) bit << usibr << c */
	clr r31
	rol r31

	/* if (bit != last_bit) */
	cp r31, r24
	breq 1f

	/* Sync to bit edge, recv_tick = 0, last_bit = bit */
	mov r24, r31
	ldi r25, 0xc0 /* recv_tick = -1 */
	/* Fall through to get to uart_process_bit_loop_tail */

	/* recv_tick++ */
1:	subi r25, 0xc0

	/* If recv_tick == 1 */
	brvc uart_process_bit_loop_tail

	/* if (!recv_byte) check for start bit */
	tst r26
	breq 1f

	/* bit >> recv_byte >> c */
	lsr r31
	ror r26

	brcc uart_process_bit_loop

	/* Received full byte */
	/* ser_recv_byte = recv_byte, recv_byte = 0, recv_ready = true */
	sts ser_recv_byte, r26
	clr r26
	ldi r31, 1
	sts ser_recv_ready, r31

	/* Fall through to !recv_bit, will skip as r31 will be 1 */

	/* if (!bit) Found start bit, recv_byte = 0x80 */
1:	sbrs r31, 0
	ldi r26, _BV(7)

uart_process_bit_loop_tail:
	clc
	rjmp uart_process_bit_loop

uart_process_done:
	sts last_bit, r24
	sts recv_tick, r25
	sts recv_byte, r26



	/* Generate next output word */

	lds r31, send_byte
	lds r24, send_state
	/* usi_uart_next_br = 0 */
	ldi r25, 0

	/* mask = 0xf0, generate top 4 bits first */
	ldi r26, 0xf0

	/* r30/r31 send_buf */

generate_output_loop:
	/* If send_state == SEND_IDLE, we need a new byte */
	cpi r24, SEND_IDLE
	brne generate_next_bit

	/* If send_prod == 0, there's no new byte yet, just generate 1's */
	lds r24, send_prod
	tst r24
	brne generate_next_byte

generate_one:
	/* usi_uart_next_br |= mask */
	or r25, r26
	rjmp generate_zero


generate_next_byte:
	/* r31 = send_buf[send_consumer] */
	lds r30, send_consumer
	ldi r31, 0
	subi r30, lo8(-(send_buf))
	sbci r31, hi8(-(send_buf))
	ld r31, Z

	/* send_consumer++ */
	lds r30, send_consumer
	inc r30
	sts send_consumer, r30

	/* if (send_prof == send_consumer) */
	cp r30, r24
	brne 1f

	/* Send complete, reset buffer */
	sts send_prod, __zero_reg__
	sts send_consumer, __zero_reg__

	/* send_state = SEND_DATA0 */
1:	ldi r24, SEND_DATA0

	/* Send the start bit */
	rjmp generate_zero

generate_next_bit:
	/* send_state-- */
	subi r24, 1

	/* Stop bit is always 1 */
	breq generate_one

	/* send_byte >>= 1 */
	lsr r31

	/* if (ret) */
	brcs generate_one

generate_zero:

	/* mask = ~mask, now generate bottom 4 bits */
	com r26

	/* while (mask > 0) */
	brpl generate_output_loop

	sts send_state, r24
	sts send_byte, r31
	sts usi_uart_next_br, r25

	pop r31
	pop r30
	pop r26
	pop r25
	pop r24
	pop r1
	out SREG, r1
	pop r1
	sbi USICR, USIOIE
	reti
