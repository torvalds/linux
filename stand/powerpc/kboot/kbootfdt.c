/*-
 * Copyright (C) 2014 Nathan Whitehorn
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

#include <sys/types.h>
#include <fdt_platform.h>
#include <libfdt.h>
#include "bootstrap.h"
#include "host_syscall.h"

static void
add_node_to_fdt(void *buffer, const char *path, int fdt_offset)
{
        int child_offset, fd, pfd, error, dentsize;
	char subpath[512];
	void *propbuf;
	ssize_t proplen;

	struct host_dent {
		unsigned long d_fileno;
		unsigned long d_off;
		unsigned short d_reclen;
		char d_name[];
		/* uint8_t	d_type; */
	};
	char dents[2048];
	struct host_dent *dent;
	int d_type;

	fd = host_open(path, O_RDONLY, 0);
	while (1) {
	    dentsize = host_getdents(fd, dents, sizeof(dents));
	    if (dentsize <= 0)
	      break;
	    for (dent = (struct host_dent *)dents;
	      (char *)dent < dents + dentsize;
	      dent = (struct host_dent *)((void *)dent + dent->d_reclen)) {
		sprintf(subpath, "%s/%s", path, dent->d_name);
		if (strcmp(dent->d_name, ".") == 0 ||
		    strcmp(dent->d_name, "..") == 0)
			continue;
		d_type = *((char *)(dent) + dent->d_reclen - 1);
		if (d_type == 4 /* DT_DIR */) {
			child_offset = fdt_add_subnode(buffer, fdt_offset,
			    dent->d_name);
			if (child_offset < 0) {
				printf("Error %d adding node %s/%s, skipping\n",
				    child_offset, path, dent->d_name);
				continue;
			}
		
			add_node_to_fdt(buffer, subpath, child_offset);
		} else {
			propbuf = malloc(1024);
			proplen = 0;
			pfd = host_open(subpath, O_RDONLY, 0);
			if (pfd > 0) {
				proplen = host_read(pfd, propbuf, 1024);
				host_close(pfd);
			}
			error = fdt_setprop(buffer, fdt_offset, dent->d_name,
			    propbuf, proplen);
			free(propbuf);
			if (error)
				printf("Error %d adding property %s to "
				    "node %d\n", error, dent->d_name,
				    fdt_offset);
		}
	    }
	}

	host_close(fd);
}

/* Fix up wrong values added to the device tree by prom_init() in Linux */

static void
fdt_linux_fixups(void *fdtp)
{
	int offset, len;
	const void *prop;

	/*
	 * Remove /memory/available properties, which reflect long-gone OF
	 * state
	 */

	offset = fdt_path_offset(fdtp, "/memory@0");
	if (offset > 0)
		fdt_delprop(fdtp, offset, "available");

	/*
	 * Add reservations for OPAL and RTAS state if present
	 */

	offset = fdt_path_offset(fdtp, "/ibm,opal");
	if (offset > 0) {
		const uint64_t *base, *size;
		base = fdt_getprop(fdtp, offset, "opal-base-address",
		    &len);
		size = fdt_getprop(fdtp, offset, "opal-runtime-size",
		    &len);
		if (base != NULL && size != NULL)
			fdt_add_mem_rsv(fdtp, fdt64_to_cpu(*base),
			    fdt64_to_cpu(*size));
	}
	offset = fdt_path_offset(fdtp, "/rtas");
	if (offset > 0) {
		const uint32_t *base, *size;
		base = fdt_getprop(fdtp, offset, "linux,rtas-base", &len);
		size = fdt_getprop(fdtp, offset, "rtas-size", &len);
		if (base != NULL && size != NULL)
			fdt_add_mem_rsv(fdtp, fdt32_to_cpu(*base),
			    fdt32_to_cpu(*size));
	}

	/*
	 * Patch up /chosen nodes so that the stored handles mean something,
	 * where possible.
	 */
	offset = fdt_path_offset(fdtp, "/chosen");
	if (offset > 0) {
		fdt_delprop(fdtp, offset, "cpu"); /* This node not meaningful */

		offset = fdt_path_offset(fdtp, "/chosen");
		prop = fdt_getprop(fdtp, offset, "linux,stdout-package", &len);
		if (prop != NULL) {
			fdt_setprop(fdtp, offset, "stdout", prop, len);
			offset = fdt_path_offset(fdtp, "/chosen");
			fdt_setprop(fdtp, offset, "stdin", prop, len);
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
	add_node_to_fdt(buffer, "/proc/device-tree",
	    fdt_path_offset(buffer, "/"));
	fdt_linux_fixups(buffer);

	fdt_pack(buffer);

	fdt_load_dtb_addr(buffer);
	free(buffer);
	
	return (0);
}

void
fdt_platform_fixups(void)
{

}

