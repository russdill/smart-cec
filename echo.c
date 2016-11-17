#include <avr/interrupt.h>
#include <avr/wdt.h>


#include "usi_uart.c"
#include "osccal.c"

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
