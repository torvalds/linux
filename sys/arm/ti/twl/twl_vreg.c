/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011
 *	Ben Gray <ben.r.gray@gmail.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Texas Instruments TWL4030/TWL5030/TWL60x0/TPS659x0 Power Management.
 *
 * This driver covers the voltages regulators (LDO), allows for enabling &
 * disabling the voltage output and adjusting the voltage level.
 *
 * Voltage regulators can belong to different power groups, in this driver we
 * put the regulators under our control in the "Application power group".
 *
 *
 * FLATTENED DEVICE TREE (FDT)
 * Startup override settings can be specified in the FDT, if they are they
 * should be under the twl parent device and take the following form:
 *
 *    voltage-regulators = "name1", "millivolts1",
 *                         "name2", "millivolts2";
 *
 * Each override should be a pair, the first entry is the name of the regulator
 * the second is the voltage (in millivolts) to set for the given regulator.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>

#include "twl.h"
#include "twl_vreg.h"

static int twl_vreg_debug = 1;


/*
 * Power Groups bits for the 4030 and 6030 devices
 */
#define TWL4030_P3_GRP		0x80	/* Peripherals, power group */
#define TWL4030_P2_GRP		0x40	/* Modem power group */
#define TWL4030_P1_GRP		0x20	/* Application power group (FreeBSD control) */

#define TWL6030_P3_GRP		0x04	/* Modem power group */
#define TWL6030_P2_GRP		0x02	/* Connectivity power group */
#define TWL6030_P1_GRP		0x01	/* Application power group (FreeBSD control) */

/*
 * Register offsets within a LDO regulator register set
 */
#define TWL_VREG_GRP		0x00	/* Regulator GRP register */
#define TWL_VREG_STATE		0x02
#define TWL_VREG_VSEL		0x03	/* Voltage select register */

#define UNDF  0xFFFF

static const uint16_t twl6030_voltages[] = {
	0000, 1000, 1100, 1200, 1300, 1400, 1500, 1600,
	1700, 1800, 1900, 2000, 2100, 2200, 2300, 2400,
	2500, 2600, 2700, 2800, 2900, 3000, 3100, 3200,
	3300, UNDF, UNDF, UNDF, UNDF, UNDF, UNDF, 2750
};

static const uint16_t twl4030_vaux1_voltages[] = {
	1500, 1800, 2500, 2800, 3000, 3000, 3000, 3000
};
static const uint16_t twl4030_vaux2_voltages[] = {
	1700, 1700, 1900, 1300, 1500, 1800, 2000, 2500,
	2100, 2800, 2200, 2300, 2400, 2400, 2400, 2400
};
static const uint16_t twl4030_vaux3_voltages[] = {
	1500, 1800, 2500, 2800, 3000, 3000, 3000, 3000
};
static const uint16_t twl4030_vaux4_voltages[] = {
	700,  1000, 1200, 1300, 1500, 1800, 1850, 2500,
	2600, 2800, 2850, 3000, 3150, 3150, 3150, 3150
};
static const uint16_t twl4030_vmmc1_voltages[] = {
	1850, 2850, 3000, 3150
};
static const uint16_t twl4030_vmmc2_voltages[] = {
	1000, 1000, 1200, 1300, 1500, 1800, 1850, 2500,
	2600, 2800, 2850, 3000, 3150, 3150, 3150, 3150
};
static const uint16_t twl4030_vpll1_voltages[] = {
	1000, 1200, 1300, 1800, 2800, 3000, 3000, 3000
};
static const uint16_t twl4030_vpll2_voltages[] = {
	700,  1000, 1200, 1300, 1500, 1800, 1850, 2500,
	2600, 2800, 2850, 3000, 3150, 3150, 3150, 3150
};
static const uint16_t twl4030_vsim_voltages[] = {
	1000, 1200, 1300, 1800, 2800, 3000, 3000, 3000
};
static const uint16_t twl4030_vdac_voltages[] = {
	1200, 1300, 1800, 1800
};
#if 0 /* vdd1, vdd2, vdio, not currently used. */
static const uint16_t twl4030_vdd1_voltages[] = {
	800, 1450
};
static const uint16_t twl4030_vdd2_voltages[] = {
	800, 1450, 1500
};
static const uint16_t twl4030_vio_voltages[] = {
	1800, 1850
};
#endif
static const uint16_t twl4030_vintana2_voltages[] = {
	2500, 2750
};

/**
 *  Support voltage regulators for the different IC's
 */
