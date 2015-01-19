#ifndef MESON_CS_DCDC_REGULATOR_H
#define MESON_CS_DCDC_REGULATOR_H
#include <linux/regulator/driver.h>

#define MESON_CS_MAX_STEPS 16

struct meson_cs_pdata_t {
	struct regulator_init_data *meson_cs_init_data;
	int voltage_step_table[MESON_CS_MAX_STEPS];
	int default_uV;
       int (*get_voltage)(void);
       int (*set_voltage)(unsigned int);
};


struct meson_cs_regulator_dev {
	struct regulator *regulator;
	struct regulator_desc desc;
	struct regulator_dev *rdev;
	int min_uV;
	int max_uV;
	int cur_uV;
	struct mutex io_lock;
	int *voltage_step_table;
};

int meson_cs_dcdc_set_voltage_global(int minuV, int maxuV);
int meson_cs_dcdc_get_voltage_global(void);

#endif
