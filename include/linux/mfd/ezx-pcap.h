/*
 * Copyright 2009 Daniel Ribeiro <drwyrm@gmail.com>
 *
 * For further information, please see http://wiki.openezx.org/PCAP2
 */

#ifndef EZX_PCAP_H
#define EZX_PCAP_H

struct pcap_subdev {
	int id;
	const char *name;
	void *platform_data;
};

struct pcap_platform_data {
	unsigned int irq_base;
	unsigned int config;
	int gpio;
	void (*init) (void *);	/* board specific init */
	int num_subdevs;
	struct pcap_subdev *subdevs;
};

struct pcap_chip;

int ezx_pcap_write(struct pcap_chip *, u8, u32);
int ezx_pcap_read(struct pcap_chip *, u8, u32 *);
int ezx_pcap_set_bits(struct pcap_chip *, u8, u32, u32);
int pcap_to_irq(struct pcap_chip *, int);
int irq_to_pcap(struct pcap_chip *, int);
int pcap_adc_async(struct pcap_chip *, u8, u32, u8[], void *, void *);
int pcap_adc_sync(struct pcap_chip *, u8, u32, u8[], u16[]);
void pcap_set_ts_bits(struct pcap_chip *, u32);

#define PCAP_SECOND_PORT	1
#define PCAP_CS_AH		2

#define PCAP_REGISTER_WRITE_OP_BIT	0x80000000
#define PCAP_REGISTER_READ_OP_BIT	0x00000000

#define PCAP_REGISTER_VALUE_MASK	0x01ffffff
#define PCAP_REGISTER_ADDRESS_MASK	0x7c000000
#define PCAP_REGISTER_ADDRESS_SHIFT	26
#define PCAP_REGISTER_NUMBER		32
#define PCAP_CLEAR_INTERRUPT_REGISTER	0x01ffffff
#define PCAP_MASK_ALL_INTERRUPT		0x01ffffff

/* registers accessible by both pcap ports */
#define PCAP_REG_ISR		0x0	/* Interrupt Status */
#define PCAP_REG_MSR		0x1	/* Interrupt Mask */
#define PCAP_REG_PSTAT		0x2	/* Processor Status */
#define PCAP_REG_VREG2		0x6	/* Regulator Bank 2 Control */
#define PCAP_REG_AUXVREG	0x7	/* Auxiliary Regulator Control */
#define PCAP_REG_BATT		0x8	/* Battery Control */
#define PCAP_REG_ADC		0x9	/* AD Control */
#define PCAP_REG_ADR		0xa	/* AD Result */
#define PCAP_REG_CODEC		0xb	/* Audio Codec Control */
#define PCAP_REG_RX_AMPS	0xc	/* RX Audio Amplifiers Control */
#define PCAP_REG_ST_DAC		0xd	/* Stereo DAC Control */
#define PCAP_REG_BUSCTRL	0x14	/* Connectivity Control */
#define PCAP_REG_PERIPH		0x15	/* Peripheral Control */
#define PCAP_REG_LOWPWR		0x18	/* Regulator Low Power Control */
#define PCAP_REG_TX_AMPS	0x1a	/* TX Audio Amplifiers Control */
#define PCAP_REG_GP		0x1b	/* General Purpose */
#define PCAP_REG_TEST1		0x1c
#define PCAP_REG_TEST2		0x1d
#define PCAP_REG_VENDOR_TEST1	0x1e
#define PCAP_REG_VENDOR_TEST2	0x1f

/* registers accessible by pcap port 1 only (a1200, e2 & e6) */
#define PCAP_REG_INT_SEL	0x3	/* Interrupt Select */
#define PCAP_REG_SWCTRL		0x4	/* Switching Regulator Control */
#define PCAP_REG_VREG1		0x5	/* Regulator Bank 1 Control */
#define PCAP_REG_RTC_TOD	0xe	/* RTC Time of Day */
#define PCAP_REG_RTC_TODA	0xf	/* RTC Time of Day Alarm */
#define PCAP_REG_RTC_DAY	0x10	/* RTC Day */
#define PCAP_REG_RTC_DAYA	0x11	/* RTC Day Alarm */
#define PCAP_REG_MTRTMR		0x12	/* AD Monitor Timer */
#define PCAP_REG_PWR		0x13	/* Power Control */
#define PCAP_REG_AUXVREG_MASK	0x16	/* Auxiliary Regulator Mask */
#define PCAP_REG_VENDOR_REV	0x17
#define PCAP_REG_PERIPH_MASK	0x19	/* Peripheral Mask */

