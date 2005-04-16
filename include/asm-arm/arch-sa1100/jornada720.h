/*
 * linux/include/asm-arm/arch-sa1100/jornada720.h
 *
 * Created 2000/11/29 by John Ankcorn <jca@lcs.mit.edu>
 *
 * This file contains the hardware specific definitions for HP Jornada 720
 *
 */

#ifndef __ASM_ARCH_HARDWARE_H
#error "include <asm/hardware.h> instead"
#endif

#define SA1111_BASE             (0x40000000)

#define GPIO_JORNADA720_KEYBOARD	GPIO_GPIO(0)
#define GPIO_JORNADA720_MOUSE		GPIO_GPIO(9)

#define GPIO_JORNADA720_KEYBOARD_IRQ	IRQ_GPIO0
#define GPIO_JORNADA720_MOUSE_IRQ		IRQ_GPIO9

#ifndef __ASSEMBLY__

void jornada720_mcu_init(void);
void jornada_contrast(int arg_contrast);
void jornada720_battery(void);
int jornada720_getkey(unsigned char *data, int size);
#endif
