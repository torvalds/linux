/*	$OpenBSD: config.c,v 1.43 2022/10/06 21:35:52 kn Exp $	*/

/*
 * Copyright (c) 2012, 2018 Mark Kettenis
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

#include <sys/types.h>
#include <sys/queue.h>
#include <assert.h>
#include <err.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mdesc.h"
#include "ldomctl.h"
#include "ldom_util.h"

#define LDC_GUEST	0
#define LDC_HV		1
#define LDC_SP		2

#define LDC_HVCTL_SVC	1
#define LDC_CONSOLE_SVC	2

#define MAX_STRANDS_PER_CORE	16

struct core {
	struct guest *guests[MAX_STRANDS_PER_CORE];
	TAILQ_ENTRY(core) link;
};

TAILQ_HEAD(, core) cores;

struct component {
	const char *path;
	const char *nac;
	int assigned;

	struct md_node *hv_node;
	TAILQ_ENTRY(component) link;
};

TAILQ_HEAD(, component) components;

struct hostbridge {
	const char *path;

	uint64_t num_msi_eqs;
	uint64_t num_msis;
	uint64_t max_vpcis;
	TAILQ_ENTRY(hostbridge) link;
};

TAILQ_HEAD(, hostbridge) hostbridges;

struct frag {
	TAILQ_ENTRY(frag) link;
	uint64_t base;
};

struct guest **guests;
struct console **consoles;
struct cpu **cpus;
struct device **pcie_busses;
struct device **network_devices;
struct mblock **mblocks;
struct ldc_endpoint **ldc_endpoints;
extern struct domain *domain;

TAILQ_HEAD(, rootcomplex) rootcomplexes;

uint64_t max_cpus;
bool have_cwqs;
bool have_rngs;

uint64_t max_guests;
uint64_t max_hv_ldcs;
uint64_t max_guest_ldcs;
uint64_t md_maxsize;
uint64_t md_elbow_room;
uint64_t max_mblocks;
uint64_t directio_capability;

uint64_t max_devices = 16;

uint64_t rombase;
uint64_t romsize;
uint64_t uartbase;

uint64_t max_page_size;

uint64_t content_version;
uint64_t stick_frequency;
uint64_t tod_frequency;
uint64_t tod;
uint64_t erpt_pa;
uint64_t erpt_size;

struct md *pri;
struct md *hvmd;
struct md *protomd;

struct guest *guest_lookup(const char *);
void guest_prune_phys_io(struct guest *);
void guest_prune_pcie(struct guest *, struct md_node *, const char *);
void guest_add_vpcie(struct guest *, uint64_t);
void guest_fixup_phys_io(struct guest *);

TAILQ_HEAD(, frag) free_frags = TAILQ_HEAD_INITIALIZER(free_frags);
TAILQ_HEAD(, cpu) free_cpus = TAILQ_HEAD_INITIALIZER(free_cpus);
int total_cpus;
TAILQ_HEAD(, mblock) free_memory = TAILQ_HEAD_INITIALIZER(free_memory);
uint64_t total_memory;

struct cpu *
pri_find_cpu(uint64_t pid)
{
	struct cpu *cpu = NULL;

	TAILQ_FOREACH(cpu, &free_cpus, link) {
		if (cpu->pid == pid)
			break;
	}

	return cpu;
}

void
pri_link_core(struct md *md, struct md_node *node, struct core *core)
{
	struct md_node *node2;
	struct md_prop *prop;
	struct cpu *cpu;
	uint64_t pid;

	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "back") == 0) {
			node2 = prop->d.arc.node;
			if (strcmp(node2->name->str, "cpu") != 0) {
				pri_link_core(md, node2, core);
				continue;
			}

			pid = -1;
			if (!md_get_prop_val(md, node2, "pid", &pid))
				md_get_prop_val(md, node2, "id", &pid);

			cpu = pri_find_cpu(pid);
			if (cpu == NULL)
				errx(1, "couldn't determine core for VCPU %lld\n", pid);
			cpu->core = core;
		}
	}
}

void
pri_add_core(struct md *md, struct md_node *node)
{
	struct core *core;

	core = xzalloc(sizeof(*core));
	TAILQ_INSERT_TAIL(&cores, core, link);

	pri_link_core(md, node, core);
}

void
pri_init_cores(struct md *md)
{
	struct md_node *node;
	const void *type;
	size_t len;

	TAILQ_INIT(&cores);

	TAILQ_FOREACH(node, &md->node_list, link) {
		if (strcmp(node->name->str, "tlb") == 0 &&
		    md_get_prop_data(md, node, "type", &type, &len) &&
		    strcmp(type, "data") == 0) {
			pri_add_core(md, node);
		}
	}
}

void
pri_add_hostbridge(struct md *md, struct md_node *node)
{
	struct hostbridge *hostbridge;

	hostbridge = xzalloc(sizeof(*hostbridge));
	md_get_prop_str(md, node, "path", &hostbridge->path);
	md_get_prop_val(md, node, "#msi-eqs", &hostbridge->num_msi_eqs);
	md_get_prop_val(md, node, "#msi", &hostbridge->num_msis);
	if (!md_get_prop_val(md, node, "#max-vpcis", &hostbridge->max_vpcis))
		hostbridge->max_vpcis = 10;
	TAILQ_INSERT_TAIL(&hostbridges, hostbridge, link);
}

void
pri_init_components(struct md *md)
{
	struct component *component;
	struct md_node *node;
	const char *path;
	const char *nac;
	const char *type;

	TAILQ_INIT(&components);
	TAILQ_INIT(&hostbridges);

	TAILQ_FOREACH(node, &md->node_list, link) {
		if (strcmp(node->name->str, "component") != 0)
			continue;

		if (md_get_prop_str(md, node, "assignable-path", &path)) {
			component = xzalloc(sizeof(*component));
			component->path = path;
			if (md_get_prop_str(md, node, "nac", &nac))
				component->nac = nac;
			else
				component->nac = "-";
			TAILQ_INSERT_TAIL(&components, component, link);
		}

		if (md_get_prop_str(md, node, "type", &type) &&
		    strcmp(type, "hostbridge") == 0)
			pri_add_hostbridge(md, node);
	}
}

void
pri_init_phys_io(struct md *md)
{
	struct md_node *node;
	const char *device_type;
	uint64_t cfg_handle;
	struct rootcomplex *rootcomplex;
	char *path;
	size_t len;

	TAILQ_INIT(&rootcomplexes);

	TAILQ_FOREACH(node, &md->node_list, link) {
		if (strcmp(node->name->str, "iodevice") == 0 &&
		    md_get_prop_str(md, node, "device-type", &device_type) &&
		    strcmp(device_type, "pciex") == 0) {
			if (!md_get_prop_val(md, node, "cfg-handle",
					     &cfg_handle))
				continue;

			rootcomplex = xzalloc(sizeof(*rootcomplex));
			md_get_prop_val(md, node, "#msi-eqs",
			    &rootcomplex->num_msi_eqs);
			md_get_prop_val(md, node, "#msi",
			    &rootcomplex->num_msis);
			md_get_prop_data(md, node, "msi-ranges",
			    &rootcomplex->msi_ranges, &len);
			rootcomplex->num_msi_ranges =
			    len / (2 * sizeof(uint64_t));
			md_get_prop_data(md, node, "virtual-dma",
			    &rootcomplex->vdma_ranges, &len);
			rootcomplex->num_vdma_ranges =
			    len / (2 * sizeof(uint64_t));
			rootcomplex->cfghandle = cfg_handle;
			xasprintf(&path, "/@%llx", cfg_handle);
			rootcomplex->path = path;
			TAILQ_INSERT_TAIL(&rootcomplexes, rootcomplex, link);
		}
	}
}

void
pri_add_cpu(struct md *md, struct md_node *node)
{
	struct cpu *cpu;
	uint64_t mmu_page_size_list;
	uint64_t page_size;

	cpu = xzalloc(sizeof(*cpu));
	/*
	 * Only UltraSPARC T1 CPUs have a "pid" property.  All other
	 * just have a "id" property that can be used as the physical ID.
	 */
	if (!md_get_prop_val(md, node, "pid", &cpu->pid))
		md_get_prop_val(md, node, "id", &cpu->pid);
	cpu->vid = -1;
	cpu->gid = -1;
	cpu->partid = -1;
	cpu->resource_id = -1;
	TAILQ_INSERT_TAIL(&free_cpus, cpu, link);
	total_cpus++;

	mmu_page_size_list = 0x9;
	md_get_prop_val(md, node, "mmu-page-size-list", &mmu_page_size_list);

	page_size = 1024;
	while (mmu_page_size_list) {
		page_size *= 8;
		mmu_page_size_list >>= 1;
	}

	if (page_size > max_page_size)
		max_page_size = page_size;
}

struct cpu *
pri_alloc_cpu(uint64_t pid)
{
	struct cpu *cpu;

	if (pid == -1 && !TAILQ_EMPTY(&free_cpus)) {
		cpu = TAILQ_FIRST(&free_cpus);
		TAILQ_REMOVE(&free_cpus, cpu, link);
		return cpu;
	}

	TAILQ_FOREACH(cpu, &free_cpus, link) {
		if (cpu->pid == pid) {
			TAILQ_REMOVE(&free_cpus, cpu, link);
			return cpu;
		}
	}

	return NULL;
}

void
pri_free_cpu(struct cpu *cpu)
{
	TAILQ_INSERT_TAIL(&free_cpus, cpu, link);
}

void
pri_add_mblock(struct md *md, struct md_node *node)
{
	struct mblock *mblock;

	mblock = xzalloc(sizeof(*mblock));
	md_get_prop_val(md, node, "base", &mblock->membase);
	md_get_prop_val(md, node, "size", &mblock->memsize);
	mblock->resource_id = -1;
	TAILQ_INSERT_TAIL(&free_memory, mblock, link);
	total_memory += mblock->memsize;
}

struct mblock *
pri_alloc_memory(uint64_t base, uint64_t size)
{
	struct mblock *mblock, *new_mblock;
	uint64_t memend;

	if (base == -1 && !TAILQ_EMPTY(&free_memory)) {
		mblock = TAILQ_FIRST(&free_memory);
		base = mblock->membase;
	}

	TAILQ_FOREACH(mblock, &free_memory, link) {
		if (base >= mblock->membase &&
		    base < mblock->membase + mblock->memsize) {
			if (base > mblock->membase) {
				new_mblock = xzalloc(sizeof(*new_mblock));
				new_mblock->membase = mblock->membase;
				new_mblock->memsize = base - mblock->membase;
				new_mblock->resource_id = -1;
				TAILQ_INSERT_BEFORE(mblock, new_mblock, link);
			}

			memend = mblock->membase + mblock->memsize;
			mblock->membase = base + size;
			mblock->memsize = memend - mblock->membase;
			if (mblock->memsize == 0) {
				TAILQ_REMOVE(&free_memory, mblock, link);
				free(mblock);
			}

			total_memory -= size;

			new_mblock = xzalloc(sizeof(*new_mblock));
			new_mblock->membase = base;
			new_mblock->memsize = size;
			new_mblock->resource_id = -1;
			return new_mblock;
		}
	}

	return NULL;
}

