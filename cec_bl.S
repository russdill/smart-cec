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
 *
 * CEC Bootloader.
 *
 * This is a CEC based bootloader. In general, it has a long list of cons and
 * should never be in a consumer device:
 *
 * - It's slow, it takes about 3 minutes to program 4kb.
 * - It's insecure, any CEC device on the bus can reprogram the device.
 * - The only acknowledgement provided is the CEC protocol ack, it otherwise
 *   sends no messages.
 * - It currently only operates on a fixed CEC address of 0 (TV)
 *
 * However, it does offer the advantage of being able to reprogram a device
 * without accessing it and connecting up debug pins.
 *
 * Messages have two general formats, one without data and one with data:
 *
 * src/tgt | op | cmd | crc1 | crc2
 * src/tgt | op | cmd | d0 | d1 | d2 | d3 | d4 | d5 | d6 | d7 | crc1 | crc2
 *
 * Messages have a trailing CRC16, produced with a polynomial of 0x18005 and
 * an initial CRC of 0xffff. If the running CRC value when EOM is received
 * is zero, the message is acked. It is not acked otherwise.
 *
 * The CEC target address must be 0 (TV) and the CEC opcode must be 0x89,
 * vendor command. The available commands are:
 *
 * 0x00: ping - Just acks if the device is present, no action
 * 0x03: erase - Erase the program memory. This also resets the data write
         pointer.
 * 0x05: data write - Write a data block. Data should be sent 8 bytes at a
 *       time starting from address zero.
 * 0x01: run - Exit the bootloader. This should be called once the new
 *       program is written.
 *
 * The host side bootloader programmer should rewrite the vector table reset
 * address to the start address of the bootloader. This allows the bootloader
 * to examine the wakeup reason before executing the user program. If the
 * wakeup reason is watchdog or ext reset, it will instead execute the
 * bootloader code. The bootloader can be exited by power cycling the device
 * or sending the run command.
 *
 * The total size of the bootloader is 0x128 bytes. Given 64 byte erase
 * blocks, it takes up 5 erase blocks. These 5 erase blocks should be placed
 * at the end of flash, for a 4kb device, that means the bootloader address
 * should be 0xec0.
 */

#define __SFR_OFFSET 0

#include <avr/io.h>
#include <avr/iotn45.h>

#include "cec_spec.h"
#include "div.h"

#define PAGESIZE	SPM_PAGESIZE

#define CEC_DDR		DDRB
#define CEC_PIN		PINB
#define CEC_PORT	PORTB
#define CEC_PBIN	PB3
#define CEC_PBOUT	PB4

/* Our delay loop is three cycles per iteration, 256 iterations */
#define US_PER_S	1000000
#define US_TO_TICKS(us) DIV_ROUND((us) * _F_CPU, 256 * 3 * US_PER_S)


/*
 * r0/r1 scratch for flash programming
 * r2 eom
 * r3 zero reg
 * r4 ack
 * r5 bit
 * r6 scratch (crc16)
 * r16 tick counter
 * r17 byte
 *                 0     1     2     3     4     5     6     7     8    9
 * r18 bit_state  {bit7, bit6, bit5, bit4, bit3, bit2, bit1, bit0, eom, ack}
 *               -3             -2      -1   0   1   .... 8     9
 * r19 byte_idx {source/target, opcode, cmd, d0, d1, ..., crc1, crc2}
 * r20 cmd {[3] = erase, [5] = data write, [1] = run, [0] = "ping"}
 * r22/r23 last data buffer pointer
 * r24/r25 scratch
 * r26/r27 crc
 * r28/r29 data buffer
 * r30/r31 flash pointer
 */

/*
 * Align our data buffer to 0x100 to make it easy to check it's length and
 * reset it.
 */
	.section	.bss
	.balign		0x100
buf:
	.zero		PAGESIZE

        .section        .text.bl, "ax", @progbits
main:

	/* Record wakeup reason */
	in	r24, MCUSR
	andi	r24, ~(_BV(WDRF) | _BV(EXTRF))

	/* Clear wakeup reason */
	clr	r3
	out	MCUSR, r3

	/* Disable watchdog */
	ldi	r25, _BV(WDCE) | _BV(WDE)
	out	WDTCR, r25
	out	WDTCR, r3

	/* Jump to user application */
	tst	r24
	brne	main-4

	/* Configure CEC pins */
1:	sbi	CEC_DDR, PB4
	sbi	CEC_PORT, PB3

reset_ack:
	clr	r4		/* ack = 0 */

	/* Loop until line is low. Ticks counts down to zero here */
wait_for_low:
	sbis	CEC_PIN, PB3
	rjmp	wait_for_low

	/* Drive ack line low if we need to ack */
	cpi	r18, 9		/* Only during ack cycle */
	brne	1f
	sbrc	r4, 0
	sbi	CEC_PORT, PB4

	/* Watch/time low period */
1:	ldi	r16, US_TO_TICKS(CEC_T6_LATE0)
sample_loop:

	/* Sample when we reach 1050uS */
	cpi	r16, US_TO_TICKS(CEC_T6_LATE0 - CEC_NOM_SAMPLE)
	brne	1f
	clr	r5		/* bit = 0 */
	sbis	CEC_PIN, PB3
	inc	r5		/* bit = 1 */

	/* Check if it's time to stop acking */
1:	cpi	r16, US_TO_TICKS(CEC_T6_LATE0 - CEC_0)
	brne	1f
	cbi	CEC_PORT, PB4

	/* Delay 256 * 3 cycles */
