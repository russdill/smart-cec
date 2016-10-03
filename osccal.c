#include <avr/io.h>
#include <avr/eeprom.h>

CEC_PUBLIC void load_osccal(void)
{
#if 0
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
	EEAR = 0;
	EECR |= _BV(EERE);
	OSCCAL = EEDR;
#endif
}

