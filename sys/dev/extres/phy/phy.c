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
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/sx.h>

#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include  <dev/extres/phy/phy.h>
#include  <dev/extres/phy/phy_internal.h>

#include "phydev_if.h"

MALLOC_DEFINE(M_PHY, "phy", "Phy framework");

/* Default phy methods. */
static int phynode_method_init(struct phynode *phynode);
static int phynode_method_enable(struct phynode *phynode, bool disable);
static int phynode_method_status(struct phynode *phynode, int *status);


/*
 * Phy controller methods.
 */
static phynode_method_t phynode_methods[] = {
	PHYNODEMETHOD(phynode_init,		phynode_method_init),
	PHYNODEMETHOD(phynode_enable,		phynode_method_enable),
	PHYNODEMETHOD(phynode_status,		phynode_method_status),

	PHYNODEMETHOD_END
};
DEFINE_CLASS_0(phynode, phynode_class, phynode_methods, 0);

static phynode_list_t phynode_list = TAILQ_HEAD_INITIALIZER(phynode_list);
struct sx phynode_topo_lock;
SX_SYSINIT(phy_topology, &phynode_topo_lock, "Phy topology lock");

/* ----------------------------------------------------------------------------
 *
 * Default phy methods for base class.
 *
 */

static int
phynode_method_init(struct phynode *phynode)
{

	return (0);
}

static int
phynode_method_enable(struct phynode *phynode, bool enable)
{

	if (!enable)
		return (ENXIO);

	return (0);
}

static int
phynode_method_status(struct phynode *phynode, int *status)
{
	*status = PHY_STATUS_ENABLED;
	return (0);
}

/* ----------------------------------------------------------------------------
 *
 * Internal functions.
 *
 */
/*
 * Create and initialize phy object, but do not register it.
 */
struct phynode *
phynode_create(device_t pdev, phynode_class_t phynode_class,
    struct phynode_init_def *def)
{
	struct phynode *phynode;


	/* Create object and initialize it. */
	phynode = malloc(sizeof(struct phynode), M_PHY, M_WAITOK | M_ZERO);
	kobj_init((kobj_t)phynode, (kobj_class_t)phynode_class);
	sx_init(&phynode->lock, "Phy node lock");

	/* Allocate softc if required. */
	if (phynode_class->size > 0) {
		phynode->softc = malloc(phynode_class->size, M_PHY,
		    M_WAITOK | M_ZERO);
	}

	/* Rest of init. */
	TAILQ_INIT(&phynode->consumers_list);
	phynode->id = def->id;
	phynode->pdev = pdev;
#ifdef FDT
	phynode->ofw_node = def->ofw_node;
#endif

	return (phynode);
}

/* Register phy object. */
struct phynode *
phynode_register(struct phynode *phynode)
{
	int rv;

#ifdef FDT
	if (phynode->ofw_node <= 0)
		phynode->ofw_node = ofw_bus_get_node(phynode->pdev);
	if (phynode->ofw_node <= 0)
		return (NULL);
#endif

	rv = PHYNODE_INIT(phynode);
	if (rv != 0) {
		printf("PHYNODE_INIT failed: %d\n", rv);
		return (NULL);
	}

	PHY_TOPO_XLOCK();
	TAILQ_INSERT_TAIL(&phynode_list, phynode, phylist_link);
	PHY_TOPO_UNLOCK();
#ifdef FDT
	OF_device_register_xref(OF_xref_from_node(phynode->ofw_node),
	    phynode->pdev);
#endif
	return (phynode);
}

static struct phynode *
phynode_find_by_id(device_t dev, intptr_t id)
{
	struct phynode *entry;

	PHY_TOPO_ASSERT();

	TAILQ_FOREACH(entry, &phynode_list, phylist_link) {
		if ((entry->pdev == dev) && (entry->id ==  id))
			return (entry);
	}

	return (NULL);
}

/* --------------------------------------------------------------------------
 *
 * Phy providers interface
 *
 */

void *
phynode_get_softc(struct phynode *phynode)
{

	return (phynode->softc);
}

device_t
phynode_get_device(struct phynode *phynode)
{

	return (phynode->pdev);
}

intptr_t phynode_get_id(struct phynode *phynode)
{

	return (phynode->id);
}

#ifdef FDT
phandle_t
phynode_get_ofw_node(struct phynode *phynode)
{

	return (phynode->ofw_node);
}
#endif

