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

#ifndef DEV_EXTRES_PHY_INTERNAL_H
#define DEV_EXTRES_PHY_INTERNAL_H

/* Forward declarations. */
struct phy;
struct phynode;

typedef TAILQ_HEAD(phynode_list, phynode) phynode_list_t;
typedef TAILQ_HEAD(phy_list, phy) phy_list_t;

/*
 * Phy node
 */
struct phynode {
	KOBJ_FIELDS;

	TAILQ_ENTRY(phynode)	phylist_link;	/* Global list entry */
	phy_list_t		consumers_list;	/* Consumers list */


	/* Details of this device. */
	const char		*name;		/* Globally unique name */

	device_t		pdev;		/* Producer device_t */
	void			*softc;		/* Producer softc */
	intptr_t		id;		/* Per producer unique id */
#ifdef FDT
	 phandle_t		ofw_node;	/* OFW node of phy */
#endif
	struct sx		lock;		/* Lock for this phy */
	int			ref_cnt;	/* Reference counter */
	int			enable_cnt;	/* Enabled counter */
};

struct phy {
	device_t		cdev;		/* consumer device*/
	struct phynode		*phynode;
	TAILQ_ENTRY(phy)	link;		/* Consumers list entry */

	int			enable_cnt;
};


#define PHY_TOPO_SLOCK()	sx_slock(&phynode_topo_lock)
#define PHY_TOPO_XLOCK()	sx_xlock(&phynode_topo_lock)
#define PHY_TOPO_UNLOCK()	sx_unlock(&phynode_topo_lock)
#define PHY_TOPO_ASSERT()	sx_assert(&phynode_topo_lock, SA_LOCKED)
#define PHY_TOPO_XASSERT() 	sx_assert(&phynode_topo_lock, SA_XLOCKED)

#define PHYNODE_SLOCK(_sc)	sx_slock(&((_sc)->lock))
#define PHYNODE_XLOCK(_sc)	sx_xlock(&((_sc)->lock))
#define PHYNODE_UNLOCK(_sc)	sx_unlock(&((_sc)->lock))

extern struct sx phynode_topo_lock;

#endif /* DEV_EXTRES_PHY_INTERNAL_H */