void
pri_delete_devalias(struct md *md)
{
	struct md_node *node;

	/*
	 * There may be multiple "devalias" nodes.  Only remove the one
	 * that resides under the "openboot" node.
	 */
	node = md_find_node(protomd, "openboot");
	assert(node);
	node = md_find_subnode(protomd, node, "devalias");
	if (node)
		md_delete_node(protomd, node);
}

void
pri_init(struct md *md)
{
	struct md_node *node, *node2;
	struct md_prop *prop;
	uint64_t base, size;
	uint64_t offset, guest_use;

	node = md_find_node(pri, "platform");
	if (node == NULL)
		errx(1, "platform node not found");

	md_get_prop_val(md, node, "max-cpus", &max_cpus);

	node = md_find_node(pri, "firmware");
	if (node == NULL)
		errx(1, "firmware node not found");

	md_get_prop_val(md, node, "max_guests", &max_guests);
	md_get_prop_val(md, node, "max_hv_ldcs", &max_hv_ldcs);
	md_get_prop_val(md, node, "max_guest_ldcs", &max_guest_ldcs);
	md_get_prop_val(md, node, "md_elbow_room", &md_elbow_room);
	md_get_prop_val(md, node, "max_mblocks", &max_mblocks);
	md_get_prop_val(md, node, "directio_capability", &directio_capability);

	node = md_find_node(md, "read_only_memory");
	if (node == NULL)
		errx(1, "read_only_memory node not found");
	if (!md_get_prop_val(md, node, "base", &base))
		errx(1, "missing base property in read_only_memory node");
	if (!md_get_prop_val(md, node, "size", &size))
		errx(1, "missing size property in read_only_memory node");
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0) {
			node2 = prop->d.arc.node;
			if (!md_get_prop_val(md, node2, "guest_use",
			    &guest_use) || guest_use == 0)
				continue;
			if (!md_get_prop_val(md, node2, "offset", &offset) ||
			    !md_get_prop_val(md, node2, "size", &size))
				continue;
			rombase = base + offset;
			romsize = size;
		}
	}
	if (romsize == 0)
		errx(1, "no suitable firmware image found");

	node = md_find_node(md, "platform");
	assert(node);
	md_set_prop_val(md, node, "domaining-enabled", 0x1);

	md_write(md, "pri");

	protomd = md_copy(md);
	md_find_delete_node(protomd, "components");
	md_find_delete_node(protomd, "domain-services");
	md_find_delete_node(protomd, "channel-devices");
	md_find_delete_node(protomd, "channel-endpoints");
	md_find_delete_node(protomd, "firmware");
	md_find_delete_node(protomd, "ldc_endpoints");
	md_find_delete_node(protomd, "memory-segments");
	pri_delete_devalias(protomd);
	md_collect_garbage(protomd);
	md_write(protomd, "protomd");

	guests = xzalloc(max_guests * sizeof(*guests));
	consoles = xzalloc(max_guests * sizeof(*consoles));
	cpus = xzalloc(max_cpus * sizeof(*cpus));
	pcie_busses = xzalloc(max_devices * sizeof(*pcie_busses));
	network_devices = xzalloc(max_devices * sizeof(*network_devices));
	mblocks = xzalloc(max_mblocks * sizeof(*mblocks));
	ldc_endpoints = xzalloc(max_guest_ldcs * sizeof(*ldc_endpoints));

	node = md_find_node(md, "cpus");
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0)
			pri_add_cpu(md, prop->d.arc.node);
	}

	node = md_find_node(md, "memory");
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0)
			pri_add_mblock(md, prop->d.arc.node);
	}

	pri_init_cores(md);
	pri_init_components(md);
	pri_init_phys_io(md);
}

void
hvmd_fixup_guest(struct md *md, struct md_node *guest, struct md_node *node)
{
	struct md_prop *prop;

	TAILQ_FOREACH(prop, &guest->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0) {
			if (prop->d.arc.node == node)
				return;
		}
	}

	md_add_prop_arc(md, guest, "fwd", node);
}

uint64_t fragsize;
TAILQ_HEAD(, mblock) frag_mblocks;
struct mblock *hvmd_mblock;

void
hvmd_init_frag(struct md *md, struct md_node *node)
{
	struct frag *frag;
	struct mblock *mblock;
	uint64_t base, size;

	md_get_prop_val(md, node, "base", &base);
	md_get_prop_val(md, node, "size", &size);

	pri_alloc_memory(base, size);

	mblock = xzalloc(sizeof(*mblock));
	mblock->membase = base;
	mblock->memsize = size;
	TAILQ_INSERT_TAIL(&frag_mblocks, mblock, link);

	while (size > fragsize) {
		frag = xmalloc(sizeof(*frag));
		frag->base = base;
		TAILQ_INSERT_TAIL(&free_frags, frag, link);
		base += fragsize;
		size -= fragsize;
	}
}

uint64_t
hvmd_alloc_frag(uint64_t base)
{
	struct frag *frag = TAILQ_FIRST(&free_frags);

	if (base != -1) {
		TAILQ_FOREACH(frag, &free_frags, link) {
			if (frag->base == base)
				break;
		}
	}

	if (frag == NULL)
		return -1;

	TAILQ_REMOVE(&free_frags, frag, link);
	base = frag->base;
	free(frag);

	return base;
}

void
hvmd_free_frag(uint64_t base)
{
	struct frag *frag;

	frag = xmalloc(sizeof(*frag));
	frag->base = base;
	TAILQ_INSERT_TAIL(&free_frags, frag, link);
}

void
hvmd_init_mblock(struct md *md, struct md_node *node)
{
	struct mblock *mblock;
	uint64_t resource_id;
	struct md_node *node2;
	struct md_prop *prop;

	if (!md_get_prop_val(md, node, "resource_id", &resource_id))
		errx(1, "missing resource_id property in mblock node");

	if (resource_id >= max_mblocks)
		errx(1, "resource_id larger than max_mblocks");

	mblock = xzalloc(sizeof(*mblock));
	md_get_prop_val(md, node, "membase", &mblock->membase);
	md_get_prop_val(md, node, "memsize", &mblock->memsize);
	md_get_prop_val(md, node, "realbase", &mblock->realbase);
	mblock->resource_id = resource_id;
	mblocks[resource_id] = mblock;
	mblock->hv_node = node;

	/* Fixup missing links. */
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "back") == 0) {
			node2 = prop->d.arc.node;
			if (strcmp(node2->name->str, "guest") == 0)
				hvmd_fixup_guest(md, node2, node);
		}
	}
}

void
hvmd_init_console(struct md *md, struct md_node *node)
{
	struct console *console;
	uint64_t resource_id;

	if (!md_get_prop_val(md, node, "resource_id", &resource_id))
		errx(1, "missing resource_id property in console node");

	if (resource_id >= max_guests)
		errx(1, "resource_id larger than max_guests");

	console = xzalloc(sizeof(*console));
	md_get_prop_val(md, node, "ino", &console->ino);
	md_get_prop_val(md, node, "uartbase", &console->uartbase);
	console->resource_id = resource_id;
	consoles[resource_id] = console;
	console->hv_node = node;
}

void
hvmd_init_cpu(struct md *md, struct md_node *node)
{
	struct cpu *cpu;
	uint64_t pid;
	uint64_t resource_id;
	struct md_node *node2;
	struct md_prop *prop;

	if (!md_get_prop_val(md, node, "resource_id", &resource_id))
		errx(1, "missing resource_id property in cpu node");

	if (resource_id >= max_cpus)
		errx(1, "resource_id larger than max-cpus");

	if (!md_get_prop_val(md, node, "pid", &pid))
		errx(1, "missing pid property in cpu node");

	cpu = pri_alloc_cpu(pid);
	md_get_prop_val(md, node, "vid", &cpu->vid);
	if (!md_get_prop_val(md, node, "gid", &cpu->gid))
		cpu->gid = 0;
	md_get_prop_val(md, node, "partid", &cpu->partid);
	cpu->resource_id = resource_id;
	cpus[resource_id] = cpu;
	cpu->hv_node = node;

	/* Fixup missing links. */
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "back") == 0) {
			node2 = prop->d.arc.node;
			if (strcmp(node2->name->str, "guest") == 0)
				hvmd_fixup_guest(md, node2, node);
		}
	}
}

void
hvmd_init_device(struct md *md, struct md_node *node)
{
	struct hostbridge *hostbridge;
	struct device *device;
	uint64_t resource_id;
	struct md_node *node2;
	struct md_prop *prop;
	char *path;

	if (strcmp(node->name->str, "pcie_bus") != 0 &&
	    strcmp(node->name->str, "network_device") != 0)
		return;

	if (!md_get_prop_val(md, node, "resource_id", &resource_id))
		errx(1, "missing resource_id property in ldc_endpoint node");

	if (resource_id >= max_devices)
		errx(1, "resource_id larger than max_devices");

	device = xzalloc(sizeof(*device));
	md_get_prop_val(md, node, "gid", &device->gid);
	md_get_prop_val(md, node, "cfghandle", &device->cfghandle);
	md_get_prop_val(md, node, "rcid", &device->rcid);
	device->resource_id = resource_id;
	if (strcmp(node->name->str, "pcie_bus") == 0)
		pcie_busses[resource_id] = device;
	else
		network_devices[resource_id] = device;
	device->hv_node = node;

	/* Fixup missing links. */
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "back") == 0) {
			node2 = prop->d.arc.node;
			if (strcmp(node2->name->str, "guest") == 0)
				hvmd_fixup_guest(md, node2, node);
		}
	}

	xasprintf(&path, "/@%llx", device->cfghandle);
	TAILQ_FOREACH(hostbridge, &hostbridges, link) {
		if (strcmp(hostbridge->path, path) == 0)
			break;
	}
	free(path);
	if (hostbridge == NULL)
		return;

	device->msi_eqs_per_vpci =
	    hostbridge->num_msi_eqs / hostbridge->max_vpcis;
	device->msis_per_vpci =
	    hostbridge->num_msis / hostbridge->max_vpcis;
	device->msi_base = hostbridge->num_msis;

	device->num_msi_eqs = device->msi_eqs_per_vpci +
	    hostbridge->num_msi_eqs % hostbridge->max_vpcis;
	device->num_msis = device->msis_per_vpci +
	    hostbridge->num_msis % hostbridge->max_vpcis;
	device->msi_ranges[0] = 0;
	device->msi_ranges[1] = device->num_msis;
}