/* PCAP2 Interrupts */
#define PCAP_NIRQS		23
#define PCAP_IRQ_ADCDONE	0	/* ADC done port 1 */
#define PCAP_IRQ_TS		1	/* Touch Screen */
#define PCAP_IRQ_1HZ		2	/* 1HZ timer */
#define PCAP_IRQ_WH		3	/* ADC above high limit */
#define PCAP_IRQ_WL		4	/* ADC below low limit */
#define PCAP_IRQ_TODA		5	/* Time of day alarm */
#define PCAP_IRQ_USB4V		6	/* USB above 4V */
#define PCAP_IRQ_ONOFF		7	/* On/Off button */
#define PCAP_IRQ_ONOFF2		8	/* On/Off button 2 */
#define PCAP_IRQ_USB1V		9	/* USB above 1V */
#define PCAP_IRQ_MOBPORT	10
#define PCAP_IRQ_MIC		11	/* Mic attach/HS button */
#define PCAP_IRQ_HS		12	/* Headset attach */
#define PCAP_IRQ_ST		13
#define PCAP_IRQ_PC		14	/* Power Cut */
#define PCAP_IRQ_WARM		15
#define PCAP_IRQ_EOL		16	/* Battery End Of Life */
#define PCAP_IRQ_CLK		17
#define PCAP_IRQ_SYSRST		18	/* System Reset */
#define PCAP_IRQ_DUMMY		19
#define PCAP_IRQ_ADCDONE2	20	/* ADC done port 2 */
#define PCAP_IRQ_SOFTRESET	21
#define PCAP_IRQ_MNEXB		22

/* voltage regulators */
#define V1		0
#define V2		1
#define V3		2
#define V4		3
#define V5		4
#define V6		5
#define V7		6
#define V8		7
#define V9		8
#define V10		9
#define VAUX1		10
#define VAUX2		11
#define VAUX3		12
#define VAUX4		13
#define VSIM		14
#define VSIM2		15
#define VVIB		16
#define SW1		17
#define SW2		18
#define SW3		19
#define SW1S		20
#define SW2S		21

#define PCAP_BATT_DAC_MASK		0x000000ff
#define PCAP_BATT_DAC_SHIFT		0
#define PCAP_BATT_B_FDBK		(1 << 8)
#define PCAP_BATT_EXT_ISENSE		(1 << 9)
#define PCAP_BATT_V_COIN_MASK		0x00003c00
#define PCAP_BATT_V_COIN_SHIFT		10
#define PCAP_BATT_I_COIN		(1 << 14)
#define PCAP_BATT_COIN_CH_EN		(1 << 15)
#define PCAP_BATT_EOL_SEL_MASK		0x000e0000
#define PCAP_BATT_EOL_SEL_SHIFT		17
#define PCAP_BATT_EOL_CMP_EN		(1 << 20)
#define PCAP_BATT_BATT_DET_EN		(1 << 21)
#define PCAP_BATT_THERMBIAS_CTRL	(1 << 22)

#define PCAP_ADC_ADEN			(1 << 0)
#define PCAP_ADC_RAND			(1 << 1)
#define PCAP_ADC_AD_SEL1		(1 << 2)
#define PCAP_ADC_AD_SEL2		(1 << 3)
#define PCAP_ADC_ADA1_MASK		0x00000070
#define PCAP_ADC_ADA1_SHIFT		4
#define PCAP_ADC_ADA2_MASK		0x00000380
#define PCAP_ADC_ADA2_SHIFT		7
#define PCAP_ADC_ATO_MASK		0x00003c00
#define PCAP_ADC_ATO_SHIFT		10
#define PCAP_ADC_ATOX			(1 << 14)
#define PCAP_ADC_MTR1			(1 << 15)
#define PCAP_ADC_MTR2			(1 << 16)
#define PCAP_ADC_TS_M_MASK		0x000e0000
#define PCAP_ADC_TS_M_SHIFT		17
#define PCAP_ADC_TS_REF_LOWPWR		(1 << 20)
#define PCAP_ADC_TS_REFENB		(1 << 21)
#define PCAP_ADC_BATT_I_POLARITY	(1 << 22)
#define PCAP_ADC_BATT_I_ADC		(1 << 23)

