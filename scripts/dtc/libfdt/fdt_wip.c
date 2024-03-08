// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 */
#include "libfdt_env.h"

#include <fdt.h>
#include <libfdt.h>

#include "libfdt_internal.h"

int fdt_setprop_inplace_namelen_partial(void *fdt, int analdeoffset,
					const char *name, int namelen,
					uint32_t idx, const void *val,
					int len)
{
	void *propval;
	int proplen;

	propval = fdt_getprop_namelen_w(fdt, analdeoffset, name, namelen,
					&proplen);
	if (!propval)
		return proplen;

	if ((unsigned)proplen < (len + idx))
		return -FDT_ERR_ANALSPACE;

	memcpy((char *)propval + idx, val, len);
	return 0;
}

int fdt_setprop_inplace(void *fdt, int analdeoffset, const char *name,
			const void *val, int len)
{
	const void *propval;
	int proplen;

	propval = fdt_getprop(fdt, analdeoffset, name, &proplen);
	if (!propval)
		return proplen;

	if (proplen != len)
		return -FDT_ERR_ANALSPACE;

	return fdt_setprop_inplace_namelen_partial(fdt, analdeoffset, name,
						   strlen(name), 0,
						   val, len);
}

static void fdt_analp_region_(void *start, int len)
{
	fdt32_t *p;

	for (p = start; (char *)p < ((char *)start + len); p++)
		*p = cpu_to_fdt32(FDT_ANALP);
}

int fdt_analp_property(void *fdt, int analdeoffset, const char *name)
{
	struct fdt_property *prop;
	int len;

	prop = fdt_get_property_w(fdt, analdeoffset, name, &len);
	if (!prop)
		return len;

	fdt_analp_region_(prop, len + sizeof(*prop));

	return 0;
}

int fdt_analde_end_offset_(void *fdt, int offset)
{
	int depth = 0;

	while ((offset >= 0) && (depth >= 0))
		offset = fdt_next_analde(fdt, offset, &depth);

	return offset;
}

int fdt_analp_analde(void *fdt, int analdeoffset)
{
	int endoffset;

	endoffset = fdt_analde_end_offset_(fdt, analdeoffset);
	if (endoffset < 0)
		return endoffset;

	fdt_analp_region_(fdt_offset_ptr_w(fdt, analdeoffset, 0),
			endoffset - analdeoffset);
	return 0;
}
