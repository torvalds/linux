#ifndef __LINUX_PMIC_DA903X_H
#define __LINUX_PMIC_DA903X_H

/* Unified sub device IDs for DA9030/DA9034 */
enum {
	DA9030_ID_LED_1,
	DA9030_ID_LED_2,
	DA9030_ID_LED_3,
	DA9030_ID_LED_4,
	DA9030_ID_LED_PC,
	DA9030_ID_VIBRA,
	DA9030_ID_WLED,
	DA9030_ID_BUCK1,
	DA9030_ID_BUCK2,
	DA9030_ID_LDO1,
	DA9030_ID_LDO2,
	DA9030_ID_LDO3,
	DA9030_ID_LDO4,
	DA9030_ID_LDO5,
	DA9030_ID_LDO6,
	DA9030_ID_LDO7,
	DA9030_ID_LDO8,
	DA9030_ID_LDO9,
	DA9030_ID_LDO10,
	DA9030_ID_LDO11,
	DA9030_ID_LDO12,
	DA9030_ID_LDO13,
	DA9030_ID_LDO14,
	DA9030_ID_LDO15,
	DA9030_ID_LDO16,
	DA9030_ID_LDO17,
	DA9030_ID_LDO18,
	DA9030_ID_LDO19,
	DA9030_ID_LDO_INT,	/* LDO Internal */
	DA9030_ID_BAT,		/* battery charger */

	DA9034_ID_LED_1,
	DA9034_ID_LED_2,
	DA9034_ID_VIBRA,
	DA9034_ID_WLED,
	DA9034_ID_TOUCH,

	DA9034_ID_BUCK1,
	DA9034_ID_BUCK2,
	DA9034_ID_LDO1,
	DA9034_ID_LDO2,
	DA9034_ID_LDO3,
	DA9034_ID_LDO4,
	DA9034_ID_LDO5,
	DA9034_ID_LDO6,
	DA9034_ID_LDO7,
	DA9034_ID_LDO8,
	DA9034_ID_LDO9,
	DA9034_ID_LDO10,
	DA9034_ID_LDO11,
	DA9034_ID_LDO12,
	DA9034_ID_LDO13,
	DA9034_ID_LDO14,
	DA9034_ID_LDO15,
};

/*
 * DA9030/DA9034 LEDs sub-devices uses generic "struct led_info"
 * as the platform_data
 */

/* DA9030 flags for "struct led_info"
 */
#define DA9030_LED_RATE_ON	(0 << 5)
#define DA9030_LED_RATE_052S	(1 << 5)
#define DA9030_LED_DUTY_1_16	(0 << 3)
#define DA9030_LED_DUTY_1_8	(1 << 3)
#define DA9030_LED_DUTY_1_4	(2 << 3)
#define DA9030_LED_DUTY_1_2	(3 << 3)

#define DA9030_VIBRA_MODE_1P3V	(0 << 1)
#define DA9030_VIBRA_MODE_2P7V	(1 << 1)
#define DA9030_VIBRA_FREQ_1HZ	(0 << 2)
#define DA9030_VIBRA_FREQ_2HZ	(1 << 2)
#define DA9030_VIBRA_FREQ_4HZ	(2 << 2)
#define DA9030_VIBRA_FREQ_8HZ	(3 << 2)
#define DA9030_VIBRA_DUTY_ON	(0 << 4)
#define DA9030_VIBRA_DUTY_75P	(1 << 4)
#define DA9030_VIBRA_DUTY_50P	(2 << 4)
#define DA9030_VIBRA_DUTY_25P	(3 << 4)

/* DA9034 flags for "struct led_info" */
#define DA9034_LED_RAMP		(1 << 7)

/* DA9034 touch screen platform data */
struct da9034_touch_pdata {
	int	interval_ms;	/* sampling interval while pen down */
	int	x_inverted;
	int	y_inverted;
};

/* DA9030 battery charger data */
struct power_supply_info;

struct da9030_battery_info {
	/* battery parameters */
	struct power_supply_info *battery_info;

	/* current and voltage to use for battery charging */
	unsigned int charge_milliamp;
	unsigned int charge_millivolt;

	/* voltage thresholds (in millivolts) */
	int vbat_low;
	int vbat_crit;
	int vbat_charge_start;
	int vbat_charge_stop;
	int vbat_charge_restart;

	/* battery nominal minimal and maximal voltages in millivolts */
	int vcharge_min;
	int vcharge_max;

	/* Temperature thresholds. These are DA9030 register values
	   "as is" and should be measured for each battery type */
	int tbat_low;
	int tbat_high;
	int tbat_restart;


	/* battery monitor interval (seconds) */
	unsigned int batmon_interval;

	/* platform callbacks for battery low and critical events */
	void (*battery_low)(void);
	void (*battery_critical)(void);
};

struct da903x_subdev_info {
	int		id;
	const char	*name;
	void		*platform_data;
};

struct da903x_platform_data {
	int num_subdevs;
	struct da903x_subdev_info *subdevs;
};

