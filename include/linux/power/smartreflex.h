/* SPDX-License-Identifier: GPL-2.0 */
/*
 * OMAP Smartreflex Defines and Routines
 *
 * Author: Thara Gopinath	<thara@ti.com>
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Thara Gopinath <thara@ti.com>
 *
 * Copyright (C) 2008 Nokia Corporation
 * Kalle Jokiniemi
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Lesly A M <x0080970@ti.com>
 */

#ifndef __POWER_SMARTREFLEX_H
#define __POWER_SMARTREFLEX_H

#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/platform_data/voltage-omap.h>

/*
 * Different Smartreflex IPs version. The v1 is the 65nm version used in
 * OMAP3430. The v2 is the update for the 45nm version of the IP
 * used in OMAP3630 and OMAP4430
 */
#define SR_TYPE_V1	1
#define SR_TYPE_V2	2

/* SMART REFLEX REG ADDRESS OFFSET */
#define SRCONFIG		0x00
#define SRSTATUS		0x04
#define SENVAL			0x08
#define SENMIN			0x0C
#define SENMAX			0x10
#define SENAVG			0x14
#define AVGWEIGHT		0x18
#define NVALUERECIPROCAL	0x1c
#define SENERROR_V1		0x20
#define ERRCONFIG_V1		0x24
#define IRQ_EOI			0x20
#define IRQSTATUS_RAW		0x24
#define IRQSTATUS		0x28
#define IRQENABLE_SET		0x2C
#define IRQENABLE_CLR		0x30
#define SENERROR_V2		0x34
#define ERRCONFIG_V2		0x38

/* Bit/Shift Positions */

/* SRCONFIG */
#define SRCONFIG_ACCUMDATA_SHIFT	22
#define SRCONFIG_SRCLKLENGTH_SHIFT	12
#define SRCONFIG_SENNENABLE_V1_SHIFT	5
#define SRCONFIG_SENPENABLE_V1_SHIFT	3
#define SRCONFIG_SENNENABLE_V2_SHIFT	1
#define SRCONFIG_SENPENABLE_V2_SHIFT	0
#define SRCONFIG_CLKCTRL_SHIFT		0

#define SRCONFIG_ACCUMDATA_MASK		(0x3ff << 22)

#define SRCONFIG_SRENABLE		BIT(11)
#define SRCONFIG_SENENABLE		BIT(10)
#define SRCONFIG_ERRGEN_EN		BIT(9)
#define SRCONFIG_MINMAXAVG_EN		BIT(8)
#define SRCONFIG_DELAYCTRL		BIT(2)

/* AVGWEIGHT */
#define AVGWEIGHT_SENPAVGWEIGHT_SHIFT	2
#define AVGWEIGHT_SENNAVGWEIGHT_SHIFT	0

/* NVALUERECIPROCAL */
#define NVALUERECIPROCAL_SENPGAIN_SHIFT	20
#define NVALUERECIPROCAL_SENNGAIN_SHIFT	16
#define NVALUERECIPROCAL_RNSENP_SHIFT	8
#define NVALUERECIPROCAL_RNSENN_SHIFT	0

/* ERRCONFIG */
#define ERRCONFIG_ERRWEIGHT_SHIFT	16
#define ERRCONFIG_ERRMAXLIMIT_SHIFT	8
#define ERRCONFIG_ERRMINLIMIT_SHIFT	0

#define SR_ERRWEIGHT_MASK		(0x07 << 16)
#define SR_ERRMAXLIMIT_MASK		(0xff << 8)
#define SR_ERRMINLIMIT_MASK		(0xff << 0)

#define ERRCONFIG_VPBOUNDINTEN_V1	BIT(31)
#define ERRCONFIG_VPBOUNDINTST_V1	BIT(30)
#define	ERRCONFIG_MCUACCUMINTEN		BIT(29)
#define ERRCONFIG_MCUACCUMINTST		BIT(28)
#define	ERRCONFIG_MCUVALIDINTEN		BIT(27)
#define ERRCONFIG_MCUVALIDINTST		BIT(26)
#define ERRCONFIG_MCUBOUNDINTEN		BIT(25)
#define	ERRCONFIG_MCUBOUNDINTST		BIT(24)
#define	ERRCONFIG_MCUDISACKINTEN	BIT(23)
#define ERRCONFIG_VPBOUNDINTST_V2	BIT(23)
#define ERRCONFIG_MCUDISACKINTST	BIT(22)
#define ERRCONFIG_VPBOUNDINTEN_V2	BIT(22)

