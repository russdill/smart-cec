#define __SFR_OFFSET 0

#include <avr/io.h>
#include <avr/iotn45.h>

#include "time.h"

#define __zero_reg__ r1

			     /*   low  /  high */
#define NEC_START	0xf7 /*  9.00ms/4.50ms */
#define NEC_REPEAT	0xf3 /*  9.00ms/2.25ms */
#define NEC_0		0x00 /* 565.5uS/565.5uS */
#define NEC_1		0x02 /* 565.5uS/1687.5uS */

/* 565.5uS */
#define NEC_PERIOD_1	NS_TO_JIFFIES_RND(565500)
#define NEC_PERIOD_0_6	NS_TO_JIFFIES_RND(565500 * 6 / 10)
#define NEC_PERIOD_1_4	NS_TO_JIFFIES_RND(565500 * 14 / 10)


	.section	.bss.ir_nec_isr, "aw", @nobits
ir_nec_last_time:
	.zero	2
ir_nec_byte:
	.zero	1
ir_nec_bit:
	.zero	1
ir_nec_byte_pos:
	.zero	1
ir_nec_buf:
	.zero	2
	.global ir_nec_last_pins
ir_nec_last_pins:
	.zero	1

	.section	.text.__vector_1, "ax", @progbits
.global	__vector_1
	.type	__vector_1, @function
__vector_1:

	push r1
	in r1, SREG
	push r1
	clr __zero_reg__
	push r19
	push r20
	push r21
	push r22
	push r23
	push r24

	/* GIMSK &= ~_BV(INT0); */
	/* Clear the interrupt enable so we don't re-enter */
	in r22, GIMSK
	cbr r22, _BV(INT0)
	out GIMSK, r22

	/* Let other interrupts run, FIXME: Move to after jiffies call */
	sei

	/*
	 * Our state machine assumes a transition has occurred, but a fast
	 * toggle can cause an interrupt but we still have the same state.
	 * Ignore this case.
	 */

	/* pins = PINB & _BV(PB2) */
	in r19, PINB

	andi r19, _BV(PB2)
	bst r19, PB2

	/* if (pins == ir_nec_last_pins) goto out_label */
	lds r22, ir_nec_last_pins
	cp r19, r22
	breq out_label_trampoline

	/* ir_nec_last_pins = pins */
	sts ir_nec_last_pins, r19

	/* j = jiffies() */
	/* Clobbers: r19-r24, returns r22-r24, requires r1 = 0 */
	rcall jiffies

	/* ir_nec_last_time */
	lds r20, ir_nec_last_time
	lds r21, ir_nec_last_time+1

	/* ir_nec_last_time = j */
	sts ir_nec_last_time+1, r23
	sts ir_nec_last_time, r22

	/* delta = j - ir_nec_last_time */
	sub r22, r20
	sbc r23, r21

 	/* for (period = 0; */
	clr r24
	ldi r19, hi8(NEC_PERIOD_1_4)
period_loop:
	/* if (delta < NS_TO_JIFFIES_UP(NEC_PERIOD * 14ULL) / 10) goto 1f */
	cpi r22, lo8(NEC_PERIOD_1_4)
	cpc r23, r19
	brlo 1f

	/* delta -= NS_TO_JIFFIES_RND(NEC_PERIOD) */
	subi r22, lo8(NEC_PERIOD_1)
	sbci r23, hi8(NEC_PERIOD_1)

	/* ; period < 16; period++) */
	subi r24, -1
	brhs period_loop

	/* if (delta < NS_TO_JIFFIES(NEC_PERIOD * 6ULL) / 10) protocol_error*/
1:	cpi r22, lo8(NEC_PERIOD_0_6)
	ldi r19, hi8(NEC_PERIOD_0_6)
	cpc r23, r19
	brlo error_label

	/* if (ir_nec_last_pins == 0) goto 1f */
	brtc 1f

	/* ir_nec_bit = periods << 4 */
	swap r24
	sts ir_nec_bit, r24

	/* goto out_label */
out_label_trampoline:
	rjmp out_label



	/* ir_nec_bit = ir_nec_bit | period */
1:	lds r19, ir_nec_bit
	or r24, r19

	/* if (ir_nec_bit == NEC_START) goto NEC_START_label */
	cpi r24, NEC_START /* NEC_START */
	breq NEC_START_label

	/* if (NEC_0 or NEC_1) goto NEC_0_1_label */
	cpi r24, NEC_1
	breq NEC_0_1_label

	cpi r24, NEC_0
	breq NEC_0_1_label

	cpi r24, NEC_REPEAT
	brne error_label

	/* NEC_REPEAT */
	sts ir_nec_repeat_timer, __zero_reg__

	/* Fall through to reset state machine */

error_label:
	/* Clear byte and state machine */
	sts ir_nec_byte, __zero_reg__
	rjmp out_label

NEC_START_label:
	/* ir_nec_repeat_timer = 0 */
	sts ir_nec_repeat_timer, __zero_reg__

	/* ir_nec_has_last = 0 */
	sts ir_nec_has_last, __zero_reg__

	/* ir_nec_byte_pos = 0 */
	sts ir_nec_byte_pos, __zero_reg__

	/* goto out_label */
	rjmp next_byte



NEC_0_1_label:
	/* Make sure out state machine is running */
	lds r22, ir_nec_byte
	tst r22
	breq out_label

	/* Set carry bit if NEC_1 */
	sbrc r24, 1
	sec

	/* Shift carry bit into ir_nec_byte */
	ror r22
	sts ir_nec_byte, r22

	/* More bits left before we have a full byte */
	brcc out_label

	/* Shifted out last bit, byte done */
	lds r24, ir_nec_byte_pos

	/* if (ir_nec_byte_pos & 1) goto 1f */
	sbrs r24, 0
	rjmp 1f

	/* if (~ir_nec_byte != ir_nec_buf[ir_nec_byte_pos / 2]) goto out_label */
	lds r19, ir_nec_buf
	sbrc r24, 1
	lds r19, ir_nec_buf+1
	com r22
	cpse r22, r19
	rjmp out_label

	/* if (ir_nec_byte_pos != 3) goto 2f */
	cpi r24, 3
	brne 2f

	/* ir_nec_has_last = true */
	ldi r24, 1
	sts ir_nec_has_last, r24

	/* ir_nec_output = ir_nec_buf */
	lds r19, ir_nec_buf
	sts ir_nec_output, r19
	lds r19, ir_nec_buf+1
	sts ir_nec_output+1, r19

	/* ir_nec_ready = true */
	sts ir_nec_ready, r24

	/* goto out_label */
	rjmp out_label

	/* ir_nec_buf[ir_nec_byte_pos / 2] = ir_nec_byte */
1:	sbrs r24, 1
	sts ir_nec_buf, r22
	sbrc r24, 1
	sts ir_nec_buf+1, r22

	/* Move to the next byte */
	/* ir_nec_byte_pos++ */
2:	inc r24
	sts ir_nec_byte_pos, r24

next_byte:
	/* We finish when we shift out this bit */
	ldi r22, _BV(7)
	sts ir_nec_byte, r22

out_label:
	/* GIMSK |= _BV(INT0) */
	in r22, GIMSK
	ori r22, _BV(INT0)
	out GIMSK, r22

	pop r24
	pop r23
	pop r22
	pop r21
	pop r20
	pop r19
	pop r1
	out SREG, r1
	pop r1
	reti

