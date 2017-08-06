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