#define ERRCONFIG_STATUS_V1_MASK	(ERRCONFIG_VPBOUNDINTST_V1 | \
					ERRCONFIG_MCUACCUMINTST | \
					ERRCONFIG_MCUVALIDINTST | \
					ERRCONFIG_MCUBOUNDINTST | \
					ERRCONFIG_MCUDISACKINTST)
/* IRQSTATUS */
#define IRQSTATUS_MCUACCUMINT		BIT(3)
#define IRQSTATUS_MCVALIDINT		BIT(2)
#define IRQSTATUS_MCBOUNDSINT		BIT(1)
#define IRQSTATUS_MCUDISABLEACKINT	BIT(0)

/* IRQENABLE_SET and IRQENABLE_CLEAR */
#define IRQENABLE_MCUACCUMINT		BIT(3)
#define IRQENABLE_MCUVALIDINT		BIT(2)
#define IRQENABLE_MCUBOUNDSINT		BIT(1)
#define IRQENABLE_MCUDISABLEACKINT	BIT(0)

/* Common Bit values */

#define SRCLKLENGTH_12MHZ_SYSCLK	0x3c
#define SRCLKLENGTH_13MHZ_SYSCLK	0x41
#define SRCLKLENGTH_19MHZ_SYSCLK	0x60
#define SRCLKLENGTH_26MHZ_SYSCLK	0x82
#define SRCLKLENGTH_38MHZ_SYSCLK	0xC0

/*
 * 3430 specific values. Maybe these should be passed from board file or
 * pmic structures.
 */
#define OMAP3430_SR_ACCUMDATA		0x1f4

#define OMAP3430_SR1_SENPAVGWEIGHT	0x03
#define OMAP3430_SR1_SENNAVGWEIGHT	0x03

#define OMAP3430_SR2_SENPAVGWEIGHT	0x01
#define OMAP3430_SR2_SENNAVGWEIGHT	0x01

#define OMAP3430_SR_ERRWEIGHT		0x04
#define OMAP3430_SR_ERRMAXLIMIT		0x02

enum sr_instance {
	OMAP_SR_MPU,			/* shared with iva on omap3 */
	OMAP_SR_CORE,
	OMAP_SR_IVA,
	OMAP_SR_NR,
};

struct omap_sr {
	char				*name;
	struct list_head		node;
	struct platform_device		*pdev;
	struct omap_sr_nvalue_table	*nvalue_table;
	struct voltagedomain		*voltdm;
	struct dentry			*dbg_dir;
	unsigned int			irq;
	int				srid;
	int				ip_type;
	int				nvalue_count;
	bool				autocomp_active;
	u32				clk_length;
	u32				err_weight;
	u32				err_minlimit;
	u32				err_maxlimit;
	u32				accum_data;
	u32				senn_avgweight;
	u32				senp_avgweight;
	u32				senp_mod;
	u32				senn_mod;
	void __iomem			*base;
};

/**
 * test_cond_timeout - busy-loop, testing a condition
 * @cond: condition to test until it evaluates to true
 * @timeout: maximum number of microseconds in the timeout
 * @index: loop index (integer)
 *
 * Loop waiting for @cond to become true or until at least @timeout
 * microseconds have passed.  To use, define some integer @index in the
 * calling code.  After running, if @index == @timeout, then the loop has
 * timed out.
 *
 * Copied from omap_test_timeout */
#define sr_test_cond_timeout(cond, timeout, index)		\
({								\
	for (index = 0; index < timeout; index++) {		\
		if (cond)					\
			break;					\
		udelay(1);					\
	}							\
})

/**
 * struct omap_sr_pmic_data - Strucutre to be populated by pmic code to pass
 *				pmic specific info to smartreflex driver
 *
 * @sr_pmic_init:	API to initialize smartreflex on the PMIC side.
 */
struct omap_sr_pmic_data {
	void (*sr_pmic_init) (void);
};

/**
 * struct omap_smartreflex_dev_attr - Smartreflex Device attribute.
 *
 * @sensor_voltdm_name:       Name of voltdomain of SR instance
 */
struct omap_smartreflex_dev_attr {
	const char      *sensor_voltdm_name;
};

