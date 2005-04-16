/*
 * linux/include/asm-arm/arch-sa1100/simpad.h
 *
 * based of assabet.h same as HUW_Webpanel
 *
 * This file contains the hardware specific definitions for SIMpad
 *
 * 2001/05/14 Juergen Messerer <juergen.messerer@freesurf.ch>
 */

#ifndef __ASM_ARCH_SIMPAD_H
#define __ASM_ARCH_SIMPAD_H


#define GPIO_UART1_RTS	GPIO_GPIO14
#define GPIO_UART1_DTR	GPIO_GPIO7
#define GPIO_UART1_CTS	GPIO_GPIO8
#define GPIO_UART1_DCD	GPIO_GPIO23
#define GPIO_UART1_DSR	GPIO_GPIO6

#define GPIO_UART3_RTS	GPIO_GPIO12
#define GPIO_UART3_DTR	GPIO_GPIO16
#define GPIO_UART3_CTS	GPIO_GPIO13
#define GPIO_UART3_DCD	GPIO_GPIO18
#define GPIO_UART3_DSR	GPIO_GPIO17

#define GPIO_POWER_BUTTON	GPIO_GPIO0
#define GPIO_UCB1300_IRQ	GPIO_GPIO22	/* UCB GPIO and touchscreen */

#define IRQ_UART1_CTS	IRQ_GPIO15
#define IRQ_UART1_DCD	GPIO_GPIO23
#define IRQ_UART1_DSR	GPIO_GPIO6
#define IRQ_UART3_CTS	GPIO_GPIO13
#define IRQ_UART3_DCD	GPIO_GPIO18
#define IRQ_UART3_DSR	GPIO_GPIO17

#define IRQ_GPIO_UCB1300_IRQ IRQ_GPIO22
#define IRQ_GPIO_POWER_BUTTON IRQ_GPIO0


/*---  PCMCIA  ---*/
#define GPIO_CF_CD              GPIO_GPIO24
#define GPIO_CF_IRQ             GPIO_GPIO1
#define IRQ_GPIO_CF_IRQ         IRQ_GPIO1
#define IRQ_GPIO_CF_CD          IRQ_GPIO24

/*--- SmartCard ---*/
#define GPIO_SMART_CARD		GPIO_GPIO10
#define IRQ_GPIO_SMARD_CARD	IRQ_GPIO10

// CS3 Latch is write only, a shadow is necessary

#define CS3BUSTYPE unsigned volatile long
#define CS3_BASE        0xf1000000

#define VCC_5V_EN       0x0001 // For 5V PCMCIA
#define VCC_3V_EN       0x0002 // FOR 3.3V PCMCIA
#define EN1             0x0004 // This is only for EPROM's
#define EN0             0x0008 // Both should be enable for 3.3V or 5V
#define DISPLAY_ON      0x0010
#define PCMCIA_BUFF_DIS 0x0020
#define MQ_RESET        0x0040
#define PCMCIA_RESET    0x0080
#define DECT_POWER_ON   0x0100
#define IRDA_SD         0x0200 // Shutdown for powersave
#define RS232_ON        0x0400
#define SD_MEDIAQ       0x0800 // Shutdown for powersave
#define LED2_ON         0x1000
#define IRDA_MODE       0x2000 // Fast/Slow IrDA mode
#define ENABLE_5V       0x4000 // Enable 5V circuit
#define RESET_SIMCARD   0x8000

#define RS232_ENABLE    0x0440
#define PCMCIAMASK      0x402f


struct simpad_battery {
	unsigned char ac_status;	/* line connected yes/no */
	unsigned char status;		/* battery loading yes/no */
	unsigned char percentage;	/* percentage loaded */
	unsigned short life;		/* life till empty */
};

/* These should match the apm_bios.h definitions */
#define SIMPAD_AC_STATUS_AC_OFFLINE      0x00
#define SIMPAD_AC_STATUS_AC_ONLINE       0x01
#define SIMPAD_AC_STATUS_AC_BACKUP       0x02   /* What does this mean? */
#define SIMPAD_AC_STATUS_AC_UNKNOWN      0xff

/* These bitfields are rarely "or'd" together */
#define SIMPAD_BATT_STATUS_HIGH          0x01
#define SIMPAD_BATT_STATUS_LOW           0x02
#define SIMPAD_BATT_STATUS_CRITICAL      0x04
#define SIMPAD_BATT_STATUS_CHARGING      0x08
#define SIMPAD_BATT_STATUS_CHARGE_MAIN   0x10
#define SIMPAD_BATT_STATUS_DEAD          0x20   /* Battery will not charge */
#define SIMPAD_BATT_NOT_INSTALLED        0x20   /* For expansion pack batteries */
#define SIMPAD_BATT_STATUS_FULL          0x40   /* Battery fully charged (and connected to AC) */
#define SIMPAD_BATT_STATUS_NOBATT        0x80
#define SIMPAD_BATT_STATUS_UNKNOWN       0xff

extern int simpad_get_battery(struct simpad_battery* );

#endif // __ASM_ARCH_SIMPAD_H








