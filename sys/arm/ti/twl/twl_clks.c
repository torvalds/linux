/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012
 *	Ben Gray <bgray@freebsd.org>.
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
 * This driver covers the external clocks, allows for enabling &
 * disabling their output.
 *
 *
 *
 * FLATTENED DEVICE TREE (FDT)
 * Startup override settings can be specified in the FDT, if they are they
 * should be under the twl parent device and take the following form:
 *
 *    external-clocks = "name1", "state1",
 *                      "name2", "state2",
 *                      etc;
 *
 * Each override should be a pair, the first entry is the name of the clock
 * the second is the state to set, possible strings are either "on" or "off".
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
#include "twl_clks.h"


static int twl_clks_debug = 1;


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
 * Register offsets within a clk regulator register set
 */
#define TWL_CLKS_GRP		0x00	/* Regulator GRP register */
#define TWL_CLKS_STATE		0x02	/* TWL6030 only */



/**
 *  Support voltage regulators for the different IC's
 */
struct twl_clock {
	const char	*name;
	uint8_t		subdev;
	uint8_t		regbase;
};

static const struct twl_clock twl4030_clocks[] = {
	{ "32kclkout", 0, 0x8e },
	{ NULL, 0, 0x00 } 
};

static const struct twl_clock twl6030_clocks[] = {
	{ "clk32kg",     0, 0xbc },
	{ "clk32kao",    0, 0xb9 },
	{ "clk32kaudio", 0, 0xbf },
	{ NULL, 0, 0x00 } 
};

#define TWL_CLKS_MAX_NAMELEN  32

struct twl_clk_entry {
	LIST_ENTRY(twl_clk_entry) link;
	struct sysctl_oid *oid;
	char		       name[TWL_CLKS_MAX_NAMELEN];
	uint8_t            sub_dev;  /* the sub-device number for the clock */
	uint8_t            reg_off;  /* register base address of the clock */
};

struct twl_clks_softc {
	device_t           sc_dev;   /* twl_clk device */
	device_t           sc_pdev;  /* parent device (twl) */
	struct sx          sc_sx;    /* internal locking */
	struct intr_config_hook sc_init_hook;
	LIST_HEAD(twl_clk_list, twl_clk_entry) sc_clks_list;
};

/**
 *	Macros for driver shared locking
 */
#define TWL_CLKS_XLOCK(_sc)			sx_xlock(&(_sc)->sc_sx)
#define	TWL_CLKS_XUNLOCK(_sc)		sx_xunlock(&(_sc)->sc_sx)
#define TWL_CLKS_SLOCK(_sc)			sx_slock(&(_sc)->sc_sx)
#define	TWL_CLKS_SUNLOCK(_sc)		sx_sunlock(&(_sc)->sc_sx)
#define TWL_CLKS_LOCK_INIT(_sc)		sx_init(&(_sc)->sc_sx, "twl_clks")
#define TWL_CLKS_LOCK_DESTROY(_sc)	sx_destroy(&(_sc)->sc_sx);

#define TWL_CLKS_ASSERT_LOCKED(_sc)	sx_assert(&(_sc)->sc_sx, SA_LOCKED);

#define TWL_CLKS_LOCK_UPGRADE(_sc)               \
	do {                                         \
		while (!sx_try_upgrade(&(_sc)->sc_sx))   \
			pause("twl_clks_ex", (hz / 100));    \
	} while(0)
#define TWL_CLKS_LOCK_DOWNGRADE(_sc)	sx_downgrade(&(_sc)->sc_sx);




/**
 *	twl_clks_read_1 - read single register from the TWL device
 *	twl_clks_write_1 - writes a single register in the TWL device
 *	@sc: device context
 *	@clk: the clock device we're reading from / writing to
 *	@off: offset within the clock's register set
 *	@val: the value to write or a pointer to a variable to store the result
 *
 *	RETURNS:
 *	Zero on success or an error code on failure.
 */
static inline int
twl_clks_read_1(struct twl_clks_softc *sc, struct twl_clk_entry *clk,
	uint8_t off, uint8_t *val)
{
	return (twl_read(sc->sc_pdev, clk->sub_dev, clk->reg_off + off, val, 1));
}

static inline int
twl_clks_write_1(struct twl_clks_softc *sc, struct twl_clk_entry *clk,
	uint8_t off, uint8_t val)
{
	return (twl_write(sc->sc_pdev, clk->sub_dev, clk->reg_off + off, &val, 1));
}


/**
 *	twl_clks_is_enabled - determines if a clock is enabled
 *	@dev: TWL CLK device
 *	@name: the name of the clock
 *	@enabled: upon return will contain the 'enabled' state
 *
 *	LOCKING:
 *	Internally the function takes and releases the TWL lock.
 *
 *	RETURNS:
 *	Zero on success or a negative error code on failure.
 */