struct twl_regulator {
	const char	*name;
	uint8_t		subdev;
	uint8_t		regbase;

	uint16_t	fixedvoltage;

	const uint16_t	*voltages;
	uint32_t	num_voltages;
};

#define TWL_REGULATOR_ADJUSTABLE(name, subdev, reg, voltages) \
	{ name, subdev, reg, 0, voltages, (sizeof(voltages)/sizeof(voltages[0])) }
#define TWL_REGULATOR_FIXED(name, subdev, reg, voltage) \
	{ name, subdev, reg, voltage, NULL, 0 }

static const struct twl_regulator twl4030_regulators[] = {
	TWL_REGULATOR_ADJUSTABLE("vaux1",    0, 0x17, twl4030_vaux1_voltages),
	TWL_REGULATOR_ADJUSTABLE("vaux2",    0, 0x1B, twl4030_vaux2_voltages),
	TWL_REGULATOR_ADJUSTABLE("vaux3",    0, 0x1F, twl4030_vaux3_voltages),
	TWL_REGULATOR_ADJUSTABLE("vaux4",    0, 0x23, twl4030_vaux4_voltages),
	TWL_REGULATOR_ADJUSTABLE("vmmc1",    0, 0x27, twl4030_vmmc1_voltages),
	TWL_REGULATOR_ADJUSTABLE("vmmc2",    0, 0x2B, twl4030_vmmc2_voltages),
	TWL_REGULATOR_ADJUSTABLE("vpll1",    0, 0x2F, twl4030_vpll1_voltages),
	TWL_REGULATOR_ADJUSTABLE("vpll2",    0, 0x33, twl4030_vpll2_voltages),
	TWL_REGULATOR_ADJUSTABLE("vsim",     0, 0x37, twl4030_vsim_voltages),
	TWL_REGULATOR_ADJUSTABLE("vdac",     0, 0x3B, twl4030_vdac_voltages),
	TWL_REGULATOR_ADJUSTABLE("vintana2", 0, 0x43, twl4030_vintana2_voltages),
	TWL_REGULATOR_FIXED("vintana1", 0, 0x3F, 1500),
	TWL_REGULATOR_FIXED("vintdig",  0, 0x47, 1500),
	TWL_REGULATOR_FIXED("vusb1v5",  0, 0x71, 1500),
	TWL_REGULATOR_FIXED("vusb1v8",  0, 0x74, 1800),
	TWL_REGULATOR_FIXED("vusb3v1",  0, 0x77, 3100),
	{ NULL, 0, 0x00, 0, NULL, 0 }
};

static const struct twl_regulator twl6030_regulators[] = {
	TWL_REGULATOR_ADJUSTABLE("vaux1", 0, 0x84, twl6030_voltages),
	TWL_REGULATOR_ADJUSTABLE("vaux2", 0, 0x89, twl6030_voltages),
	TWL_REGULATOR_ADJUSTABLE("vaux3", 0, 0x8C, twl6030_voltages),
	TWL_REGULATOR_ADJUSTABLE("vmmc",  0, 0x98, twl6030_voltages),
	TWL_REGULATOR_ADJUSTABLE("vpp",   0, 0x9C, twl6030_voltages),
	TWL_REGULATOR_ADJUSTABLE("vusim", 0, 0xA4, twl6030_voltages),
	TWL_REGULATOR_FIXED("vmem",  0, 0x64, 1800),
	TWL_REGULATOR_FIXED("vusb",  0, 0xA0, 3300),
	TWL_REGULATOR_FIXED("v1v8",  0, 0x46, 1800),
	TWL_REGULATOR_FIXED("v2v1",  0, 0x4C, 2100),
	TWL_REGULATOR_FIXED("v1v29", 0, 0x40, 1290),
	TWL_REGULATOR_FIXED("vcxio", 0, 0x90, 1800),
	TWL_REGULATOR_FIXED("vdac",  0, 0x94, 1800),
	TWL_REGULATOR_FIXED("vana",  0, 0x80, 2100),
	{ NULL, 0, 0x00, 0, NULL, 0 } 
};

#define TWL_VREG_MAX_NAMELEN  32

struct twl_regulator_entry {
	LIST_ENTRY(twl_regulator_entry) entries;
	char                 name[TWL_VREG_MAX_NAMELEN];
	struct sysctl_oid   *oid;
	uint8_t          sub_dev;           /* TWL sub-device group */
	uint8_t          reg_off;           /* base register offset for the LDO */
	uint16_t         fixed_voltage;	    /* the (milli)voltage if LDO is fixed */ 
	const uint16_t  *supp_voltages;     /* pointer to an array of possible voltages */
	uint32_t         num_supp_voltages; /* the number of supplied voltages */
};

