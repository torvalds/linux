/*-
 * Copyright 2016 Michal Meloun <mmel@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include "opt_platform.h"
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/kobj.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/sx.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif
#include <dev/extres/regulator/regulator.h>

#include "regdev_if.h"

SYSCTL_NODE(_hw, OID_AUTO, regulator, CTLFLAG_RD, NULL, "Regulators");

MALLOC_DEFINE(M_REGULATOR, "regulator", "Regulator framework");

#define	DIV_ROUND_UP(n,d) howmany(n, d)

/* Forward declarations. */
struct regulator;
struct regnode;

typedef TAILQ_HEAD(regnode_list, regnode) regnode_list_t;
typedef TAILQ_HEAD(regulator_list, regulator) regulator_list_t;

/* Default regulator methods. */
static int regnode_method_enable(struct regnode *regnode, bool enable,
    int *udelay);
static int regnode_method_status(struct regnode *regnode, int *status);
static int regnode_method_set_voltage(struct regnode *regnode, int min_uvolt,
    int max_uvolt, int *udelay);
static int regnode_method_get_voltage(struct regnode *regnode, int *uvolt);
static void regulator_shutdown(void *dummy);

/*
 * Regulator controller methods.
 */
static regnode_method_t regnode_methods[] = {
	REGNODEMETHOD(regnode_enable,		regnode_method_enable),
	REGNODEMETHOD(regnode_status,		regnode_method_status),
	REGNODEMETHOD(regnode_set_voltage,	regnode_method_set_voltage),
	REGNODEMETHOD(regnode_get_voltage,	regnode_method_get_voltage),

	REGNODEMETHOD_END
};
DEFINE_CLASS_0(regnode, regnode_class, regnode_methods, 0);

/*
 * Regulator node - basic element for modelling SOC and bard power supply
 * chains. Its contains producer data.
 */
struct regnode {
	KOBJ_FIELDS;

	TAILQ_ENTRY(regnode)	reglist_link;	/* Global list entry */
	regulator_list_t	consumers_list;	/* Consumers list */

	/* Cache for already resolved names */
	struct regnode		*parent;	/* Resolved parent */

	/* Details of this device. */
	const char		*name;		/* Globally unique name */
	const char		*parent_name;	/* Parent name */

	device_t		pdev;		/* Producer device_t */
	void			*softc;		/* Producer softc */
	intptr_t		id;		/* Per producer unique id */
#ifdef FDT
	 phandle_t 		ofw_node;	/* OFW node of regulator */
#endif
	int			flags;		/* REGULATOR_FLAGS_ */
	struct sx		lock;		/* Lock for this regulator */
	int			ref_cnt;	/* Reference counter */
	int			enable_cnt;	/* Enabled counter */

	struct regnode_std_param std_param;	/* Standard parameters */

	struct sysctl_ctx_list	sysctl_ctx;
};

/*
 * Per consumer data, information about how a consumer is using a regulator
 * node.
 * A pointer to this structure is used as a handle in the consumer interface.
 */
struct regulator {
	device_t		cdev;		/* Consumer device */
	struct regnode		*regnode;
	TAILQ_ENTRY(regulator)	link;		/* Consumers list entry */

	int			enable_cnt;
	int 			min_uvolt;	/* Requested uvolt range */
	int 			max_uvolt;
};

/*
 * Regulator names must be system wide unique.
 */
static regnode_list_t regnode_list = TAILQ_HEAD_INITIALIZER(regnode_list);

static struct sx		regnode_topo_lock;
SX_SYSINIT(regulator_topology, &regnode_topo_lock, "Regulator topology lock");

#define REG_TOPO_SLOCK()	sx_slock(&regnode_topo_lock)
#define REG_TOPO_XLOCK()	sx_xlock(&regnode_topo_lock)
#define REG_TOPO_UNLOCK()	sx_unlock(&regnode_topo_lock)
#define REG_TOPO_ASSERT()	sx_assert(&regnode_topo_lock, SA_LOCKED)
#define REG_TOPO_XASSERT() 	sx_assert(&regnode_topo_lock, SA_XLOCKED)

#define REGNODE_SLOCK(_sc)	sx_slock(&((_sc)->lock))
#define REGNODE_XLOCK(_sc)	sx_xlock(&((_sc)->lock))
#define REGNODE_UNLOCK(_sc)	sx_unlock(&((_sc)->lock))

