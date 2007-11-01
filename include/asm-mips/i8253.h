/*
 *  Machine specific IO port address definition for generic.
 *  Written by Osamu Tomita <tomita@cinet.co.jp>
 */
#ifndef __ASM_I8253_H
#define __ASM_I8253_H

/* i8253A PIT registers */
#define PIT_MODE		0x43
#define PIT_CH0			0x40
#define PIT_CH2			0x42

extern void setup_pit_timer(void);

#endif /* __ASM_I8253_H */