void
hvmd_init_endpoint(struct md *md, struct md_node *node)
{
	struct ldc_endpoint *endpoint;
	uint64_t resource_id;

	if (!md_get_prop_val(md, node, "resource_id", &resource_id))
		errx(1, "missing resource_id property in ldc_endpoint node");

	if (resource_id >= max_guest_ldcs)
		errx(1, "resource_id larger than max_guest_ldcs");

	if (ldc_endpoints[resource_id]) {
		/*
		 * Some machine descriptions seem to have duplicate
		 * arcs.  Fortunately, these can be easily detected
		 * and ignored.
		 */
		if (ldc_endpoints[resource_id]->hv_node == node)
			return;
		errx(1, "duplicate resource_id");
	}

	endpoint = xzalloc(sizeof(*endpoint));
	endpoint->target_guest = -1;
	endpoint->tx_ino = -1;
	endpoint->rx_ino = -1;
	endpoint->private_svc = -1;
	endpoint->svc_id = -1;
	md_get_prop_val(md, node, "target_type", &endpoint->target_type);
	md_get_prop_val(md, node, "target_guest", &endpoint->target_guest);
	md_get_prop_val(md, node, "channel", &endpoint->channel);
	md_get_prop_val(md, node, "target_channel", &endpoint->target_channel);
	md_get_prop_val(md, node, "tx-ino", &endpoint->tx_ino);
	md_get_prop_val(md, node, "rx-ino", &endpoint->rx_ino);
	md_get_prop_val(md, node, "private_svc", &endpoint->private_svc);
	md_get_prop_val(md, node, "svc_id", &endpoint->svc_id);
	endpoint->resource_id = resource_id;
	ldc_endpoints[resource_id] = endpoint;
	endpoint->hv_node = node;
}

void
hvmd_init_guest(struct md *md, struct md_node *node)
{
	struct guest *guest;
	struct md_node *node2;
	struct md_prop *prop;
	uint64_t resource_id;
	struct ldc_endpoint *endpoint;
	char *path;

	if (!md_get_prop_val(md, node, "resource_id", &resource_id))
		errx(1, "missing resource_id property in guest node");

	if (resource_id >= max_guests)
		errx(1, "resource_id larger than max_guests");

	guest = xzalloc(sizeof(*guest));
	TAILQ_INIT(&guest->cpu_list);
	TAILQ_INIT(&guest->device_list);
	TAILQ_INIT(&guest->subdevice_list);
	TAILQ_INIT(&guest->mblock_list);
	TAILQ_INIT(&guest->endpoint_list);
	md_get_prop_str(md, node, "name", &guest->name);
	md_get_prop_val(md, node, "gid", &guest->gid);
	md_get_prop_val(md, node, "pid", &guest->pid);
	md_get_prop_val(md, node, "tod-offset", &guest->tod_offset);
	md_get_prop_val(md, node, "perfctraccess", &guest->perfctraccess);
	md_get_prop_val(md, node, "perfctrhtaccess", &guest->perfctrhtaccess);
	md_get_prop_val(md, node, "rngctlaccessible", &guest->rngctlaccessible);
	md_get_prop_val(md, node, "mdpa", &guest->mdpa);
	guest->resource_id = resource_id;
	guests[resource_id] = guest;
	guest->hv_node = node;

	if (strcmp(guest->name, "primary") == 0 && guest->gid != 0)
		errx(1, "gid of primary domain isn't 0");

	hvmd_alloc_frag(guest->mdpa);

	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0) {
			node2 = prop->d.arc.node;
			if (strcmp(node2->name->str, "console") == 0) {
				md_get_prop_val(md, node2, "resource_id",
				    &resource_id);
				guest->console = consoles[resource_id];
				consoles[resource_id]->guest = guest;
			}
			if (strcmp(node2->name->str, "cpu") == 0) {
				md_get_prop_val(md, node2, "resource_id",
				    &resource_id);
				TAILQ_INSERT_TAIL(&guest->cpu_list,
				    cpus[resource_id], link);
				cpus[resource_id]->guest = guest;
			}
			if (strcmp(node2->name->str, "pcie_bus") == 0) {
				md_get_prop_val(md, node2, "resource_id",
				    &resource_id);
				TAILQ_INSERT_TAIL(&guest->device_list,
				    pcie_busses[resource_id], link);
				pcie_busses[resource_id]->guest = guest;
			}
			if (strcmp(node2->name->str, "network_device") == 0) {
				md_get_prop_val(md, node2, "resource_id",
				    &resource_id);
				TAILQ_INSERT_TAIL(&guest->device_list,
				    network_devices[resource_id], link);
				network_devices[resource_id]->guest = guest;
			}
			if (strcmp(node2->name->str, "mblock") == 0) {
				md_get_prop_val(md, node2, "resource_id",
				    &resource_id);
				TAILQ_INSERT_TAIL(&guest->mblock_list,
				    mblocks[resource_id], link);
				mblocks[resource_id]->guest = guest;
			}
			if (strcmp(node2->name->str, "ldc_endpoint") == 0) {
				md_get_prop_val(md, node2, "resource_id",
				    &resource_id);
				TAILQ_INSERT_TAIL(&guest->endpoint_list,
				    ldc_endpoints[resource_id], link);
				ldc_endpoints[resource_id]->guest = guest;
			}
		}
	}

	TAILQ_FOREACH(endpoint, &guest->endpoint_list, link) {
		if (endpoint->channel >= guest->endpoint_id)
			guest->endpoint_id = endpoint->channel + 1;
	}

	xasprintf(&path, "%s.md", guest->name);
	guest->md = md_read(path);

	if (guest->md == NULL)
		err(1, "unable to get guest MD");

	free(path);
}

void
hvmd_init(struct md *md)
{
	struct md_node *node;
	struct md_prop *prop;

	node = md_find_node(md, "root");
	md_get_prop_val(md, node, "content-version", &content_version);
	md_get_prop_val(md, node, "stick-frequency", &stick_frequency);
	md_get_prop_val(md, node, "tod-frequency", &tod_frequency);
	md_get_prop_val(md, node, "tod", &tod);
	md_get_prop_val(md, node, "erpt-pa", &erpt_pa);
	md_get_prop_val(md, node, "erpt-size", &erpt_size);
	md_get_prop_val(md, node, "uartbase", &uartbase);

	node = md_find_node(md, "platform");
	if (node)
		md_get_prop_val(md, node, "stick-frequency", &stick_frequency);

	node = md_find_node(md, "hvmd_mblock");
	if (node) {
		hvmd_mblock = xzalloc(sizeof(*hvmd_mblock));
		md_get_prop_val(md, node, "base", &hvmd_mblock->membase);
		md_get_prop_val(md, node, "size", &hvmd_mblock->memsize);
		md_get_prop_val(md, node, "md_maxsize", &md_maxsize);
		pri_alloc_memory(hvmd_mblock->membase, hvmd_mblock->memsize);
	}

	node = md_find_node(md, "frag_space");
	md_get_prop_val(md, node, "fragsize", &fragsize);
	if (fragsize == 0)
		fragsize = md_maxsize;
	TAILQ_INIT(&frag_mblocks);
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0)
			hvmd_init_frag(md, prop->d.arc.node);
	}
	pri_alloc_memory(0, fragsize);

	node = md_find_node(md, "consoles");
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0)
			hvmd_init_console(md, prop->d.arc.node);
	}

	node = md_find_node(md, "cpus");
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0)
			hvmd_init_cpu(md, prop->d.arc.node);
	}

	have_cwqs = (md_find_node(md, "cwqs") != NULL);
	have_rngs = (md_find_node(md, "rngs") != NULL);

	node = md_find_node(md, "devices");
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0)
			hvmd_init_device(md, prop->d.arc.node);
	}

	node = md_find_node(md, "memory");
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0)
			hvmd_init_mblock(md, prop->d.arc.node);
	}

	node = md_find_node(md, "ldc_endpoints");
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0)
			hvmd_init_endpoint(md, prop->d.arc.node);
	}

	node = md_find_node(md, "guests");
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0)
			hvmd_init_guest(md, prop->d.arc.node);
	}

	hvmd_alloc_frag(-1);
}

void
hvmd_finalize_cpu(struct md *md, struct cpu *cpu)
{
	struct md_node *parent;
	struct md_node *node;
	int i;

	for (i = 0; i < MAX_STRANDS_PER_CORE; i++) {
		if (cpu->core->guests[i] == cpu->guest) {
			cpu->partid = i + 1;
			break;
		}
		if (cpu->core->guests[i] == NULL) {
			cpu->core->guests[i] = cpu->guest;
			cpu->partid = i + 1;
			break;
		}
	}

	parent = md_find_node(md, "cpus");
	assert(parent);

	node = md_add_node(md, "cpu");
	md_link_node(md, parent, node);
	md_add_prop_val(md, node, "pid", cpu->pid);
	md_add_prop_val(md, node, "vid", cpu->vid);
	md_add_prop_val(md, node, "gid", cpu->gid);
	md_add_prop_val(md, node, "partid", cpu->partid);
	md_add_prop_val(md, node, "resource_id", cpu->resource_id);
	cpu->hv_node = node;
}

void
hvmd_finalize_cpus(struct md *md)
{
	struct md_node *parent;
	struct md_node *node;
	uint64_t resource_id;

	parent = md_find_node(md, "root");
	assert(parent);

	node = md_add_node(md, "cpus");
	md_link_node(md, parent, node);

	for (resource_id = 0; resource_id < max_cpus; resource_id++) {
		if (cpus[resource_id])
			hvmd_finalize_cpu(md, cpus[resource_id]);
	}
}

void
hvmd_finalize_maus(struct md *md)
{
	struct md_node *parent;
	struct md_node *node;
	struct md_node *child;
	int i;

	parent = md_find_node(md, "root");
	assert(parent);

	node = md_add_node(md, "maus");
	md_link_node(md, parent, node);

	if (have_cwqs) {
		node = md_add_node(md, "cwqs");
		md_link_node(md, parent, node);
	}

	if (have_rngs) {
		node = md_add_node(md, "rngs");
		md_link_node(md, parent, node);
		child = md_add_node(md, "rng");
		md_link_node(md, node, child);
		for (i = 0; i < max_cpus; i++) {
			if (cpus[i])
				md_link_node(md, cpus[i]->hv_node, child);
		}
	}
}

void
hvmd_finalize_device(struct md *md, struct device *device, const char *name)
{
	struct md_node *parent;
	struct md_node *node;

	parent = md_find_node(md, "devices");
	assert(parent);

	node = md_add_node(md, name);
	md_link_node(md, parent, node);
	md_add_prop_val(md, node, "resource_id", device->resource_id);
	md_add_prop_val(md, node, "cfghandle", device->cfghandle);
	md_add_prop_val(md, node, "gid", device->gid);
	md_add_prop_val(md, node, "rcid", device->rcid);
	device->hv_node = node;
}

