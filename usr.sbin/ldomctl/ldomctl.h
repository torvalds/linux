/*	$OpenBSD: ldomctl.h,v 1.16 2022/10/06 21:35:52 kn Exp $	*/

/*
 * Copyright (c) 2012 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define LDOMCTL_CU	"/usr/bin/cu"

struct core;
struct guest;

struct console {
	uint64_t ino;
	uint64_t resource_id;
	uint64_t uartbase;

	struct guest *guest;
	struct ldc_endpoint *client_endpoint;
	struct ldc_endpoint *server_endpoint;
	struct md_node *hv_node;
	TAILQ_ENTRY(console) link;
};

struct cpu {
	uint64_t pid, vid, gid, partid;
	uint64_t resource_id;

	struct guest *guest;
	struct md_node *hv_node;
	struct core *core;
	TAILQ_ENTRY(cpu) link;
};

struct device {
	uint64_t gid;
	uint64_t cfghandle;
	uint64_t resource_id;
	uint64_t rcid;

	int virtual;

	uint64_t num_msi_eqs;
	uint64_t num_msis;
	uint64_t msi_ranges[2];
	uint64_t num_vpci;

	uint64_t msi_eqs_per_vpci;
	uint64_t msis_per_vpci;
	uint64_t msi_base;

	struct guest *guest;
	struct md_node *hv_node;
	TAILQ_ENTRY(device) link;
};

struct subdevice {
	const char *path;
	TAILQ_ENTRY(subdevice) link;
};

struct rootcomplex {
	uint64_t num_msi_eqs;
	uint64_t num_msis;
	const void *msi_ranges;
	int num_msi_ranges;
	const void *vdma_ranges;
	int num_vdma_ranges;
	uint64_t cfghandle;
	const char *path;
	TAILQ_ENTRY(rootcomplex) link;
};

struct mblock {
	uint64_t membase;
	uint64_t memsize;
	uint64_t realbase;
	uint64_t resource_id;

	struct guest *guest;
	struct md_node *hv_node;
	TAILQ_ENTRY(mblock) link;
};

struct ldc_endpoint {
	uint64_t target_type;
	uint64_t target_guest;
	uint64_t channel;
	uint64_t target_channel;
	uint64_t tx_ino;
	uint64_t rx_ino;
	uint64_t resource_id;
	uint64_t private_svc;
	uint64_t svc_id;

	struct guest *guest;
	struct md_node *hv_node;
	TAILQ_ENTRY(ldc_endpoint) link;
};

struct ldc_channel {
	struct ldc_endpoint *client_endpoint;
	struct ldc_endpoint *server_endpoint;
};

struct guest {
	const char *name;
	uint64_t gid;
	uint64_t pid;
	uint64_t resource_id;
	uint64_t tod_offset;
	uint64_t perfctraccess;
	uint64_t perfctrhtaccess;
	uint64_t rngctlaccessible;
	uint64_t mdpa;

	struct md_node *hv_node;

	struct md *md;
	uint64_t cpu_vid;
	uint64_t endpoint_id;

	struct console *console;
	TAILQ_HEAD(, cpu) cpu_list;
	int num_cpus;
	TAILQ_HEAD(, device) device_list;
	TAILQ_HEAD(, subdevice) subdevice_list;
	TAILQ_HEAD(, mblock) mblock_list;
	TAILQ_HEAD(, ldc_endpoint) endpoint_list;

	struct ldc_channel domain_services;
	struct ldc_channel vio[8];
	int num_vios;

	TAILQ_ENTRY(guest) link;
};

extern TAILQ_HEAD(guest_head, guest) guest_list;

void add_guest(struct md_node *);

extern struct md *hvmd;
extern uint64_t hv_mdpa;
extern uint64_t hv_membase;
extern uint64_t hv_memsize;

struct vdisk {
	SIMPLEQ_ENTRY(vdisk)	entry;
	const char		*path;
	const char		*devalias;
};

struct vnet {
	SIMPLEQ_ENTRY(vnet)	entry;
	uint64_t		mac_addr;
	uint64_t		mtu;
	const char		*devalias;
};

struct var {
	SIMPLEQ_ENTRY(var)	entry;
	const char		*name;
	const char		*str;
};

struct iodev {
	SIMPLEQ_ENTRY(iodev)	entry;
	const char		*dev;
};

struct domain {
	SIMPLEQ_ENTRY(domain)	entry;
	const char *name;
	uint64_t vcpu, vcpu_stride;
	uint64_t memory;
	SIMPLEQ_HEAD(, vdisk) vdisk_list;
	SIMPLEQ_HEAD(, vnet) vnet_list;
	SIMPLEQ_HEAD(, var) var_list;
	SIMPLEQ_HEAD(, iodev) iodev_list;
};

struct ldom_config {
	SIMPLEQ_HEAD(, domain) domain_list;
};

int parse_config(const char *, struct ldom_config *);
void build_config(const char *, int);
void list_components(void);
