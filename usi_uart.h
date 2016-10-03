#ifndef _USI_UART_H_
#define _USI_UART_H_

#include <stdbool.h>

#ifndef USI_UART_PUBLIC
#define USI_UART_PUBLIC
#endif

USI_UART_PUBLIC void usi_uart_num(unsigned char c);
USI_UART_PUBLIC void usi_uart_put(char c);
USI_UART_PUBLIC unsigned char usi_uart_write_P(const char *str, unsigned char len);
USI_UART_PUBLIC bool usi_uart_write_empty(void);

USI_UART_PUBLIC void usi_uart_init(void);


extern unsigned char ser_recv_byte;
extern bool ser_recv_ready;

#endif

