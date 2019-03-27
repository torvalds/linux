/*-
 * Copyright (C) 2014-2015 Nathan Whitehorn
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include <sys/param.h>
#include <fdt_platform.h>
#include <openfirm.h>
#include <libfdt.h>
#include "bootstrap.h"

extern int command_fdt_internal(int argc, char *argv[]);

static int
OF_hasprop(phandle_t node, const char *prop)
{
	return (OF_getproplen(node, (char *)prop) > 0);
}

static void
add_node_to_fdt(void *buffer, phandle_t node, int fdt_offset)
{
	int i, child_offset, error;
	char name[255], *lastprop, *subname;
	void *propbuf;
	ssize_t proplen;

	lastprop = NULL;
	while (OF_nextprop(node, lastprop, name) > 0) {
		proplen = OF_getproplen(node, name);

		/* Detect and correct for errors and strangeness */
		if (proplen < 0)
			proplen = 0;
		if (proplen > 1024)
			proplen = 1024;

		propbuf = malloc(proplen);
		if (propbuf == NULL) {
			printf("Cannot allocate memory for prop %s\n", name);
			return;
		}
		OF_getprop(node, name, propbuf, proplen);
		error = fdt_setprop(buffer, fdt_offset, name, propbuf, proplen);
		free(propbuf);
		lastprop = name;
		if (error)
			printf("Error %d adding property %s to "
			    "node %d\n", error, name, fdt_offset);
	}

	if (!OF_hasprop(node, "phandle") && !OF_hasprop(node, "linux,phandle")
	    && !OF_hasprop(node, "ibm,phandle"))
		fdt_setprop(buffer, fdt_offset, "phandle", &node, sizeof(node));

	for (node = OF_child(node); node > 0; node = OF_peer(node)) {
		OF_package_to_path(node, name, sizeof(name));
		subname = strrchr(name, '/');
		subname++;
		child_offset = fdt_add_subnode(buffer, fdt_offset, subname);
		if (child_offset < 0) {
			printf("Error %d adding node %s (%s), skipping\n",
			    child_offset, name, subname);
			continue;
		}
	
                add_node_to_fdt(buffer, node, child_offset);
	}
}

static void
ofwfdt_fixups(void *fdtp)
{
	int offset, len, i;
	phandle_t node;
	ihandle_t rtas;
	const void *prop;

	/*
	 * Instantiate and add reservations for RTAS state if present
	 */

	offset = fdt_path_offset(fdtp, "/rtas");
	if (offset > 0) {
		uint32_t base;
		void *rtasmem;
		char path[255];

		node = OF_finddevice("/rtas");
		OF_package_to_path(node, path, sizeof(path));
		OF_getprop(node, "rtas-size", &len, sizeof(len));

		/* Allocate memory */
		rtasmem = OF_claim(0, len, 4096);

		/* Instantiate RTAS */
		rtas = OF_open(path);
		base = 0;
		OF_call_method("instantiate-rtas", rtas, 1, 1, (cell_t)rtas,
		    &base);

		/* Store info to FDT using Linux convention */
		base = cpu_to_fdt32(base);
		fdt_setprop(fdtp, offset, "linux,rtas-entry", &base,
		    sizeof(base));
		base = cpu_to_fdt32((uint32_t)rtasmem);
		offset = fdt_path_offset(fdtp, "/rtas");
		fdt_setprop(fdtp, offset, "linux,rtas-base", &base,
		    sizeof(base));

		/* Mark RTAS private data area reserved */
		fdt_add_mem_rsv(fdtp, base, len);
	} else {
		/*
		 * Remove /memory/available properties, which reflect long-gone
		 * OF state. Note that this doesn't work if we need RTAS still,
		 * since that's part of the firmware.
		 */
		offset = fdt_path_offset(fdtp, "/memory@0");
		if (offset > 0)
			fdt_delprop(fdtp, offset, "available");
	}

	
	/*
	 * Convert stored ihandles under /chosen to xref phandles
	 */
	offset = fdt_path_offset(fdtp, "/chosen");
	if (offset > 0) {
		const char *chosenprops[] = {"stdout", "stdin", "mmu", "cpu",
		    NULL};
		const uint32_t *ihand;
		for (i = 0; chosenprops[i] != NULL; i++) {
			ihand = fdt_getprop(fdtp, offset, chosenprops[i], &len);
			if (ihand != NULL && len == sizeof(*ihand)) {
				node = OF_instance_to_package(
				    fdt32_to_cpu(*ihand));
				if (OF_hasprop(node, "phandle"))
					OF_getprop(node, "phandle", &node,
					    sizeof(node));
				else if (OF_hasprop(node, "linux,phandle"))
					OF_getprop(node, "linux,phandle", &node,
					    sizeof(node));
				else if (OF_hasprop(node, "ibm,phandle"))
					OF_getprop(node, "ibm,phandle", &node,
					    sizeof(node));
				node = cpu_to_fdt32(node);
				fdt_setprop(fdtp, offset, chosenprops[i], &node,
				    sizeof(node));
			}

			/* Refind node in case it moved */
			offset = fdt_path_offset(fdtp, "/chosen");
		}
	}
}

int
fdt_platform_load_dtb(void)
{
        void *buffer;
        size_t buflen = 409600;

        buffer = malloc(buflen);
        fdt_create_empty_tree(buffer, buflen);
        add_node_to_fdt(buffer, OF_peer(0), fdt_path_offset(buffer, "/"));
        ofwfdt_fixups(buffer);
        fdt_pack(buffer);

        fdt_load_dtb_addr(buffer);
        free(buffer);

        return (0);
}

void
fdt_platform_fixups(void)
{

}

static int
command_fdt(int argc, char *argv[])
{
 
	return (command_fdt_internal(argc, argv));
}
 
COMMAND_SET(fdt, "fdt", "flattened device tree handling", command_fdt);

