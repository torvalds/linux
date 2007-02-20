/*
 * include/asm-mips/vr41xx/cmbvr4133.h
 *
 * Include file for NEC CMB-VR4133.
 *
 * Author: Yoichi Yuasa <yyuasa@mvista.com, or source@mvista.com> and
 *         Jun Sun <jsun@mvista.com, or source@mvista.com> and
 *         Alex Sapkov <asapkov@ru.mvista.com>
 *
 * 2002-2004 (c) MontaVista, Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __NEC_CMBVR4133_H
#define __NEC_CMBVR4133_H

#include <asm/vr41xx/irq.h>

/*
 * General-Purpose I/O Pin Number
 */
#define CMBVR41XX_INTA_PIN		1
#define CMBVR41XX_INTB_PIN		1
#define CMBVR41XX_INTC_PIN		3
#define CMBVR41XX_INTD_PIN		1
#define CMBVR41XX_INTE_PIN		1

/*
 * Interrupt Number
 */
#define CMBVR41XX_INTA_IRQ		GIU_IRQ(CMBVR41XX_INTA_PIN)
#define CMBVR41XX_INTB_IRQ		GIU_IRQ(CMBVR41XX_INTB_PIN)
#define CMBVR41XX_INTC_IRQ		GIU_IRQ(CMBVR41XX_INTC_PIN)
#define CMBVR41XX_INTD_IRQ		GIU_IRQ(CMBVR41XX_INTD_PIN)
#define CMBVR41XX_INTE_IRQ		GIU_IRQ(CMBVR41XX_INTE_PIN)

#define I8259A_IRQ_BASE			72
#define I8259_IRQ(x)			(I8259A_IRQ_BASE + (x))
#define TIMER_IRQ			I8259_IRQ(0)
#define KEYBOARD_IRQ			I8259_IRQ(1)
#define I8259_SLAVE_IRQ			I8259_IRQ(2)
#define UART3_IRQ			I8259_IRQ(3)
#define UART1_IRQ			I8259_IRQ(4)
#define UART2_IRQ			I8259_IRQ(5)
#define FDC_IRQ				I8259_IRQ(6)
#define PARPORT_IRQ			I8259_IRQ(7)
#define RTC_IRQ				I8259_IRQ(8)
#define USB_IRQ				I8259_IRQ(9)
#define I8259_INTA_IRQ			I8259_IRQ(10)
#define AUDIO_IRQ			I8259_IRQ(11)
#define AUX_IRQ				I8259_IRQ(12)
#define IDE_PRIMARY_IRQ			I8259_IRQ(14)
#define IDE_SECONDARY_IRQ		I8259_IRQ(15)

#endif /* __NEC_CMBVR4133_H */