void
hvmd_finalize_pcie_device(struct md *md, struct device *device)
{
	struct rootcomplex *rootcomplex;
	struct md_node *node, *child, *parent;
	struct component *component;
	struct subdevice *subdevice;
	uint64_t resource_id = 0;
	char *path;

	hvmd_finalize_device(md, device,
	    device->virtual ? "virtual_pcie_bus" : "pcie_bus");
	node = device->hv_node;

	if (!directio_capability)
		return;

	TAILQ_FOREACH(rootcomplex, &rootcomplexes, link) {
		if (rootcomplex->cfghandle == device->cfghandle)
			break;
	}
	if (rootcomplex == NULL)
		return;

	md_add_prop_val(md, node, "allow_bypass", 0);

	md_add_prop_val(md, node, "#msi-eqs", device->num_msi_eqs);
	md_add_prop_val(md, node, "#msi", device->num_msis);
	md_add_prop_data(md, node, "msi-ranges", (void *)device->msi_ranges,
	    sizeof(device->msi_ranges));
	md_add_prop_data(md, node, "virtual-dma", rootcomplex->vdma_ranges,
	    rootcomplex->num_vdma_ranges * 2 * sizeof(uint64_t));

	xasprintf(&path, "/@%llx", device->cfghandle);

	if (!device->virtual) {
		parent = md_add_node(md, "pcie_assignable_devices");
		md_link_node(md, node, parent);

		TAILQ_FOREACH(component, &components, link) {
			const char *path2 = component->path;

			if (strncmp(path, path2, strlen(path)) != 0)
				continue;

			path2 = strchr(path2, '/');
			if (path2 == NULL || *path2++ == 0)
				continue;
			path2 = strchr(path2, '/');
			if (path2 == NULL || *path2++ == 0)
				continue;

			child = md_add_node(md, "pcie_device");
			md_link_node(md, parent, child);

			md_add_prop_str(md, child, "path", path2);
			md_add_prop_val(md, child, "resource_id", resource_id);
			resource_id++;

			component->hv_node = child;
		}
	}

	parent = md_add_node(md, "pcie_assigned_devices");
	md_link_node(md, node, parent);

	TAILQ_FOREACH(subdevice, &device->guest->subdevice_list, link) {
		if (strncmp(path, subdevice->path, strlen(path)) != 0)
			continue;
		TAILQ_FOREACH(component, &components, link) {
			if (strcmp(subdevice->path, component->path) == 0)
				md_link_node(md, parent, component->hv_node);
		}
	}

	free(path);
}

void
hvmd_finalize_devices(struct md *md)
{
	struct md_node *parent;
	struct md_node *node;
	uint64_t resource_id;

	parent = md_find_node(md, "root");
	assert(parent);

	node = md_add_node(md, "devices");
	md_link_node(md, parent, node);

	for (resource_id = 0; resource_id < max_devices; resource_id++) {
		if (pcie_busses[resource_id])
			hvmd_finalize_pcie_device(md, pcie_busses[resource_id]);
	}
	for (resource_id = 0; resource_id < max_devices; resource_id++) {
		if (network_devices[resource_id])
			hvmd_finalize_device(md, network_devices[resource_id],
			    "network_device");
	}
}

void
hvmd_finalize_mblock(struct md *md, struct mblock *mblock)
{
	struct md_node *parent;
	struct md_node *node;

	parent = md_find_node(md, "memory");
	assert(parent);

	node = md_add_node(md, "mblock");
	md_link_node(md, parent, node);
	md_add_prop_val(md, node, "membase", mblock->membase);
	md_add_prop_val(md, node, "memsize", mblock->memsize);
	md_add_prop_val(md, node, "realbase", mblock->realbase);
	md_add_prop_val(md, node, "resource_id", mblock->resource_id);
	mblock->hv_node = node;
}

void
hvmd_finalize_memory(struct md *md)
{
	struct md_node *parent;
	struct md_node *node;
	uint64_t resource_id;

	parent = md_find_node(md, "root");
	assert(parent);

	node = md_add_node(md, "memory");
	md_link_node(md, parent, node);

	for (resource_id = 0; resource_id < max_mblocks; resource_id++) {
		if (mblocks[resource_id])
			hvmd_finalize_mblock(md, mblocks[resource_id]);
	}
}

void
hvmd_finalize_endpoint(struct md *md, struct ldc_endpoint *endpoint)
{
	struct md_node *parent;
	struct md_node *node;

	parent = md_find_node(md, "ldc_endpoints");
	assert(parent);

	node = md_add_node(md, "ldc_endpoint");
	md_link_node(md, parent, node);
	md_add_prop_val(md, node, "resource_id", endpoint->resource_id);
	md_add_prop_val(md, node, "target_type", endpoint->target_type);
	md_add_prop_val(md, node, "channel", endpoint->channel);
	if (endpoint->target_guest != -1)
		md_add_prop_val(md, node, "target_guest",
		    endpoint->target_guest);
	md_add_prop_val(md, node, "target_channel", endpoint->target_channel);
	if (endpoint->tx_ino != -1)
		md_add_prop_val(md, node, "tx-ino", endpoint->tx_ino);
	if (endpoint->rx_ino != -1)
		md_add_prop_val(md, node, "rx-ino", endpoint->rx_ino);
	if (endpoint->private_svc != -1)
		md_add_prop_val(md, node, "private_svc",
		    endpoint->private_svc);
	if (endpoint->svc_id != -1)
		md_add_prop_val(md, node, "svc_id", endpoint->svc_id);
	endpoint->hv_node = node;
}

void
hvmd_finalize_endpoints(struct md *md)
{
	struct md_node *parent;
	struct md_node *node;
	uint64_t resource_id;

	parent = md_find_node(md, "root");
	assert(parent);

	node = md_add_node(md, "ldc_endpoints");
	md_link_node(md, parent, node);

	for (resource_id = 0; resource_id < max_guest_ldcs; resource_id++) {
		if (ldc_endpoints[resource_id])
			hvmd_finalize_endpoint(md, ldc_endpoints[resource_id]);
	}
}

void
hvmd_finalize_console(struct md *md, struct console *console)
{
	struct md_node *parent;
	struct md_node *node;
	struct ldc_endpoint *endpoint;

	parent = md_find_node(md, "consoles");
	assert(parent);

	node = md_add_node(md, "console");
	md_link_node(md, parent, node);
	md_add_prop_val(md, node, "resource_id", console->resource_id);
	md_add_prop_val(md, node, "ino", console->ino);
	console->hv_node = node;

	if (console->uartbase) {
		md_add_prop_val(md, node, "uartbase", console->uartbase);
		return;
	}

	TAILQ_FOREACH(endpoint, &console->guest->endpoint_list, link) {
		if (endpoint->rx_ino == console->ino) {
			md_link_node(md, node, endpoint->hv_node);
			break;
		}
	}
}

void
hvmd_finalize_consoles(struct md *md)
{
	struct md_node *parent;
	struct md_node *node;
	uint64_t resource_id;

	parent = md_find_node(md, "root");
	assert(parent);

	node = md_add_node(md, "consoles");
	md_link_node(md, parent, node);

	for (resource_id = 0; resource_id < max_guests; resource_id++) {
		if (consoles[resource_id])
			hvmd_finalize_console(md, consoles[resource_id]);
	}
}

void
hvmd_finalize_guest(struct md *md, struct guest *guest)
{
	struct md_node *node;
	struct md_node *parent;
	struct cpu *cpu;
	struct device *device;
	struct mblock *mblock;
	struct ldc_endpoint *endpoint;

	parent = md_find_node(md, "guests");
	assert(parent);

	node = md_add_node(md, "guest");
	md_link_node(md, parent, node);
	md_add_prop_str(md, node, "name", guest->name);
	md_add_prop_val(md, node, "gid", guest->gid);
	md_add_prop_val(md, node, "pid", guest->pid);
	md_add_prop_val(md, node, "resource_id", guest->resource_id);
	md_add_prop_val(md, node, "tod-offset", guest->tod_offset);
	md_add_prop_val(md, node, "reset-reason", 0);
	md_add_prop_val(md, node, "perfctraccess", guest->perfctraccess);
	md_add_prop_val(md, node, "perfctrhtaccess", guest->perfctrhtaccess);
	md_add_prop_val(md, node, "rngctlaccessible", guest->rngctlaccessible);
	md_add_prop_val(md, node, "diagpriv", 0);
	md_add_prop_val(md, node, "mdpa", guest->mdpa);
	md_add_prop_val(md, node, "rombase", rombase);
	md_add_prop_val(md, node, "romsize", romsize);
	md_add_prop_val(md, node, "uartbase", uartbase);
	guest->hv_node = node;

	node = md_add_node(md, "virtual_devices");
	md_link_node(md, guest->hv_node, node);
	md_add_prop_val(md, node, "cfghandle", 0x100);

	node = md_add_node(md, "channel_devices");
	md_link_node(md, guest->hv_node, node);
	md_add_prop_val(md, node, "cfghandle", 0x200);

	if (guest->console)
		md_link_node(md, guest->hv_node, guest->console->hv_node);
	TAILQ_FOREACH(cpu, &guest->cpu_list, link)
		md_link_node(md, guest->hv_node, cpu->hv_node);
	TAILQ_FOREACH(device, &guest->device_list, link)
		md_link_node(md, guest->hv_node, device->hv_node);
	TAILQ_FOREACH(mblock, &guest->mblock_list, link)
		md_link_node(md, guest->hv_node, mblock->hv_node);
	TAILQ_FOREACH(endpoint, &guest->endpoint_list, link)
		md_link_node(md, guest->hv_node, endpoint->hv_node);
}

void
hvmd_finalize_guests(struct md *md)
{
	struct md_node *parent;
	struct md_node *node;
	uint64_t resource_id;

	parent = md_find_node(md, "root");
	assert(parent);

	node = md_add_node(md, "guests");
	md_link_node(md, parent, node);

	for (resource_id = 0; resource_id < max_guests; resource_id++) {
		if (guests[resource_id])
			hvmd_finalize_guest(md, guests[resource_id]);
	}
}

void
hvmd_finalize(void)
{
	struct md *md;
	struct md_node *node;
	struct md_node *parent;
	struct mblock *mblock;

	md = md_alloc();
	node = md_add_node(md, "root");
	md_add_prop_val(md, node, "content-version", content_version);
	if (content_version <= 0x100000000) {
		md_add_prop_val(md, node, "stick-frequency", stick_frequency);
		if (tod_frequency != 0)
			md_add_prop_val(md, node, "tod-frequency",
			    tod_frequency);
		if (tod != 0)
			md_add_prop_val(md, node, "tod", tod);
		if (erpt_pa != 0)
			md_add_prop_val(md, node, "erpt-pa", erpt_pa);
		if (erpt_size != 0)
			md_add_prop_val(md, node, "erpt-size", erpt_size);

		parent = node;
		node = md_add_node(md, "platform");
		md_link_node(md, parent, node);
		md_add_prop_val(md, node, "stick-frequency", stick_frequency);
	}

	parent = md_find_node(md, "root");
	assert(parent);

	node = md_add_node(md, "frag_space");
	md_link_node(md, parent, node);
	md_add_prop_val(md, node, "fragsize", fragsize);

	parent = md_find_node(md, "frag_space");
	TAILQ_FOREACH(mblock, &frag_mblocks, link) {
		node = md_add_node(md, "frag_mblock");
		md_link_node(md, parent, node);
		md_add_prop_val(md, node, "base", mblock->membase);
		md_add_prop_val(md, node, "size", mblock->memsize);
	}

	if (hvmd_mblock) {
		parent = md_find_node(md, "root");
		assert(parent);

		node = md_add_node(md, "hvmd_mblock");
		md_link_node(md, parent, node);
		md_add_prop_val(md, node, "base", hvmd_mblock->membase);
		md_add_prop_val(md, node, "size", hvmd_mblock->memsize);
		md_add_prop_val(md, node, "md_maxsize", md_maxsize);
	}

	hvmd_finalize_cpus(md);
	hvmd_finalize_maus(md);
	hvmd_finalize_devices(md);
	hvmd_finalize_memory(md);
	hvmd_finalize_endpoints(md);
	hvmd_finalize_consoles(md);
	hvmd_finalize_guests(md);

	md_write(md, "hv.md");
}

