#ifndef __LINUX_MFD_TPS6586X_H
#define __LINUX_MFD_TPS6586X_H

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

struct tps6586x_subdev_info {
	int		id;
	const char	*name;
	void		*platform_data;
};

struct tps6586x_platform_data {
	int num_subdevs;
	struct tps6586x_subdev_info *subdevs;

	int gpio_base;
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