SYSINIT(regulator_shutdown, SI_SUB_LAST, SI_ORDER_ANY, regulator_shutdown,
    NULL);

/*
 * Disable unused regulator
 * We run this function at SI_SUB_LAST which mean that every driver that needs
 * regulator should have already enable them.
 * All the remaining regulators should be those left enabled by the bootloader
 * or enable by default by the PMIC.
 */
static void
regulator_shutdown(void *dummy)
{
	struct regnode *entry;
	int status, ret;
	int disable = 1;

	REG_TOPO_SLOCK();
	TUNABLE_INT_FETCH("hw.regulator.disable_unused", &disable);
	TAILQ_FOREACH(entry, &regnode_list, reglist_link) {
		if (!entry->std_param.always_on && disable) {
			if (bootverbose)
				printf("regulator: shutting down %s\n",
				    entry->name);
			ret = regnode_status(entry, &status);
			if (ret == 0 && status == REGULATOR_STATUS_ENABLED)
				regnode_stop(entry, 0);
		}
	}
	REG_TOPO_UNLOCK();
}

/*
 * sysctl handler
 */
static int
regnode_uvolt_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct regnode *regnode = arg1;
	int rv, uvolt;

	if (regnode->std_param.min_uvolt == regnode->std_param.max_uvolt) {
		uvolt = regnode->std_param.min_uvolt;
	} else {
		REG_TOPO_SLOCK();
		if ((rv = regnode_get_voltage(regnode, &uvolt)) != 0) {
			REG_TOPO_UNLOCK();
			return (rv);
		}
		REG_TOPO_UNLOCK();
	}

	return sysctl_handle_int(oidp, &uvolt, sizeof(uvolt), req);
}

/* ----------------------------------------------------------------------------
 *
 * Default regulator methods for base class.
 *
 */
static int
regnode_method_enable(struct regnode *regnode, bool enable, int *udelay)
{

	if (!enable)
		return (ENXIO);

	*udelay = 0;
	return (0);
}

static int
regnode_method_status(struct regnode *regnode, int *status)
{
	*status = REGULATOR_STATUS_ENABLED;
	return (0);
}

static int
regnode_method_set_voltage(struct regnode *regnode, int min_uvolt, int max_uvolt,
    int *udelay)
{

	if ((min_uvolt > regnode->std_param.max_uvolt) ||
	    (max_uvolt < regnode->std_param.min_uvolt))
		return (ERANGE);
	*udelay = 0;
	return (0);
}

static int
regnode_method_get_voltage(struct regnode *regnode, int *uvolt)
{

	return (regnode->std_param.min_uvolt +
	    (regnode->std_param.max_uvolt - regnode->std_param.min_uvolt) / 2);
}

/* ----------------------------------------------------------------------------
 *
 * Internal functions.
 *
 */

static struct regnode *
regnode_find_by_name(const char *name)
{
	struct regnode *entry;

	REG_TOPO_ASSERT();

	TAILQ_FOREACH(entry, &regnode_list, reglist_link) {
		if (strcmp(entry->name, name) == 0)
			return (entry);
	}
	return (NULL);
}

static struct regnode *
regnode_find_by_id(device_t dev, intptr_t id)
{
	struct regnode *entry;

	REG_TOPO_ASSERT();

	TAILQ_FOREACH(entry, &regnode_list, reglist_link) {
		if ((entry->pdev == dev) && (entry->id ==  id))
			return (entry);
	}

	return (NULL);
}

/*
 * Create and initialize regulator object, but do not register it.
 */
struct regnode *
regnode_create(device_t pdev, regnode_class_t regnode_class,
    struct regnode_init_def *def)
{
	struct regnode *regnode;
	struct sysctl_oid *regnode_oid;

	KASSERT(def->name != NULL, ("regulator name is NULL"));
	KASSERT(def->name[0] != '\0', ("regulator name is empty"));

	REG_TOPO_SLOCK();
	if (regnode_find_by_name(def->name) != NULL)
		panic("Duplicated regulator registration: %s\n", def->name);
	REG_TOPO_UNLOCK();