struct ldc_endpoint *
hvmd_add_endpoint(struct guest *guest)
{
	struct ldc_endpoint *endpoint;
	uint64_t resource_id;

	for (resource_id = 0; resource_id < max_guest_ldcs; resource_id++)
		if (ldc_endpoints[resource_id] == NULL)
			break;
	assert(resource_id < max_guest_ldcs);

	endpoint = xzalloc(sizeof(*endpoint));
	endpoint->target_guest = -1;
	endpoint->tx_ino = -1;
	endpoint->rx_ino = -1;
	endpoint->private_svc = -1;
	endpoint->svc_id = -1;
	endpoint->resource_id = resource_id;
	ldc_endpoints[resource_id] = endpoint;

	TAILQ_INSERT_TAIL(&guest->endpoint_list, endpoint, link);
	endpoint->guest = guest;

	return endpoint;
}

struct console *
hvmd_add_console(struct guest *guest)
{
	struct guest *primary;
	struct console *console;
	uint64_t resource_id;
	uint64_t client_channel, server_channel;

	primary = guest_lookup("primary");
	client_channel = guest->endpoint_id++;
	server_channel = primary->endpoint_id++;

	for (resource_id = 0; resource_id < max_guests; resource_id++)
		if (consoles[resource_id] == NULL)
			break;
	assert(resource_id < max_guests);

	console = xzalloc(sizeof(*console));
	console->ino = 0x11;
	console->resource_id = resource_id;
	consoles[resource_id] = console;

	console->client_endpoint = hvmd_add_endpoint(guest);
	console->client_endpoint->tx_ino = 0x11;
	console->client_endpoint->rx_ino = 0x11;
	console->client_endpoint->target_type = LDC_GUEST;
	console->client_endpoint->target_guest = primary->gid;
	console->client_endpoint->target_channel = server_channel;
	console->client_endpoint->channel = client_channel;
	console->client_endpoint->private_svc = LDC_CONSOLE_SVC;

	console->server_endpoint = hvmd_add_endpoint(primary);
	console->server_endpoint->tx_ino = 2 * server_channel;
	console->server_endpoint->rx_ino = 2 * server_channel + 1;
	console->server_endpoint->target_type = LDC_GUEST;
	console->server_endpoint->target_guest = guest->gid;
	console->server_endpoint->channel = server_channel;
	console->server_endpoint->target_channel = client_channel;

	guest->console = console;
	console->guest = guest;

	return console;
}

void
hvmd_add_domain_services(struct guest *guest)
{
	struct guest *primary;
	struct ldc_channel *ds = &guest->domain_services;
	uint64_t client_channel, server_channel;

	primary = guest_lookup("primary");
	client_channel = guest->endpoint_id++;
	server_channel = primary->endpoint_id++;

	ds->client_endpoint = hvmd_add_endpoint(guest);
	ds->client_endpoint->tx_ino = 2 * client_channel;
	ds->client_endpoint->rx_ino = 2 * client_channel + 1;
	ds->client_endpoint->target_type = LDC_GUEST;
	ds->client_endpoint->target_guest = primary->gid;
	ds->client_endpoint->target_channel = server_channel;
	ds->client_endpoint->channel = client_channel;

	ds->server_endpoint = hvmd_add_endpoint(primary);
	ds->server_endpoint->tx_ino = 2 * server_channel;
	ds->server_endpoint->rx_ino = 2 * server_channel + 1;
	ds->server_endpoint->target_type = LDC_GUEST;
	ds->server_endpoint->target_guest = guest->gid;
	ds->server_endpoint->channel = server_channel;
	ds->server_endpoint->target_channel = client_channel;
}

struct ldc_channel *
hvmd_add_vio(struct guest *guest)
{
	struct guest *primary;
	struct ldc_channel *lc = &guest->vio[guest->num_vios++];
	uint64_t client_channel, server_channel;

	primary = guest_lookup("primary");
	client_channel = guest->endpoint_id++;
	server_channel = primary->endpoint_id++;

	lc->client_endpoint = hvmd_add_endpoint(guest);
	lc->client_endpoint->tx_ino = 2 * client_channel;
	lc->client_endpoint->rx_ino = 2 * client_channel + 1;
	lc->client_endpoint->target_type = LDC_GUEST;
	lc->client_endpoint->target_guest = primary->gid;
	lc->client_endpoint->target_channel = server_channel;
	lc->client_endpoint->channel = client_channel;

	lc->server_endpoint = hvmd_add_endpoint(primary);
	lc->server_endpoint->tx_ino = 2 * server_channel;
	lc->server_endpoint->rx_ino = 2 * server_channel + 1;
	lc->server_endpoint->target_type = LDC_GUEST;
	lc->server_endpoint->target_guest = guest->gid;
	lc->server_endpoint->channel = server_channel;
	lc->server_endpoint->target_channel = client_channel;

	return lc;
}

struct guest *
hvmd_add_guest(const char *name)
{
	struct guest *guest;
	uint64_t resource_id;

	for (resource_id = 0; resource_id < max_guests; resource_id++)
		if (guests[resource_id] == NULL)
			break;
	assert(resource_id < max_guests);

	guest = xzalloc(sizeof(*guest));
	TAILQ_INIT(&guest->cpu_list);
	TAILQ_INIT(&guest->device_list);
	TAILQ_INIT(&guest->subdevice_list);
	TAILQ_INIT(&guest->mblock_list);
	TAILQ_INIT(&guest->endpoint_list);
	guests[resource_id] = guest;
	guest->name = name;
	guest->gid = resource_id;
	guest->pid = resource_id + 1;
	guest->resource_id = resource_id;
	guest->mdpa = hvmd_alloc_frag(-1);

	hvmd_add_console(guest);
	hvmd_add_domain_services(guest);

	return guest;
}

struct md_node *
guest_add_channel_endpoints(struct guest *guest)
{
	struct md *md = guest->md;
	struct md_node *parent;
	struct md_node *node;

	parent = md_find_node(md, "root");
	assert(parent);

	node = md_add_node(md, "channel-endpoints");
	md_link_node(md, parent, node);

	return node;
}

struct md_node *
guest_add_endpoint(struct guest *guest, uint64_t id)
{
	struct md *md = guest->md;
	struct md_node *parent;
	struct md_node *node;

	parent = md_find_node(md, "channel-endpoints");
	if (parent == NULL)
		parent = guest_add_channel_endpoints(guest);

	node = md_add_node(md, "channel-endpoint");
	md_link_node(md, parent, node);
	md_add_prop_val(md, node, "id", id);
	md_add_prop_val(md, node, "tx-ino", 2 * id);
	md_add_prop_val(md, node, "rx-ino", 2 * id + 1);

	return node;
}

struct md_node *
guest_add_vcc(struct guest *guest)
{
	const char compatible[] = "SUNW,sun4v-virtual-console-concentrator";
	struct md *md = guest->md;
	struct md_node *parent;
	struct md_node *node;

	parent = md_find_node(md, "channel-devices");
	assert(parent != NULL);

	node = md_add_node(md, "virtual-device");
	md_link_node(md, parent, node);
	md_add_prop_str(md, node, "name", "virtual-console-concentrator");
	md_add_prop_data(md, node, "compatible", compatible,
	    sizeof(compatible));
	md_add_prop_str(md, node, "device_type", "vcc");
	md_add_prop_val(md, node, "cfg-handle", 0x0);
	md_add_prop_str(md, node, "svc-name", "primary-vcc0");

	return node;
}

struct md_node *
guest_find_vcc(struct guest *guest)
{
	struct md *md = guest->md;
	struct md_node *node, *node2;
	struct md_prop *prop;
	const char *name;

	node = md_find_node(md, "channel-devices");
	assert(node != NULL);

	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0) {
			node2 = prop->d.arc.node;
			if (!md_get_prop_str(md, node2, "name", &name))
				continue;
			if (strcmp(name, "virtual-console-concentrator") == 0)
				return node2;
		}
	}

	return NULL;
}

struct md_node *
guest_add_vcc_port(struct guest *guest, struct md_node *vcc,
    const char *domain, uint64_t id, uint64_t channel)
{
	struct md *md = guest->md;
	struct md_node *node;
	struct md_node *child;

	if (vcc == NULL)
		vcc = guest_find_vcc(guest);
	if (vcc == NULL)
		vcc = guest_add_vcc(guest);

	node = md_add_node(md, "virtual-device-port");
	md_link_node(md, vcc, node);
	md_add_prop_str(md, node, "name", "vcc-port");
	md_add_prop_val(md, node, "id", id);
	md_add_prop_str(md, node, "vcc-domain-name", domain);
	md_add_prop_str(md, node, "vcc-group-name", domain);
	/* OpenBSD doesn't care about this, but Solaris might. */
	md_add_prop_val(md, node, "vcc-tcp-port", 5000 + id);

	child = guest_add_endpoint(guest, channel);
	md_link_node(md, node, child);

	return node;
}

struct md_node *
guest_add_vds(struct guest *guest)
{
	const char compatible[] = "SUNW,sun4v-disk-server";
	struct md *md = guest->md;
	struct md_node *parent;
	struct md_node *node;

	parent = md_find_node(md, "channel-devices");
	assert(parent != NULL);

	node = md_add_node(md, "virtual-device");
	md_link_node(md, parent, node);
	md_add_prop_str(md, node, "name", "virtual-disk-server");
	md_add_prop_data(md, node, "compatible", compatible,
	    sizeof(compatible));
	md_add_prop_str(md, node, "device_type", "vds");
	md_add_prop_val(md, node, "cfg-handle", 0x0);
	md_add_prop_str(md, node, "svc-name", "primary-vds0");

	return node;
}

struct md_node *
guest_find_vds(struct guest *guest)
{
	struct md *md = guest->md;
	struct md_node *node, *node2;
	struct md_prop *prop;
	const char *name;

	node = md_find_node(md, "channel-devices");
	assert(node != NULL);

	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0) {
			node2 = prop->d.arc.node;
			if (!md_get_prop_str(md, node2, "name", &name))
				continue;
			if (strcmp(name, "virtual-disk-server") == 0)
				return node2;
		}
	}

	return NULL;
}

struct md_node *
guest_add_vds_port(struct guest *guest, struct md_node *vds,
    const char *path, uint64_t id, uint64_t channel)
{
	struct md *md = guest->md;
	struct md_node *node;
	struct md_node *child;

	if (vds == NULL)
		vds = guest_find_vds(guest);
	if (vds == NULL)
		vds = guest_add_vds(guest);

	node = md_add_node(md, "virtual-device-port");
	md_link_node(md, vds, node);
	md_add_prop_str(md, node, "name", "vds-port");
	md_add_prop_val(md, node, "id", id);
	md_add_prop_str(md, node, "vds-block-device", path);

	child = guest_add_endpoint(guest, channel);
	md_link_node(md, node, child);

	return node;
}

