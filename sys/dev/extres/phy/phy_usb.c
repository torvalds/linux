/*-
 * Copyright 2018 Michal Meloun <mmel@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/queue.h>
#include <sys/sx.h>


#include <dev/extres/phy/phy_usb.h>
#include <dev/extres/phy/phy_internal.h>

#include "phydev_if.h"

/*
 * USB phy controller methods.
 */
static phynode_usb_method_t phynode_usb_methods[] = {

	PHYNODEUSBMETHOD_END
};
DEFINE_CLASS_1(phynode_usb, phynode_usb_class, phynode_usb_methods,
    0, phynode_class);

/*
 * Create and initialize phy object, but do not register it.
 */
struct phynode *
phynode_usb_create(device_t pdev, phynode_class_t phynode_class,
    struct phynode_usb_init_def *def)

{
	struct phynode *phynode;
	struct phynode_usb_sc *sc;

	phynode = phynode_create(pdev, phynode_class, &def->phynode_init_def);
	if (phynode == NULL)
		return (NULL);
	sc = phynode_get_softc(phynode);
	sc->std_param = def->std_param;
	return (phynode);
}

struct phynode
*phynode_usb_register(struct phynode *phynode)
{

	return (phynode_register(phynode));
}

/* --------------------------------------------------------------------------
 *
 * Real consumers executive
 *
 */

/*
 * Set USB phy mode. (PHY_USB_MODE_*)
 */
int
phynode_usb_set_mode(struct phynode *phynode, int usb_mode)
{
	int rv;

	PHY_TOPO_ASSERT();

	PHYNODE_XLOCK(phynode);
	rv = PHYNODE_USB_SET_MODE(phynode, usb_mode);
	PHYNODE_UNLOCK(phynode);
	return (rv);
}

/*
 * Get USB phy mode. (PHY_USB_MODE_*)
 */
int
phynode_usb_get_mode(struct phynode *phynode, int *usb_mode)
{
	int rv;

	PHY_TOPO_ASSERT();

	PHYNODE_XLOCK(phynode);
	rv = PHYNODE_USB_GET_MODE(phynode, usb_mode);
	PHYNODE_UNLOCK(phynode);
	return (rv);
}

 /* --------------------------------------------------------------------------
 *
 * USB phy consumers interface.
 *
 */
int phy_usb_set_mode(phy_t phy, int usb_mode)
{
	int rv;
	struct phynode *phynode;

	phynode = phy->phynode;
	KASSERT(phynode->ref_cnt > 0,
	   ("Attempt to access unreferenced phy.\n"));

	PHY_TOPO_SLOCK();
	rv = phynode_usb_set_mode(phynode, usb_mode);
	PHY_TOPO_UNLOCK();
	return (rv);
}

int phy_usb_get_mode(phy_t phy, int *usb_mode)
{
	int rv;
	struct phynode *phynode;

	phynode = phy->phynode;
	KASSERT(phynode->ref_cnt > 0,
	   ("Attempt to access unreferenced phy.\n"));

	PHY_TOPO_SLOCK();
	rv = phynode_usb_get_mode(phynode, usb_mode);
	PHY_TOPO_UNLOCK();
	return (rv);
}