#define PCAP_ADC_BANK_0			0
#define PCAP_ADC_BANK_1			1
/* ADC bank 0 */
#define PCAP_ADC_CH_COIN		0
#define PCAP_ADC_CH_BATT		1
#define PCAP_ADC_CH_BPLUS		2
#define PCAP_ADC_CH_MOBPORTB		3
#define PCAP_ADC_CH_TEMPERATURE		4
#define PCAP_ADC_CH_CHARGER_ID		5
#define PCAP_ADC_CH_AD6			6
/* ADC bank 1 */
#define PCAP_ADC_CH_AD7			0
#define PCAP_ADC_CH_AD8			1
#define PCAP_ADC_CH_AD9			2
#define PCAP_ADC_CH_TS_X1		3
#define PCAP_ADC_CH_TS_X2		4
#define PCAP_ADC_CH_TS_Y1		5
#define PCAP_ADC_CH_TS_Y2		6

#define PCAP_ADC_T_NOW			0
#define PCAP_ADC_T_IN_BURST		1
#define PCAP_ADC_T_OUT_BURST		2

#define PCAP_ADC_ATO_IN_BURST		6
#define PCAP_ADC_ATO_OUT_BURST		0

#define PCAP_ADC_TS_M_XY		1
#define PCAP_ADC_TS_M_PRESSURE		2
#define PCAP_ADC_TS_M_PLATE_X		3
#define PCAP_ADC_TS_M_PLATE_Y		4
#define PCAP_ADC_TS_M_STANDBY		5
#define PCAP_ADC_TS_M_NONTS		6

#define PCAP_ADR_ADD1_MASK		0x000003ff
#define PCAP_ADR_ADD1_SHIFT		0
#define PCAP_ADR_ADD2_MASK		0x000ffc00
#define PCAP_ADR_ADD2_SHIFT		10
#define PCAP_ADR_ADINC1			(1 << 20)
#define PCAP_ADR_ADINC2			(1 << 21)
#define PCAP_ADR_ASC			(1 << 22)
#define PCAP_ADR_ONESHOT		(1 << 23)

#define PCAP_BUSCTRL_FSENB		(1 << 0)
#define PCAP_BUSCTRL_USB_SUSPEND	(1 << 1)
#define PCAP_BUSCTRL_USB_PU		(1 << 2)
#define PCAP_BUSCTRL_USB_PD		(1 << 3)
#define PCAP_BUSCTRL_VUSB_EN		(1 << 4)
#define PCAP_BUSCTRL_USB_PS		(1 << 5)
#define PCAP_BUSCTRL_VUSB_MSTR_EN	(1 << 6)
#define PCAP_BUSCTRL_VBUS_PD_ENB	(1 << 7)
#define PCAP_BUSCTRL_CURRLIM		(1 << 8)
#define PCAP_BUSCTRL_RS232ENB		(1 << 9)
#define PCAP_BUSCTRL_RS232_DIR		(1 << 10)
#define PCAP_BUSCTRL_SE0_CONN		(1 << 11)
#define PCAP_BUSCTRL_USB_PDM		(1 << 12)
#define PCAP_BUSCTRL_BUS_PRI_ADJ	(1 << 24)

/* leds */
#define PCAP_LED0		0
#define PCAP_LED1		1
#define PCAP_BL0		2
#define PCAP_BL1		3
#define PCAP_LED_3MA		0
#define PCAP_LED_4MA		1
#define PCAP_LED_5MA		2
#define PCAP_LED_9MA		3
#define PCAP_LED_T_MASK		0xf
#define PCAP_LED_C_MASK		0x3
#define PCAP_BL_MASK		0x1f
#define PCAP_BL0_SHIFT		0
#define PCAP_LED0_EN		(1 << 5)
#define PCAP_LED1_EN		(1 << 6)
#define PCAP_LED0_T_SHIFT	7
#define PCAP_LED1_T_SHIFT	11
#define PCAP_LED0_C_SHIFT	15
#define PCAP_LED1_C_SHIFT	17
#define PCAP_BL1_SHIFT		20

/* RTC */
#define PCAP_RTC_DAY_MASK	0x3fff
#define PCAP_RTC_TOD_MASK	0xffff
#define PCAP_RTC_PC_MASK	0x7
#define SEC_PER_DAY		86400

#endif
