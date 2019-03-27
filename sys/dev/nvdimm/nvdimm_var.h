/*-
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 * Copyright (c) 2018, 2019 Intel Corporation
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef __DEV_NVDIMM_VAR_H__
#define	__DEV_NVDIMM_VAR_H__

#define NVDIMM_INDEX_BLOCK_SIGNATURE "NAMESPACE_INDEX"

struct nvdimm_label_index {
	char		signature[16];
	uint8_t		flags[3];
	uint8_t		label_size;
	uint32_t	seq;
	uint64_t	this_offset;
	uint64_t	this_size;
	uint64_t	other_offset;
	uint64_t	label_offset;
	uint32_t	slot_cnt;
	uint16_t	rev_major;
	uint16_t	rev_minor;
	uint64_t	checksum;
	uint8_t		free[0];
};

struct nvdimm_label {
	struct uuid	uuid;
	char		name[64];
	uint32_t	flags;
	uint16_t	nlabel;
	uint16_t	position;
	uint64_t	set_cookie;
	uint64_t	lba_size;
	uint64_t	dimm_phys_addr;
	uint64_t	raw_size;
	uint32_t	slot;
	uint8_t		alignment;
	uint8_t		reserved[3];
	struct uuid	type_guid;
	struct uuid	address_abstraction_guid;
	uint8_t		reserved1[88];
	uint64_t	checksum;
};

struct nvdimm_label_entry {
	SLIST_ENTRY(nvdimm_label_entry) link;
	struct nvdimm_label	label;
};

_Static_assert(sizeof(struct nvdimm_label_index) == 72, "Incorrect layout");
_Static_assert(sizeof(struct nvdimm_label) == 256, "Incorrect layout");

typedef uint32_t nfit_handle_t;

enum nvdimm_root_ivar {
	NVDIMM_ROOT_IVAR_ACPI_HANDLE,
	NVDIMM_ROOT_IVAR_DEVICE_HANDLE,
	NVDIMM_ROOT_IVAR_MAX,
};
__BUS_ACCESSOR(nvdimm_root, acpi_handle, NVDIMM_ROOT, ACPI_HANDLE, ACPI_HANDLE)
__BUS_ACCESSOR(nvdimm_root, device_handle, NVDIMM_ROOT, DEVICE_HANDLE,
    nfit_handle_t)

struct nvdimm_root_dev {
	SLIST_HEAD(, SPA_mapping) spas;
};

struct nvdimm_dev {
	device_t	nv_dev;
	nfit_handle_t	nv_handle;
	uint64_t	**nv_flush_addr;
	int		nv_flush_addr_cnt;
	uint32_t	label_area_size;
	uint32_t	max_label_xfer;
	struct nvdimm_label_index *label_index;
	SLIST_HEAD(, nvdimm_label_entry) labels;
};

enum SPA_mapping_type {
	SPA_TYPE_VOLATILE_MEMORY	= 0,
	SPA_TYPE_PERSISTENT_MEMORY	= 1,
	SPA_TYPE_CONTROL_REGION		= 2,
	SPA_TYPE_DATA_REGION		= 3,
	SPA_TYPE_VOLATILE_VIRTUAL_DISK	= 4,
	SPA_TYPE_VOLATILE_VIRTUAL_CD	= 5,
	SPA_TYPE_PERSISTENT_VIRTUAL_DISK= 6,
	SPA_TYPE_PERSISTENT_VIRTUAL_CD	= 7,
	SPA_TYPE_UNKNOWN		= 127,
};

struct nvdimm_spa_dev {
	int			spa_domain;
	uint64_t		spa_phys_base;
	uint64_t		spa_len;
	uint64_t		spa_efi_mem_flags;
	void			*spa_kva;
	struct vm_object	*spa_obj;
	struct cdev		*spa_dev;
	struct g_geom		*spa_g;
};

struct g_spa {
	struct nvdimm_spa_dev	*dev;
	struct g_provider	*spa_p;
	struct bio_queue_head	spa_g_queue;
	struct mtx		spa_g_mtx;
	struct mtx		spa_g_stat_mtx;
	struct devstat		*spa_g_devstat;
	struct proc		*spa_g_proc;
	bool			spa_g_proc_run;
	bool			spa_g_proc_exiting;
};

struct nvdimm_namespace {
	SLIST_ENTRY(nvdimm_namespace) link;
	struct SPA_mapping	*spa;
	struct nvdimm_spa_dev	dev;
};

struct SPA_mapping {
	SLIST_ENTRY(SPA_mapping) link;
	enum SPA_mapping_type	spa_type;
	int			spa_nfit_idx;
	struct nvdimm_spa_dev	dev;
	SLIST_HEAD(, nvdimm_namespace) namespaces;
};

MALLOC_DECLARE(M_NVDIMM);

void acpi_nfit_get_dimm_ids(ACPI_TABLE_NFIT *nfitbl, nfit_handle_t **listp,
    int *countp);
void acpi_nfit_get_spa_range(ACPI_TABLE_NFIT *nfitbl, uint16_t range_index,
    ACPI_NFIT_SYSTEM_ADDRESS **spa);
void acpi_nfit_get_spa_ranges(ACPI_TABLE_NFIT *nfitbl,
    ACPI_NFIT_SYSTEM_ADDRESS ***listp, int *countp);
void acpi_nfit_get_region_mappings_by_spa_range(ACPI_TABLE_NFIT *nfitbl,
    uint16_t spa_range_index, ACPI_NFIT_MEMORY_MAP ***listp, int *countp);
void acpi_nfit_get_control_region(ACPI_TABLE_NFIT *nfitbl,
    uint16_t control_region_index, ACPI_NFIT_CONTROL_REGION **out);
void acpi_nfit_get_flush_addrs(ACPI_TABLE_NFIT *nfitbl, nfit_handle_t dimm,
    uint64_t ***listp, int *countp);
enum SPA_mapping_type nvdimm_spa_type_from_uuid(struct uuid *);
struct nvdimm_dev *nvdimm_find_by_handle(nfit_handle_t nv_handle);
int nvdimm_spa_init(struct SPA_mapping *spa, ACPI_NFIT_SYSTEM_ADDRESS *nfitaddr,
    enum SPA_mapping_type spa_type);
void nvdimm_spa_fini(struct SPA_mapping *spa);
int nvdimm_spa_dev_init(struct nvdimm_spa_dev *dev, const char *name);
void nvdimm_spa_dev_fini(struct nvdimm_spa_dev *dev);
int nvdimm_create_namespaces(struct SPA_mapping *spa, ACPI_TABLE_NFIT *nfitbl);
void nvdimm_destroy_namespaces(struct SPA_mapping *spa);

#endif		/* __DEV_NVDIMM_VAR_H__ */