struct md_node *
guest_add_vsw(struct guest *guest)
{
	const char compatible[] = "SUNW,sun4v-network-switch";
	struct md *md = guest->md;
	struct md_node *parent;
	struct md_node *node;

	parent = md_find_node(md, "channel-devices");
	assert(parent != NULL);

	node = md_add_node(md, "virtual-device");
	md_link_node(md, parent, node);
	md_add_prop_str(md, node, "name", "virtual-network-switch");
	md_add_prop_data(md, node, "compatible", compatible,
	    sizeof(compatible));
	md_add_prop_str(md, node, "device_type", "vsw");
	md_add_prop_val(md, node, "cfg-handle", 0x0);
	md_add_prop_str(md, node, "svc-name", "primary-vsw0");

	return node;
}

struct md_node *
guest_find_vsw(struct guest *guest)
{
	struct md *md = guest->md;
	struct md_node *node, *node2;
	struct md_prop *prop;
	const char *name;

	node = md_find_node(md, "channel-devices");
	assert(node != NULL);

	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0) {
			node2 = prop->d.arc.node;
			if (!md_get_prop_str(md, node2, "name", &name))
				continue;
			if (strcmp(name, "virtual-network-switch") == 0)
				return node2;
		}
	}

	return NULL;
}

struct md_node *
guest_add_vsw_port(struct guest *guest, struct md_node *vds,
    uint64_t id, uint64_t channel)
{
	struct md *md = guest->md;
	struct md_node *node;
	struct md_node *child;
	uint64_t mac_addr;

	if (vds == NULL)
		vds = guest_find_vsw(guest);
	if (vds == NULL)
		vds = guest_add_vsw(guest);
	if (!md_get_prop_val(md, vds, "local-mac-address", &mac_addr)) {
		mac_addr = 0x00144ff80000 + (arc4random() & 0x3ffff);
		md_add_prop_val(md, vds, "local-mac-address", mac_addr);
	}

	node = md_add_node(md, "virtual-device-port");
	md_link_node(md, vds, node);
	md_add_prop_str(md, node, "name", "vsw-port");
	md_add_prop_val(md, node, "id", id);

	child = guest_add_endpoint(guest, channel);
	md_link_node(md, node, child);

	return node;
}

struct md_node *
guest_add_console_device(struct guest *guest)
{
	const char compatible[] = "SUNW,sun4v-console";
	struct md *md = guest->md;
	struct md_node *parent;
	struct md_node *node;

	parent = md_find_node(md, "virtual-devices");
	assert(parent);

	node = md_add_node(md, "virtual-device");
	md_link_node(md, parent, node);
	md_add_prop_str(md, node, "name", "console");
	md_add_prop_str(md, node, "device-type", "serial");
	md_add_prop_val(md, node, "intr", 0x1);
	md_add_prop_val(md, node, "ino", 0x11);
	md_add_prop_val(md, node, "channel#", 0);
	md_add_prop_val(md, node, "cfg-handle", 0x1);
	md_add_prop_data(md, node, "compatible", compatible,
	    sizeof(compatible));

	return node;
}

struct md_node *
guest_add_vdc(struct guest *guest, uint64_t cfghandle)
{
	const char compatible[] = "SUNW,sun4v-disk";
	struct md *md = guest->md;
	struct md_node *parent;
	struct md_node *node;

	parent = md_find_node(md, "channel-devices");
	assert(parent);

	node = md_add_node(md, "virtual-device");
	md_link_node(md, parent, node);
	md_add_prop_str(md, node, "name", "disk");
	md_add_prop_str(md, node, "device-type", "block");
	md_add_prop_val(md, node, "cfg-handle", cfghandle);
	md_add_prop_data(md, node, "compatible", compatible,
	    sizeof(compatible));

	return node;
}

struct md_node *
guest_add_vdc_port(struct guest *guest, struct md_node *vdc,
    uint64_t cfghandle, uint64_t id, uint64_t channel)
{
	struct md *md = guest->md;
	struct md_node *node;
	struct md_node *child;

	if (vdc == NULL)
		vdc = guest_add_vdc(guest, cfghandle);

	node = md_add_node(md, "virtual-device-port");
	md_link_node(md, vdc, node);
	md_add_prop_str(md, node, "name", "vdc-port");
	md_add_prop_val(md, node, "id", id);

	child = guest_add_endpoint(guest, channel);
	md_link_node(md, node, child);

	return node;
}

struct md_node *
guest_add_vnet(struct guest *guest, uint64_t mac_addr, uint64_t mtu,
    uint64_t cfghandle)
{
	const char compatible[] = "SUNW,sun4v-network";
	struct md *md = guest->md;
	struct md_node *parent;
	struct md_node *node;

	parent = md_find_node(md, "channel-devices");
	assert(parent);

	node = md_add_node(md, "virtual-device");
	md_link_node(md, parent, node);
	md_add_prop_str(md, node, "name", "network");
	md_add_prop_str(md, node, "device-type", "network");
	md_add_prop_val(md, node, "cfg-handle", cfghandle);
	md_add_prop_data(md, node, "compatible", compatible,
	    sizeof(compatible));
	if (mac_addr == -1)
		mac_addr = 0x00144ff80000 + (arc4random() & 0x3ffff);
	md_add_prop_val(md, node, "local-mac-address", mac_addr);
	md_add_prop_val(md, node, "mtu", mtu);

	return node;
}

struct md_node *
guest_add_vnet_port(struct guest *guest, struct md_node *vdc,
    uint64_t mac_addr, uint64_t remote_mac_addr, uint64_t mtu, uint64_t cfghandle,
    uint64_t id, uint64_t channel)
{
	struct md *md = guest->md;
	struct md_node *node;
	struct md_node *child;

	if (vdc == NULL)
		vdc = guest_add_vnet(guest, mac_addr, mtu, cfghandle);

	node = md_add_node(md, "virtual-device-port");
	md_link_node(md, vdc, node);
	md_add_prop_str(md, node, "name", "vnet-port");
	md_add_prop_val(md, node, "id", id);
	md_add_prop_val(md, node, "switch-port", 0);
	md_add_prop_data(md, node, "remote-mac-address",
	    (uint8_t *)&remote_mac_addr, sizeof(remote_mac_addr));

	child = guest_add_endpoint(guest, channel);
	md_link_node(md, node, child);

	return node;
}

struct md_node *
guest_add_channel_devices(struct guest *guest)
{
	const char compatible[] = "SUNW,sun4v-channel-devices";
	struct md *md = guest->md;
	struct md_node *parent;
	struct md_node *node;

	parent = md_find_node(md, "virtual-devices");
	assert(parent);

	node = md_add_node(md, "channel-devices");
	md_link_node(md, parent, node);
	md_add_prop_str(md, node, "name", "channel-devices");
	md_add_prop_str(md, node, "device-type", "channel-devices");
	md_add_prop_data(md, node, "compatible", compatible,
	    sizeof(compatible));
	md_add_prop_val(md, node, "cfg-handle", 0x200);

	return node;
}

struct md_node *
guest_add_domain_services(struct guest *guest)
{
	struct md *md = guest->md;
	struct md_node *parent;
	struct md_node *node;

	parent = md_find_node(md, "root");
	assert(parent);

	node = md_add_node(md, "domain-services");
	md_link_node(md, parent, node);

	return node;
}

struct md_node *
guest_add_domain_services_port(struct guest *guest, uint64_t id)
{
	struct md *md = guest->md;
	struct md_node *parent;
	struct md_node *node;
	struct md_node *child;

	parent = md_find_node(md, "domain-services");
	if (parent == NULL)
		parent = guest_add_domain_services(guest);

	node = md_add_node(md, "domain-services-port");
	md_link_node(md, parent, node);
	md_add_prop_val(md, node, "id", id);

	child = guest_add_endpoint(guest,
	    guest->domain_services.client_endpoint->channel);
	md_link_node(md, node, child);

	return node;
}

void
guest_add_devalias(struct guest *guest, const char *name, const char *path)
{
	struct md *md = guest->md;
	struct md_node *parent;
	struct md_node *node;

	parent = md_find_node(md, "openboot");
	assert(parent);

	node = md_find_subnode(md, parent, "devalias");
	if (node == NULL) {
		node = md_add_node(md, "devalias");
		md_link_node(md, parent, node);
	}

	md_add_prop_str(md, node, name, path);
}

void
guest_set_domaining_enabled(struct guest *guest)
{
	struct md *md = guest->md;
	struct md_node *node;

	node = md_find_node(md, "platform");
	assert(node);

	md_set_prop_val(md, node, "domaining-enabled", 0x1);
}

void
guest_set_mac_address(struct guest *guest)
{
	struct md *md = guest->md;
	struct md_node *node;
	uint64_t mac_address;
	uint64_t hostid;

	node = md_find_node(md, "platform");
	assert(node);

	mac_address = 0x00144ff80000 + (arc4random() & 0x3ffff);
	md_set_prop_val(md, node, "mac-address", mac_address);

	hostid = 0x84000000 | (mac_address & 0x00ffffff);
	md_set_prop_val(md, node, "hostid", hostid);
}

struct md_node *
guest_find_vc(struct guest *guest)
{
	struct md *md = guest->md;
	struct md_node *node, *node2;
	struct md_node *vc = NULL;
	struct md_prop *prop;
	const char *name;

	node = md_find_node(md, "channel-devices");
	assert(node != NULL);

	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0) {
			node2 = prop->d.arc.node;
			if (!md_get_prop_str(md, node2, "name", &name))
				continue;
			if (strcmp(name, "virtual-channel") == 0)
				vc = node2;
		}
	}

	return vc;
}

struct md_node *
guest_add_vc_port(struct guest *guest, struct md_node *vc,
    const char *domain, uint64_t id, uint64_t channel)
{
	struct md *md = guest->md;
	struct md_node *node;
	struct md_node *child;
	char *str;

	if (vc == NULL)
		vc = guest_find_vc(guest);
	assert(vc);

	node = md_add_node(md, "virtual-device-port");
	md_link_node(md, vc, node);
	md_add_prop_str(md, node, "name", "vldc-port");
	md_add_prop_val(md, node, "id", id);
	xasprintf(&str, "ldom-%s", domain);
	md_add_prop_str(md, node, "vldc-svc-name", str);
	free(str);

	child = guest_add_endpoint(guest, channel);
	md_link_node(md, node, child);

	return node;
}

