// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dm_services.h"
#include "include/gpio_types.h"
#include "../hw_factory.h"


#include "../hw_gpio.h"
#include "../hw_ddc.h"
#include "../hw_hpd.h"
#include "../hw_generic.h"


#include "dcn/dcn_4_1_0_offset.h"
#include "dcn/dcn_4_1_0_sh_mask.h"

#include "reg_helper.h"
#include "../hpd_regs.h"
#include "hw_factory_dcn401.h"

#define DCN_BASE__INST0_SEG2                       0x000034C0

/* begin *********************
 * macros to expend register list macro defined in HW object header file */

/* DCN */
#define block HPD
#define reg_num 0

#undef BASE_INNER
#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)



#define REG(reg_name)\
		BASE(reg ## reg_name ## _BASE_IDX) + reg ## reg_name

#define SF_HPD(reg_name, field_name, post_fix)\
	.field_name = HPD0_ ## reg_name ## __ ## field_name ## post_fix

#define REGI(reg_name, block, id)\
	BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
				reg ## block ## id ## _ ## reg_name

#define SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

/* macros to expend register list macro defined in HW object header file
 * end *********************/



#define hpd_regs(id) \
{\
	HPD_REG_LIST(id)\
}

static const struct hpd_registers hpd_regs[] = {
	hpd_regs(0),
	hpd_regs(1),
	hpd_regs(2),
	hpd_regs(3),
//	hpd_regs(4),
};

static const struct hpd_sh_mask hpd_shift = {
		HPD_MASK_SH_LIST(__SHIFT)
};

static const struct hpd_sh_mask hpd_mask = {
		HPD_MASK_SH_LIST(_MASK)
};

#include "../ddc_regs.h"

 /* set field name */
#define SF_DDC(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

static const struct ddc_registers ddc_data_regs_dcn[] = {
	ddc_data_regs_dcn2(1),
	ddc_data_regs_dcn2(2),
	ddc_data_regs_dcn2(3),
	ddc_data_regs_dcn2(4),
	{
		// add a dummy entry for cases no such port
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,},
		.ddc_setup = 0,
		.phy_aux_cntl = 0,
		.dc_gpio_aux_ctrl_5 = 0
	},
	{
		// add a dummy entry for cases no such port
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,},
		.ddc_setup = 0,
		.phy_aux_cntl = 0,
		.dc_gpio_aux_ctrl_5 = 0
	},
	{
			DDC_GPIO_VGA_REG_LIST(DATA),
			.ddc_setup = 0,
			.phy_aux_cntl = 0,
			.dc_gpio_aux_ctrl_5 = 0
	}
};

static const struct ddc_registers ddc_clk_regs_dcn[] = {
	ddc_clk_regs_dcn2(1),
	ddc_clk_regs_dcn2(2),
	ddc_clk_regs_dcn2(3),
	ddc_clk_regs_dcn2(4),
	{
		// add a dummy entry for cases no such port
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,},
		.ddc_setup = 0,
		.phy_aux_cntl = 0,
		.dc_gpio_aux_ctrl_5 = 0
	},
	{
		// add a dummy entry for cases no such port
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,},
		.ddc_setup = 0,
		.phy_aux_cntl = 0,
		.dc_gpio_aux_ctrl_5 = 0
	},
	{
			DDC_GPIO_VGA_REG_LIST(CLK),
			.ddc_setup = 0,
			.phy_aux_cntl = 0,
			.dc_gpio_aux_ctrl_5 = 0
	}
};

static const struct ddc_sh_mask ddc_shift[] = {
	DDC_MASK_SH_LIST_DCN2(__SHIFT, 1),
	DDC_MASK_SH_LIST_DCN2(__SHIFT, 2),
	DDC_MASK_SH_LIST_DCN2(__SHIFT, 3),
	DDC_MASK_SH_LIST_DCN2(__SHIFT, 4),
	DDC_MASK_SH_LIST_DCN2(__SHIFT, 5),
	DDC_MASK_SH_LIST_DCN2(__SHIFT, 6),
	DDC_MASK_SH_LIST_DCN2_VGA(__SHIFT)
};

