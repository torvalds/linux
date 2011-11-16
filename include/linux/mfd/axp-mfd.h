#ifndef __LINUX_AXP_MFD_H_
#define __LINUX_AXP_MFD_H_

/* Unified sub device IDs for AXP */
enum {
	AXP18_ID_LDO1,
	AXP18_ID_LDO2,
	AXP18_ID_LDO3,
	AXP18_ID_LDO4,
	AXP18_ID_LDO5,
	AXP18_ID_BUCK1,
	AXP18_ID_BUCK2,
	AXP18_ID_BUCK3,
	AXP18_ID_SW1,
	AXP18_ID_SW2,

	AXP18_ID_SUPPLY,

	AXP19_ID_LDO1,
	AXP19_ID_LDO2,
	AXP19_ID_LDO3,
	AXP19_ID_LDO4,

	AXP19_ID_BUCK1,
	AXP19_ID_BUCK2,
	AXP19_ID_BUCK3,

	AXP19_ID_LDOIO0,

	AXP19_ID_SUPPLY,

	AXP19_ID_GPIO,

	AXP20_ID_LDO1,
	AXP20_ID_LDO2,
	AXP20_ID_LDO3,
	AXP20_ID_LDO4,

	AXP20_ID_BUCK2,
	AXP20_ID_BUCK3,

	AXP20_ID_LDOIO0,

	AXP20_ID_SUPPLY,

	AXP20_ID_GPIO,

};