	/* Create object and initialize it. */
	regnode = malloc(sizeof(struct regnode), M_REGULATOR,
	    M_WAITOK | M_ZERO);
	kobj_init((kobj_t)regnode, (kobj_class_t)regnode_class);
	sx_init(&regnode->lock, "Regulator node lock");

	/* Allocate softc if required. */
	if (regnode_class->size > 0) {
		regnode->softc = malloc(regnode_class->size, M_REGULATOR,
		    M_WAITOK | M_ZERO);
	}


	/* Copy all strings unless they're flagged as static. */
	if (def->flags & REGULATOR_FLAGS_STATIC) {
		regnode->name = def->name;
		regnode->parent_name = def->parent_name;
	} else {
		regnode->name = strdup(def->name, M_REGULATOR);
		if (def->parent_name != NULL)
			regnode->parent_name = strdup(def->parent_name,
			    M_REGULATOR);
	}

	/* Rest of init. */
	TAILQ_INIT(&regnode->consumers_list);
	regnode->id = def->id;
	regnode->pdev = pdev;
	regnode->flags = def->flags;
	regnode->parent = NULL;
	regnode->std_param = def->std_param;
#ifdef FDT
	regnode->ofw_node = def->ofw_node;
#endif

	sysctl_ctx_init(&regnode->sysctl_ctx);
	regnode_oid = SYSCTL_ADD_NODE(&regnode->sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_hw_regulator),
	    OID_AUTO, regnode->name,
	    CTLFLAG_RD, 0, "A regulator node");

	SYSCTL_ADD_INT(&regnode->sysctl_ctx,
	    SYSCTL_CHILDREN(regnode_oid),
	    OID_AUTO, "min_uvolt",
	    CTLFLAG_RD, &regnode->std_param.min_uvolt, 0,
	    "Minimal voltage (in uV)");
	SYSCTL_ADD_INT(&regnode->sysctl_ctx,
	    SYSCTL_CHILDREN(regnode_oid),
	    OID_AUTO, "max_uvolt",
	    CTLFLAG_RD, &regnode->std_param.max_uvolt, 0,
	    "Maximal voltage (in uV)");
	SYSCTL_ADD_INT(&regnode->sysctl_ctx,
	    SYSCTL_CHILDREN(regnode_oid),
	    OID_AUTO, "min_uamp",
	    CTLFLAG_RD, &regnode->std_param.min_uamp, 0,
	    "Minimal amperage (in uA)");
	SYSCTL_ADD_INT(&regnode->sysctl_ctx,
	    SYSCTL_CHILDREN(regnode_oid),
	    OID_AUTO, "max_uamp",
	    CTLFLAG_RD, &regnode->std_param.max_uamp, 0,
	    "Maximal amperage (in uA)");
	SYSCTL_ADD_INT(&regnode->sysctl_ctx,
	    SYSCTL_CHILDREN(regnode_oid),
	    OID_AUTO, "ramp_delay",
	    CTLFLAG_RD, &regnode->std_param.ramp_delay, 0,
	    "Ramp delay (in uV/us)");
	SYSCTL_ADD_INT(&regnode->sysctl_ctx,
	    SYSCTL_CHILDREN(regnode_oid),
	    OID_AUTO, "enable_delay",
	    CTLFLAG_RD, &regnode->std_param.enable_delay, 0,
	    "Enable delay (in us)");
	SYSCTL_ADD_INT(&regnode->sysctl_ctx,
	    SYSCTL_CHILDREN(regnode_oid),
	    OID_AUTO, "enable_cnt",
	    CTLFLAG_RD, &regnode->enable_cnt, 0,
	    "The regulator enable counter");
	SYSCTL_ADD_U8(&regnode->sysctl_ctx,
	    SYSCTL_CHILDREN(regnode_oid),
	    OID_AUTO, "boot_on",
	    CTLFLAG_RD, (uint8_t *) &regnode->std_param.boot_on, 0,
	    "Is enabled on boot");
	SYSCTL_ADD_U8(&regnode->sysctl_ctx,
	    SYSCTL_CHILDREN(regnode_oid),
	    OID_AUTO, "always_on",
	    CTLFLAG_RD, (uint8_t *)&regnode->std_param.always_on, 0,
	    "Is always enabled");

	SYSCTL_ADD_PROC(&regnode->sysctl_ctx,
	    SYSCTL_CHILDREN(regnode_oid),
	    OID_AUTO, "uvolt",
	    CTLTYPE_INT | CTLFLAG_RD,
	    regnode, 0, regnode_uvolt_sysctl,
	    "I",
	    "Current voltage (in uV)");

	return (regnode);
}

