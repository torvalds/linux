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

static int
uint32_t_compare(const void *a, const void *b)
{

	return (*(const uint32_t *)a - *(const uint32_t *)b);
}

static int
find_matches(ACPI_TABLE_NFIT *nfitbl, uint16_t type, uint16_t offset,
    uint64_t mask, uint64_t value, void **ptrs, int ptrs_len)
{
	ACPI_NFIT_HEADER *h, *end;
	uint64_t val;
	size_t load_size;
	int count;

	h = (ACPI_NFIT_HEADER *)(nfitbl + 1);
	end = (ACPI_NFIT_HEADER *)((char *)nfitbl +
	    nfitbl->Header.Length);
	load_size = roundup2(flsl(mask), 8) / 8;
	count = 0;

	while (h < end) {
		if (h->Type == type) {
			bcopy((char *)h + offset, &val, load_size);
			val &= mask;
			if (val == value) {
				if (ptrs_len > 0) {
					ptrs[count] = h;
					ptrs_len--;
				}
				count++;
			}
		}
		if (h->Length == 0)
			break;
		h = (ACPI_NFIT_HEADER *)((char *)h + h->Length);
	}
	return (count);
}

static void
malloc_find_matches(ACPI_TABLE_NFIT *nfitbl, uint16_t type, uint16_t offset,
    uint64_t mask, uint64_t value, void ***ptrs, int *ptrs_len)
{
	int count;

	count = find_matches(nfitbl, type, offset, mask, value, NULL, 0);
	*ptrs_len = count;
	if (count == 0) {
		*ptrs = NULL;
		return;
	}
	*ptrs = mallocarray(count, sizeof(void *), M_NVDIMM, M_WAITOK);
	find_matches(nfitbl, type, offset, mask, value, *ptrs, *ptrs_len);
}

void
acpi_nfit_get_dimm_ids(ACPI_TABLE_NFIT *nfitbl, nfit_handle_t **listp,
    int *countp)
{
	ACPI_NFIT_SYSTEM_ADDRESS **spas;
	ACPI_NFIT_MEMORY_MAP ***regions;
	int i, j, k, maxids, num_spas, *region_counts;

	acpi_nfit_get_spa_ranges(nfitbl, &spas, &num_spas);
	if (num_spas == 0) {
		*listp = NULL;
		*countp = 0;
		return;
	}
	regions = mallocarray(num_spas, sizeof(uint16_t *), M_NVDIMM,
	    M_WAITOK);
	region_counts = mallocarray(num_spas, sizeof(int), M_NVDIMM, M_WAITOK);
	for (i = 0; i < num_spas; i++) {
		acpi_nfit_get_region_mappings_by_spa_range(nfitbl,
		    spas[i]->RangeIndex, &regions[i], &region_counts[i]);
	}
	maxids = 0;
	for (i = 0; i < num_spas; i++) {
		maxids += region_counts[i];
	}
	*listp = mallocarray(maxids, sizeof(nfit_handle_t), M_NVDIMM, M_WAITOK);
	k = 0;
	for (i = 0; i < num_spas; i++) {
		for (j = 0; j < region_counts[i]; j++)
			(*listp)[k++] = regions[i][j]->DeviceHandle;
	}
	qsort((*listp), maxids, sizeof(uint32_t), uint32_t_compare);
	i = 0;
	for (j = 1; j < maxids; j++) {
		if ((*listp)[i] != (*listp)[j])
			(*listp)[++i] = (*listp)[j];
	}
	*countp = i + 1;
	free(region_counts, M_NVDIMM);
	for (i = 0; i < num_spas; i++)
		free(regions[i], M_NVDIMM);
	free(regions, M_NVDIMM);
	free(spas, M_NVDIMM);
}

void
acpi_nfit_get_spa_range(ACPI_TABLE_NFIT *nfitbl, uint16_t range_index,
    ACPI_NFIT_SYSTEM_ADDRESS **spa)
{

	*spa = NULL;
	find_matches(nfitbl, ACPI_NFIT_TYPE_SYSTEM_ADDRESS,
	    offsetof(ACPI_NFIT_SYSTEM_ADDRESS, RangeIndex), UINT16_MAX,
	    range_index, (void **)spa, 1);
}

void
acpi_nfit_get_spa_ranges(ACPI_TABLE_NFIT *nfitbl,
    ACPI_NFIT_SYSTEM_ADDRESS ***listp, int *countp)
{

	malloc_find_matches(nfitbl, ACPI_NFIT_TYPE_SYSTEM_ADDRESS, 0, 0, 0,
	    (void ***)listp, countp);
}

void
acpi_nfit_get_region_mappings_by_spa_range(ACPI_TABLE_NFIT *nfitbl,
    uint16_t spa_range_index, ACPI_NFIT_MEMORY_MAP ***listp, int *countp)
{

	malloc_find_matches(nfitbl, ACPI_NFIT_TYPE_MEMORY_MAP,
	    offsetof(ACPI_NFIT_MEMORY_MAP, RangeIndex), UINT16_MAX,
	    spa_range_index, (void ***)listp, countp);
}

void acpi_nfit_get_control_region(ACPI_TABLE_NFIT *nfitbl,
    uint16_t control_region_index, ACPI_NFIT_CONTROL_REGION **out)
{

	*out = NULL;
	find_matches(nfitbl, ACPI_NFIT_TYPE_CONTROL_REGION,
	    offsetof(ACPI_NFIT_CONTROL_REGION, RegionIndex), UINT16_MAX,
	    control_region_index, (void **)out, 1);
}

void
acpi_nfit_get_flush_addrs(ACPI_TABLE_NFIT *nfitbl, nfit_handle_t dimm,
    uint64_t ***listp, int *countp)
{
	ACPI_NFIT_FLUSH_ADDRESS *subtable;
	int i;

	subtable = NULL;
	find_matches(nfitbl, ACPI_NFIT_TYPE_FLUSH_ADDRESS,
	    offsetof(ACPI_NFIT_FLUSH_ADDRESS, DeviceHandle), UINT32_MAX,
	    dimm, (void **)&subtable, 1);
	if (subtable == NULL || subtable->HintCount == 0) {
		*listp = NULL;
		*countp = 0;
		return;
	}
	*countp = subtable->HintCount;
	*listp = mallocarray(subtable->HintCount, sizeof(uint64_t *), M_NVDIMM,
	    M_WAITOK);
	for (i = 0; i < subtable->HintCount; i++)
		(*listp)[i] = (uint64_t *)(intptr_t)subtable->HintAddress[i];
}