struct guest *
guest_create(const char *name)
{
	struct guest *guest;
	struct guest *primary;
	struct md_node *node;

	primary = guest_lookup("primary");

	guest = hvmd_add_guest(name);
	guest->md = md_copy(protomd);

	md_find_delete_node(guest->md, "dimm_configuration");
	md_find_delete_node(guest->md, "platform_services");
	md_collect_garbage(guest->md);

	guest_set_domaining_enabled(guest);
	guest_set_mac_address(guest);
	guest_add_channel_devices(guest);
	guest_add_domain_services_port(guest, 0);
	guest_add_console_device(guest);
	guest_add_devalias(guest, "virtual-console",
	    "/virtual-devices/console@1");

	guest_add_vcc_port(primary, NULL, guest->name, guest->gid - 1,
	    guest->console->server_endpoint->channel);

	guest_add_vc_port(primary, NULL, guest->name, guest->gid + 2,
	    guest->domain_services.server_endpoint->channel);

	node = md_find_node(guest->md, "root");
	md_add_prop_val(guest->md, node, "reset-reason", 0);

	return guest;
}

int
guest_match_path(struct guest *guest, const char *path)
{
	struct subdevice *subdevice;
	size_t len = strlen(path);

	TAILQ_FOREACH(subdevice, &guest->subdevice_list, link) {
		const char *path2 = subdevice->path;
		size_t len2 = strlen(path2);
		
		if (strncmp(path, path2, len < len2 ? len : len2) == 0)
			return 1;
	}

	return 0;
}

void
guest_prune_phys_io(struct guest *guest)
{
	const char compatible[] = "SUNW,sun4v-vpci";
	struct md *md = guest->md;
	struct md_node *node, *node2;
	struct md_prop *prop, *prop2;
	const char *device_type;
	uint64_t cfg_handle;
	char *path;

	node = md_find_node(guest->md, "phys_io");
	TAILQ_FOREACH_SAFE(prop, &node->prop_list, link, prop2) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0) {
			node2 = prop->d.arc.node;
			if (!md_get_prop_str(md, node2, "device-type",
			    &device_type))
				device_type = "unknown";
			if (strcmp(device_type, "pciex") != 0) {
				md_delete_node(md, node2);
				continue;
			}

			if (!md_get_prop_val(md, node2, "cfg-handle",
			    &cfg_handle)) {
				md_delete_node(md, node2);
				continue;
			}

			xasprintf(&path, "/@%llx", cfg_handle);
			if (!guest_match_path(guest, path)) {
				md_delete_node(md, node2);
				continue;
			}

			md_set_prop_data(md, node2, "compatible",
			    compatible, sizeof(compatible));
			md_add_prop_val(md, node2, "virtual-root-complex", 1);
			guest_prune_pcie(guest, node2, path);
			free(path);

			guest_add_vpcie(guest, cfg_handle);
		}
	}
}

void
guest_prune_pcie(struct guest *guest, struct md_node *node, const char *path)
{
	struct md *md = guest->md;
	struct md_node *node2;
	struct md_prop *prop, *prop2;
	uint64_t device_number;
	char *path2;

	TAILQ_FOREACH_SAFE(prop, &node->prop_list, link, prop2) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0) {
			node2 = prop->d.arc.node;
			if (strcmp(node2->name->str, "wart") == 0) {
				md_delete_node(md, node2);
				continue;
			}
			if (!md_get_prop_val(md, node2, "device-number",
			    &device_number))
				continue;
			xasprintf(&path2, "%s/@%llx", path, device_number);
			if (guest_match_path(guest, path2))
				guest_prune_pcie(guest, node2, path2);
			else
				md_delete_node(md, node2);
			free(path2);
		}
	}
}

void
guest_add_vpcie(struct guest *guest, uint64_t cfghandle)
{
	struct device *device, *phys_device = NULL;
	uint64_t resource_id;

	for (resource_id = 0; resource_id < max_devices; resource_id++) {
		if (pcie_busses[resource_id] &&
		    pcie_busses[resource_id]->cfghandle == cfghandle) {
			phys_device = pcie_busses[resource_id];
			break;
		}
	}
	if (phys_device == NULL)
		errx(1, "no matching physical device");
			
	for (resource_id = 0; resource_id < max_devices; resource_id++) {
		if (pcie_busses[resource_id] == NULL)
			break;
	}
	if (resource_id >= max_devices)
		errx(1, "no available resource_id");

	device = xzalloc(sizeof(*device));
	device->gid = guest->gid;
	device->cfghandle = cfghandle;
	device->resource_id = resource_id;
	device->rcid = phys_device->rcid;
	device->virtual = 1;
	device->guest = guest;

	device->num_msi_eqs = phys_device->msi_eqs_per_vpci;
	device->num_msis = phys_device->msis_per_vpci;
	phys_device->msi_base -= phys_device->msis_per_vpci;
	device->msi_ranges[0] = phys_device->msi_base;
	device->msi_ranges[1] = device->num_msis;

	pcie_busses[resource_id] = device;
	TAILQ_INSERT_TAIL(&guest->device_list, device, link);
}

void
guest_fixup_phys_io(struct guest *guest)
{
	struct md *md = guest->md;
	struct md_node *node, *node2;
	struct md_prop *prop, *prop2;
	struct device *device;
	uint64_t cfg_handle;
	uint64_t mapping[3];
	const void *buf;
	size_t len;

	if (!directio_capability)
		return;

	node = md_find_node(guest->md, "phys_io");
	TAILQ_FOREACH_SAFE(prop, &node->prop_list, link, prop2) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0) {
			node2 = prop->d.arc.node;

			if (!md_get_prop_val(md, node2, "cfg-handle",
			    &cfg_handle))
				continue;

			TAILQ_FOREACH(device, &guest->device_list, link) {
				if (device->cfghandle == cfg_handle)
					break;
			}
			if (device == NULL)
				continue;

			md_set_prop_val(md, node2, "#msi-eqs",
			    device->num_msi_eqs);
			md_set_prop_val(md, node2, "#msi",
			    device->num_msis);
			md_set_prop_data(md, node2, "msi-ranges",
			    (void *)device->msi_ranges,
			    sizeof(device->msi_ranges));

			md_get_prop_data(md, node2, "msi-eq-to-devino",
			    &buf, &len);
			memcpy(mapping, buf, sizeof(mapping));
			mapping[1] = device->num_msi_eqs;
			md_set_prop_data(md, node2, "msi-eq-to-devino",
			    (void *)mapping, sizeof(mapping));
		}
	}
}

struct guest *
guest_lookup(const char *name)
{
	uint64_t resource_id;

	for (resource_id = 0; resource_id < max_guests; resource_id++) {
		if (guests[resource_id] &&
		    strcmp(guests[resource_id]->name, name) == 0)
			return guests[resource_id];
	}

	return NULL;
}

void
guest_delete_virtual_device_port(struct guest *guest, struct md_node *port)
{
	struct md *md = guest->md;
	struct md_node *node;
	struct md_prop *prop;

	TAILQ_FOREACH(node, &md->node_list, link) {
		if (strcmp(node->name->str, "virtual-device-port") != 0)
			continue;
		TAILQ_FOREACH(prop, &node->prop_list, link) {
			if (prop->tag == MD_PROP_ARC &&
			    prop->d.arc.node == port) {
				md_delete_node(md, node);
				return;
			}
		}
	}
}

void
guest_delete_endpoint(struct guest *guest, struct ldc_endpoint *endpoint)
{
	struct md *md = guest->md;
	struct md_node *node, *node2;
	struct md_prop *prop;
	uint64_t id, resource_id;

	node = md_find_node(md, "channel-endpoints");
	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0) {
			node2 = prop->d.arc.node;
			if (!md_get_prop_val(hvmd, node2, "id", &id))
				continue;
			if (id == endpoint->channel) {
				guest_delete_virtual_device_port(guest, node2);
				md_delete_node(md, node2);
				break;
			}
		}
	}

	TAILQ_REMOVE(&guest->endpoint_list, endpoint, link);
	ldc_endpoints[endpoint->resource_id] = NULL;

	/* Delete peer as well. */
	for (resource_id = 0; resource_id < max_guest_ldcs; resource_id++) {
		struct ldc_endpoint *peer = ldc_endpoints[resource_id];

		if (peer && peer->target_type == LDC_GUEST &&
		    peer->target_channel == endpoint->channel &&
		    peer->channel == endpoint->target_channel &&
		    peer->target_guest == guest->gid)
			guest_delete_endpoint(peer->guest, peer);
	}

	free(endpoint);
}

void
guest_delete(struct guest *guest)
{
	struct cpu *cpu, *cpu2;
	struct mblock *mblock, *mblock2;
	struct ldc_endpoint *endpoint, *endpoint2;

	consoles[guest->console->resource_id] = NULL;
	free(guest->console);

	TAILQ_FOREACH_SAFE(cpu, &guest->cpu_list, link, cpu2) {
		TAILQ_REMOVE(&guest->cpu_list, cpu, link);
		cpus[cpu->resource_id] = NULL;
		pri_free_cpu(cpu);
	}

	TAILQ_FOREACH_SAFE(mblock, &guest->mblock_list, link, mblock2) {
		TAILQ_REMOVE(&guest->mblock_list, mblock, link);
		mblocks[mblock->resource_id] = NULL;
		free(mblock);
	}

	TAILQ_FOREACH_SAFE(endpoint, &guest->endpoint_list, link, endpoint2)
		guest_delete_endpoint(guest, endpoint);

	hvmd_free_frag(guest->mdpa);

	guests[guest->resource_id] = NULL;
	free(guest);
}

void
guest_delete_cpu(struct guest *guest, uint64_t vid)
{
	struct cpu *cpu;

	TAILQ_FOREACH(cpu, &guest->cpu_list, link) {
		if (cpu->vid == vid) {
			TAILQ_REMOVE(&guest->cpu_list, cpu, link);
			cpus[cpu->resource_id] = NULL;
			pri_free_cpu(cpu);
			return;
		}
	}
}

void
guest_add_cpu(struct guest *guest, uint64_t stride)
{
	struct cpu *cpu;

	cpu = pri_alloc_cpu(-1);

	/* 
	 * Allocate (but don't assign) additional virtual CPUs if the
	 * specified stride is bigger than one.
	 */
	while (stride-- > 1)
		pri_alloc_cpu(-1);

	if (cpu->resource_id == -1) {
		uint64_t resource_id;

		for (resource_id = 0; resource_id < max_cpus; resource_id++)
			if (cpus[resource_id] == NULL)
				break;
		assert(resource_id < max_cpus);
		cpu->resource_id = resource_id;
	}
	cpus[cpu->resource_id] = cpu;

	cpu->vid = guest->cpu_vid++;
	cpu->gid = guest->gid;
	cpu->partid = 1;

	TAILQ_INSERT_TAIL(&guest->cpu_list, cpu, link);
	cpu->guest = guest;
}

void
guest_delete_memory(struct guest *guest)
{
	struct mblock *mblock, *tmp;

	TAILQ_FOREACH_SAFE(mblock, &guest->mblock_list, link, tmp) {
		if (mblock->resource_id != -1)
			mblocks[mblock->resource_id] = NULL;
		TAILQ_REMOVE(&guest->mblock_list, mblock, link);
		free(mblock);
	}
}

