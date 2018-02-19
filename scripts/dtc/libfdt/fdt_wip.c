/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 *
 * libfdt is dual licensed: you can use it either under the terms of
 * the GPL, or the BSD license, at your option.
 *
 *  a) This library is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of the
 *     License, or (at your option) any later version.
 *
 *     This library is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this library; if not, write to the Free
 *     Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 *     MA 02110-1301 USA
 *
 * Alternatively,
 *
 *  b) Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *     1. Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *     2. Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 *     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "libfdt_env.h"

#include <fdt.h>
#include <libfdt.h>

#include "libfdt_internal.h"

int fdt_setprop_inplace_namelen_partial(void *fdt, int nodeoffset,
					const char *name, int namelen,
					uint32_t idx, const void *val,
					int len)
{
	void *propval;
	int proplen;

	propval = fdt_getprop_namelen_w(fdt, nodeoffset, name, namelen,
					&proplen);
	if (!propval)
		return proplen;

	if (proplen < (len + idx))
		return -FDT_ERR_NOSPACE;

	memcpy((char *)propval + idx, val, len);
	return 0;
}

int fdt_setprop_inplace(void *fdt, int nodeoffset, const char *name,
			const void *val, int len)
{
	const void *propval;
	int proplen;

	propval = fdt_getprop(fdt, nodeoffset, name, &proplen);
	if (!propval)
		return proplen;

	if (proplen != len)
		return -FDT_ERR_NOSPACE;

	return fdt_setprop_inplace_namelen_partial(fdt, nodeoffset, name,
						   strlen(name), 0,
						   val, len);
}

static void _fdt_nop_region(void *start, int len)
{
	fdt32_t *p;

	for (p = start; (char *)p < ((char *)start + len); p++)
		*p = cpu_to_fdt32(FDT_NOP);
}

int fdt_nop_property(void *fdt, int nodeoffset, const char *name)
{
	struct fdt_property *prop;
	int len;

	prop = fdt_get_property_w(fdt, nodeoffset, name, &len);
	if (!prop)
		return len;

	_fdt_nop_region(prop, len + sizeof(*prop));

	return 0;
}

int _fdt_node_end_offset(void *fdt, int offset)
{
	int depth = 0;

	while ((offset >= 0) && (depth >= 0))
		offset = fdt_next_node(fdt, offset, &depth);

	return offset;
}

int fdt_nop_node(void *fdt, int nodeoffset)
{
	int endoffset;

	endoffset = _fdt_node_end_offset(fdt, nodeoffset);
	if (endoffset < 0)
		return endoffset;

	_fdt_nop_region(fdt_offset_ptr_w(fdt, nodeoffset, 0),
			endoffset - nodeoffset);
	return 0;
}
