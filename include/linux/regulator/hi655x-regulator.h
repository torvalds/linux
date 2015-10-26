/*
 * Device driver for regulators in HI655X IC
 *
 * Copyright (c) 2015 Hisilicon.
 *
 * Fei Wang <w.f@huawei.com>
 *
 * this regulator's probe function will be called lots of times,,
 * because of there are lots of regulator nodes in dtb.
 * so,that's say, the driver must be inited before the regulator nodes
 * registor to system.
 *
 * Makefile have proved my guess, please refor to the makefile.
 * when the code is rebuild i hope we can build pmu sub_system.
 * init order can not base on compile
 */

#ifndef __HISI_HI655X_REGULATOR_H__
#define __HISI_HI655X_REGULATOR_H__

enum hi655x_regulator_type {
	PMIC_BUCK_TYPE = 0,
	PMIC_LDO_TYPE  = 1,
	PMIC_LVS_TYPE  = 2,
	PMIC_BOOST_TYPE = 3,
	MTCMOS_SC_ON_TYPE      = 4,
	MTCMOS_ACPU_ON_TYPE    = 5,
	SCHARGE_TYPE           = 6,
};

struct hi655x_regulator_ctrl_regs {
	unsigned int  enable_reg;
	unsigned int  disable_reg;
	unsigned int  status_reg;
};

struct hi655x_regulator_vset_regs {
	unsigned int vset_reg;
};

struct hi655x_regulator_ctrl_data {
	int          shift;
	unsigned int mask;
};

struct hi655x_regulator_vset_data {
	int          shift;
	unsigned int mask;
};

struct hi655x_regulator {
	int status;                             /*this property in child  node*/
	unsigned int off_on_delay;              /*this property in parent node*/
	enum hi655x_regulator_type type;        /*this property in child  node*/
	int regulator_id;

	/*this property must be unify which is in child node*/
	struct hi655x_regulator_ctrl_regs   ctrl_regs;
	struct hi655x_regulator_ctrl_data   ctrl_data;

	struct hi655x_regulator_vset_regs   vset_regs;
	struct hi655x_regulator_vset_data   vset_data;
	unsigned int                        vol_numb;
	unsigned int                        *vset_table;
	struct regulator_desc rdesc;
	int (*dt_parse)(struct hi655x_regulator*, struct platform_device*);
};

#endif