/* Register regulator object. */
struct regnode *
regnode_register(struct regnode *regnode)
{
	int rv;

#ifdef FDT
	if (regnode->ofw_node <= 0)
		regnode->ofw_node = ofw_bus_get_node(regnode->pdev);
	if (regnode->ofw_node <= 0)
		return (NULL);
#endif

	rv = REGNODE_INIT(regnode);
	if (rv != 0) {
		printf("REGNODE_INIT failed: %d\n", rv);
		return (NULL);
	}

	REG_TOPO_XLOCK();
	TAILQ_INSERT_TAIL(&regnode_list, regnode, reglist_link);
	REG_TOPO_UNLOCK();
#ifdef FDT
	OF_device_register_xref(OF_xref_from_node(regnode->ofw_node),
	    regnode->pdev);
#endif
	return (regnode);
}

static int
regnode_resolve_parent(struct regnode *regnode)
{

	/* All ready resolved or no parent? */
	if ((regnode->parent != NULL) ||
	    (regnode->parent_name == NULL))
		return (0);

	regnode->parent = regnode_find_by_name(regnode->parent_name);
	if (regnode->parent == NULL)
		return (ENODEV);
	return (0);
}

static void
regnode_delay(int usec)
{
	int ticks;

	if (usec == 0)
		return;
	ticks = (usec * hz + 999999) / 1000000;

	if (cold || ticks < 2)
		DELAY(usec);
	else
		pause("REGULATOR", ticks);
}

/* --------------------------------------------------------------------------
 *
 * Regulator providers interface
 *
 */

const char *
regnode_get_name(struct regnode *regnode)
{

	return (regnode->name);
}

const char *
regnode_get_parent_name(struct regnode *regnode)
{

	return (regnode->parent_name);
}

int
regnode_get_flags(struct regnode *regnode)
{

	return (regnode->flags);
}

void *
regnode_get_softc(struct regnode *regnode)
{

	return (regnode->softc);
}

device_t
regnode_get_device(struct regnode *regnode)
{

	return (regnode->pdev);
}

struct regnode_std_param *regnode_get_stdparam(struct regnode *regnode)
{

	return (&regnode->std_param);
}

void regnode_topo_unlock(void)
{

	REG_TOPO_UNLOCK();
}

void regnode_topo_xlock(void)
{

	REG_TOPO_XLOCK();
}

void regnode_topo_slock(void)
{

	REG_TOPO_SLOCK();
}


/* --------------------------------------------------------------------------
 *
 * Real consumers executive
 *
 */
struct regnode *
regnode_get_parent(struct regnode *regnode)
{
	int rv;

	REG_TOPO_ASSERT();

	rv = regnode_resolve_parent(regnode);
	if (rv != 0)
		return (NULL);

	return (regnode->parent);
}

/*
 * Enable regulator.
 */
int
regnode_enable(struct regnode *regnode)
{
	int udelay;
	int rv;

	REG_TOPO_ASSERT();

	/* Enable regulator for each node in chain, starting from source. */
	rv = regnode_resolve_parent(regnode);
	if (rv != 0)
		return (rv);
	if (regnode->parent != NULL) {
		rv = regnode_enable(regnode->parent);
		if (rv != 0)
			return (rv);
	}

	/* Handle this node. */
	REGNODE_XLOCK(regnode);
	if (regnode->enable_cnt == 0) {
		rv = REGNODE_ENABLE(regnode, true, &udelay);
		if (rv != 0) {
			REGNODE_UNLOCK(regnode);
			return (rv);
		}
		regnode_delay(udelay);
	}
	regnode->enable_cnt++;
	REGNODE_UNLOCK(regnode);
	return (0);
}

/*
 * Disable regulator.
 */