1: 	dec	r3
	brne	1b

	dec	r16
	brne	sample_loop

	/* Wait for line to come back up, here ticks count up */
2:	sbis	CEC_PIN, PB3
	rjmp	line_high

	inc	r16
	cpi	r16, US_TO_TICKS(CEC_START_LOW_LATE - CEC_T6_LATE0)

	/* Line low too long even for a start condition */
	breq	reset_ack

	/* Delay 256 * 3 cycles */
1:	dec	r3
	brne	1b
	rjmp	2b


line_high:
	cpi	r16, US_TO_TICKS(CEC_START_LOW_EARLY - CEC_T6_LATE0)
	brlo	1f

	/* Detected start condition */
	ldi	r26, 0xff	/* Reset CRC to 0xffff */
	ldi	r27, 0xff
	ldi	r19, -3		/* Reset byte_idx to -3 */
	mov	r4, r26		/* Default to acking (any non-zero value will do) */
	clr	r2		/* EOM not yet seen */
	movw	r28, r22	/* Reset data buffer pointer */
reset_bit_state_trampoline:
	rjmp	reset_bit_state

1:	cpi	r18, 8
	brne	not_eom

	/*
	 * EOM cycle, check that if eom is set, crc is zero. Otherwise stop
	 * acking.
	 */
	mov	r2, r5		/* eom = bit */
	tst	r5		/* eom == 0? */
	breq	next_bit_state_trampoline
	sbiw	r26, 0		/* crc == 0? */
	breq	next_bit_state_trampoline
1:	rjmp	cmd_done	/* Will clear ack */

not_eom:
	cpi	r18, 9
	brne	not_ack

	/* Ack cycle */
	tst	r2		/* eom */
	breq	reset_bit_state_trampoline
	tst	r4		/* ack */
	breq	reset_bit_state_trampoline

	/* Process commands if eom and ack (completely received messsage) */
	cpi	r20, 3
	brne	not_erase

	/* Perform flash erase */
	ldi	r30, lo8(BOOTLOADER_ADDRESS)
	ldi	r31, hi8(BOOTLOADER_ADDRESS)

erase_loop:
	subi	r30, lo8(PAGESIZE)
	sbci	r31, hi8(PAGESIZE)

	out	SPMCSR, r20	/* Command is 3 */
	spm
	sbiw	r30, 0
	brne	erase_loop

	/* Load RAM pointer (flash pointer is now zero) */
	ldi	r22, lo8(buf)
	ldi	r23, hi8(buf)

not_erase:
	cpi	r20, 5
	brne	not_write

	/*
	 * Data write, if we've reached a page boundary, we need to write
	 * out the buffer.
	 */
	cpi	r28, PAGESIZE
	brne	write_done

	/* Copy our RAM buffer to the flash buffer */
	mov	r25, r30	/* Save the current flash pointer low byte */
	clr	r28
	ldi	r24, 1
1:	ld	r0, Y+
	ld	r1, Y+
	out	SPMCSR, r24
	spm
	subi	r30, -2		/* We don't care about carry since pages don't
				 * cross 0x100 boundaries */
	cpi	r28, PAGESIZE
	brne	1b

	mov	r30, r25	/* Reset original flash pointer */

	/* Write out the flash buffer */
	out	SPMCSR, r20	/* Command is 5 */
	spm

	/* Advance the  flash pointer one page */
	subi	r30, -PAGESIZE
	sbci	r31, -1

	/* Reset the RAM buffer */
	clr	r28

write_done:
	/* Successful read, update data pointer */
	movw	r22, r28

not_write:
	cpi	r20, 1
	brne	cmd_done

	/* Run image */
	rjmp	main-4

cmd_done:
	clr	r4		/* Clear ack bit, accept no more data */
next_bit_state_trampoline:
	rjmp	next_bit_state

not_ack:
	/* Shift in next data bit */
	lsl	r17
	or	r17, r5

	cpi	r18, 7
	brne	next_bit_state

	/* Full byte received (r17), add it to the CRC (r26/r27) */
	eor	r26, r17
	mov	r25, r26
	swap	r25
	eor	r25, r26
	mov	r6, r25
	lsr	r25
	lsr	r25
	eor	r25, r6
	mov	r6, r25
	lsr	r25
	eor	r25, r6
	andi	r25, 0x07
	mov	r6, r26
	mov	r26, r27
	lsr	r25
	ror	r6
	ror	r25
	mov	r27, r6
	eor	r26, r25
	lsr	r6
	ror	r25
	eor	r27, r6
	eor	r26, r25

	/* Ignore bytes if we aren't acking them */
	tst	r4		/* ack == 0? */
	breq	next_byte_idx

	cpi	r19, -3		/* -3, first byte */
	brne	not_address_byte

	/* Check that the target address is 0 (TV) */
	andi	r17, 0xf
	brne	clear_ack

not_address_byte:
	cpi	r19, -2		/* -2, second byte */
	brne	not_opcode_byte

	/* Process the opcode byte, clear ack if it isn't vendor command */
	cpi	r17, 0x89
	breq	next_byte_idx

clear_ack:
	clr	r4

not_opcode_byte:
	cpi	r19, -1		/* -1, third byte */
	brne	not_vendor_command_byte

	/* Process our vendor command byte */
	mov	r20, r17

not_vendor_command_byte:
	/* Make sure we are in the word storing range, 0-7 */
	sbrs	r19, 3

	/* Store the byte in our buffer */
	st	y+, r17

next_byte_idx:
	inc	r19
	rjmp	next_bit_state

reset_bit_state:
	ldi	r18, 0xff

next_bit_state:
	inc	r18
	rjmp	wait_for_low

