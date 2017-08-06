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
#include <avr/eeprom.h>

CEC_PUBLIC void load_osccal(void)
{
#if 0
	/* Step osccal value by most 1 each time */
	unsigned char osccal;
	unsigned char curr;

	osccal = eeprom_read_byte(0x00);
	if (osccal == 0xff)
		return;

	curr = OSCCAL;
	for (;;) {
		if (curr == osccal)
			break;
		else if (curr > osccal)
			curr--;
		else
			curr++;
		OSCCAL = curr;
	}
#else
	/* Minimal, just blast it */
	EEAR = 0;
	EECR |= _BV(EERE);
	OSCCAL = EEDR;
#endif
}