int
regnode_disable(struct regnode *regnode)
{
	int udelay;
	int rv;

	REG_TOPO_ASSERT();
	rv = 0;

	REGNODE_XLOCK(regnode);
	/* Disable regulator for each node in chain, starting from consumer. */
	if (regnode->enable_cnt == 1 &&
	    (regnode->flags & REGULATOR_FLAGS_NOT_DISABLE) == 0 &&
	    !regnode->std_param.always_on) {
		rv = REGNODE_ENABLE(regnode, false, &udelay);
		if (rv != 0) {
			REGNODE_UNLOCK(regnode);
			return (rv);
		}
		regnode_delay(udelay);
	}
	regnode->enable_cnt--;
	REGNODE_UNLOCK(regnode);

	rv = regnode_resolve_parent(regnode);
	if (rv != 0)
		return (rv);
	if (regnode->parent != NULL)
		rv = regnode_disable(regnode->parent);
	return (rv);
}

/*
 * Stop regulator.
 */
int
regnode_stop(struct regnode *regnode, int depth)
{
	int udelay;
	int rv;

	REG_TOPO_ASSERT();
	rv = 0;

	REGNODE_XLOCK(regnode);
	/* The first node must not be enabled. */
	if ((regnode->enable_cnt != 0) && (depth == 0)) {
		REGNODE_UNLOCK(regnode);
		return (EBUSY);
	}
	/* Disable regulator for each node in chain, starting from consumer */
	if ((regnode->enable_cnt == 0) &&
	    ((regnode->flags & REGULATOR_FLAGS_NOT_DISABLE) == 0)) {
		rv = REGNODE_STOP(regnode, &udelay);
		if (rv != 0) {
			REGNODE_UNLOCK(regnode);
			return (rv);
		}
		regnode_delay(udelay);
	}
	REGNODE_UNLOCK(regnode);

	rv = regnode_resolve_parent(regnode);
	if (rv != 0)
		return (rv);
	if (regnode->parent != NULL && regnode->parent->enable_cnt == 0)
		rv = regnode_stop(regnode->parent, depth + 1);
	return (rv);
}

/*
 * Get regulator status. (REGULATOR_STATUS_*)
 */
int
regnode_status(struct regnode *regnode, int *status)
{
	int rv;

	REG_TOPO_ASSERT();

	REGNODE_XLOCK(regnode);
	rv = REGNODE_STATUS(regnode, status);
	REGNODE_UNLOCK(regnode);
	return (rv);
}

/*
 * Get actual regulator voltage.
 */
int
regnode_get_voltage(struct regnode *regnode, int *uvolt)
{
	int rv;

	REG_TOPO_ASSERT();

	REGNODE_XLOCK(regnode);
	rv = REGNODE_GET_VOLTAGE(regnode, uvolt);
	REGNODE_UNLOCK(regnode);

	/* Pass call into parent, if regulator is in bypass mode. */
	if (rv == ENOENT) {
		rv = regnode_resolve_parent(regnode);
		if (rv != 0)
			return (rv);
		if (regnode->parent != NULL)
			rv = regnode_get_voltage(regnode->parent, uvolt);

	}
	return (rv);
}

/*
 * Set regulator voltage.
 */
int
regnode_set_voltage(struct regnode *regnode, int min_uvolt, int max_uvolt)
{
	int udelay;
	int rv;

	REG_TOPO_ASSERT();

	REGNODE_XLOCK(regnode);

	rv = REGNODE_SET_VOLTAGE(regnode, min_uvolt, max_uvolt, &udelay);
	if (rv == 0)
		regnode_delay(udelay);
	REGNODE_UNLOCK(regnode);
	return (rv);
}

/*
 * Consumer variant of regnode_set_voltage().
 */