struct twl_vreg_softc {
	device_t        sc_dev;
	device_t        sc_pdev;
	struct sx       sc_sx;

	struct intr_config_hook sc_init_hook;
	LIST_HEAD(twl_regulator_list, twl_regulator_entry) sc_vreg_list;
};


#define TWL_VREG_XLOCK(_sc)			sx_xlock(&(_sc)->sc_sx)
#define	TWL_VREG_XUNLOCK(_sc)		sx_xunlock(&(_sc)->sc_sx)
#define TWL_VREG_SLOCK(_sc)			sx_slock(&(_sc)->sc_sx)
#define	TWL_VREG_SUNLOCK(_sc)		sx_sunlock(&(_sc)->sc_sx)
#define TWL_VREG_LOCK_INIT(_sc)		sx_init(&(_sc)->sc_sx, "twl_vreg")
#define TWL_VREG_LOCK_DESTROY(_sc)	sx_destroy(&(_sc)->sc_sx);

#define TWL_VREG_ASSERT_LOCKED(_sc)	sx_assert(&(_sc)->sc_sx, SA_LOCKED);

#define TWL_VREG_LOCK_UPGRADE(_sc)               \
	do {                                         \
		while (!sx_try_upgrade(&(_sc)->sc_sx))   \
			pause("twl_vreg_ex", (hz / 100));    \
	} while(0)
#define TWL_VREG_LOCK_DOWNGRADE(_sc)	sx_downgrade(&(_sc)->sc_sx);




/**
 *	twl_vreg_read_1 - read single register from the TWL device
 *	twl_vreg_write_1 - write a single register in the TWL device
 *	@sc: device context
 *	@clk: the clock device we're reading from / writing to
 *	@off: offset within the clock's register set
 *	@val: the value to write or a pointer to a variable to store the result
 *
 *	RETURNS:
 *	Zero on success or an error code on failure.
 */
static inline int
twl_vreg_read_1(struct twl_vreg_softc *sc, struct twl_regulator_entry *regulator,
	uint8_t off, uint8_t *val)
{
	return (twl_read(sc->sc_pdev, regulator->sub_dev, 
	    regulator->reg_off + off, val, 1));
}

static inline int
twl_vreg_write_1(struct twl_vreg_softc *sc, struct twl_regulator_entry *regulator,
	uint8_t off, uint8_t val)
{
	return (twl_write(sc->sc_pdev, regulator->sub_dev,
	    regulator->reg_off + off, &val, 1));
}

/**
 *	twl_millivolt_to_vsel - gets the vsel bit value to write into the register
 *	                        for a desired voltage and regulator
 *	@sc: the device soft context
 *	@regulator: pointer to the regulator device
 *	@millivolts: the millivolts to find the bit value for
 *	@vsel: upon return will contain the corresponding register value
 *
 *	Accepts a (milli)voltage value and tries to find the closest match to the
 *	actual supported voltages for the given regulator.  If a match is found
 *	within 100mv of the target, @vsel is written with the match and 0 is
 *	returned. If no voltage match is found the function returns an non-zero
 *	value.
 *
 *	RETURNS:
 *	Zero on success or an error code on failure.
 */
static int
twl_vreg_millivolt_to_vsel(struct twl_vreg_softc *sc,
	struct twl_regulator_entry *regulator, int millivolts, uint8_t *vsel)
{
	int delta, smallest_delta;
	unsigned i, closest_idx;

	TWL_VREG_ASSERT_LOCKED(sc);

	if (regulator->supp_voltages == NULL)
		return (EINVAL);

	/* Loop over the support voltages and try and find the closest match */
	closest_idx = 0;
	smallest_delta = 0x7fffffff;
	for (i = 0; i < regulator->num_supp_voltages; i++) {

		/* Ignore undefined values */
		if (regulator->supp_voltages[i] == UNDF)
			continue;

		/* Calculate the difference */
		delta = millivolts - (int)regulator->supp_voltages[i];
		if (abs(delta) < smallest_delta) {
			smallest_delta = abs(delta);
			closest_idx = i;
		}
	}

	/* Check we got a voltage that was within 100mv of the actual target, this
	 * is just a value I picked out of thin air.
	 */
	if ((smallest_delta > 100) && (closest_idx < 0x100))
		return (EINVAL);

	*vsel = closest_idx;
	return (0);
}