int
twl_clks_is_enabled(device_t dev, const char *name, int *enabled)
{
	struct twl_clks_softc *sc = device_get_softc(dev);
	struct twl_clk_entry *clk;
	int found = 0;
	int err;
	uint8_t grp, state;

	TWL_CLKS_SLOCK(sc);

	LIST_FOREACH(clk, &sc->sc_clks_list, link) {
		if (strcmp(clk->name, name) == 0) {
			found = 1;
			break;
		}
	}

	if (!found) {
		TWL_CLKS_SUNLOCK(sc);
		return (EINVAL);
	}


	if (twl_is_4030(sc->sc_pdev)) {

		err = twl_clks_read_1(sc, clk, TWL_CLKS_GRP, &grp);
		if (!err)
			*enabled = (grp & TWL4030_P1_GRP);

	} else if (twl_is_6030(sc->sc_pdev) || twl_is_6025(sc->sc_pdev)) {

		TWL_CLKS_LOCK_UPGRADE(sc);

		/* Check the clock is in the application group */
		if (twl_is_6030(sc->sc_pdev)) {
			err = twl_clks_read_1(sc, clk, TWL_CLKS_GRP, &grp);
			if (err) {
				TWL_CLKS_LOCK_DOWNGRADE(sc);
				goto done;
			}
			
			if (!(grp & TWL6030_P1_GRP)) {
				TWL_CLKS_LOCK_DOWNGRADE(sc);
				*enabled = 0; /* disabled */
				goto done;
			}
		}

		/* Read the application mode state and verify it's ON */
		err = twl_clks_read_1(sc, clk, TWL_CLKS_STATE, &state);
		if (!err)
			*enabled = ((state & 0x0C) == 0x04);
			
		TWL_CLKS_LOCK_DOWNGRADE(sc);

	} else {
		err = EINVAL;
	}

done:
	TWL_CLKS_SUNLOCK(sc);
	return (err);
}


/**
 *	twl_clks_set_state - enables/disables a clock output
 *	@sc: device context
 *	@clk: the clock entry to enable/disable
 *	@enable: non-zero the clock is enabled, zero the clock is disabled
 *
 *	LOCKING:
 *	The TWL CLK lock must be held before this function is called.
 *
 *	RETURNS:
 *	Zero on success or an error code on failure.
 */
static int
twl_clks_set_state(struct twl_clks_softc *sc, struct twl_clk_entry *clk,
	int enable)
{
	int xlocked;
	int err;
	uint8_t grp;

	TWL_CLKS_ASSERT_LOCKED(sc);

	/* Upgrade the lock to exclusive because about to perform read-mod-write */
	xlocked = sx_xlocked(&sc->sc_sx);
	if (!xlocked)
		TWL_CLKS_LOCK_UPGRADE(sc);

	err = twl_clks_read_1(sc, clk, TWL_CLKS_GRP, &grp);
	if (err)
		goto done;

	if (twl_is_4030(sc->sc_pdev)) {

		/* On the TWL4030 we just need to ensure the clock is in the right
		 * power domain, don't need to turn on explicitly like TWL6030.
		 */
		if (enable)
			grp |= TWL4030_P1_GRP;
		else
			grp &= ~(TWL4030_P1_GRP | TWL4030_P2_GRP | TWL4030_P3_GRP);
		
		err = twl_clks_write_1(sc, clk, TWL_CLKS_GRP, grp);

	} else if (twl_is_6030(sc->sc_pdev) || twl_is_6025(sc->sc_pdev)) {

		/* Make sure the clock belongs to at least the APP power group */
		if (twl_is_6030(sc->sc_pdev) && !(grp & TWL6030_P1_GRP)) {
			grp |= TWL6030_P1_GRP;
			err = twl_clks_write_1(sc, clk, TWL_CLKS_GRP, grp);
			if (err)
				goto done;
		}

		/* On TWL6030 we need to make sure we disable power for all groups */
		if (twl_is_6030(sc->sc_pdev))
			grp = TWL6030_P1_GRP | TWL6030_P2_GRP | TWL6030_P3_GRP;
		else
			grp = 0x00;

		/* Set the state of the clock */
		if (enable)
			err = twl_clks_write_1(sc, clk, TWL_CLKS_STATE, (grp << 5) | 0x01);
		else
			err = twl_clks_write_1(sc, clk, TWL_CLKS_STATE, (grp << 5));

	} else {
		
		err = EINVAL;
	}

done:
	if (!xlocked)
		TWL_CLKS_LOCK_DOWNGRADE(sc);

	if ((twl_clks_debug > 1) && !err)
		device_printf(sc->sc_dev, "%s : %sabled\n", clk->name,
			enable ? "en" : "dis");

	return (err);
}


