/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com>
 */
#ifndef MFD_AB8500_H
#define MFD_AB8500_H

#include <linux/mutex.h>

struct device;

/*
 * AB8500 bank addresses
 */
#define AB8500_SYS_CTRL1_BLOCK	0x1
#define AB8500_SYS_CTRL2_BLOCK	0x2
#define AB8500_REGU_CTRL1	0x3
#define AB8500_REGU_CTRL2	0x4
#define AB8500_USB		0x5
#define AB8500_TVOUT		0x6
#define AB8500_DBI		0x7
#define AB8500_ECI_AV_ACC	0x8
#define AB8500_RESERVED		0x9
#define AB8500_GPADC		0xA
#define AB8500_CHARGER		0xB
#define AB8500_GAS_GAUGE	0xC
#define AB8500_AUDIO		0xD
#define AB8500_INTERRUPT	0xE
#define AB8500_RTC		0xF
#define AB8500_MISC		0x10
#define AB8500_DEVELOPMENT	0x11
#define AB8500_DEBUG		0x12
#define AB8500_PROD_TEST	0x13
#define AB8500_OTP_EMUL		0x15

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
#define AB8500_INT_GPIO6R		40
#define AB8500_INT_GPIO7R		41
#define AB8500_INT_GPIO8R		42
#define AB8500_INT_GPIO9R		43
#define AB8500_INT_GPIO10R		44
#define AB8500_INT_GPIO11R		45
#define AB8500_INT_GPIO12R		46
#define AB8500_INT_GPIO13R		47
#define AB8500_INT_GPIO24R		48
#define AB8500_INT_GPIO25R		49
#define AB8500_INT_GPIO36R		50
#define AB8500_INT_GPIO37R		51
#define AB8500_INT_GPIO38R		52
#define AB8500_INT_GPIO39R		53
#define AB8500_INT_GPIO40R		54
#define AB8500_INT_GPIO41R		55
#define AB8500_INT_GPIO6F		56
#define AB8500_INT_GPIO7F		57
#define AB8500_INT_GPIO8F		58
#define AB8500_INT_GPIO9F		59
#define AB8500_INT_GPIO10F		60
#define AB8500_INT_GPIO11F		61
#define AB8500_INT_GPIO12F		62
#define AB8500_INT_GPIO13F		63
#define AB8500_INT_GPIO24F		64
#define AB8500_INT_GPIO25F		65
#define AB8500_INT_GPIO36F		66
#define AB8500_INT_GPIO37F		67
#define AB8500_INT_GPIO38F		68
#define AB8500_INT_GPIO39F		69
#define AB8500_INT_GPIO40F		70
#define AB8500_INT_GPIO41F		71
#define AB8500_INT_ADP_SOURCE_ERROR	72
#define AB8500_INT_ADP_SINK_ERROR	73
#define AB8500_INT_ADP_PROBE_PLUG	74
#define AB8500_INT_ADP_PROBE_UNPLUG	75
#define AB8500_INT_ADP_SENSE_OFF	76
#define AB8500_INT_USB_PHY_POWER_ERR	78
#define AB8500_INT_USB_LINK_STATUS	79
#define AB8500_INT_BTEMP_LOW		80
#define AB8500_INT_BTEMP_LOW_MEDIUM	81
#define AB8500_INT_BTEMP_MEDIUM_HIGH	82
#define AB8500_INT_BTEMP_HIGH		83
#define AB8500_INT_USB_CHARGER_NOT_OK	89
#define AB8500_INT_ID_WAKEUP_R		90
#define AB8500_INT_ID_DET_R1R		92
#define AB8500_INT_ID_DET_R2R		93
#define AB8500_INT_ID_DET_R3R		94
#define AB8500_INT_ID_DET_R4R		95
#define AB8500_INT_ID_WAKEUP_F		96
#define AB8500_INT_ID_DET_R1F		98
#define AB8500_INT_ID_DET_R2F		99
#define AB8500_INT_ID_DET_R3F		100
#define AB8500_INT_ID_DET_R4F		101
#define AB8500_INT_USB_CHG_DET_DONE	102
#define AB8500_INT_USB_CH_TH_PROT_F	104
#define AB8500_INT_USB_CH_TH_PROT_R    105
#define AB8500_INT_MAIN_CH_TH_PROT_F   106
#define AB8500_INT_MAIN_CH_TH_PROT_R	107
#define AB8500_INT_USB_CHARGER_NOT_OKF	111

#define AB8500_NR_IRQS			112
#define AB8500_NUM_IRQ_REGS		14

/**
 * struct ab8500 - ab8500 internal structure
 * @dev: parent device
 * @lock: read/write operations lock
 * @irq_lock: genirq bus lock
 * @irq: irq line
 * @chip_id: chip revision id
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

	int		irq_base;
	int		irq;
	u8		chip_id;

	int (*write) (struct ab8500 *a8500, u16 addr, u8 data);
	int (*read) (struct ab8500 *a8500, u16 addr);

	unsigned long	tx_buf[4];
	unsigned long	rx_buf[4];

	u8 mask[AB8500_NUM_IRQ_REGS];
	u8 oldmask[AB8500_NUM_IRQ_REGS];
};

struct regulator_reg_init;
struct regulator_init_data;
struct ab8500_gpio_platform_data;

/**
 * struct ab8500_platform_data - AB8500 platform data
 * @irq_base: start of AB8500 IRQs, AB8500_NR_IRQS will be used
 * @init: board-specific initialization after detection of ab8500
 * @num_regulator_reg_init: number of regulator init registers
 * @regulator_reg_init: regulator init registers
 * @num_regulator: number of regulators
 * @regulator: machine-specific constraints for regulators
 */
struct ab8500_platform_data {
	int irq_base;
	void (*init) (struct ab8500 *);
	int num_regulator_reg_init;
	struct ab8500_regulator_reg_init *regulator_reg_init;
	int num_regulator;
	struct regulator_init_data *regulator;
	struct ab8500_gpio_platform_data *gpio;
};

extern int __devinit ab8500_init(struct ab8500 *ab8500);
extern int __devexit ab8500_exit(struct ab8500 *ab8500);

#endif /* MFD_AB8500_H */
