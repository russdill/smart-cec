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

#ifndef _IR_NEC_H_
#define _IR_NEC_H_

#ifndef IR_NEC_PUBLIC
#define IR_NEC_PUBLIC
#endif

extern unsigned char ir_nec_output[2];
extern bool ir_nec_ready;

IR_NEC_PUBLIC void ir_nec_init(void);
IR_NEC_PUBLIC void ir_nec_periodic(unsigned char delta_long);

IR_NEC_PUBLIC void ir_nec_release();

#endif