/*
 * The smart reflex driver supports CLASS1 CLASS2 and CLASS3 SR.
 * The smartreflex class driver should pass the class type.
 * Should be used to populate the class_type field of the
 * omap_smartreflex_class_data structure.
 */
#define SR_CLASS1	0x1
#define SR_CLASS2	0x2
#define SR_CLASS3	0x3

/**
 * struct omap_sr_class_data - Smartreflex class driver info
 *
 * @enable:		API to enable a particular class smaartreflex.
 * @disable:		API to disable a particular class smartreflex.
 * @configure:		API to configure a particular class smartreflex.
 * @notify:		API to notify the class driver about an event in SR.
 *			Not needed for class3.
 * @notify_flags:	specify the events to be notified to the class driver
 * @class_type:		specify which smartreflex class.
 *			Can be used by the SR driver to take any class
 *			based decisions.
 */
struct omap_sr_class_data {
	int (*enable)(struct omap_sr *sr);
	int (*disable)(struct omap_sr *sr, int is_volt_reset);
	int (*configure)(struct omap_sr *sr);
	int (*notify)(struct omap_sr *sr, u32 status);
	u8 notify_flags;
	u8 class_type;
};

/**
 * struct omap_sr_nvalue_table	- Smartreflex n-target value info
 *
 * @efuse_offs:	  The offset of the efuse where n-target values are stored.
 * @nvalue:	  The n-target value.
 * @errminlimit:  The value of the ERRMINLIMIT bitfield for this n-target
 * @volt_nominal: microvolts DC that the VDD is initially programmed to
 */
struct omap_sr_nvalue_table {
	u32 efuse_offs;
	u32 nvalue;
	u32 errminlimit;
	unsigned long volt_nominal;
};

/**
 * struct omap_sr_data - Smartreflex platform data.
 *
 * @name:		instance name
 * @ip_type:		Smartreflex IP type.
 * @senp_mod:		SENPENABLE value of the sr CONFIG register
 * @senn_mod:		SENNENABLE value for sr CONFIG register
 * @err_weight		ERRWEIGHT value of the sr ERRCONFIG register
 * @err_maxlimit	ERRMAXLIMIT value of the sr ERRCONFIG register
 * @accum_data		ACCUMDATA value of the sr CONFIG register
 * @senn_avgweight	SENNAVGWEIGHT value of the sr AVGWEIGHT register
 * @senp_avgweight	SENPAVGWEIGHT value of the sr AVGWEIGHT register
 * @nvalue_count:	Number of distinct nvalues in the nvalue table
 * @enable_on_init:	whether this sr module needs to enabled at
 *			boot up or not.
 * @nvalue_table:	table containing the  efuse offsets and nvalues
 *			corresponding to them.
 * @voltdm:		Pointer to the voltage domain associated with the SR
 */
struct omap_sr_data {
	const char			*name;
	int				ip_type;
	u32				senp_mod;
	u32				senn_mod;
	u32				err_weight;
	u32				err_maxlimit;
	u32				accum_data;
	u32				senn_avgweight;
	u32				senp_avgweight;
	int				nvalue_count;
	bool				enable_on_init;
	struct omap_sr_nvalue_table	*nvalue_table;
	struct voltagedomain		*voltdm;
};


extern struct omap_sr_data omap_sr_pdata[OMAP_SR_NR];

#ifdef CONFIG_POWER_AVS_OMAP

/* Smartreflex module enable/disable interface */
void omap_sr_enable(struct voltagedomain *voltdm);
void omap_sr_disable(struct voltagedomain *voltdm);
void omap_sr_disable_reset_volt(struct voltagedomain *voltdm);

/* Smartreflex driver hooks to be called from Smartreflex class driver */
int sr_enable(struct omap_sr *sr, unsigned long volt);
void sr_disable(struct omap_sr *sr);
int sr_configure_errgen(struct omap_sr *sr);
int sr_disable_errgen(struct omap_sr *sr);
int sr_configure_minmax(struct omap_sr *sr);

/* API to register the smartreflex class driver with the smartreflex driver */
int sr_register_class(struct omap_sr_class_data *class_data);
#else
static inline void omap_sr_enable(struct voltagedomain *voltdm) {}
static inline void omap_sr_disable(struct voltagedomain *voltdm) {}
static inline void omap_sr_disable_reset_volt(
		struct voltagedomain *voltdm) {}
#endif
#endif