/**
 *	twl_vreg_is_regulator_enabled - returns the enabled status of the regulator
 *	@sc: the device soft context
 *	@regulator: pointer to the regulator device
 *	@enabled: stores the enabled status, zero disabled, non-zero enabled
 *
 *	LOCKING:
 *	On entry expects the TWL VREG lock to be held. Will upgrade the lock to
 *	exclusive if not already but, if so, it will be downgraded again before
 *	returning.
 *
 *	RETURNS:
 *	Zero on success or an error code on failure.
 */
static int
twl_vreg_is_regulator_enabled(struct twl_vreg_softc *sc,
	struct twl_regulator_entry *regulator, int *enabled)
{
	int err;
	uint8_t grp;
	uint8_t state;
	int xlocked;
	
	if (enabled == NULL)
		return (EINVAL);

	TWL_VREG_ASSERT_LOCKED(sc);

	xlocked = sx_xlocked(&sc->sc_sx);
	if (!xlocked)
		TWL_VREG_LOCK_UPGRADE(sc);

	/* The status reading is different for the different devices */
	if (twl_is_4030(sc->sc_pdev)) {

		err = twl_vreg_read_1(sc, regulator, TWL_VREG_GRP, &state);
		if (err)
			goto done;

		*enabled = (state & TWL4030_P1_GRP);

	} else if (twl_is_6030(sc->sc_pdev) || twl_is_6025(sc->sc_pdev)) {

		/* Check the regulator is in the application group */
		if (twl_is_6030(sc->sc_pdev)) {
			err = twl_vreg_read_1(sc, regulator, TWL_VREG_GRP, &grp);
			if (err)
				goto done;

			if (!(grp & TWL6030_P1_GRP)) {
				*enabled = 0; /* disabled */
				goto done;
			}
		}

		/* Read the application mode state and verify it's ON */
		err = twl_vreg_read_1(sc, regulator, TWL_VREG_STATE, &state);
		if (err)
			goto done;

		*enabled = ((state & 0x0C) == 0x04);

	} else {
		err = EINVAL;
	}

done:
	if (!xlocked)
		TWL_VREG_LOCK_DOWNGRADE(sc);

	return (err);
}

/**
 *	twl_vreg_disable_regulator - disables a voltage regulator
 *	@sc: the device soft context
 *	@regulator: pointer to the regulator device
 *
 *	Disables the regulator which will stop the output drivers.
 *
 *	LOCKING:
 *	On entry expects the TWL VREG lock to be held. Will upgrade the lock to
 *	exclusive if not already but, if so, it will be downgraded again before
 *	returning.
 *
 *	RETURNS:
 *	Zero on success or a positive error code on failure.
 */
static int
twl_vreg_disable_regulator(struct twl_vreg_softc *sc,
	struct twl_regulator_entry *regulator)
{
	int err = 0;
	uint8_t grp;
	int xlocked;

	TWL_VREG_ASSERT_LOCKED(sc);

	xlocked = sx_xlocked(&sc->sc_sx);
	if (!xlocked)
		TWL_VREG_LOCK_UPGRADE(sc);

	if (twl_is_4030(sc->sc_pdev)) {

		/* Read the regulator CFG_GRP register */
		err = twl_vreg_read_1(sc, regulator, TWL_VREG_GRP, &grp);
		if (err)
			goto done;

		/* On the TWL4030 we just need to remove the regulator from all the
		 * power groups.
		 */
		grp &= ~(TWL4030_P1_GRP | TWL4030_P2_GRP | TWL4030_P3_GRP);
		err = twl_vreg_write_1(sc, regulator, TWL_VREG_GRP, grp);

	} else if (twl_is_6030(sc->sc_pdev) || twl_is_6025(sc->sc_pdev)) {

		/* On TWL6030 we need to make sure we disable power for all groups */
		if (twl_is_6030(sc->sc_pdev))
			grp = TWL6030_P1_GRP | TWL6030_P2_GRP | TWL6030_P3_GRP;
		else
			grp = 0x00;

		/* Write the resource state to "OFF" */
		err = twl_vreg_write_1(sc, regulator, TWL_VREG_STATE, (grp << 5));
	}

done:
	if (!xlocked)
		TWL_VREG_LOCK_DOWNGRADE(sc);
	
	return (err);
}

