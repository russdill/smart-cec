#include <avr/io.h>
#include <avr/pgmspace.h>

#include <util/atomic.h>

#include "time.h"
#include "usi_uart.h"

#define BAUD		9600UL

/* Get the USI to cycle 4 times per baud division */
#define TCNT_TOT	HZ_TO_JIFFIES_RND(BAUD * 4)
#define TCNT_TOP	(TCNT_TOT - 1)

extern bool ser_overflow;
volatile unsigned char send_buf[25];
volatile unsigned char send_prod;

USI_UART_PUBLIC void usi_uart_put(char c)
{
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		send_buf[send_prod++] = c;
	}
}

USI_UART_PUBLIC void __attribute__ ((noinline)) usi_uart_num(unsigned char c)
{
	c += '0';
	if (c > '9')
		c += 'a' - ':';
	usi_uart_put(c);
}

USI_UART_PUBLIC unsigned char __attribute__ ((unused)) usi_uart_write_P(const char *str, unsigned char len)
{
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		memcpy_P((void *) send_buf + send_prod, str, len);
		send_prod += len;
	}

	return len;
}

USI_UART_PUBLIC bool usi_uart_write_empty(void)
{
	return send_prod == 0;
}

USI_UART_PUBLIC void usi_uart_init(void)
{
	USIBR = 0xff;
	USIDR = 0xff;
	USISR = 8;

	/* Set DO to output */
	DDRB |= _BV(PB1);

	/* Select Three-wire mode and Timer/Counter0 Compare Match clock */
	USICR = _BV(USIOIE) | _BV(USIWM0) | _BV(USICS0);

	/* Enable clear to compare mode */
	TCCR0A = _BV(WGM01);

	TCCR0B = TCNT0_PRESCALER_VAL;
	OCR0A = TCNT_TOP;
}
