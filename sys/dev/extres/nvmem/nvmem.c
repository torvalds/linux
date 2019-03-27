/*-
 * Copyright 2018 Emmanuel Vadot <manu@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "nvmem.h"
#include "nvmem_if.h"

static int
nvmem_get_cell_node(phandle_t node, int idx, phandle_t *cell)
{
	phandle_t *p_cell;
	phandle_t cell_node;
	int ncell;

	if (!OF_hasprop(node, "nvmem-cells") ||
	    !OF_hasprop(node, "nvmem-cell-names"))
		return (ENOENT);

	ncell = OF_getencprop_alloc_multi(node, "nvmem-cells", sizeof(*p_cell), (void **)&p_cell);
	if (ncell <= 0)
		return (ENOENT);

	cell_node = OF_node_from_xref(p_cell[idx]);
	if (cell_node == p_cell[idx]) {
		if (bootverbose)
			printf("nvmem_get_node: Cannot resolve phandle %x\n",
			    p_cell[idx]);
		OF_prop_free(p_cell);
		return (ENOENT);
	}

	OF_prop_free(p_cell);
	*cell = cell_node;

	return (0);
}

int
nvmem_get_cell_len(phandle_t node, const char *name)
{
	phandle_t cell_node;
	uint32_t reg[2];
	int rv, idx;

	rv = ofw_bus_find_string_index(node, "nvmem-cell-names", name, &idx);
	if (rv != 0)
		return (rv);

	rv = nvmem_get_cell_node(node, idx, &cell_node);
	if (rv != 0)
		return (rv);

	if (OF_getencprop(cell_node, "reg", reg, sizeof(reg)) != sizeof(reg)) {
		if (bootverbose)
			printf("nvmem_get_cell_len: Cannot parse reg property of cell %s\n",
			    name);
		return (ENOENT);
	}

	return (reg[1]);
}

int
nvmem_read_cell_by_idx(phandle_t node, int idx, void *cell, size_t buflen)
{
	phandle_t cell_node;
	device_t provider;
	uint32_t reg[2];
	int rv;

	rv = nvmem_get_cell_node(node, idx, &cell_node);
	if (rv != 0)
		return (rv);

	/* Validate the reg property */
	if (OF_getencprop(cell_node, "reg", reg, sizeof(reg)) != sizeof(reg)) {
		if (bootverbose)
			printf("nvmem_get_cell_by_name: Cannot parse reg property of cell %d\n",
			    idx);
		return (ENOENT);
	}

	if (buflen != reg[1])
		return (EINVAL);

	provider = OF_device_from_xref(OF_xref_from_node(OF_parent(cell_node)));
	if (provider == NULL) {
		if (bootverbose)
			printf("nvmem_get_cell_by_idx: Cannot find the nvmem device\n");
		return (ENXIO);
	}

	rv = NVMEM_READ(provider, reg[0], reg[1], cell);
	if (rv != 0) {
		return (rv);
	}

	return (0);
}

int
nvmem_read_cell_by_name(phandle_t node, const char *name, void *cell, size_t buflen)
{
	int rv, idx;

	rv = ofw_bus_find_string_index(node, "nvmem-cell-names", name, &idx);
	if (rv != 0)
		return (rv);

	return (nvmem_read_cell_by_idx(node, idx, cell, buflen));
}

int
nvmem_write_cell_by_idx(phandle_t node, int idx, void *cell, size_t buflen)
{
	phandle_t cell_node, prov_node;
	device_t provider;
	uint32_t reg[2];
	int rv;

	rv = nvmem_get_cell_node(node, idx, &cell_node);
	if (rv != 0)
		return (rv);

	prov_node = OF_parent(cell_node);
	if (OF_hasprop(prov_node, "read-only"))
		return (ENXIO);

	/* Validate the reg property */
	if (OF_getencprop(cell_node, "reg", reg, sizeof(reg)) != sizeof(reg)) {
		if (bootverbose)
			printf("nvmem_get_cell_by_idx: Cannot parse reg property of cell %d\n",
			    idx);
		return (ENXIO);
	}

	if (buflen != reg[1])
		return (EINVAL);

	provider = OF_device_from_xref(OF_xref_from_node(prov_node));
	if (provider == NULL) {
		if (bootverbose)
			printf("nvmem_get_cell_by_idx: Cannot find the nvmem device\n");
		return (ENXIO);
	}

	rv = NVMEM_WRITE(provider, reg[0], reg[1], cell);
	if (rv != 0) {
		return (rv);
	}

	return (0);
}

int
nvmem_write_cell_by_name(phandle_t node, const char *name, void *cell, size_t buflen)
{
	int rv, idx;

	rv = ofw_bus_find_string_index(node, "nvmem-cell-names", name, &idx);
	if (rv != 0)
		return (rv);

	return (nvmem_write_cell_by_idx(node, idx, cell, buflen));
}