/**
 *	twl_vreg_enable_regulator - enables the voltage regulator
 *	@sc: the device soft context
 *	@regulator: pointer to the regulator device
 *
 *	Enables the regulator which will enable the voltage out at the currently
 *	set voltage.  Set the voltage before calling this function to avoid
 *	driving the voltage too high/low by mistake.
 *
 *	LOCKING:
 *	On entry expects the TWL VREG lock to be held. Will upgrade the lock to
 *	exclusive if not already but, if so, it will be downgraded again before
 *	returning.
 *
 *	RETURNS:
 *	Zero on success or a positive error code on failure.
 */
static int
twl_vreg_enable_regulator(struct twl_vreg_softc *sc,
    struct twl_regulator_entry *regulator)
{
	int err;
	uint8_t grp;
	int xlocked;

	TWL_VREG_ASSERT_LOCKED(sc);

	xlocked = sx_xlocked(&sc->sc_sx);
	if (!xlocked)
		TWL_VREG_LOCK_UPGRADE(sc);


	err = twl_vreg_read_1(sc, regulator, TWL_VREG_GRP, &grp);
	if (err)
		goto done;

	/* Enable the regulator by ensuring it's in the application power group
	 * and is in the "on" state.
	 */
	if (twl_is_4030(sc->sc_pdev)) {

		/* On the TWL4030 we just need to ensure the regulator is in the right
		 * power domain, don't need to turn on explicitly like TWL6030.
		 */
		grp |= TWL4030_P1_GRP;
		err = twl_vreg_write_1(sc, regulator, TWL_VREG_GRP, grp);

	} else if (twl_is_6030(sc->sc_pdev) || twl_is_6025(sc->sc_pdev)) {

		if (twl_is_6030(sc->sc_pdev) && !(grp & TWL6030_P1_GRP)) {
			grp |= TWL6030_P1_GRP;
			err = twl_vreg_write_1(sc, regulator, TWL_VREG_GRP, grp);
			if (err)
				goto done;
		}

		/* Write the resource state to "ON" */
		err = twl_vreg_write_1(sc, regulator, TWL_VREG_STATE, (grp << 5) | 0x01);
	}

done:
	if (!xlocked)
		TWL_VREG_LOCK_DOWNGRADE(sc);
	
	return (err);
}

/**
 *	twl_vreg_write_regulator_voltage - sets the voltage level on a regulator
 *	@sc: the device soft context
 *	@regulator: pointer to the regulator structure
 *	@millivolts: the voltage to set
 *
 *	Sets the voltage output on a given regulator, if the regulator is not
 *	enabled, it will be enabled.
 *
 *	LOCKING:
 *	On entry expects the TWL VREG lock to be held, may upgrade the lock to
 *	exclusive but if so it will be downgraded once again before returning.
 *
 *	RETURNS:
 *	Zero on success or an error code on failure.
 */
static int
twl_vreg_write_regulator_voltage(struct twl_vreg_softc *sc,
    struct twl_regulator_entry *regulator, int millivolts)
{
	int err;
	uint8_t vsel;
	int xlocked;

	TWL_VREG_ASSERT_LOCKED(sc);

	/* If millivolts is zero then we simply disable the output */
	if (millivolts == 0)
		return (twl_vreg_disable_regulator(sc, regulator));

	/* If the regulator has a fixed voltage then check the setting matches
	 * and simply enable.
	 */
	if (regulator->supp_voltages == NULL || regulator->num_supp_voltages == 0) {
		if (millivolts != regulator->fixed_voltage)
			return (EINVAL);

		return (twl_vreg_enable_regulator(sc, regulator));
	}

	/* Get the VSEL value for the given voltage */
	err = twl_vreg_millivolt_to_vsel(sc, regulator, millivolts, &vsel);
	if (err)
		return (err);

	
	/* Need to upgrade because writing the voltage and enabling should be atomic */
	xlocked = sx_xlocked(&sc->sc_sx);
	if (!xlocked)
		TWL_VREG_LOCK_UPGRADE(sc);


	/* Set voltage and enable (atomically) */
	err = twl_vreg_write_1(sc, regulator, TWL_VREG_VSEL, (vsel & 0x1f));
	if (!err) {
		err = twl_vreg_enable_regulator(sc, regulator);
	}

	if (!xlocked)
		TWL_VREG_LOCK_DOWNGRADE(sc);

	if ((twl_vreg_debug > 1) && !err)
		device_printf(sc->sc_dev, "%s : setting voltage to %dmV (vsel: 0x%x)\n",
		    regulator->name, millivolts, vsel);

	return (err);
}

