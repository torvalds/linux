/*************************************************************************
SPDX-License-Identifier: BSD-3-Clause

Copyright (c) 2003-2007  Cavium Networks (support@cavium.com). All rights
reserved.


Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Cavium Networks nor the names of
      its contributors may be used to endorse or promote products
      derived from this software without specific prior written
      permission.

This Software, including technical data, may be subject to U.S. export  control laws, including the U.S. Export Administration Act and its  associated regulations, and may be subject to export or import  regulations in other countries.

TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
AND WITH ALL FAULTS AND CAVIUM  NETWORKS MAKES NO PROMISES, REPRESENTATIONS OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES WITH YOU.

*************************************************************************/
/* $FreeBSD$ */

/**
 * @file
 * External interface for the Cavium Octeon ethernet driver.
 *
 * $Id: cavium-ethernet.h 41589 2009-03-19 19:58:58Z cchavva $
 *
 */
#ifndef CAVIUM_ETHERNET_H
#define CAVIUM_ETHERNET_H

#include <sys/taskqueue.h>
#include <net/if_media.h>
#include <net/ifq.h>

/**
 * This is the definition of the Ethernet driver's private
 * driver state stored in ifp->if_softc.
 */
typedef struct {
	/* XXX FreeBSD device softcs must start with an ifnet pointer.  */
	struct ifnet *ifp;

	int                     port;           /* PKO hardware output port */
	int                     queue;          /* PKO hardware queue for the port */
	int                     fau;            /* Hardware fetch and add to count outstanding tx buffers */
	int                     imode;          /* Type of port. This is one of the enums in cvmx_helper_interface_mode_t */
#if 0
	struct ifnet_stats stats;          /* Device statistics */
#endif
	uint64_t                link_info;      /* Last negotiated link state */
	void (*poll)(struct ifnet *ifp);   /* Called periodically to check link status */

	/*
	 * FreeBSD additions.
	 */
	device_t dev;
	device_t miibus;

	int (*open)(struct ifnet *ifp);
	int (*stop)(struct ifnet *ifp);

	int (*init)(struct ifnet *ifp);
	void (*uninit)(struct ifnet *ifp);

	uint8_t mac[6];
	int phy_id;
	const char *phy_device;
	int (*mdio_read)(struct ifnet *, int, int);
	void (*mdio_write)(struct ifnet *, int, int, int);

	struct ifqueue tx_free_queue[16];

	int need_link_update;
	struct task link_task;
	struct ifmedia media;
	int if_flags;

	struct mtx tx_mtx;
} cvm_oct_private_t;


/**
 * Free a work queue entry received in a intercept callback.
 *
 * @param work_queue_entry
 *               Work queue entry to free
 * @return Zero on success, Negative on failure.
 */
int cvm_oct_free_work(void *work_queue_entry);

#endif