void
guest_add_memory(struct guest *guest, uint64_t base, uint64_t size)
{
	struct mblock *mblock;
	uint64_t resource_id;

	mblock = pri_alloc_memory(base, size);
	if (mblock == NULL)
		errx(1, "unable to allocate guest memory");
	for (resource_id = 0; resource_id < max_cpus; resource_id++)
		if (mblocks[resource_id] == NULL)
			break;
	assert(resource_id < max_mblocks);
	mblock->resource_id = resource_id;
	mblocks[resource_id] = mblock;

	mblock->realbase = mblock->membase & (max_page_size - 1);
	if (mblock->realbase == 0)
		mblock->realbase = max_page_size;

	TAILQ_INSERT_TAIL(&guest->mblock_list, mblock, link);
	mblock->guest = guest;
}

void
guest_add_vdisk(struct guest *guest, uint64_t id, const char *path,
    const char *user_devalias)
{
	struct guest *primary;
	struct ldc_channel *lc;
	char *devalias;
	char *devpath;

	primary = guest_lookup("primary");

	lc = hvmd_add_vio(guest);
	guest_add_vds_port(primary, NULL, path, id,
	    lc->server_endpoint->channel);
	guest_add_vdc_port(guest, NULL, id, 0, lc->client_endpoint->channel);

	xasprintf(&devalias, "disk%d", id);
	xasprintf(&devpath,
	    "/virtual-devices@100/channel-devices@200/disk@%d", id);
	if (id == 0)
		guest_add_devalias(guest, "disk", devpath);
	guest_add_devalias(guest, devalias, devpath);
	if (user_devalias != NULL)
		guest_add_devalias(guest, user_devalias, devpath);
	free(devalias);
	free(devpath);
}

void
guest_add_vnetwork(struct guest *guest, uint64_t id, uint64_t mac_addr,
    uint64_t mtu, const char *user_devalias)
{
	struct guest *primary;
	struct ldc_channel *lc;
	char *devalias;
	char *devpath;
	struct md_node *node;
	uint64_t remote_mac_addr = -1;

	primary = guest_lookup("primary");

	lc = hvmd_add_vio(guest);
	guest_add_vsw_port(primary, NULL, id, lc->server_endpoint->channel);
	node = guest_find_vsw(primary);
	md_get_prop_val(primary->md, node, "local-mac-address", &remote_mac_addr);
	guest_add_vnet_port(guest, NULL, mac_addr, remote_mac_addr, mtu, id, 0,
	    lc->client_endpoint->channel);

	xasprintf(&devalias, "net%d", id);
	xasprintf(&devpath,
	    "/virtual-devices@100/channel-devices@200/network@%d", id);
	if (id == 0)
		guest_add_devalias(guest, "net", devpath);
	guest_add_devalias(guest, devalias, devpath);
	if (user_devalias != NULL)
		guest_add_devalias(guest, user_devalias, devpath);
	free(devalias);
	free(devpath);
}

void
guest_add_variable(struct guest *guest, const char *name, const char *str)
{
	struct md *md = guest->md;
	struct md_node *parent;
	struct md_node *node;

	node = md_find_node(md, "variables");
	if (node == NULL) {
		parent = md_find_node(md, "root");
		assert(parent);

		node = md_add_node(md, "variables");
		md_link_node(md, parent, node);
	}

	md_add_prop_str(md, node, name, str);
}

void
guest_add_iodev(struct guest *guest, const char *dev)
{
	struct component *component;
	struct subdevice *subdevice;

	if (!directio_capability)
		errx(1, "direct I/O not supported by hypervisor");

	TAILQ_FOREACH(component, &components, link) {
		if (strcmp(component->nac, dev) == 0 ||
		    strcmp(component->path, dev) == 0)
			break;
	}

	if (component == NULL)
		errx(1, "incorrect device path %s", dev);
	if (component->assigned)
		errx(1, "device path %s already assigned", dev);

	subdevice = xzalloc(sizeof(*subdevice));
	subdevice->path = component->path;
	TAILQ_INSERT_TAIL(&guest->subdevice_list, subdevice, link);
	component->assigned = 1;
}

struct cpu *
guest_find_cpu(struct guest *guest, uint64_t pid)
{
	struct cpu *cpu;

	TAILQ_FOREACH(cpu, &guest->cpu_list, link)
		if (cpu->pid == pid)
			return cpu;

	return NULL;
}

void
guest_finalize(struct guest *guest)
{
	struct md *md = guest->md;
	struct md_node *node, *node2;
	struct md_prop *prop, *prop2;
	struct mblock *mblock;
	struct md_node *parent;
	struct md_node *child;
	struct cpu *cpu;
	uint64_t pid;
	const char *name;
	char *path;

	node = md_find_node(md, "cpus");
	TAILQ_FOREACH_SAFE(prop, &node->prop_list, link, prop2) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0) {
			node2 = prop->d.arc.node;
			if (!md_get_prop_val(md, node2, "pid", &pid))
				if (!md_get_prop_val(md, node2, "id", &pid))
					continue;
			cpu = guest_find_cpu(guest, pid);
			if (cpu == NULL) {
				md_delete_node(md, node2);
				continue;
			}
			md_set_prop_val(md, node2, "id", cpu->vid);
		}
	}

	/*
	 * We don't support crypto units yet, so delete any "ncp" and
	 * "n2cp" nodes.  If we don't, Solaris whines about not being
	 * able to configure crypto work queues.
	 */
	node = md_find_node(md, "virtual-devices");
	TAILQ_FOREACH_SAFE(prop, &node->prop_list, link, prop2) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0) {
			node2 = prop->d.arc.node;
			if (!md_get_prop_str(md, node2, "name", &name))
				continue;
			if (strcmp(name, "ncp") == 0)
				md_delete_node(md, node2);
			if (strcmp(name, "n2cp") == 0)
				md_delete_node(md, node2);
		}
	}

	node = md_find_node(md, "memory");
	TAILQ_FOREACH_SAFE(prop, &node->prop_list, link, prop2) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0) {
			node2 = prop->d.arc.node;
			md_delete_node(md, node2);
		}
	}

	if (strcmp(guest->name, "primary") != 0)
		guest_prune_phys_io(guest);
	guest_fixup_phys_io(guest);

	md_collect_garbage(md);

	parent = md_find_node(md, "memory");
	TAILQ_FOREACH(mblock, &guest->mblock_list, link) {
		child = md_add_node(md, "mblock");
		md_add_prop_val(md, child, "base", mblock->realbase);
		md_add_prop_val(md, child, "size", mblock->memsize);
		md_link_node(md, parent, child);
	}

	xasprintf(&path, "%s.md", guest->name);
	md_write(guest->md, path);
	free(path);
}

struct guest *
primary_init(void)
{
	struct guest *guest;

	guest = guest_lookup("primary");
	assert(guest);

	guest_set_domaining_enabled(guest);

	return guest;
}

void
build_config(const char *filename, int noaction)
{
	struct guest *primary;
	struct guest *guest;
	struct ldc_endpoint *endpoint;
	struct component *component;
	uint64_t resource_id;
	int i;

	struct ldom_config conf;
	struct domain *domain;
	struct vdisk *vdisk;
	struct vnet *vnet;
	struct var *var;
	struct iodev *iodev;
	uint64_t num_cpus = 0, primary_num_cpus = 0;
	uint64_t primary_stride = 1;
	uint64_t memory = 0, primary_memory = 0;

	SIMPLEQ_INIT(&conf.domain_list);
	if (parse_config(filename, &conf) < 0)
		exit(1);

	pri = md_read("pri");
	if (pri == NULL)
		err(1, "unable to get PRI");
	hvmd = md_read("hv.md");
	if (hvmd == NULL)
		err(1, "unable to get Hypervisor MD");

	pri_init(pri);
	pri_alloc_memory(hv_membase, hv_memsize);

	SIMPLEQ_FOREACH(domain, &conf.domain_list, entry) {
		if (strcmp(domain->name, "primary") == 0) {
			primary_num_cpus = domain->vcpu;
			primary_stride = domain->vcpu_stride;
			primary_memory = domain->memory;
		}
		num_cpus += (domain->vcpu * domain->vcpu_stride);
		memory += domain->memory;
	}
	if (primary_num_cpus == 0 && total_cpus > num_cpus)
		primary_num_cpus = total_cpus - num_cpus;
	if (primary_memory == 0 && total_memory > memory)
		primary_memory = total_memory - memory;
	if (num_cpus > total_cpus || primary_num_cpus == 0)
		errx(1, "not enough VCPU resources available");
	if (memory > total_memory || primary_memory == 0)
		errx(1, "not enough memory available");

	if (noaction)
		exit(0);

	hvmd_init(hvmd);
	primary = primary_init();

	for (resource_id = 0; resource_id <max_guests; resource_id++)
		if (guests[resource_id] &&
		    strcmp(guests[resource_id]->name, "primary") != 0)
			guest_delete(guests[resource_id]);

	primary->endpoint_id = 0;
	TAILQ_FOREACH(endpoint, &primary->endpoint_list, link) {
		if (endpoint->channel >= primary->endpoint_id)
			primary->endpoint_id = endpoint->channel + 1;
	}

	for (i = 0; i < max_cpus; i++)
		guest_delete_cpu(primary, i);
	for (i = 0; i < primary_num_cpus; i++)
		guest_add_cpu(primary, primary_stride);
	guest_delete_memory(primary);
	guest_add_memory(primary, -1, primary_memory);

	SIMPLEQ_FOREACH(domain, &conf.domain_list, entry) {
		if (strcmp(domain->name, "primary") != 0)
			continue;
		SIMPLEQ_FOREACH(var, &domain->var_list, entry)
			guest_add_variable(primary, var->name, var->str);
	}

	SIMPLEQ_FOREACH(domain, &conf.domain_list, entry) {
		if (strcmp(domain->name, "primary") == 0)
			continue;
		guest = guest_create(domain->name);
		for (i = 0; i < domain->vcpu; i++)
			guest_add_cpu(guest, domain->vcpu_stride);
		guest_add_memory(guest, -1, domain->memory);
		i = 0;
		SIMPLEQ_FOREACH(vdisk, &domain->vdisk_list, entry)
			guest_add_vdisk(guest, i++, vdisk->path,
			    vdisk->devalias);
		i = 0;
		SIMPLEQ_FOREACH(vnet, &domain->vnet_list, entry)
			guest_add_vnetwork(guest, i++, vnet->mac_addr,
			    vnet->mtu, vnet->devalias);
		SIMPLEQ_FOREACH(var, &domain->var_list, entry)
			guest_add_variable(guest, var->name, var->str);
		SIMPLEQ_FOREACH(iodev, &domain->iodev_list, entry)
			guest_add_iodev(guest, iodev->dev);

		guest_finalize(guest);
	}

	TAILQ_FOREACH(component, &components, link) {
		if (component->assigned)
			continue;
		guest_add_iodev(primary, component->path);
	}

	guest_finalize(primary);
	hvmd_finalize();
}

void
list_components(void)
{
	struct component *component;

	pri = md_read("pri");
	if (pri == NULL)
		err(1, "unable to get PRI");

	pri_init_components(pri);

	printf("%-16s %s\n", "PATH", "NAME");
	TAILQ_FOREACH(component, &components, link) {
		printf("%-16s %s\n", component->path, component->nac);
	}
}
