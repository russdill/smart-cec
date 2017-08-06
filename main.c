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

#include <avr/interrupt.h>

#include <util/delay.h>

#define CEC_DDR		DDRB
#define CEC_PIN		PINB
#define CEC_PORT	PORTB
#define CEC_PBIN	PB3
#define CEC_PBOUT	PB4

#define CEC_FIXED_LOGICAL_ADDRESS CEC_ADDR_TV

#include "usi_uart.h"

#include "avr-cec/cec_spec.h"
#include "avr-cec/time.h"

#include "avr-cec/cec.c"
#include "ir_nec.c"
#include "cec_tv.c"
#include "usi_uart.c"
#include "osccal.c"

int main(void) __attribute__((OS_main));
int main(void)
{
	unsigned int last_j_short = 0;
	unsigned char last_j_long = 0;

	load_osccal();

	usi_uart_init();
	ir_nec_init();
	cec_init();

	sei();

	for (;;) {
		__uint24 j;
		unsigned int delta_short;
		unsigned char delta_long;
		unsigned char j_long;

		/* Assumes less than ~32ms has passed, can glitch otherwise */
		j = jiffies();

		delta_short = ((unsigned int) j) - last_j_short;
		last_j_short = j;

		cec_periodic(delta_short);

		j_long = j >> LJIFFIES_SHIFT;
		delta_long = j_long - last_j_long;
		last_j_long = j_long;

		cec_tv_periodic(delta_long);
		ir_nec_periodic(delta_long);
	}

	return 0;
}