/**
 *	twl_clks_disable - disables a clock output
 *	@dev: TWL clk device
*	@name: the name of the clock
 *
 *	LOCKING:
 *	Internally the function takes and releases the TWL lock.
 *
 *	RETURNS:
*	Zero on success or an error code on failure.
 */
int
twl_clks_disable(device_t dev, const char *name)
{
	struct twl_clks_softc *sc = device_get_softc(dev);
	struct twl_clk_entry *clk;
	int err = EINVAL;

	TWL_CLKS_SLOCK(sc);

	LIST_FOREACH(clk, &sc->sc_clks_list, link) {
		if (strcmp(clk->name, name) == 0) {
			err = twl_clks_set_state(sc, clk, 0);
			break;
		}
	}
	
	TWL_CLKS_SUNLOCK(sc);
	return (err);
}

/**
 *	twl_clks_enable - enables a clock output
 *	@dev: TWL clk device
 *	@name: the name of the clock
 *
 *	LOCKING:
 *	Internally the function takes and releases the TWL CLKS lock.
 *
 *	RETURNS:
 *	Zero on success or an error code on failure.
 */
int
twl_clks_enable(device_t dev, const char *name)
{
	struct twl_clks_softc *sc = device_get_softc(dev);
	struct twl_clk_entry *clk;
	int err = EINVAL;

	TWL_CLKS_SLOCK(sc);

	LIST_FOREACH(clk, &sc->sc_clks_list, link) {
		if (strcmp(clk->name, name) == 0) {
			err = twl_clks_set_state(sc, clk, 1);
			break;
		}
	}
	
	TWL_CLKS_SUNLOCK(sc);
	return (err);
}

/**
 *	twl_clks_sysctl_clock - reads the state of the clock
 *	@SYSCTL_HANDLER_ARGS: arguments for the callback
 *
 *	Returns the clock status; disabled is zero and enabled is non-zero.
 *
 *	LOCKING:
 *	It's expected the TWL lock is held while this function is called.
 *
 *	RETURNS:
 *	EIO if device is not present, otherwise 0 is returned.
 */
static int
twl_clks_sysctl_clock(SYSCTL_HANDLER_ARGS)
{
	struct twl_clks_softc *sc = (struct twl_clks_softc*)arg1;
	int err;
	int enabled = 0;

	if ((err = twl_clks_is_enabled(sc->sc_dev, oidp->oid_name, &enabled)) != 0)
		return err;
	
	return sysctl_handle_int(oidp, &enabled, 0, req);
}

/**
 *	twl_clks_add_clock - adds single clock sysctls for the device
 *	@sc: device soft context
 *	@name: the name of the regulator
 *	@nsub: the number of the subdevice
 *	@regbase: the base address of the clocks registers
 *
 *	Adds a single clock to the device and also a sysctl interface for 
 *	querying it's status.
 *
 *	LOCKING:
 *	It's expected the exclusive lock is held while this function is called.
 *
 *	RETURNS:
 *	Pointer to the new clock entry on success, otherwise NULL on failure.
 */
static struct twl_clk_entry*
twl_clks_add_clock(struct twl_clks_softc *sc, const char *name,
	uint8_t nsub, uint8_t regbase)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);
	struct twl_clk_entry *new;

	TWL_CLKS_ASSERT_LOCKED(sc);

	new = malloc(sizeof(struct twl_clk_entry), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (new == NULL)
		return (NULL);


	strncpy(new->name, name, TWL_CLKS_MAX_NAMELEN);
	new->name[TWL_CLKS_MAX_NAMELEN - 1] = '\0';

	new->sub_dev = nsub;
	new->reg_off = regbase;



	/* Add a sysctl entry for the clock */
	new->oid = SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, name,
	    CTLTYPE_INT | CTLFLAG_RD, sc, 0,
	    twl_clks_sysctl_clock, "I", "external clock");

	/* Finally add the regulator to list of supported regulators */
	LIST_INSERT_HEAD(&sc->sc_clks_list, new, link);

	return (new);
}

/**
 *	twl_clks_add_clocks - populates the internal list of clocks
 *	@sc: device soft context
 *	@chip: the name of the chip used in the hints
 *	@clks the list of clocks supported by the device
 *
 *	Loops over the list of clocks and adds them to the device context. Also
 *	scans the FDT to determine if there are any clocks that should be
 *	enabled/disabled automatically.
 *
 *	LOCKING:
 *	Internally takes the exclusive lock while adding the clocks to the
 *	device context.
 *
 *	RETURNS:
 *	Always returns 0.
 */
