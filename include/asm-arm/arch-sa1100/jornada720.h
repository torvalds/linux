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

/* MCU COMMANDS */
#define MCU_GetBatteryData  0xc0
#define MCU_GetScanKeyCode  0x90
#define MCU_GetTouchSamples 0xa0
#define MCU_GetContrast     0xD0
#define MCU_SetContrast     0xD1
#define MCU_GetBrightness   0xD2
#define MCU_SetBrightness   0xD3
#define MCU_ContrastOff     0xD8
#define MCU_BrightnessOff   0xD9
#define MCU_PWMOFF          0xDF
#define MCU_TxDummy         0x11
#define MCU_ErrorCode       0x00

#ifndef __ASSEMBLY__

void jornada720_mcu_init(void);
void jornada_contrast(int arg_contrast);
void jornada720_battery(void);
int jornada720_getkey(unsigned char *data, int size);
#endif
