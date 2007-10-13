/*
 *  Machine specific IO port address definition for generic.
 *  Written by Osamu Tomita <tomita@cinet.co.jp>
 */
#ifndef _MACH_IO_PORTS_H
#define _MACH_IO_PORTS_H

/* i8253A PIT registers */
#define PIT_MODE		0x43
#define PIT_CH0			0x40
#define PIT_CH2			0x42

/* i8259A PIC registers */
#define PIC_MASTER_CMD		0x20
#define PIC_MASTER_IMR		0x21
#define PIC_MASTER_ISR		PIC_MASTER_CMD
#define PIC_MASTER_POLL		PIC_MASTER_ISR
#define PIC_MASTER_OCW3		PIC_MASTER_ISR
#define PIC_SLAVE_CMD		0xa0
#define PIC_SLAVE_IMR		0xa1

/* i8259A PIC related value */
#define PIC_CASCADE_IR		2
#define MASTER_ICW4_DEFAULT	0x01
#define SLAVE_ICW4_DEFAULT	0x01
#define PIC_ICW4_AEOI		2

extern void setup_pit_timer(void);

#endif /* !_MACH_IO_PORTS_H */
