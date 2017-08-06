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
#include <avr/wdt.h>

#include "usi_uart.c"
#include "osccal.c"

/* Simple serial echo test */
int main(void)
{
	load_osccal();
	usi_uart_init();

	sei();

	for (;;) {
		unsigned char byte;
again:
		wdt_reset();

		ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
			if (!ser_recv_ready)
				goto again;

			byte = ser_recv_byte;
			ser_recv_ready = false;
		}

		usi_uart_put(byte);
	}

	return 0;
}