/**
 *	twl_vreg_read_regulator_voltage - reads the voltage on a given regulator
 *	@sc: the device soft context
 *	@regulator: pointer to the regulator structure
 *	@millivolts: upon return will contain the voltage on the regulator
 *
 *	LOCKING:
 *	On entry expects the TWL VREG lock to be held. It will upgrade the lock to
 *	exclusive if not already, but if so, it will be downgraded again before
 *	returning.
 *
 *	RETURNS:
 *	Zero on success, or otherwise an error code.
 */
static int
twl_vreg_read_regulator_voltage(struct twl_vreg_softc *sc,
    struct twl_regulator_entry *regulator, int *millivolts)
{
	int err;
	int en = 0;
	int xlocked;
	uint8_t vsel;
	
	TWL_VREG_ASSERT_LOCKED(sc);
	
	/* Need to upgrade the lock because checking enabled state and voltage
	 * should be atomic.
	 */
	xlocked = sx_xlocked(&sc->sc_sx);
	if (!xlocked)
		TWL_VREG_LOCK_UPGRADE(sc);


	/* Check if the regulator is currently enabled */
	err = twl_vreg_is_regulator_enabled(sc, regulator, &en);
	if (err)
		goto done;

	*millivolts = 0;	
	if (!en)
		goto done;


	/* Not all voltages are adjustable */
	if (regulator->supp_voltages == NULL || !regulator->num_supp_voltages) {
		*millivolts = regulator->fixed_voltage;
		goto done;
	}

	/* For variable voltages read the voltage register */
	err = twl_vreg_read_1(sc, regulator, TWL_VREG_VSEL, &vsel);
	if (err)
		goto done;

	vsel &= (regulator->num_supp_voltages - 1);
	if (regulator->supp_voltages[vsel] == UNDF) {
		err = EINVAL;
		goto done;
	}

	*millivolts = regulator->supp_voltages[vsel];

done:
	if (!xlocked)
		TWL_VREG_LOCK_DOWNGRADE(sc);
	
	if ((twl_vreg_debug > 1) && !err)
		device_printf(sc->sc_dev, "%s : reading voltage is %dmV (vsel: 0x%x)\n",
		    regulator->name, *millivolts, vsel);

	return (err);
}

/**
 *	twl_vreg_get_voltage - public interface to read the voltage on a regulator
 *	@dev: TWL VREG device
 *	@name: the name of the regulator to read the voltage of
 *	@millivolts: pointer to an integer that upon return will contain the mV
 *
 *	If the regulator is disabled the function will set the @millivolts to zero.
 *
 *	LOCKING:
 *	Internally the function takes and releases the TWL VREG lock.
 *
 *	RETURNS:
 *	Zero on success or a negative error code on failure.
 */
int
twl_vreg_get_voltage(device_t dev, const char *name, int *millivolts)
{
	struct twl_vreg_softc *sc;
	struct twl_regulator_entry *regulator;
	int err = EINVAL;

	if (millivolts == NULL)
		return (EINVAL);

	sc = device_get_softc(dev);

	TWL_VREG_SLOCK(sc);

	LIST_FOREACH(regulator, &sc->sc_vreg_list, entries) {
		if (strcmp(regulator->name, name) == 0) {
			err = twl_vreg_read_regulator_voltage(sc, regulator, millivolts);
			break;
		}
	}

	TWL_VREG_SUNLOCK(sc);

	return (err);
}

/**
 *	twl_vreg_set_voltage - public interface to write the voltage on a regulator
 *	@dev: TWL VREG device
 *	@name: the name of the regulator to read the voltage of
 *	@millivolts: the voltage to set in millivolts
 *
 *	Sets the output voltage on a given regulator. If the regulator is a fixed
 *	voltage reg then the @millivolts value should match the fixed voltage. If
 *	a variable regulator then the @millivolt value must fit within the max/min
 *	range of the given regulator.
 *
 *	LOCKING:
 *	Internally the function takes and releases the TWL VREG lock.
 *
 *	RETURNS:
 *	Zero on success or a negative error code on failure.
 */
int
twl_vreg_set_voltage(device_t dev, const char *name, int millivolts)
{
	struct twl_vreg_softc *sc;
	struct twl_regulator_entry *regulator;
	int err = EINVAL;

	sc = device_get_softc(dev);

	TWL_VREG_SLOCK(sc);

	LIST_FOREACH(regulator, &sc->sc_vreg_list, entries) {
		if (strcmp(regulator->name, name) == 0) {
			err = twl_vreg_write_regulator_voltage(sc, regulator, millivolts);
			break;
		}
	}

	TWL_VREG_SUNLOCK(sc);

	return (err);
}