static const struct ddc_sh_mask ddc_mask[] = {
	DDC_MASK_SH_LIST_DCN2(_MASK, 1),
	DDC_MASK_SH_LIST_DCN2(_MASK, 2),
	DDC_MASK_SH_LIST_DCN2(_MASK, 3),
	DDC_MASK_SH_LIST_DCN2(_MASK, 4),
	DDC_MASK_SH_LIST_DCN2(_MASK, 5),
	DDC_MASK_SH_LIST_DCN2(_MASK, 6),
	DDC_MASK_SH_LIST_DCN2_VGA(_MASK)
};

#include "../generic_regs.h"

/* set field name */
#define SF_GENERIC(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define generic_regs(id) \
{\
	GENERIC_REG_LIST(id)\
}

static const struct generic_registers generic_regs[] = {
	generic_regs(A),
	generic_regs(B),
};

static const struct generic_sh_mask generic_shift[] = {
	GENERIC_MASK_SH_LIST(__SHIFT, A),
	GENERIC_MASK_SH_LIST(__SHIFT, B),
};

static const struct generic_sh_mask generic_mask[] = {
	GENERIC_MASK_SH_LIST(_MASK, A),
	GENERIC_MASK_SH_LIST(_MASK, B),
};

static void define_generic_registers(struct hw_gpio_pin *pin, uint32_t en)
{
	struct hw_generic *generic = HW_GENERIC_FROM_BASE(pin);

	generic->regs = &generic_regs[en];
	generic->shifts = &generic_shift[en];
	generic->masks = &generic_mask[en];
	generic->base.regs = &generic_regs[en].gpio;
}

static void define_ddc_registers(
		struct hw_gpio_pin *pin,
		uint32_t en)
{
	struct hw_ddc *ddc = HW_DDC_FROM_BASE(pin);

	switch (pin->id) {
	case GPIO_ID_DDC_DATA:
		ddc->regs = &ddc_data_regs_dcn[en];
		ddc->base.regs = &ddc_data_regs_dcn[en].gpio;
		break;
	case GPIO_ID_DDC_CLOCK:
		ddc->regs = &ddc_clk_regs_dcn[en];
		ddc->base.regs = &ddc_clk_regs_dcn[en].gpio;
		break;
	default:
		ASSERT_CRITICAL(false);
		return;
	}

	ddc->shifts = &ddc_shift[en];
	ddc->masks = &ddc_mask[en];

}

static void define_hpd_registers(struct hw_gpio_pin *pin, uint32_t en)
{
	struct hw_hpd *hpd = HW_HPD_FROM_BASE(pin);

	hpd->regs = &hpd_regs[en];
	hpd->shifts = &hpd_shift;
	hpd->masks = &hpd_mask;
	hpd->base.regs = &hpd_regs[en].gpio;
}


/* function table */
static const struct hw_factory_funcs funcs = {
	.init_ddc_data = dal_hw_ddc_init,
	.init_generic = dal_hw_generic_init,
	.init_hpd = dal_hw_hpd_init,
	.get_ddc_pin = dal_hw_ddc_get_pin,
	.get_hpd_pin = dal_hw_hpd_get_pin,
	.get_generic_pin = dal_hw_generic_get_pin,
	.define_hpd_registers = define_hpd_registers,
	.define_ddc_registers = define_ddc_registers,
	.define_generic_registers = define_generic_registers
};

/*
 * dal_hw_factory_dcn401_init
 *
 * @brief
 * Initialize HW factory function pointers and pin info
 *
 * @param
 * struct hw_factory *factory - [out] struct of function pointers
 */
void dal_hw_factory_dcn401_init(struct hw_factory *factory)
{
	factory->number_of_pins[GPIO_ID_DDC_DATA] = 8;
	factory->number_of_pins[GPIO_ID_DDC_CLOCK] = 8;
	factory->number_of_pins[GPIO_ID_GENERIC] = 4;
	factory->number_of_pins[GPIO_ID_HPD] = 5;
	factory->number_of_pins[GPIO_ID_GPIO_PAD] = 28;
	factory->number_of_pins[GPIO_ID_VIP_PAD] = 0;
	factory->number_of_pins[GPIO_ID_SYNC] = 0;
	factory->number_of_pins[GPIO_ID_GSL] = 0;/*add this*/

	factory->funcs = &funcs;
}