/* bit definitions for DA9030 events */
#define DA9030_EVENT_ONKEY		(1 << 0)
#define	DA9030_EVENT_PWREN		(1 << 1)
#define	DA9030_EVENT_EXTON		(1 << 2)
#define	DA9030_EVENT_CHDET		(1 << 3)
#define	DA9030_EVENT_TBAT		(1 << 4)
#define	DA9030_EVENT_VBATMON		(1 << 5)
#define	DA9030_EVENT_VBATMON_TXON	(1 << 6)
#define	DA9030_EVENT_CHIOVER		(1 << 7)
#define	DA9030_EVENT_TCTO		(1 << 8)
#define	DA9030_EVENT_CCTO		(1 << 9)
#define	DA9030_EVENT_ADC_READY		(1 << 10)
#define	DA9030_EVENT_VBUS_4P4		(1 << 11)
#define	DA9030_EVENT_VBUS_4P0		(1 << 12)
#define	DA9030_EVENT_SESS_VALID		(1 << 13)
#define	DA9030_EVENT_SRP_DETECT		(1 << 14)
#define	DA9030_EVENT_WATCHDOG		(1 << 15)
#define	DA9030_EVENT_LDO15		(1 << 16)
#define	DA9030_EVENT_LDO16		(1 << 17)
#define	DA9030_EVENT_LDO17		(1 << 18)
#define	DA9030_EVENT_LDO18		(1 << 19)
#define	DA9030_EVENT_LDO19		(1 << 20)
#define	DA9030_EVENT_BUCK2		(1 << 21)

/* bit definitions for DA9034 events */
#define DA9034_EVENT_ONKEY		(1 << 0)
#define DA9034_EVENT_EXTON		(1 << 2)
#define DA9034_EVENT_CHDET		(1 << 3)
#define DA9034_EVENT_TBAT		(1 << 4)
#define DA9034_EVENT_VBATMON		(1 << 5)
#define DA9034_EVENT_REV_IOVER		(1 << 6)
#define DA9034_EVENT_CH_IOVER		(1 << 7)
#define DA9034_EVENT_CH_TCTO		(1 << 8)
#define DA9034_EVENT_CH_CCTO		(1 << 9)
#define DA9034_EVENT_USB_DEV		(1 << 10)
#define DA9034_EVENT_OTGCP_IOVER	(1 << 11)
#define DA9034_EVENT_VBUS_4P55		(1 << 12)
#define DA9034_EVENT_VBUS_3P8		(1 << 13)
#define DA9034_EVENT_SESS_1P8		(1 << 14)
#define DA9034_EVENT_SRP_READY		(1 << 15)
#define DA9034_EVENT_ADC_MAN		(1 << 16)
#define DA9034_EVENT_ADC_AUTO4		(1 << 17)
#define DA9034_EVENT_ADC_AUTO5		(1 << 18)
#define DA9034_EVENT_ADC_AUTO6		(1 << 19)
#define DA9034_EVENT_PEN_DOWN		(1 << 20)
#define DA9034_EVENT_TSI_READY		(1 << 21)
#define DA9034_EVENT_UART_TX		(1 << 22)
#define DA9034_EVENT_UART_RX		(1 << 23)
#define DA9034_EVENT_HEADSET		(1 << 25)
#define DA9034_EVENT_HOOKSWITCH		(1 << 26)
#define DA9034_EVENT_WATCHDOG		(1 << 27)

extern int da903x_register_notifier(struct device *dev,
		struct notifier_block *nb, unsigned int events);
extern int da903x_unregister_notifier(struct device *dev,
		struct notifier_block *nb, unsigned int events);

/* Status Query Interface */
#define DA9030_STATUS_ONKEY		(1 << 0)
#define DA9030_STATUS_PWREN1		(1 << 1)
#define DA9030_STATUS_EXTON		(1 << 2)
#define DA9030_STATUS_CHDET		(1 << 3)
#define DA9030_STATUS_TBAT		(1 << 4)
#define DA9030_STATUS_VBATMON		(1 << 5)
#define DA9030_STATUS_VBATMON_TXON	(1 << 6)
#define DA9030_STATUS_MCLKDET		(1 << 7)

#define DA9034_STATUS_ONKEY		(1 << 0)
#define DA9034_STATUS_EXTON		(1 << 2)
#define DA9034_STATUS_CHDET		(1 << 3)
#define DA9034_STATUS_TBAT		(1 << 4)
#define DA9034_STATUS_VBATMON		(1 << 5)
#define DA9034_STATUS_PEN_DOWN		(1 << 6)
#define DA9034_STATUS_MCLKDET		(1 << 7)
#define DA9034_STATUS_USB_DEV		(1 << 8)
#define DA9034_STATUS_HEADSET		(1 << 9)
#define DA9034_STATUS_HOOKSWITCH	(1 << 10)
#define DA9034_STATUS_REMCON		(1 << 11)
#define DA9034_STATUS_VBUS_VALID_4P55	(1 << 12)
#define DA9034_STATUS_VBUS_VALID_3P8	(1 << 13)
#define DA9034_STATUS_SESS_VALID_1P8	(1 << 14)
#define DA9034_STATUS_SRP_READY		(1 << 15)

extern int da903x_query_status(struct device *dev, unsigned int status);


/* NOTE: the functions below are not intended for use outside
 * of the DA903x sub-device drivers
 */
extern int da903x_write(struct device *dev, int reg, uint8_t val);
extern int da903x_writes(struct device *dev, int reg, int len, uint8_t *val);
extern int da903x_read(struct device *dev, int reg, uint8_t *val);
extern int da903x_reads(struct device *dev, int reg, int len, uint8_t *val);
extern int da903x_update(struct device *dev, int reg, uint8_t val, uint8_t mask);
extern int da903x_set_bits(struct device *dev, int reg, uint8_t bit_mask);
extern int da903x_clr_bits(struct device *dev, int reg, uint8_t bit_mask);
#endif /* __LINUX_PMIC_DA903X_H */