/**
 *	twl_sysctl_voltage - reads or writes the voltage for a regulator
 *	@SYSCTL_HANDLER_ARGS: arguments for the callback
 *
 *	Callback for the sysctl entry for the regulator, simply used to return
 *	the voltage on a particular regulator.
 *
 *	LOCKING:
 *	Takes the TWL_VREG shared lock internally.
 *
 *	RETURNS:
 *	Zero on success or an error code on failure.
 */
static int
twl_vreg_sysctl_voltage(SYSCTL_HANDLER_ARGS)
{
	struct twl_vreg_softc *sc = (struct twl_vreg_softc*)arg1;
	struct twl_regulator_entry *regulator;
	int voltage;
	int found = 0;

	TWL_VREG_SLOCK(sc);

	/* Find the regulator with the matching name */
	LIST_FOREACH(regulator, &sc->sc_vreg_list, entries) {
		if (strcmp(regulator->name, oidp->oid_name) == 0) {
			found = 1;
			break;
		}
	}

	/* Sanity check that we found the regulator */
	if (!found) {
		TWL_VREG_SUNLOCK(sc);
		return (EINVAL);
	}

	twl_vreg_read_regulator_voltage(sc, regulator, &voltage);

	TWL_VREG_SUNLOCK(sc);

	return sysctl_handle_int(oidp, &voltage, 0, req);
}

/**
 *	twl_add_regulator - adds single voltage regulator sysctls for the device
 *	@sc: device soft context
 *	@name: the name of the regulator
 *	@nsub: the number of the subdevice
 *	@regbase: the base address of the voltage regulator registers
 *	@fixed_voltage: if a fixed voltage regulator this defines it's voltage
 *	@voltages: if a variable voltage regulator, an array of possible voltages
 *	@num_voltages: the number of entries @voltages
 *
 *	Adds a voltage regulator to the device and also a sysctl interface for the
 *	regulator.
 *
 *	LOCKING:
 *	The TWL_VEG exclusive lock must be held while this function is called.
 *
 *	RETURNS:
 *	Pointer to the new regulator entry on success, otherwise on failure NULL.
 */
static struct twl_regulator_entry*
twl_vreg_add_regulator(struct twl_vreg_softc *sc, const char *name,
	uint8_t nsub, uint8_t regbase, uint16_t fixed_voltage,
	const uint16_t *voltages, uint32_t num_voltages)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);
	struct twl_regulator_entry *new;

	new = malloc(sizeof(struct twl_regulator_entry), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (new == NULL)
		return (NULL);


	strncpy(new->name, name, TWL_VREG_MAX_NAMELEN);
	new->name[TWL_VREG_MAX_NAMELEN - 1] = '\0';

	new->sub_dev = nsub;
	new->reg_off = regbase;

	new->fixed_voltage = fixed_voltage;

	new->supp_voltages = voltages;
	new->num_supp_voltages = num_voltages;


	/* Add a sysctl entry for the voltage */
	new->oid = SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, name,
	    CTLTYPE_INT | CTLFLAG_RD, sc, 0,
	    twl_vreg_sysctl_voltage, "I", "voltage regulator");

	/* Finally add the regulator to list of supported regulators */
	LIST_INSERT_HEAD(&sc->sc_vreg_list, new, entries);

	return (new);
}

/**
 *	twl_vreg_add_regulators - adds any voltage regulators to the device
 *	@sc: device soft context
 *	@chip: the name of the chip used in the hints
 *	@regulators: the list of possible voltage regulators
 *
 *	Loops over the list of regulators and matches up with the FDT values,
 *	adjusting the actual voltage based on the supplied values.
 *
 *	LOCKING:
 *	The TWL_VEG exclusive lock must be held while this function is called.
 *
 *	RETURNS:
 *	Always returns 0.
 */
