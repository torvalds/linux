/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Semihalf.
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/slicer.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#ifdef DEBUG
#define debugf(fmt, args...) do { printf("%s(): ", __func__);	\
    printf(fmt,##args); } while (0)
#else
#define debugf(fmt, args...)
#endif

static int fill_slices(device_t dev, const char *provider,
    struct flash_slice *slices, int *slices_num);
static void fdt_slicer_init(void);

static int
fill_slices_from_node(phandle_t node, struct flash_slice *slices, int *count)
{
	char *label;
	phandle_t child;
	u_long base, size;
	int flags, i;
	ssize_t nmlen;

	i = 0;
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		flags = FLASH_SLICES_FLAG_NONE;

		/* Nodes with a compatible property are not slices. */
		if (OF_hasprop(child, "compatible"))
			continue;

		if (i == FLASH_SLICES_MAX_NUM) {
			debugf("not enough buffer for slice i=%d\n", i);
			break;
		}

		/* Retrieve start and size of the slice. */
		if (fdt_regsize(child, &base, &size) != 0) {
			debugf("error during processing reg property, i=%d\n",
			    i);
			continue;
		}

		if (size == 0) {
			debugf("slice i=%d with no size\n", i);
			continue;
		}

		/* Retrieve label. */
		nmlen = OF_getprop_alloc(child, "label", (void **)&label);
		if (nmlen <= 0) {
			/* Use node name if no label defined */
			nmlen = OF_getprop_alloc(child, "name", (void **)&label);
			if (nmlen <= 0) {
				debugf("slice i=%d with no name\n", i);
				label = NULL;
			}
		}

		if (OF_hasprop(child, "read-only"))
			flags |= FLASH_SLICES_FLAG_RO;

		/* Fill slice entry data. */
		slices[i].base = base;
		slices[i].size = size;
		slices[i].label = label;
		slices[i].flags = flags;
		i++;
	}

	*count = i;
	return (0);
}

static int
fill_slices(device_t dev, const char *provider __unused,
    struct flash_slice *slices, int *slices_num)
{
	phandle_t child, node;

	/*
	 * We assume the caller provides buffer for FLASH_SLICES_MAX_NUM
	 * flash_slice structures.
	 */
	if (slices == NULL) {
		*slices_num = 0;
		return (ENOMEM);
	}

	node = ofw_bus_get_node(dev);

	/*
	 * If there is a child node whose compatible is "fixed-partitions" then
	 * we have new-style data where all partitions are the children of that
	 * node.  Otherwise we have old-style data where all the children of the
	 * device node are the partitions.
	 */
	child = fdt_find_compatible(node, "fixed-partitions", false);
	if (child == 0)
		return fill_slices_from_node(node, slices, slices_num);
	else
		return fill_slices_from_node(child, slices, slices_num);
}

static void
fdt_slicer_init(void)
{

	flash_register_slicer(fill_slices, FLASH_SLICES_TYPE_NAND, false);
	flash_register_slicer(fill_slices, FLASH_SLICES_TYPE_CFI, false);
	flash_register_slicer(fill_slices, FLASH_SLICES_TYPE_SPI, false);
}

static void
fdt_slicer_cleanup(void)
{

	flash_register_slicer(NULL, FLASH_SLICES_TYPE_NAND, true);
	flash_register_slicer(NULL, FLASH_SLICES_TYPE_CFI, true);
	flash_register_slicer(NULL, FLASH_SLICES_TYPE_SPI, true);
}

/*
 * Must be initialized after GEOM classes (SI_SUB_DRIVERS/SI_ORDER_FIRST),
 * i. e. after g_init() is called, due to the use of the GEOM topology_lock
 * in flash_register_slicer().  However, must be before SI_SUB_CONFIGURE.
 */
SYSINIT(fdt_slicer, SI_SUB_DRIVERS, SI_ORDER_SECOND, fdt_slicer_init, NULL);
SYSUNINIT(fdt_slicer, SI_SUB_DRIVERS, SI_ORDER_SECOND, fdt_slicer_cleanup, NULL);

static int
mod_handler(module_t mod, int type, void *data)
{

	/*
	 * Nothing to do here: the SYSINIT/SYSUNINIT defined above run
	 * automatically at module load/unload time.
	 */
	return (0);
}

static moduledata_t fdt_slicer_mod = {
	"fdt_slicer", mod_handler, NULL
};

DECLARE_MODULE(fdt_slicer, fdt_slicer_mod, SI_SUB_DRIVERS, SI_ORDER_SECOND);
MODULE_DEPEND(fdt_slicer, g_flashmap, 0, 0, 0);
MODULE_VERSION(fdt_slicer, 1);
