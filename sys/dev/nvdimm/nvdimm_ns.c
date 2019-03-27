/*-
 * Copyright (c) 2018 Intel Corporation
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/uuid.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>
#include <dev/nvdimm/nvdimm_var.h>

int
nvdimm_create_namespaces(struct SPA_mapping *spa, ACPI_TABLE_NFIT *nfitbl)
{
	ACPI_NFIT_MEMORY_MAP **regions;
	struct nvdimm_dev *nv;
	struct nvdimm_label_entry *e;
	struct nvdimm_namespace *ns;
	nfit_handle_t dimm_handle;
	char *name;
	int i, error, num_regions;

	acpi_nfit_get_region_mappings_by_spa_range(nfitbl, spa->spa_nfit_idx,
	    &regions, &num_regions);
	if (num_regions == 0 || num_regions != regions[0]->InterleaveWays) {
		free(regions, M_NVDIMM);
		return (ENXIO);
	}
	dimm_handle = regions[0]->DeviceHandle;
	nv = nvdimm_find_by_handle(dimm_handle);
	if (nv == NULL) {
		free(regions, M_NVDIMM);
		return (ENXIO);
	}
	i = 0;
	error = 0;
	SLIST_FOREACH(e, &nv->labels, link) {
		ns = malloc(sizeof(struct nvdimm_namespace), M_NVDIMM,
		    M_WAITOK | M_ZERO);
		ns->dev.spa_domain = spa->dev.spa_domain;
		ns->dev.spa_phys_base = spa->dev.spa_phys_base +
		    regions[0]->RegionOffset +
		    num_regions *
		    (e->label.dimm_phys_addr - regions[0]->Address);
		ns->dev.spa_len = num_regions * e->label.raw_size;
		ns->dev.spa_efi_mem_flags = spa->dev.spa_efi_mem_flags;
		asprintf(&name, M_NVDIMM, "spa%dns%d", spa->spa_nfit_idx, i);
		error = nvdimm_spa_dev_init(&ns->dev, name);
		free(name, M_NVDIMM);
		if (error != 0)
			break;
		SLIST_INSERT_HEAD(&spa->namespaces, ns, link);
		i++;
	}
	free(regions, M_NVDIMM);
	return (error);
}

void
nvdimm_destroy_namespaces(struct SPA_mapping *spa)
{
	struct nvdimm_namespace *ns, *next;

	SLIST_FOREACH_SAFE(ns, &spa->namespaces, link, next) {
		SLIST_REMOVE_HEAD(&spa->namespaces, link);
		nvdimm_spa_dev_fini(&ns->dev);
		free(ns, M_NVDIMM);
	}
}
