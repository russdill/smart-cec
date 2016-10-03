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


