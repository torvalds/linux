#ifndef __LINUX_MFD_TPS6586X_H
#define __LINUX_MFD_TPS6586X_H

#define TPS6586X_SLEW_RATE_INSTANTLY	0x00
#define TPS6586X_SLEW_RATE_110UV	0x01
#define TPS6586X_SLEW_RATE_220UV	0x02
#define TPS6586X_SLEW_RATE_440UV	0x03
#define TPS6586X_SLEW_RATE_880UV	0x04
#define TPS6586X_SLEW_RATE_1760UV	0x05
#define TPS6586X_SLEW_RATE_3520UV	0x06
#define TPS6586X_SLEW_RATE_7040UV	0x07

#define TPS6586X_SLEW_RATE_SET		0x08
#define TPS6586X_SLEW_RATE_MASK         0x07

enum {
	TPS6586X_ID_SM_0,
	TPS6586X_ID_SM_1,
	TPS6586X_ID_SM_2,
	TPS6586X_ID_LDO_0,
	TPS6586X_ID_LDO_1,
	TPS6586X_ID_LDO_2,
	TPS6586X_ID_LDO_3,
	TPS6586X_ID_LDO_4,
	TPS6586X_ID_LDO_5,
	TPS6586X_ID_LDO_6,
	TPS6586X_ID_LDO_7,
	TPS6586X_ID_LDO_8,
	TPS6586X_ID_LDO_9,
	TPS6586X_ID_LDO_RTC,
};

enum {
	TPS6586X_INT_PLDO_0,
	TPS6586X_INT_PLDO_1,
	TPS6586X_INT_PLDO_2,
	TPS6586X_INT_PLDO_3,
	TPS6586X_INT_PLDO_4,
	TPS6586X_INT_PLDO_5,
	TPS6586X_INT_PLDO_6,
	TPS6586X_INT_PLDO_7,
	TPS6586X_INT_COMP_DET,
	TPS6586X_INT_ADC,
	TPS6586X_INT_PLDO_8,
	TPS6586X_INT_PLDO_9,
	TPS6586X_INT_PSM_0,
	TPS6586X_INT_PSM_1,
	TPS6586X_INT_PSM_2,
	TPS6586X_INT_PSM_3,
	TPS6586X_INT_RTC_ALM1,
	TPS6586X_INT_ACUSB_OVP,
	TPS6586X_INT_USB_DET,
	TPS6586X_INT_AC_DET,
	TPS6586X_INT_BAT_DET,
	TPS6586X_INT_CHG_STAT,
	TPS6586X_INT_CHG_TEMP,
	TPS6586X_INT_PP,
	TPS6586X_INT_RESUME,
	TPS6586X_INT_LOW_SYS,
	TPS6586X_INT_RTC_ALM2,
};

struct tps6586x_settings {
	int slew_rate;
};

struct tps6586x_subdev_info {
	int		id;
	const char	*name;
	void		*platform_data;
	struct device_node *of_node;
};

struct tps6586x_platform_data {
	int num_subdevs;
	struct tps6586x_subdev_info *subdevs;

	int gpio_base;
	int irq_base;
};

/*
 * NOTE: the functions below are not intended for use outside
 * of the TPS6586X sub-device drivers
 */
extern int tps6586x_write(struct device *dev, int reg, uint8_t val);
extern int tps6586x_writes(struct device *dev, int reg, int len, uint8_t *val);
extern int tps6586x_read(struct device *dev, int reg, uint8_t *val);
extern int tps6586x_reads(struct device *dev, int reg, int len, uint8_t *val);
extern int tps6586x_set_bits(struct device *dev, int reg, uint8_t bit_mask);
extern int tps6586x_clr_bits(struct device *dev, int reg, uint8_t bit_mask);
extern int tps6586x_update(struct device *dev, int reg, uint8_t val,
			   uint8_t mask);

#endif /*__LINUX_MFD_TPS6586X_H */