static int
twl_vreg_add_regulators(struct twl_vreg_softc *sc,
	const struct twl_regulator *regulators)
{
	int err;
	int millivolts;
	const struct twl_regulator *walker;
	struct twl_regulator_entry *entry;
	phandle_t child;
	char rnames[256];
	char *name, *voltage;
	int len = 0, prop_len;


	/* Add the regulators from the list */
	walker = &regulators[0];
	while (walker->name != NULL) {

		/* Add the regulator to the list */
		entry = twl_vreg_add_regulator(sc, walker->name, walker->subdev,
		    walker->regbase, walker->fixedvoltage,
		    walker->voltages, walker->num_voltages);
		if (entry == NULL)
			continue;

		walker++;
	}


	/* Check if the FDT is telling us to set any voltages */
	child = ofw_bus_get_node(sc->sc_pdev);
	if (child) {

		prop_len = OF_getprop(child, "voltage-regulators", rnames, sizeof(rnames));
		while (len < prop_len) {
			name = rnames + len;
			len += strlen(name) + 1;
			if ((len >= prop_len) || (name[0] == '\0'))
				break;
			
			voltage = rnames + len;
			len += strlen(voltage) + 1;
			if (voltage[0] == '\0')
				break;
			
			millivolts = strtoul(voltage, NULL, 0);
			
			LIST_FOREACH(entry, &sc->sc_vreg_list, entries) {
				if (strcmp(entry->name, name) == 0) {
					twl_vreg_write_regulator_voltage(sc, entry, millivolts);
					break;
				}
			}
		}
	}
	
	
	if (twl_vreg_debug) {
		LIST_FOREACH(entry, &sc->sc_vreg_list, entries) {
			err = twl_vreg_read_regulator_voltage(sc, entry, &millivolts);
			if (!err)
				device_printf(sc->sc_dev, "%s : %d mV\n", entry->name, millivolts);
		}
	}

	return (0);
}

/**
 *	twl_vreg_init - initialises the list of regulators
 *	@dev: the twl_vreg device
 *
 *	This function is called as an intrhook once interrupts have been enabled,
 *	this is done so that the driver has the option to enable/disable or set
 *	the voltage level based on settings providied in the FDT.
 *
 *	LOCKING:
 *	Takes the exclusive lock in the function.
 */
static void
twl_vreg_init(void *dev)
{
	struct twl_vreg_softc *sc;

	sc = device_get_softc((device_t)dev);

	TWL_VREG_XLOCK(sc);

	if (twl_is_4030(sc->sc_pdev))
		twl_vreg_add_regulators(sc, twl4030_regulators);
	else if (twl_is_6030(sc->sc_pdev) || twl_is_6025(sc->sc_pdev))
		twl_vreg_add_regulators(sc, twl6030_regulators);

	TWL_VREG_XUNLOCK(sc);

	config_intrhook_disestablish(&sc->sc_init_hook);
}

static int
twl_vreg_probe(device_t dev)
{
	if (twl_is_4030(device_get_parent(dev)))
		device_set_desc(dev, "TI TWL4030 PMIC Voltage Regulators");
	else if (twl_is_6025(device_get_parent(dev)) ||
	         twl_is_6030(device_get_parent(dev)))
		device_set_desc(dev, "TI TWL6025/TWL6030 PMIC Voltage Regulators");
	else
		return (ENXIO);

	return (0);
}

static int
twl_vreg_attach(device_t dev)
{
	struct twl_vreg_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_pdev = device_get_parent(dev);

	TWL_VREG_LOCK_INIT(sc);

	LIST_INIT(&sc->sc_vreg_list);

	/* We have to wait until interrupts are enabled. I2C read and write
	 * only works if the interrupts are available.
	 */
	sc->sc_init_hook.ich_func = twl_vreg_init;
	sc->sc_init_hook.ich_arg = dev;

	if (config_intrhook_establish(&sc->sc_init_hook) != 0)
		return (ENOMEM);

	return (0);
}

static int
twl_vreg_detach(device_t dev)
{
	struct twl_vreg_softc *sc;
	struct twl_regulator_entry *regulator;
	struct twl_regulator_entry *tmp;

	sc = device_get_softc(dev);

	/* Take the lock and free all the added regulators */
	TWL_VREG_XLOCK(sc);

	LIST_FOREACH_SAFE(regulator, &sc->sc_vreg_list, entries, tmp) {
		LIST_REMOVE(regulator, entries);
		sysctl_remove_oid(regulator->oid, 1, 0);
		free(regulator, M_DEVBUF);
	}

	TWL_VREG_XUNLOCK(sc);

	TWL_VREG_LOCK_DESTROY(sc);

	return (0);
}

static device_method_t twl_vreg_methods[] = {
	DEVMETHOD(device_probe,		twl_vreg_probe),
	DEVMETHOD(device_attach,	twl_vreg_attach),
	DEVMETHOD(device_detach,	twl_vreg_detach),

	{0, 0},
};

static driver_t twl_vreg_driver = {
	"twl_vreg",
	twl_vreg_methods,
	sizeof(struct twl_vreg_softc),
};

static devclass_t twl_vreg_devclass;

DRIVER_MODULE(twl_vreg, twl, twl_vreg_driver, twl_vreg_devclass, 0, 0);
MODULE_VERSION(twl_vreg, 1);