/* --------------------------------------------------------------------------
 *
 * Real consumers executive
 *
 */

/*
 * Enable phy.
 */
int
phynode_enable(struct phynode *phynode)
{
	int rv;

	PHY_TOPO_ASSERT();

	PHYNODE_XLOCK(phynode);
	if (phynode->enable_cnt == 0) {
		rv = PHYNODE_ENABLE(phynode, true);
		if (rv != 0) {
			PHYNODE_UNLOCK(phynode);
			return (rv);
		}
	}
	phynode->enable_cnt++;
	PHYNODE_UNLOCK(phynode);
	return (0);
}

/*
 * Disable phy.
 */
int
phynode_disable(struct phynode *phynode)
{
	int rv;

	PHY_TOPO_ASSERT();

	PHYNODE_XLOCK(phynode);
	if (phynode->enable_cnt == 1) {
		rv = PHYNODE_ENABLE(phynode, false);
		if (rv != 0) {
			PHYNODE_UNLOCK(phynode);
			return (rv);
		}
	}
	phynode->enable_cnt--;
	PHYNODE_UNLOCK(phynode);
	return (0);
}


/*
 * Get phy status. (PHY_STATUS_*)
 */
int
phynode_status(struct phynode *phynode, int *status)
{
	int rv;

	PHY_TOPO_ASSERT();

	PHYNODE_XLOCK(phynode);
	rv = PHYNODE_STATUS(phynode, status);
	PHYNODE_UNLOCK(phynode);
	return (rv);
}

 /* --------------------------------------------------------------------------
 *
 * Phy consumers interface.
 *
 */

/* Helper function for phy_get*() */
static phy_t
phy_create(struct phynode *phynode, device_t cdev)
{
	struct phy *phy;

	PHY_TOPO_ASSERT();

	phy =  malloc(sizeof(struct phy), M_PHY, M_WAITOK | M_ZERO);
	phy->cdev = cdev;
	phy->phynode = phynode;
	phy->enable_cnt = 0;

	PHYNODE_XLOCK(phynode);
	phynode->ref_cnt++;
	TAILQ_INSERT_TAIL(&phynode->consumers_list, phy, link);
	PHYNODE_UNLOCK(phynode);

	return (phy);
}

int
phy_enable(phy_t phy)
{
	int rv;
	struct phynode *phynode;

	phynode = phy->phynode;
	KASSERT(phynode->ref_cnt > 0,
	    ("Attempt to access unreferenced phy.\n"));

	PHY_TOPO_SLOCK();
	rv = phynode_enable(phynode);
	if (rv == 0)
		phy->enable_cnt++;
	PHY_TOPO_UNLOCK();
	return (rv);
}

int
phy_disable(phy_t phy)
{
	int rv;
	struct phynode *phynode;

	phynode = phy->phynode;
	KASSERT(phynode->ref_cnt > 0,
	   ("Attempt to access unreferenced phy.\n"));
	KASSERT(phy->enable_cnt > 0,
	   ("Attempt to disable already disabled phy.\n"));

	PHY_TOPO_SLOCK();
	rv = phynode_disable(phynode);
	if (rv == 0)
		phy->enable_cnt--;
	PHY_TOPO_UNLOCK();
	return (rv);
}

int
phy_status(phy_t phy, int *status)
{
	int rv;
	struct phynode *phynode;

	phynode = phy->phynode;
	KASSERT(phynode->ref_cnt > 0,
	   ("Attempt to access unreferenced phy.\n"));

	PHY_TOPO_SLOCK();
	rv = phynode_status(phynode, status);
	PHY_TOPO_UNLOCK();
	return (rv);
}

int
phy_get_by_id(device_t consumer_dev, device_t provider_dev, intptr_t id,
    phy_t *phy)
{
	struct phynode *phynode;

	PHY_TOPO_SLOCK();

	phynode = phynode_find_by_id(provider_dev, id);
	if (phynode == NULL) {
		PHY_TOPO_UNLOCK();
		return (ENODEV);
	}
	*phy = phy_create(phynode, consumer_dev);
	PHY_TOPO_UNLOCK();

	return (0);
}