static int
twl_clks_add_clocks(struct twl_clks_softc *sc, const struct twl_clock *clks)
{
	int err;
	const struct twl_clock *walker;
	struct twl_clk_entry *entry;
	phandle_t child;
	char rnames[256];
	char *name, *state;
	int len = 0, prop_len;
	int enable;


	TWL_CLKS_XLOCK(sc);

	/* Add the regulators from the list */
	walker = &clks[0];
	while (walker->name != NULL) {

		/* Add the regulator to the list */
		entry = twl_clks_add_clock(sc, walker->name, walker->subdev,
		    walker->regbase);
		if (entry == NULL)
			continue;

		walker++;
	}

	/* Check for any FDT settings that need to be applied */
	child = ofw_bus_get_node(sc->sc_pdev);
	if (child) {

		prop_len = OF_getprop(child, "external-clocks", rnames, sizeof(rnames));
		while (len < prop_len) {
			name = rnames + len;
			len += strlen(name) + 1;
			if ((len >= prop_len) || (name[0] == '\0'))
				break;
			
			state = rnames + len;
			len += strlen(state) + 1;
			if (state[0] == '\0')
				break;
			
			enable = !strncmp(state, "on", 2);
			
			LIST_FOREACH(entry, &sc->sc_clks_list, link) {
				if (strcmp(entry->name, name) == 0) {
					twl_clks_set_state(sc, entry, enable);
					break;
				}
			}
		}
	}
	
	TWL_CLKS_XUNLOCK(sc);

	
	if (twl_clks_debug) {
		LIST_FOREACH(entry, &sc->sc_clks_list, link) {
			err = twl_clks_is_enabled(sc->sc_dev, entry->name, &enable);
			if (!err)
				device_printf(sc->sc_dev, "%s : %s\n", entry->name,
					enable ? "on" : "off");
		}
	}

	return (0);
}

/**
 *	twl_clks_init - initialises the list of clocks
 *	@dev: the twl_clks device
 *
 *	This function is called as an intrhook once interrupts have been enabled,
 *	this is done so that the driver has the option to enable/disable a clock
 *	based on settings providied in the FDT.
 *
 *	LOCKING:
 *	May takes the exclusive lock in the function.
 */
static void
twl_clks_init(void *dev)
{
	struct twl_clks_softc *sc;

	sc = device_get_softc((device_t)dev);

	if (twl_is_4030(sc->sc_pdev))
		twl_clks_add_clocks(sc, twl4030_clocks);
	else if (twl_is_6030(sc->sc_pdev) || twl_is_6025(sc->sc_pdev))
		twl_clks_add_clocks(sc, twl6030_clocks);

	config_intrhook_disestablish(&sc->sc_init_hook);
}

static int
twl_clks_probe(device_t dev)
{
	if (twl_is_4030(device_get_parent(dev)))
		device_set_desc(dev, "TI TWL4030 PMIC External Clocks");
	else if (twl_is_6025(device_get_parent(dev)) ||
	         twl_is_6030(device_get_parent(dev)))
		device_set_desc(dev, "TI TWL6025/TWL6030 PMIC External Clocks");
	else
		return (ENXIO);

	return (0);
}

static int
twl_clks_attach(device_t dev)
{
	struct twl_clks_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_pdev = device_get_parent(dev);

	TWL_CLKS_LOCK_INIT(sc);

	LIST_INIT(&sc->sc_clks_list);


	sc->sc_init_hook.ich_func = twl_clks_init;
	sc->sc_init_hook.ich_arg = dev;

	if (config_intrhook_establish(&sc->sc_init_hook) != 0)
		return (ENOMEM);

	return (0);
}

static int
twl_clks_detach(device_t dev)
{
	struct twl_clks_softc *sc;
	struct twl_clk_entry *clk;
	struct twl_clk_entry *tmp;

	sc = device_get_softc(dev);

	TWL_CLKS_XLOCK(sc);

	LIST_FOREACH_SAFE(clk, &sc->sc_clks_list, link, tmp) {
		LIST_REMOVE(clk, link);
		sysctl_remove_oid(clk->oid, 1, 0);
		free(clk, M_DEVBUF);
	}

	TWL_CLKS_XUNLOCK(sc);

	TWL_CLKS_LOCK_DESTROY(sc);

	return (0);
}

static device_method_t twl_clks_methods[] = {
	DEVMETHOD(device_probe,		twl_clks_probe),
	DEVMETHOD(device_attach,	twl_clks_attach),
	DEVMETHOD(device_detach,	twl_clks_detach),

	{0, 0},
};

static driver_t twl_clks_driver = {
	"twl_clks",
	twl_clks_methods,
	sizeof(struct twl_clks_softc),
};

static devclass_t twl_clks_devclass;

DRIVER_MODULE(twl_clks, twl, twl_clks_driver, twl_clks_devclass, 0, 0);
MODULE_VERSION(twl_clks, 1);