#define AXP_MFD_ATTR(_name)					\
{									\
	.attr = { .name = #_name,.mode = 0644 },					\
	.show =  _name##_show,				\
	.store = _name##_store, \
}

/* AXP battery charger data */
struct power_supply_info;

struct axp_supply_init_data {
	/* battery parameters */
	struct power_supply_info *battery_info;

	/* current and voltage to use for battery charging */
	unsigned int chgcur;
	unsigned int chgvol;
	unsigned int chgend;
	/*charger control*/
	bool chgen;
	bool limit_on;
	/*charger time */
	int chgpretime;
	int chgcsttime;

	/*adc sample time */
	unsigned int sample_time;

	/* platform callbacks for battery low and critical IRQs */
	void (*battery_low)(void);
	void (*battery_critical)(void);
};

struct axp_funcdev_info {
	int		id;
	const char	*name;
	void	*platform_data;
};

struct axp_platform_data {
	int num_regl_devs;
	int num_sply_devs;
	int num_gpio_devs;
	int gpio_base;
	struct axp_funcdev_info *regl_devs;
	struct axp_funcdev_info *sply_devs;
	struct axp_funcdev_info *gpio_devs;

};

struct axp_mfd_chip {
	struct i2c_client	*client;
	struct device		*dev;
	struct axp_mfd_chip_ops	*ops;

	int			type;
	uint64_t		irqs_enabled;

	struct mutex		lock;
	struct work_struct	irq_work;

	struct blocking_notifier_head notifier_list;
};

struct axp_mfd_chip_ops {
	int	(*init_chip)(struct axp_mfd_chip *);
	int	(*enable_irqs)(struct axp_mfd_chip *, uint64_t irqs);
	int	(*disable_irqs)(struct axp_mfd_chip *, uint64_t irqs);
	int	(*read_irqs)(struct axp_mfd_chip *, uint64_t *irqs);
};

#define AXP18                      	18
#define POWER18_STATUS            	(0x00)
#define POWER18_IPS_SET             (0x01)
#define POWER18_ONOFF               (0x02)
#define POWER18_CHARGE1             (0x03)
#define POWER18_CHARGE2             (0x04)
#define POWER18_PEK                 (0x05)
#define POWER18_INTEN1              (0x06)
#define POWER18_INTEN2              (0x07)
#define POWER18_INTEN3              (0x08)
#define POWER18_INTSTS1             (0x09)
#define POWER18_INTSTS2             (0x0A)
#define POWER18_INTSTS3             (0x0B)
#define POWER18_VENDER_USED1        (0x0C)
#define POWER18_DCDCCTL             (0x0D)
#define POWER18_DC12OUT_VOL         (0x0E)
#define POWER18_LDOOUT_VOL          (0x0F)
#define POWER18_SW_CTL              (0x10)
#define POWER18_BATTERY_VOL         (0x11)
#define POWER18_BATTERY_CURRENT     (0x12)
#define POWER18_DCIN_VOL            (0x13)
#define POWER18_DCIN_CURRENT        (0x14)
#define POWER18_ADCSW_CTL           (0x15)
#define POWER18_VENDER_USED2        (0x16)
#define POWER18_EPT_SW              (0x17)
#define POWER18_DATA_BUFFER1        (0x18)
#define POWER18_DATA_BUFFER2        (0x19)
#define POWER18_VENDER_USED3        (0x1A)


#define AXP19                       19
#define POWER19_STATUS              (0x00)
#define POWER19_MODE_CHGSTATUS      (0x01)
#define POWER19_OTG_STATUS          (0x02)
#define POWER19_IC_TYPE             (0x03)
#define POWER19_DATA_BUFFER1        (0x06)
#define POWER19_DATA_BUFFER2        (0x07)
#define POWER19_DATA_BUFFER3        (0x08)
#define POWER19_DATA_BUFFER4        (0x09)
#define POWER19_VERSION             (0x0C)
#define POWER19_LDO3_DC2_CTL        (0x10)
#define POWER19_LDO24_DC13_CTL      (0x12)
#define POWER19_DC2OUT_VOL          (0x23)
#define POWER19_LDO3_DC2_DVM        (0x25)
#define POWER19_DC1OUT_VOL          (0x26)
#define POWER19_DC3OUT_VOL          (0x27)
#define POWER19_LDO24OUT_VOL        (0x28)
#define POWER19_LDO3OUT_VOL         (0x29)
#define POWER19_IPS_SET             (0x30)
#define POWER19_VOFF_SET            (0x31)
#define POWER19_OFF_CTL             (0x32)
#define POWER19_CHARGE1             (0x33)
#define POWER19_CHARGE2             (0x34)
#define POWER19_BACKUP_CHG          (0x35)
#define POWER19_POK_SET             (0x36)
#define POWER19_DCDC_FREQSET        (0x37)
#define POWER19_VLTF_CHGSET         (0x38)
#define POWER19_VHTF_CHGSET         (0x39)
#define POWER19_APS_WARNING1        (0x3A)
#define POWER19_APS_WARNING2        (0x3B)
#define POWER19_VLTF_DISCHGSET      (0x3C)
#define POWER19_VHTF_DISCHGSET      (0x3D)
#define POWER19_DCDC_MODESET        (0x80)
#define POWER19_VOUT_MONITOR        (0x81)
#define POWER19_ADC_EN1             (0x82)
#define POWER19_ADC_EN2             (0x83)
#define POWER19_ADC_SPEED           (0x84)
#define POWER19_ADC_INPUTRANGE      (0x85)
#define POWER19_TIMER_CTL           (0x8A)
#define POWER19_VBUS_DET_SRP        (0x8B)
#define POWER19_HOTOVER_CTL         (0x8F)
#define POWER19_GPIO0_CTL           (0x90)
#define POWER19_GPIO0_VOL           (0x91)
#define POWER19_GPIO1_CTL           (0x92)
#define POWER19_GPIO2_CTL           (0x93)
#define POWER19_GPIO012_SIGNAL      (0x94)
#define POWER19_SENSE_CTL           (0x95)
#define POWER19_SENSE_SIGNAL        (0x96)
#define POWER19_GPIO20_PDCTL        (0x97)
#define POWER19_PWM1_FREQ           (0x98)
#define POWER19_PWM1_DUTYDE         (0x99)
#define POWER19_PWM1_DUTY           (0x9A)
#define POWER19_PWM2_FREQ           (0x9B)
#define POWER19_PWM2_DUTYDE         (0x9C)
#define POWER19_PWM2_DUTY           (0x9D)
#define POWER19_RSTO_CTL            (0x9E)
#define POWER19_GPIO67_CTL          (0x9F)
#define POWER19_INTEN1              (0x40)
#define POWER19_INTEN2              (0x41)
#define POWER19_INTEN3              (0x42)
#define POWER19_INTEN4              (0x43)
#define POWER19_INTSTS1             (0x44)
#define POWER19_INTSTS2             (0x45)
#define POWER19_INTSTS3             (0x46)
#define POWER19_INTSTS4             (0x47)
#define POWER19_GPIO67_CFG          (0xE0)

//axp 19 adc data register
#define POWER19_BAT_AVERVOL_H8          (0x78)
#define POWER19_BAT_AVERVOL_L4          (0x79)
#define POWER19_BAT_AVERCHGCUR_H8       (0x7A)
#define POWER19_BAT_AVERCHGCUR_L5       (0x7B)
#define POWER19_ACIN_VOL_H8             (0x56)
#define POWER19_ACIN_VOL_L4             (0x57)
#define POWER19_ACIN_CUR_H8             (0x58)
#define POWER19_ACIN_CUR_L4             (0x59)
#define POWER19_VBUS_VOL_H8             (0x5A)
#define POWER19_VBUS_VOL_L4             (0x5B)
#define POWER19_VBUS_CUR_H8             (0x5C)
#define POWER19_VBUS_CUR_L4             (0x5D)
#define POWER19_BAT_AVERDISCHGCUR_H8    (0x7C)
#define POWER19_BAT_AVERDISCHGCUR_L5    (0x7D)
#define POWER19_APS_AVERVOL_H8          (0x7E)
#define POWER19_APS_AVERVOL_L4          (0x7F)
#define POWER19_BAT_CHGCOULOMB3         (0xB0)
#define POWER19_BAT_CHGCOULOMB2         (0xB1)
#define POWER19_BAT_CHGCOULOMB1         (0xB2)
#define POWER19_BAT_CHGCOULOMB0         (0xB3)
#define POWER19_BAT_DISCHGCOULOMB3      (0xB4)
#define POWER19_BAT_DISCHGCOULOMB2      (0xB5)
#define POWER19_BAT_DISCHGCOULOMB1      (0xB6)
#define POWER19_BAT_DISCHGCOULOMB0      (0xB7)
#define POWER19_COULOMB_CTL             (0xB8)
#define POWER19_BAT_POWERH8             (0x70)
#define POWER19_BAT_POWERM8             (0x71)
#define POWER19_BAT_POWERL8             (0x72)

#define AXP20                       20
#define POWER20_STATUS              (0x00)
#define POWER20_MODE_CHGSTATUS      (0x01)
#define POWER20_OTG_STATUS          (0x02)
#define POWER20_IC_TYPE             (0x03)
#define POWER20_DATA_BUFFER1        (0x04)
#define POWER20_DATA_BUFFER2        (0x05)
#define POWER20_DATA_BUFFER3        (0x06)
#define POWER20_DATA_BUFFER4        (0x07)
#define POWER20_DATA_BUFFER5        (0x08)
#define POWER20_DATA_BUFFER6        (0x09)
#define POWER20_DATA_BUFFER7        (0x0A)
#define POWER20_DATA_BUFFER8        (0x0B)
#define POWER20_DATA_BUFFER9        (0x0C)
#define POWER20_DATA_BUFFERA        (0x0D)
#define POWER20_DATA_BUFFERB        (0x0E)
#define POWER20_DATA_BUFFERC        (0x0F)
#define POWER20_LDO234_DC23_CTL     (0x12)
#define POWER20_DC2OUT_VOL          (0x23)
#define POWER20_LDO3_DC2_DVM        (0x25)
#define POWER20_DC3OUT_VOL          (0x27)
#define POWER20_LDO24OUT_VOL        (0x28)
#define POWER20_LDO3OUT_VOL         (0x29)
#define POWER20_IPS_SET             (0x30)
#define POWER20_VOFF_SET            (0x31)
#define POWER20_OFF_CTL             (0x32)
#define POWER20_CHARGE1             (0x33)
#define POWER20_CHARGE2             (0x34)
#define POWER20_BACKUP_CHG          (0x35)
#define POWER20_PEK_SET             (0x36)
#define POWER20_DCDC_FREQSET        (0x37)
#define POWER20_VLTF_CHGSET         (0x38)
#define POWER20_VHTF_CHGSET         (0x39)
#define POWER20_APS_WARNING1        (0x3A)
#define POWER20_APS_WARNING2        (0x3B)
#define POWER20_TLTF_DISCHGSET      (0x3C)
#define POWER20_THTF_DISCHGSET      (0x3D)
#define POWER20_DCDC_MODESET        (0x80)
#define POWER20_ADC_EN1             (0x82)
#define POWER20_ADC_EN2             (0x83)
#define POWER20_ADC_SPEED           (0x84)
#define POWER20_ADC_INPUTRANGE      (0x85)
#define POWER20_ADC_IRQ_RETFSET     (0x86)
#define POWER20_ADC_IRQ_FETFSET     (0x87)
#define POWER20_TIMER_CTL           (0x8A)
#define POWER20_VBUS_DET_SRP        (0x8B)
#define POWER20_HOTOVER_CTL         (0x8F)
#define POWER20_GPIO0_CTL           (0x90)
#define POWER20_GPIO0_VOL           (0x91)
#define POWER20_GPIO1_CTL           (0x92)
#define POWER20_GPIO2_CTL           (0x93)
#define POWER20_GPIO012_SIGNAL      (0x94)
#define POWER20_GPIO3_CTL           (0x95)
#define POWER20_INTEN1              (0x40)
#define POWER20_INTEN2              (0x41)
#define POWER20_INTEN3              (0x42)
#define POWER20_INTEN4              (0x43)
#define POWER20_INTEN5              (0x44)
#define POWER20_INTSTS1             (0x48)
#define POWER20_INTSTS2             (0x49)
#define POWER20_INTSTS3             (0x4A)
#define POWER20_INTSTS4             (0x4B)
#define POWER20_INTSTS5             (0x4C)

//axp 20 adc data register
#define POWER20_BAT_AVERVOL_H8          (0x78)
#define POWER20_BAT_AVERVOL_L4          (0x79)
#define POWER20_BAT_AVERCHGCUR_H8       (0x7A)
#define POWER20_BAT_AVERCHGCUR_L5       (0x7B)
#define POWER20_ACIN_VOL_H8             (0x56)
#define POWER20_ACIN_VOL_L4             (0x57)
#define POWER20_ACIN_CUR_H8             (0x58)
#define POWER20_ACIN_CUR_L4             (0x59)
#define POWER20_VBUS_VOL_H8             (0x5A)
#define POWER20_VBUS_VOL_L4             (0x5B)
#define POWER20_VBUS_CUR_H8             (0x5C)
#define POWER20_VBUS_CUR_L4             (0x5D)

#define POWER20_BAT_AVERDISCHGCUR_H8    (0x7C)
#define POWER20_BAT_AVERDISCHGCUR_L5    (0x7D)
#define POWER20_APS_AVERVOL_H8          (0x7E)
#define POWER20_APS_AVERVOL_L4          (0x7F)
#define POWER20_BAT_CHGCOULOMB3         (0xB0)
#define POWER20_BAT_CHGCOULOMB2         (0xB1)
#define POWER20_BAT_CHGCOULOMB1         (0xB2)
#define POWER20_BAT_CHGCOULOMB0         (0xB3)
#define POWER20_BAT_DISCHGCOULOMB3      (0xB4)
#define POWER20_BAT_DISCHGCOULOMB2      (0xB5)
#define POWER20_BAT_DISCHGCOULOMB1      (0xB6)
#define POWER20_BAT_DISCHGCOULOMB0      (0xB7)
#define POWER20_COULOMB_CTL             (0xB8)
#define POWER20_BAT_POWERH8             (0x70)
#define POWER20_BAT_POWERM8             (0x71)
#define POWER20_BAT_POWERL8             (0x72)


/* bit definitions for AXP events ,irq event */

/*  AXP18  */
#define	AXP18_IRQ_TEMLO								( 1<< 1)
#define	AXP18_IRQ_TEMOV								( 1<< 2)

#define	AXP18_IRQ_EXTLO								( 1<< 4)
#define	AXP18_IRQ_EXTRE								( 1<< 5)
#define	AXP18_IRQ_EXTIN								( 1<< 6)
#define	AXP18_IRQ_EXTOV      ( 1 <<  7)

#define	AXP18_IRQ_PEKLO		( 1 << 10)
#define	AXP18_IRQ_PEKSH	    ( 1 << 11)





#define	AXP18_IRQ_BATLO 	    ( 1 << 17)
#define	AXP18_IRQ_CHAOV		( 1 << 18)
#define	AXP18_IRQ_CHAST		( 1 << 19)
#define	AXP18_IRQ_BATATIN    ( 1 << 20)
#define	AXP18_IRQ_BATATOU  	( 1 << 21)
#define AXP18_IRQ_BATRE		( 1 << 22)
#define AXP18_IRQ_BATIN		( 1 << 23)

/*  AXP19  */
#define	AXP19_IRQ_USBLO		( 1 <<  1)
#define	AXP19_IRQ_USBRE		( 1 <<  2)
#define	AXP19_IRQ_USBIN		( 1 <<  3)
#define	AXP19_IRQ_USBOV     ( 1 <<  4)
#define	AXP19_IRQ_ACRE     ( 1 <<  5)
#define	AXP19_IRQ_ACIN     ( 1 <<  6)
#define	AXP19_IRQ_ACOV     ( 1 <<  7)
#define	AXP19_IRQ_TEMLO      ( 1 <<  8)
#define	AXP19_IRQ_TEMOV      ( 1 <<  9)
#define	AXP19_IRQ_CHAOV		( 1 << 10)
#define	AXP19_IRQ_CHAST 	    ( 1 << 11)
#define	AXP19_IRQ_BATATOU    ( 1 << 12)
#define	AXP19_IRQ_BATATIN  	( 1 << 13)
#define AXP19_IRQ_BATRE		( 1 << 14)
#define AXP19_IRQ_BATIN		( 1 << 15)
#define	AXP19_IRQ_PEKLO		( 1 << 16)
#define	AXP19_IRQ_PEKSH	    ( 1 << 17)
#define AXP19_IRQ_LDO3LO     ( 1 << 18)
#define AXP19_IRQ_DCDC3LO    ( 1 << 19)
#define AXP19_IRQ_DCDC2LO    ( 1 << 20)
#define AXP19_IRQ_DCDC1LO    ( 1 << 21)
#define AXP19_IRQ_CHACURLO   ( 1 << 22)
#define AXP19_IRQ_ICTEMOV    ( 1 << 23)

#define AXP19_IRQ_EXTLOWARN  ( 1 << 25)
#define AXP19_IRQ_USBSESUN  ( 1 << 26)
#define AXP19_IRQ_USBSESVA  ( 1 << 27)
#define AXP19_IRQ_USBUN     ( 1 << 28)
#define AXP19_IRQ_USBVA     ( 1 << 29)
#define AXP19_IRQ_NOECLO     ( 1 << 30)
#define AXP19_IRQ_NOEOPE     ( 1 << 31)

/*  AXP20  */
#define	AXP20_IRQ_USBLO		( 1 <<  1)
#define	AXP20_IRQ_USBRE		( 1 <<  2)
#define	AXP20_IRQ_USBIN		( 1 <<  3)
#define	AXP20_IRQ_USBOV     ( 1 <<  4)
#define	AXP20_IRQ_ACRE     ( 1 <<  5)
#define	AXP20_IRQ_ACIN     ( 1 <<  6)
#define	AXP20_IRQ_ACOV     ( 1 <<  7)
#define	AXP20_IRQ_TEMLO      ( 1 <<  8)
#define	AXP20_IRQ_TEMOV      ( 1 <<  9)
#define	AXP20_IRQ_CHAOV		( 1 << 10)
#define	AXP20_IRQ_CHAST 	    ( 1 << 11)
#define	AXP20_IRQ_BATATOU    ( 1 << 12)
#define	AXP20_IRQ_BATATIN  	( 1 << 13)
#define AXP20_IRQ_BATRE		( 1 << 14)
#define AXP20_IRQ_BATIN		( 1 << 15)
#define	AXP20_IRQ_PEKLO		( 1 << 16)
#define	AXP20_IRQ_PEKSH	    ( 1 << 17)

#define AXP20_IRQ_DCDC3LO    ( 1 << 19)
#define AXP20_IRQ_DCDC2LO    ( 1 << 20)
#define AXP20_IRQ_DCDC1LO    ( 1 << 21)
#define AXP20_IRQ_CHACURLO   ( 1 << 22)
#define AXP20_IRQ_ICTEMOV    ( 1 << 23)
#define AXP20_IRQ_EXTLOWARN1  ( 1 << 24)
#define AXP20_IRQ_EXTLOWARN2  ( 1 << 25)
#define AXP20_IRQ_USBSESUN  ( 1 << 26)
#define AXP20_IRQ_USBSESVA  ( 1 << 27)
#define AXP20_IRQ_USBUN     ( 1 << 28)
#define AXP20_IRQ_USBVA     ( 1 << 29)
#define AXP20_IRQ_NOECLO     ( 1 << 30)
#define AXP20_IRQ_NOEOPE     ( 1 << 31)
#define AXP20_IRQ_GPIO0TG     ( 1 << 32)
#define AXP20_IRQ_GPIO1TG     ( 1 << 33)
#define AXP20_IRQ_GPIO2TG     ( 1 << 34)
#define AXP20_IRQ_GPIO3TG     ( 1 << 35)

#define AXP20_IRQ_PEKFE     ( 1 << 37)
#define AXP20_IRQ_PEKRE     ( 1 << 38)
#define AXP20_IRQ_TIMER     ( 1 << 39)

/* Status Query Interface */
/*  AXP18  */
#define AXP18_STATUS_BATEN	    ( 1 <<  0)
#define AXP18_STATUS_USBEN	    ( 1 <<  1)
#define AXP18_STATUS_BATAT	    ( 1 <<  2)
#define AXP18_STATUS_SYSON	    ( 1 <<  3)
#define AXP18_STATUS_EXTVA	    ( 1 <<  4)
#define AXP18_STATUS_DC3SE	    ( 1 <<  5)
#define AXP18_STATUS_TEMOV	    ( 1 <<  6)
#define AXP18_STATUS_DCIEN	    ( 1 <<  7)

/*  AXP19  */
#define AXP19_STATUS_SOURCE    ( 1 <<  0)
#define AXP19_STATUS_ACUSBSH ( 1 <<  1)
#define AXP19_STATUS_BATCURDIR ( 1 <<  2)
#define AXP19_STATUS_USBLAVHO ( 1 <<  3)
#define AXP19_STATUS_USBVA    ( 1 <<  4)
#define AXP19_STATUS_USBEN    ( 1 <<  5)
#define AXP19_STATUS_ACVA	    ( 1 <<  6)
#define AXP19_STATUS_ACEN	    ( 1 <<  7)

#define AXP19_STATUS_OPENWAY   ( 1 <<  9)
#define AXP19_STATUS_CHACURLOEXP (1 << 10)
#define AXP19_STATUS_BATINACT  ( 1 << 11)

#define AXP19_STATUS_BATEN     ( 1 << 13)
#define AXP19_STATUS_INCHAR    ( 1 << 14)
#define AXP19_STATUS_ICTEMOV   ( 1 << 15)

/*  AXP20  */
#define AXP20_STATUS_SOURCE    ( 1 <<  0)
#define AXP20_STATUS_ACUSBSH ( 1 <<  1)
#define AXP20_STATUS_BATCURDIR ( 1 <<  2)
#define AXP20_STATUS_USBLAVHO ( 1 <<  3)
#define AXP20_STATUS_USBVA    ( 1 <<  4)
#define AXP20_STATUS_USBEN    ( 1 <<  5)
#define AXP20_STATUS_ACVA	    ( 1 <<  6)
#define AXP20_STATUS_ACEN	    ( 1 <<  7)


#define AXP20_STATUS_CHACURLOEXP (1 << 10)
#define AXP20_STATUS_BATINACT  ( 1 << 11)

#define AXP20_STATUS_BATEN     ( 1 << 13)
#define AXP20_STATUS_INCHAR    ( 1 << 14)
#define AXP20_STATUS_ICTEMOV   ( 1 << 15)


extern struct device *axp_get_dev(void);
extern int axp_register_notifier(struct device *dev,
		struct notifier_block *nb, uint64_t irqs);
extern int axp_unregister_notifier(struct device *dev,
		struct notifier_block *nb, uint64_t irqs);


/* NOTE: the functions below are not intended for use outside
 * of the AXP sub-device drivers
 */
extern int axp_write(struct device *dev, int reg, uint8_t val);
extern int axp_writes(struct device *dev, int reg, int len, uint8_t *val);
extern int axp_read(struct device *dev, int reg, uint8_t *val);
extern int axp_reads(struct device *dev, int reg, int len, uint8_t *val);
extern int axp_update(struct device *dev, int reg, uint8_t val, uint8_t mask);
extern int axp_set_bits(struct device *dev, int reg, uint8_t bit_mask);
extern int axp_clr_bits(struct device *dev, int reg, uint8_t bit_mask);
extern struct i2c_client *axp;
#endif /* __LINUX_PMIC_AXP_H */