static int
regnode_set_voltage_checked(struct regnode *regnode, struct regulator *reg,
    int min_uvolt, int max_uvolt)
{
	int udelay;
	int all_max_uvolt;
	int all_min_uvolt;
	struct regulator *tmp;
	int rv;

	REG_TOPO_ASSERT();

	REGNODE_XLOCK(regnode);
	/* Return error if requested range is outside of regulator range. */
	if ((min_uvolt > regnode->std_param.max_uvolt) ||
	    (max_uvolt < regnode->std_param.min_uvolt)) {
		REGNODE_UNLOCK(regnode);
		return (ERANGE);
	}

	/* Get actual voltage range for all consumers. */
	all_min_uvolt = regnode->std_param.min_uvolt;
	all_max_uvolt = regnode->std_param.max_uvolt;
	TAILQ_FOREACH(tmp, &regnode->consumers_list, link) {
		/* Don't take requestor in account. */
		if (tmp == reg)
			continue;
		if (all_min_uvolt < tmp->min_uvolt)
			all_min_uvolt = tmp->min_uvolt;
		if (all_max_uvolt > tmp->max_uvolt)
			all_max_uvolt = tmp->max_uvolt;
	}

	/* Test if request fits to actual contract. */
	if ((min_uvolt > all_max_uvolt) ||
	    (max_uvolt < all_min_uvolt)) {
		REGNODE_UNLOCK(regnode);
		return (ERANGE);
	}

	/* Adjust new range.*/
	if (min_uvolt < all_min_uvolt)
		min_uvolt = all_min_uvolt;
	if (max_uvolt > all_max_uvolt)
		max_uvolt = all_max_uvolt;

	rv = REGNODE_SET_VOLTAGE(regnode, min_uvolt, max_uvolt, &udelay);
	regnode_delay(udelay);
	REGNODE_UNLOCK(regnode);
	return (rv);
}

#ifdef FDT
phandle_t
regnode_get_ofw_node(struct regnode *regnode)
{

	return (regnode->ofw_node);
}
#endif

/* --------------------------------------------------------------------------
 *
 * Regulator consumers interface.
 *
 */
/* Helper function for regulator_get*() */
static regulator_t
regulator_create(struct regnode *regnode, device_t cdev)
{
	struct regulator *reg;

	REG_TOPO_ASSERT();

	reg =  malloc(sizeof(struct regulator), M_REGULATOR,
	    M_WAITOK | M_ZERO);
	reg->cdev = cdev;
	reg->regnode = regnode;
	reg->enable_cnt = 0;

	REGNODE_XLOCK(regnode);
	regnode->ref_cnt++;
	TAILQ_INSERT_TAIL(&regnode->consumers_list, reg, link);
	reg ->min_uvolt = regnode->std_param.min_uvolt;
	reg ->max_uvolt = regnode->std_param.max_uvolt;
	REGNODE_UNLOCK(regnode);

	return (reg);
}

int
regulator_enable(regulator_t reg)
{
	int rv;
	struct regnode *regnode;

	regnode = reg->regnode;
	KASSERT(regnode->ref_cnt > 0,
	   ("Attempt to access unreferenced regulator: %s\n", regnode->name));
	REG_TOPO_SLOCK();
	rv = regnode_enable(regnode);
	if (rv == 0)
		reg->enable_cnt++;
	REG_TOPO_UNLOCK();
	return (rv);
}

int
regulator_disable(regulator_t reg)
{
	int rv;
	struct regnode *regnode;

	regnode = reg->regnode;
	KASSERT(regnode->ref_cnt > 0,
	   ("Attempt to access unreferenced regulator: %s\n", regnode->name));
	KASSERT(reg->enable_cnt > 0,
	   ("Attempt to disable already disabled regulator: %s\n",
	   regnode->name));
	REG_TOPO_SLOCK();
	rv = regnode_disable(regnode);
	if (rv == 0)
		reg->enable_cnt--;
	REG_TOPO_UNLOCK();
	return (rv);
}

int
regulator_stop(regulator_t reg)
{
	int rv;
	struct regnode *regnode;

	regnode = reg->regnode;
	KASSERT(regnode->ref_cnt > 0,
	   ("Attempt to access unreferenced regulator: %s\n", regnode->name));
	KASSERT(reg->enable_cnt == 0,
	   ("Attempt to stop already enabled regulator: %s\n", regnode->name));

	REG_TOPO_SLOCK();
	rv = regnode_stop(regnode, 0);
	REG_TOPO_UNLOCK();
	return (rv);
}

int
regulator_status(regulator_t reg, int *status)
{
	int rv;
	struct regnode *regnode;

	regnode = reg->regnode;
	KASSERT(regnode->ref_cnt > 0,
	   ("Attempt to access unreferenced regulator: %s\n", regnode->name));

	REG_TOPO_SLOCK();
	rv = regnode_status(regnode, status);
	REG_TOPO_UNLOCK();
	return (rv);
}

int
regulator_get_voltage(regulator_t reg, int *uvolt)
{
	int rv;
	struct regnode *regnode;

	regnode = reg->regnode;
	KASSERT(regnode->ref_cnt > 0,
	   ("Attempt to access unreferenced regulator: %s\n", regnode->name));

	REG_TOPO_SLOCK();
	rv = regnode_get_voltage(regnode, uvolt);
	REG_TOPO_UNLOCK();
	return (rv);
}

