// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2014 David Gibson <david@gibson.dropbear.id.au>
 * Copyright (C) 2018 embedded brains GmbH
 */
#include "libfdt_env.h"

#include <fdt.h>
#include <libfdt.h>

#include "libfdt_internal.h"

static int fdt_cells(const void *fdt, int nodeoffset, const char *name)
{
	const fdt32_t *c;
	uint32_t val;
	int len;

	c = fdt_getprop(fdt, nodeoffset, name, &len);
	if (!c)
		return len;

	if (len != sizeof(*c))
		return -FDT_ERR_BADNCELLS;

	val = fdt32_to_cpu(*c);
	if (val > FDT_MAX_NCELLS)
		return -FDT_ERR_BADNCELLS;

	return (int)val;
}

int fdt_address_cells(const void *fdt, int nodeoffset)
{
	int val;

	val = fdt_cells(fdt, nodeoffset, "#address-cells");
	if (val == 0)
		return -FDT_ERR_BADNCELLS;
	if (val == -FDT_ERR_NOTFOUND)
		return 2;
	return val;
}

int fdt_size_cells(const void *fdt, int nodeoffset)
{
	int val;

	val = fdt_cells(fdt, nodeoffset, "#size-cells");
	if (val == -FDT_ERR_NOTFOUND)
		return 1;
	return val;
}

/* This function assumes that [address|size]_cells is 1 or 2 */
int fdt_appendprop_addrrange(void *fdt, int parent, int nodeoffset,
			     const char *name, uint64_t addr, uint64_t size)
{
	int addr_cells, size_cells, ret;
	uint8_t data[sizeof(fdt64_t) * 2], *prop;

	ret = fdt_address_cells(fdt, parent);
	if (ret < 0)
		return ret;
	addr_cells = ret;

	ret = fdt_size_cells(fdt, parent);
	if (ret < 0)
		return ret;
	size_cells = ret;

	/* check validity of address */
	prop = data;
	if (addr_cells == 1) {
		if ((addr > UINT32_MAX) || ((UINT32_MAX + 1 - addr) < size))
			return -FDT_ERR_BADVALUE;

		fdt32_st(prop, (uint32_t)addr);
	} else if (addr_cells == 2) {
		fdt64_st(prop, addr);
	} else {
		return -FDT_ERR_BADNCELLS;
	}

	/* check validity of size */
	prop += addr_cells * sizeof(fdt32_t);
	if (size_cells == 1) {
		if (size > UINT32_MAX)
			return -FDT_ERR_BADVALUE;

		fdt32_st(prop, (uint32_t)size);
	} else if (size_cells == 2) {
		fdt64_st(prop, size);
	} else {
		return -FDT_ERR_BADNCELLS;
	}

	return fdt_appendprop(fdt, nodeoffset, name, data,
			      (addr_cells + size_cells) * sizeof(fdt32_t));
}