void
phy_release(phy_t phy)
{
	struct phynode *phynode;

	phynode = phy->phynode;
	KASSERT(phynode->ref_cnt > 0,
	   ("Attempt to access unreferenced phy.\n"));

	PHY_TOPO_SLOCK();
	while (phy->enable_cnt > 0) {
		phynode_disable(phynode);
		phy->enable_cnt--;
	}
	PHYNODE_XLOCK(phynode);
	TAILQ_REMOVE(&phynode->consumers_list, phy, link);
	phynode->ref_cnt--;
	PHYNODE_UNLOCK(phynode);
	PHY_TOPO_UNLOCK();

	free(phy, M_PHY);
}

#ifdef FDT
int phydev_default_ofw_map(device_t provider, phandle_t xref, int ncells,
    pcell_t *cells, intptr_t *id)
{
	struct phynode *entry;
	phandle_t node;

	/* Single device can register multiple subnodes. */
	if (ncells == 0) {

		node = OF_node_from_xref(xref);
		PHY_TOPO_XLOCK();
		TAILQ_FOREACH(entry, &phynode_list, phylist_link) {
			if ((entry->pdev == provider) &&
			    (entry->ofw_node == node)) {
				*id = entry->id;
				PHY_TOPO_UNLOCK();
				return (0);
			}
		}
		PHY_TOPO_UNLOCK();
		return (ERANGE);
	}

	/* First cell is ID. */
	if (ncells == 1) {
		*id = cells[0];
		return (0);
	}

	/* No default way how to get ID, custom mapper is required. */
	return  (ERANGE);
}

int
phy_get_by_ofw_idx(device_t consumer_dev, phandle_t cnode, int idx, phy_t *phy)
{
	phandle_t xnode;
	pcell_t *cells;
	device_t phydev;
	int ncells, rv;
	intptr_t id;

	if (cnode <= 0)
		cnode = ofw_bus_get_node(consumer_dev);
	if (cnode <= 0) {
		device_printf(consumer_dev,
		    "%s called on not ofw based device\n", __func__);
		return (ENXIO);
	}
	rv = ofw_bus_parse_xref_list_alloc(cnode, "phys", "#phy-cells", idx,
	    &xnode, &ncells, &cells);
	if (rv != 0)
		return (rv);

	/* Tranlate provider to device. */
	phydev = OF_device_from_xref(xnode);
	if (phydev == NULL) {
		OF_prop_free(cells);
		return (ENODEV);
	}
	/* Map phy to number. */
	rv = PHYDEV_MAP(phydev, xnode, ncells, cells, &id);
	OF_prop_free(cells);
	if (rv != 0)
		return (rv);

	return (phy_get_by_id(consumer_dev, phydev, id, phy));
}

int
phy_get_by_ofw_name(device_t consumer_dev, phandle_t cnode, char *name,
    phy_t *phy)
{
	int rv, idx;

	if (cnode <= 0)
		cnode = ofw_bus_get_node(consumer_dev);
	if (cnode <= 0) {
		device_printf(consumer_dev,
		    "%s called on not ofw based device\n",  __func__);
		return (ENXIO);
	}
	rv = ofw_bus_find_string_index(cnode, "phy-names", name, &idx);
	if (rv != 0)
		return (rv);
	return (phy_get_by_ofw_idx(consumer_dev, cnode, idx, phy));
}

int
phy_get_by_ofw_property(device_t consumer_dev, phandle_t cnode, char *name,
    phy_t *phy)
{
	pcell_t *cells;
	device_t phydev;
	int ncells, rv;
	intptr_t id;

	if (cnode <= 0)
		cnode = ofw_bus_get_node(consumer_dev);
	if (cnode <= 0) {
		device_printf(consumer_dev,
		    "%s called on not ofw based device\n", __func__);
		return (ENXIO);
	}
	ncells = OF_getencprop_alloc_multi(cnode, name, sizeof(pcell_t),
	    (void **)&cells);
	if (ncells < 1)
		return (ENOENT);

	/* Tranlate provider to device. */
	phydev = OF_device_from_xref(cells[0]);
	if (phydev == NULL) {
		OF_prop_free(cells);
		return (ENODEV);
	}
	/* Map phy to number. */
	rv = PHYDEV_MAP(phydev, cells[0], ncells - 1 , cells + 1, &id);
	OF_prop_free(cells);
	if (rv != 0)
		return (rv);

	return (phy_get_by_id(consumer_dev, phydev, id, phy));
}
#endif