int
regulator_set_voltage(regulator_t reg, int min_uvolt, int max_uvolt)
{
	struct regnode *regnode;
	int rv;

	regnode = reg->regnode;
	KASSERT(regnode->ref_cnt > 0,
	   ("Attempt to access unreferenced regulator: %s\n", regnode->name));

	REG_TOPO_SLOCK();

	rv = regnode_set_voltage_checked(regnode, reg, min_uvolt, max_uvolt);
	if (rv == 0) {
		reg->min_uvolt = min_uvolt;
		reg->max_uvolt = max_uvolt;
	}
	REG_TOPO_UNLOCK();
	return (rv);
}

const char *
regulator_get_name(regulator_t reg)
{
	struct regnode *regnode;

	regnode = reg->regnode;
	KASSERT(regnode->ref_cnt > 0,
	   ("Attempt to access unreferenced regulator: %s\n", regnode->name));
	return (regnode->name);
}

int
regulator_get_by_name(device_t cdev, const char *name, regulator_t *reg)
{
	struct regnode *regnode;

	REG_TOPO_SLOCK();
	regnode = regnode_find_by_name(name);
	if (regnode == NULL) {
		REG_TOPO_UNLOCK();
		return (ENODEV);
	}
	*reg = regulator_create(regnode, cdev);
	REG_TOPO_UNLOCK();
	return (0);
}

int
regulator_get_by_id(device_t cdev, device_t pdev, intptr_t id, regulator_t *reg)
{
	struct regnode *regnode;

	REG_TOPO_SLOCK();

	regnode = regnode_find_by_id(pdev, id);
	if (regnode == NULL) {
		REG_TOPO_UNLOCK();
		return (ENODEV);
	}
	*reg = regulator_create(regnode, cdev);
	REG_TOPO_UNLOCK();

	return (0);
}

int
regulator_release(regulator_t reg)
{
	struct regnode *regnode;

	regnode = reg->regnode;
	KASSERT(regnode->ref_cnt > 0,
	   ("Attempt to access unreferenced regulator: %s\n", regnode->name));
	REG_TOPO_SLOCK();
	while (reg->enable_cnt > 0) {
		regnode_disable(regnode);
		reg->enable_cnt--;
	}
	REGNODE_XLOCK(regnode);
	TAILQ_REMOVE(&regnode->consumers_list, reg, link);
	regnode->ref_cnt--;
	REGNODE_UNLOCK(regnode);
	REG_TOPO_UNLOCK();

	free(reg, M_REGULATOR);
	return (0);
}

#ifdef FDT
/* Default DT mapper. */
int
regdev_default_ofw_map(device_t dev, phandle_t 	xref, int ncells,
    pcell_t *cells, intptr_t *id)
{
	if (ncells == 0)
		*id = 1;
	else if (ncells == 1)
		*id = cells[0];
	else
		return  (ERANGE);

	return (0);
}

int
regulator_parse_ofw_stdparam(device_t pdev, phandle_t node,
    struct regnode_init_def *def)
{
	phandle_t supply_xref;
	struct regnode_std_param *par;
	int rv;

	par = &def->std_param;
	rv = OF_getprop_alloc(node, "regulator-name",
	    (void **)&def->name);
	if (rv <= 0) {
		device_printf(pdev, "%s: Missing regulator name\n",
		 __func__);
		return (ENXIO);
	}

	rv = OF_getencprop(node, "regulator-min-microvolt", &par->min_uvolt,
	    sizeof(par->min_uvolt));
	if (rv <= 0)
		par->min_uvolt = 0;

	rv = OF_getencprop(node, "regulator-max-microvolt", &par->max_uvolt,
	    sizeof(par->max_uvolt));
	if (rv <= 0)
		par->max_uvolt = 0;

	rv = OF_getencprop(node, "regulator-min-microamp", &par->min_uamp,
	    sizeof(par->min_uamp));
	if (rv <= 0)
		par->min_uamp = 0;

	rv = OF_getencprop(node, "regulator-max-microamp", &par->max_uamp,
	    sizeof(par->max_uamp));
	if (rv <= 0)
		par->max_uamp = 0;

