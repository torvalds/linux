#ifndef __ASM_ARM_HARDWARE_ARM_TIMER_H
#define __ASM_ARM_HARDWARE_ARM_TIMER_H

#define TIMER_LOAD	0x00
#define TIMER_VALUE	0x04
#define TIMER_CTRL	0x08
#define TIMER_CTRL_ONESHOT	(1 << 0)
#define TIMER_CTRL_32BIT	(1 << 1)
#define TIMER_CTRL_DIV1		(0 << 2)
#define TIMER_CTRL_DIV16	(1 << 2)
#define TIMER_CTRL_DIV256	(2 << 2)
#define TIMER_CTRL_IE		(1 << 5)	/* Interrupt Enable (versatile only) */
#define TIMER_CTRL_PERIODIC	(1 << 6)
#define TIMER_CTRL_ENABLE	(1 << 7)

#define TIMER_INTCLR	0x0c
#define TIMER_RIS	0x10
#define TIMER_MIS	0x14
#define TIMER_BGLOAD	0x18

#endif
