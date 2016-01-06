#ifndef DEF_RDMA_VT_H
#define DEF_RDMA_VT_H

/*
 * Copyright(c) 2015 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * Structure that low level drivers will populate in order to register with the
 * rdmavt layer.
 */

#include "ib_verbs.h"

/*
 * Things that are driver specific, module parameters in hfi1 and qib
 */
struct rvt_driver_params {
	/*
	 * driver required fields:
	 *	node_guid
	 *	phys_port_cnt
	 *	dma_device
	 *	owner
	 * driver optional fields (rvt will provide generic value if blank):
	 *	name
	 *	node_desc
	 * rvt fields, driver value ignored:
	 *	uverbs_abi_ver
	 *	node_type
	 *	num_comp_vectors
	 *	uverbs_cmd_mask
	 */
	struct ib_device_attr props;

	/*
	 * Drivers will need to support a number of notifications to rvt in
	 * accordance with certain events. This structure should contain a mask
	 * of the supported events. Such events that the rvt may need to know
	 * about include:
	 * port errors
	 * port active
	 * lid change
	 * sm change
	 * client reregister
	 * pkey change
	 *
	 * There may also be other events that the rvt layers needs to know
	 * about this is not an exhaustive list. Some events though rvt does not
	 * need to rely on the driver for such as completion queue error.
	 */
	 int rvt_signal_supported;

	/*
	 * Anything driver specific that is not covered by props
	 * For instance special module parameters. Goes here.
	 */
};

/* Protection domain */
struct rvt_pd {
	struct ib_pd ibpd;
	int user;               /* non-zero if created from user space */
};

struct rvt_dev_info {
	/*
	 * Prior to calling for registration the driver will be responsible for
	 * allocating space for this structure.
	 *
	 * The driver will also be responsible for filling in certain members of
	 * dparms.props
	 */
	struct ib_device ibdev;

	/* Driver specific properties */
	struct rvt_driver_params dparms;

	/* PKey Table goes here */

	/*
	 * The work to create port files in /sys/class Infiniband is different
	 * depending on the driver. This should not be extracted away and
	 * instead drivers are responsible for setting the correct callback for
	 * this.
	 */
	int (*port_callback)(struct ib_device *, u8, struct kobject *);

	/* Internal use */
	int n_pds_allocated;
	spinlock_t n_pds_lock; /* Protect pd allocated count */
};

static inline struct rvt_pd *ibpd_to_rvtpd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct rvt_pd, ibpd);
}

static inline struct rvt_dev_info *ib_to_rvt(struct ib_device *ibdev)
{
	return  container_of(ibdev, struct rvt_dev_info, ibdev);
}

int rvt_register_device(struct rvt_dev_info *rvd);
void rvt_unregister_device(struct rvt_dev_info *rvd);

#endif          /* DEF_RDMA_VT_H */