	rv = OF_getencprop(node, "regulator-ramp-delay", &par->ramp_delay,
	    sizeof(par->ramp_delay));
	if (rv <= 0)
		par->ramp_delay = 0;

	rv = OF_getencprop(node, "regulator-enable-ramp-delay",
	    &par->enable_delay, sizeof(par->enable_delay));
	if (rv <= 0)
		par->enable_delay = 0;

	if (OF_hasprop(node, "regulator-boot-on"))
		par->boot_on = true;

	if (OF_hasprop(node, "regulator-always-on"))
		par->always_on = true;

	if (OF_hasprop(node, "enable-active-high"))
		par->enable_active_high = 1;

	rv = OF_getencprop(node, "vin-supply", &supply_xref,
	    sizeof(supply_xref));
	if (rv >=  0) {
		rv = OF_getprop_alloc(supply_xref, "regulator-name",
		    (void **)&def->parent_name);
		if (rv <= 0)
			def->parent_name = NULL;
	}
	return (0);
}

int
regulator_get_by_ofw_property(device_t cdev, phandle_t cnode, char *name,
    regulator_t *reg)
{
	phandle_t *cells;
	device_t regdev;
	int ncells, rv;
	intptr_t id;

	*reg = NULL;

	if (cnode <= 0)
		cnode = ofw_bus_get_node(cdev);
	if (cnode <= 0) {
		device_printf(cdev, "%s called on not ofw based device\n",
		 __func__);
		return (ENXIO);
	}

	cells = NULL;
	ncells = OF_getencprop_alloc_multi(cnode, name, sizeof(*cells),
	    (void **)&cells);
	if (ncells <= 0)
		return (ENOENT);

	/* Translate xref to device */
	regdev = OF_device_from_xref(cells[0]);
	if (regdev == NULL) {
		OF_prop_free(cells);
		return (ENODEV);
	}

	/* Map regulator to number */
	rv = REGDEV_MAP(regdev, cells[0], ncells - 1, cells + 1, &id);
	OF_prop_free(cells);
	if (rv != 0)
		return (rv);
	return (regulator_get_by_id(cdev, regdev, id, reg));
}
#endif

/* --------------------------------------------------------------------------
 *
 * Regulator utility functions.
 *
 */

/* Convert raw selector value to real voltage */
int
regulator_range_sel8_to_volt(struct regulator_range *ranges, int nranges,
   uint8_t sel, int *volt)
{
	struct regulator_range *range;
	int i;

	if (nranges == 0)
		panic("Voltage regulator have zero ranges\n");

	for (i = 0; i < nranges ; i++) {
		range = ranges  + i;

		if (!(sel >= range->min_sel &&
		      sel <= range->max_sel))
			continue;

		sel -= range->min_sel;

		*volt = range->min_uvolt + sel * range->step_uvolt;
		return (0);
	}

	return (ERANGE);
}

int
regulator_range_volt_to_sel8(struct regulator_range *ranges, int nranges,
    int min_uvolt, int max_uvolt, uint8_t *out_sel)
{
	struct regulator_range *range;
	uint8_t sel;
	int uvolt;
	int rv, i;

	if (nranges == 0)
		panic("Voltage regulator have zero ranges\n");

	for (i = 0; i < nranges; i++) {
		range = ranges  + i;
		uvolt = range->min_uvolt +
		    (range->max_sel - range->min_sel) * range->step_uvolt;

		if ((min_uvolt > uvolt) ||
		    (max_uvolt < range->min_uvolt))
			continue;

		if (min_uvolt <= range->min_uvolt)
			min_uvolt = range->min_uvolt;

		/* if step == 0 -> fixed voltage range. */
		if (range->step_uvolt == 0)
			sel = 0;
		else
			sel = DIV_ROUND_UP(min_uvolt - range->min_uvolt,
			   range->step_uvolt);


		sel += range->min_sel;

		break;
	}

	if (i >= nranges)
		return (ERANGE);

	/* Verify new settings. */
	rv = regulator_range_sel8_to_volt(ranges, nranges, sel, &uvolt);
	if (rv != 0)
		return (rv);
	if ((uvolt < min_uvolt) || (uvolt > max_uvolt))
		return (ERANGE);

	*out_sel = sel;
	return (0);
}
