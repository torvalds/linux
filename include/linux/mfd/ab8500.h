/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com>
 */
#ifndef MFD_AB8500_H
#define MFD_AB8500_H

#include <linux/device.h>

/*
 * Interrupts
 */

#define AB8500_INT_MAIN_EXT_CH_NOT_OK	0
#define AB8500_INT_UN_PLUG_TV_DET	1
#define AB8500_INT_PLUG_TV_DET		2
#define AB8500_INT_TEMP_WARM		3
#define AB8500_INT_PON_KEY2DB_F		4
#define AB8500_INT_PON_KEY2DB_R		5
#define AB8500_INT_PON_KEY1DB_F		6
#define AB8500_INT_PON_KEY1DB_R		7
#define AB8500_INT_BATT_OVV		8
#define AB8500_INT_MAIN_CH_UNPLUG_DET	10
#define AB8500_INT_MAIN_CH_PLUG_DET	11
#define AB8500_INT_USB_ID_DET_F		12
#define AB8500_INT_USB_ID_DET_R		13
#define AB8500_INT_VBUS_DET_F		14
#define AB8500_INT_VBUS_DET_R		15
#define AB8500_INT_VBUS_CH_DROP_END	16
#define AB8500_INT_RTC_60S		17
#define AB8500_INT_RTC_ALARM		18
#define AB8500_INT_BAT_CTRL_INDB	20
#define AB8500_INT_CH_WD_EXP		21
#define AB8500_INT_VBUS_OVV		22
#define AB8500_INT_MAIN_CH_DROP_END	23
#define AB8500_INT_CCN_CONV_ACC		24
#define AB8500_INT_INT_AUD		25
#define AB8500_INT_CCEOC		26
#define AB8500_INT_CC_INT_CALIB		27
#define AB8500_INT_LOW_BAT_F		28
#define AB8500_INT_LOW_BAT_R		29
#define AB8500_INT_BUP_CHG_NOT_OK	30
#define AB8500_INT_BUP_CHG_OK		31
#define AB8500_INT_GP_HW_ADC_CONV_END	32
#define AB8500_INT_ACC_DETECT_1DB_F	33
#define AB8500_INT_ACC_DETECT_1DB_R	34
#define AB8500_INT_ACC_DETECT_22DB_F	35
#define AB8500_INT_ACC_DETECT_22DB_R	36
#define AB8500_INT_ACC_DETECT_21DB_F	37
#define AB8500_INT_ACC_DETECT_21DB_R	38
#define AB8500_INT_GP_SW_ADC_CONV_END	39
#define AB8500_INT_BTEMP_LOW		72
#define AB8500_INT_BTEMP_LOW_MEDIUM	73
#define AB8500_INT_BTEMP_MEDIUM_HIGH	74
#define AB8500_INT_BTEMP_HIGH		75
#define AB8500_INT_USB_CHARGER_NOT_OK	81
#define AB8500_INT_ID_WAKEUP_R		82
#define AB8500_INT_ID_DET_R1R		84
#define AB8500_INT_ID_DET_R2R		85
#define AB8500_INT_ID_DET_R3R		86
#define AB8500_INT_ID_DET_R4R		87
#define AB8500_INT_ID_WAKEUP_F		88
#define AB8500_INT_ID_DET_R1F		90
#define AB8500_INT_ID_DET_R2F		91
#define AB8500_INT_ID_DET_R3F		92
#define AB8500_INT_ID_DET_R4F		93
#define AB8500_INT_USB_CHG_DET_DONE	94
#define AB8500_INT_USB_CH_TH_PROT_F	96
#define AB8500_INT_USB_CH_TH_PROP_R	97
#define AB8500_INT_MAIN_CH_TH_PROP_F	98
#define AB8500_INT_MAIN_CH_TH_PROT_R	99
#define AB8500_INT_USB_CHARGER_NOT_OKF	103

#define AB8500_NR_IRQS			104
#define AB8500_NUM_IRQ_REGS		13

/**
 * struct ab8500 - ab8500 internal structure
 * @dev: parent device
 * @lock: read/write operations lock
 * @irq_lock: genirq bus lock
 * @revision: chip revision
 * @irq: irq line
 * @write: register write
 * @read: register read
 * @rx_buf: rx buf for SPI
 * @tx_buf: tx buf for SPI
 * @mask: cache of IRQ regs for bus lock
 * @oldmask: cache of previous IRQ regs for bus lock
 */
struct ab8500 {
	struct device	*dev;
	struct mutex	lock;
	struct mutex	irq_lock;
	int		revision;
	int		irq_base;
	int		irq;

	int (*write) (struct ab8500 *a8500, u16 addr, u8 data);
	int (*read) (struct ab8500 *a8500, u16 addr);

	unsigned long	tx_buf[4];
	unsigned long	rx_buf[4];

	u8 mask[AB8500_NUM_IRQ_REGS];
	u8 oldmask[AB8500_NUM_IRQ_REGS];
};

/**
 * struct ab8500_platform_data - AB8500 platform data
 * @irq_base: start of AB8500 IRQs, AB8500_NR_IRQS will be used
 * @init: board-specific initialization after detection of ab8500
 */
struct ab8500_platform_data {
	int irq_base;
	void (*init) (struct ab8500 *);
};

extern int ab8500_write(struct ab8500 *a8500, u16 addr, u8 data);
extern int ab8500_read(struct ab8500 *a8500, u16 addr);
extern int ab8500_set_bits(struct ab8500 *a8500, u16 addr, u8 mask, u8 data);

extern int __devinit ab8500_init(struct ab8500 *ab8500);
extern int __devexit ab8500_exit(struct ab8500 *ab8500);

#endif /* MFD_AB8500_H */
