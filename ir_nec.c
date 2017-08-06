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

#include <avr/io.h>
#include <avr/interrupt.h>

#include <util/atomic.h>

#include <stdbool.h>
#include <stdio.h>

#include "time.h"
#include "ir_nec.h"

unsigned char ir_nec_repeat_timer;
bool ir_nec_has_last;

bool ir_nec_ready;
unsigned char ir_nec_output[2];

extern unsigned char ir_nec_last_pins;

/* Needs to be called every ~562.5uS */
/* FIXME: Set special do_release flag when starting a new keypress */
IR_NEC_PUBLIC void ir_nec_periodic(unsigned char delta_long)
{
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		ir_nec_repeat_timer += delta_long;
		if (ir_nec_repeat_timer > (unsigned int) MS_TO_LJIFFIES_UP(108 * 2.5)) {
			ir_nec_repeat_timer = 0;
			if (ir_nec_has_last)
				ir_nec_release();
			ir_nec_has_last = false;
		}
	}

}

IR_NEC_PUBLIC void ir_nec_init(void)
{
	ir_nec_last_pins = _BV(PB2);

	/* Watch both edges of INT0 */
	MCUCR |= _BV(ISC00);
	GIMSK |= _BV(INT0);
}
