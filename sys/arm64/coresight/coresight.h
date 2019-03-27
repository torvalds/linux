/*-
 * Copyright (c) 2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 *
 * $FreeBSD$
 */

#ifndef	_ARM64_CORESIGHT_CORESIGHT_H_
#define	_ARM64_CORESIGHT_CORESIGHT_H_

#include <dev/ofw/openfirm.h>

#define	CORESIGHT_ITCTRL	0xf00
#define	CORESIGHT_CLAIMSET	0xfa0
#define	CORESIGHT_CLAIMCLR	0xfa4
#define	CORESIGHT_LAR		0xfb0
#define	 CORESIGHT_UNLOCK	0xc5acce55
#define	CORESIGHT_LSR		0xfb4
#define	CORESIGHT_AUTHSTATUS	0xfb8
#define	CORESIGHT_DEVID		0xfc8
#define	CORESIGHT_DEVTYPE	0xfcc

enum cs_dev_type {
	CORESIGHT_ETMV4,
	CORESIGHT_TMC,
	CORESIGHT_DYNAMIC_REPLICATOR,
	CORESIGHT_FUNNEL,
	CORESIGHT_CPU_DEBUG,
};

struct coresight_device {
	TAILQ_ENTRY(coresight_device) link;
	device_t dev;
	phandle_t node;
	enum cs_dev_type dev_type;
	struct coresight_platform_data *pdata;
};

struct endpoint {
	TAILQ_ENTRY(endpoint) link;
	phandle_t my_node;
	phandle_t their_node;
	phandle_t dev_node;
	boolean_t slave;
	int reg;
	struct coresight_device *cs_dev;
	LIST_ENTRY(endpoint) endplink;
};

struct coresight_platform_data {
	int cpu;
	int in_ports;
	int out_ports;
	struct mtx mtx_lock;
	TAILQ_HEAD(endpoint_list, endpoint) endpoints;
};

struct coresight_desc {
	struct coresight_platform_data *pdata;
	device_t dev;
	enum cs_dev_type dev_type;
};

TAILQ_HEAD(coresight_device_list, coresight_device);

#define	ETM_N_COMPRATOR		16

struct etm_state {
	uint32_t trace_id;
};

struct etr_state {
	boolean_t started;
	uint32_t cycle;
	uint32_t offset;
	uint32_t low;
	uint32_t high;
	uint32_t bufsize;
	uint32_t flags;
#define	ETR_FLAG_ALLOCATE	(1 << 0)
#define	ETR_FLAG_RELEASE	(1 << 1)
};

struct coresight_event {
	LIST_HEAD(, endpoint) endplist;

	uint64_t addr[ETM_N_COMPRATOR];
	uint32_t naddr;
	uint8_t excp_level;
	enum cs_dev_type src;
	enum cs_dev_type sink;

	struct etr_state etr;
	struct etm_state etm;
};

struct etm_config {
	uint64_t addr[ETM_N_COMPRATOR];
	uint32_t naddr;
	uint8_t excp_level;
};

struct coresight_platform_data * coresight_get_platform_data(device_t dev);
struct endpoint * coresight_get_output_endpoint(struct coresight_platform_data *pdata);
struct coresight_device * coresight_get_output_device(struct endpoint *endp, struct endpoint **);
int coresight_register(struct coresight_desc *desc);
int coresight_init_event(int cpu, struct coresight_event *event);
void coresight_enable(int cpu, struct coresight_event *event);
void coresight_disable(int cpu, struct coresight_event *event);
void coresight_read(int cpu, struct coresight_event *event);

#endif /* !_ARM64_CORESIGHT_CORESIGHT_H_ */
