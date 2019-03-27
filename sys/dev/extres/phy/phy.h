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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef DEV_EXTRES_PHY_H
#define DEV_EXTRES_PHY_H
#include "opt_platform.h"

#include <sys/kobj.h>
#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#endif
#include "phynode_if.h"

#define	PHY_STATUS_ENABLED	0x00000001

typedef struct phy *phy_t;

/* Initialization parameters. */
struct phynode_init_def {
	intptr_t		id;		/* Phy ID */
#ifdef FDT
	 phandle_t 		ofw_node;	/* OFW node of phy */
#endif
};

/*
 * Shorthands for constructing method tables.
 */
#define	PHYNODEMETHOD		KOBJMETHOD
#define	PHYNODEMETHOD_END	KOBJMETHOD_END
#define phynode_method_t	kobj_method_t
#define phynode_class_t		kobj_class_t
DECLARE_CLASS(phynode_class);

/*
 * Provider interface
 */
struct phynode *phynode_create(device_t pdev, phynode_class_t phynode_class,
    struct phynode_init_def *def);
struct phynode *phynode_register(struct phynode *phynode);
void *phynode_get_softc(struct phynode *phynode);
device_t phynode_get_device(struct phynode *phynode);
intptr_t phynode_get_id(struct phynode *phynode);
int phynode_enable(struct phynode *phynode);
int phynode_disable(struct phynode *phynode);
int phynode_status(struct phynode *phynode, int *status);
#ifdef FDT
phandle_t phynode_get_ofw_node(struct phynode *phynode);
#endif

/*
 * Consumer interface
 */
int phy_get_by_id(device_t consumer_dev, device_t provider_dev, intptr_t id,
    phy_t *phy);
void phy_release(phy_t phy);
int phy_enable(phy_t phy);
int phy_disable(phy_t phy);
int phy_status(phy_t phy, int *value);

#ifdef FDT
int phy_get_by_ofw_name(device_t consumer, phandle_t node, char *name,
    phy_t *phy);
int phy_get_by_ofw_idx(device_t consumer, phandle_t node, int idx, phy_t *phy);
int phy_get_by_ofw_property(device_t consumer, phandle_t node, char *name,
    phy_t *phy);
#endif

#endif /* DEV_EXTRES_PHY_H */
