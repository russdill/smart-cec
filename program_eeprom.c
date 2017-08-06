/*
 * V-USB Bootloader Tiny         (c) 2016 Russ Dill <russd@asu.edu>
 *
 * License: GNU GPL v2 (see License.txt)
 */

#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>

/*
 * EEPROM data is stored from the end of the trampoline range to the start of
 * ctors.
 */
extern unsigned char __trampolines_end;
extern unsigned char __ctors_start;

int main(void) __attribute__((noreturn));
int main(void)
{
	unsigned char *src, *dst;
	wdt_disable();

	dst = (void *) 0x10;
	src = &__trampolines_end;
	do {
		eeprom_update_byte(dst, pgm_read_byte(src));
		dst++;
		src++;
	} while (src < &__ctors_start);

	/* Restart bootloader */
	wdt_enable(0);
	for (;;);
}
