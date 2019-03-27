/*-
 * Copyright (c) 1998 Doug Rabson
 * Copyright (c) 2000 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
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
 *
 *	$FreeBSD$
 */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <paths.h>
#include <devinfo.h>

#include "acpidump.h"

static void	acpi_handle_apic(struct ACPIsdt *sdp);
static struct ACPIsdt *acpi_map_sdt(vm_offset_t pa);
static void	acpi_handle_rsdt(struct ACPIsdt *rsdp);
static struct acpi_user_mapping *acpi_user_find_mapping(vm_offset_t, size_t);
static void *	acpi_map_physical(vm_offset_t, size_t);

/* Size of an address. 32-bit for ACPI 1.0, 64-bit for ACPI 2.0 and up. */
static int addr_size;

static int ncpu;

int acpi_detect(void);

static void
acpi_handle_apic(struct ACPIsdt *sdp)
{
	struct MADTbody *madtp;
	struct MADT_APIC *mp;
	struct MADT_local_apic *apic;
	struct MADT_local_sapic *sapic;

	madtp = (struct MADTbody *) sdp->body;
	mp = (struct MADT_APIC *)madtp->body;
	while (((uintptr_t)mp) - ((uintptr_t)sdp) < sdp->len) {
		switch (mp->type) {
		case ACPI_MADT_APIC_TYPE_LOCAL_APIC:
			apic = &mp->body.local_apic;
			warnx("MADT: Found CPU APIC ID %d %s",
			    apic->cpu_id,
			    apic->flags & ACPI_MADT_APIC_LOCAL_FLAG_ENABLED ?
				"enabled" : "disabled");
			if (apic->flags & ACPI_MADT_APIC_LOCAL_FLAG_ENABLED)
				ncpu++;
			break;
		case ACPI_MADT_APIC_TYPE_LOCAL_SAPIC:
			sapic = &mp->body.local_sapic;
			warnx("MADT: Found CPU SAPIC ID %d %s",
			    sapic->cpu_id,
			    sapic->flags & ACPI_MADT_APIC_LOCAL_FLAG_ENABLED ?
				"enabled" : "disabled");
			/* XXX is enable flag the same? */
			if (sapic->flags & ACPI_MADT_APIC_LOCAL_FLAG_ENABLED)
				ncpu++;
			break;
		default:
			break;
		}
		mp = (struct MADT_APIC *) ((char *)mp + mp->len);
	}
}

static int
acpi_checksum(void *p, size_t length)
{
	u_int8_t *bp;
	u_int8_t sum;

	bp = p;
	sum = 0;
	while (length--)
		sum += *bp++;

	return (sum);
}

static struct ACPIsdt *
acpi_map_sdt(vm_offset_t pa)
{
	struct	ACPIsdt *sp;

	sp = acpi_map_physical(pa, sizeof(struct ACPIsdt));
	sp = acpi_map_physical(pa, sp->len);
	return (sp);
}

static void
acpi_handle_rsdt(struct ACPIsdt *rsdp)
{
	struct ACPIsdt *sdp;
	vm_offset_t addr;
	int entries, i;

	entries = (rsdp->len - SIZEOF_SDT_HDR) / addr_size;
	for (i = 0; i < entries; i++) {
		switch (addr_size) {
		case 4:
			addr = le32dec((char*)rsdp->body + i * addr_size);
			break;
		case 8:
			addr = le64dec((char*)rsdp->body + i * addr_size);
			break;
		default:
			assert((addr = 0));
		}

		sdp = (struct ACPIsdt *)acpi_map_sdt(addr);
		if (acpi_checksum(sdp, sdp->len)) {
#if 0
			warnx("RSDT entry %d (sig %.4s) has bad checksum", i,
			    sdp->signature);
#endif
			continue;
		}
		if (!memcmp(sdp->signature, "APIC", 4))
			acpi_handle_apic(sdp);
	}
}

static char	machdep_acpi_root[] = "machdep.acpi_root";
static int      acpi_mem_fd = -1;

struct acpi_user_mapping {
	LIST_ENTRY(acpi_user_mapping) link;
	vm_offset_t     pa;
	caddr_t         va;
	size_t          size;
};

LIST_HEAD(acpi_user_mapping_list, acpi_user_mapping) maplist;

static void
acpi_user_init(void)
{

	if (acpi_mem_fd == -1) {
		acpi_mem_fd = open(_PATH_MEM, O_RDONLY);
		if (acpi_mem_fd == -1)
			err(1, "opening " _PATH_MEM);
		LIST_INIT(&maplist);
	}
}

static struct acpi_user_mapping *
acpi_user_find_mapping(vm_offset_t pa, size_t size)
{
	struct	acpi_user_mapping *map;

	/* First search for an existing mapping */
	for (map = LIST_FIRST(&maplist); map; map = LIST_NEXT(map, link)) {
		if (map->pa <= pa && map->size >= pa + size - map->pa)
			return (map);
	}

	/* Then create a new one */
	size = round_page(pa + size) - trunc_page(pa);
	pa = trunc_page(pa);
	map = malloc(sizeof(struct acpi_user_mapping));
	if (!map)
		errx(1, "out of memory");
	map->pa = pa;
	map->va = mmap(0, size, PROT_READ, MAP_SHARED, acpi_mem_fd, pa);
	map->size = size;
	if ((intptr_t) map->va == -1)
		err(1, "can't map address");
	LIST_INSERT_HEAD(&maplist, map, link);

	return (map);
}

static void *
acpi_map_physical(vm_offset_t pa, size_t size)
{
	struct	acpi_user_mapping *map;

	map = acpi_user_find_mapping(pa, size);
	return (map->va + (pa - map->pa));
}

static struct ACPIrsdp *
acpi_get_rsdp(u_long addr)
{
	struct ACPIrsdp rsdp;
	size_t len;

	/* Read in the table signature and check it. */
	pread(acpi_mem_fd, &rsdp, 8, addr);
	if (memcmp(rsdp.signature, "RSD PTR ", 8))
		return (NULL);

	/* Read the entire table. */
	pread(acpi_mem_fd, &rsdp, sizeof(rsdp), addr);

	/* Run the checksum only over the version 1 header. */
	if (acpi_checksum(&rsdp, 20))
		return (NULL);

	/* If the revision is 0, assume a version 1 length. */
	if (rsdp.revision == 0)
		len = 20;
	else
		len = rsdp.length;

	/* XXX Should handle ACPI 2.0 RSDP extended checksum here. */

	return (acpi_map_physical(addr, len));
}

static const char *
devstate(devinfo_state_t state)
{
	switch (state) {
	case DS_NOTPRESENT:
		return "not-present";
	case DS_ALIVE:
		return "alive";
	case DS_ATTACHED:
		return "attached";
	case DS_BUSY:
		return "busy";
	default:
		return "unknown-state";
	}
}

static int
acpi0_check(struct devinfo_dev *dd, void *arg)
{
	printf("%s: %s %s\n", __func__, dd->dd_name, devstate(dd->dd_state));
	/* NB: device must be present AND attached */
	if (strcmp(dd->dd_name, "acpi0") == 0)
		return (dd->dd_state == DS_ATTACHED ||
			dd->dd_state == DS_BUSY);
	return devinfo_foreach_device_child(dd, acpi0_check, arg);
}

static int
acpi0_present(void)
{
	struct devinfo_dev *root;
	int found;

	found = 0;
	devinfo_init();
	root = devinfo_handle_to_device(DEVINFO_ROOT_DEVICE);
	if (root != NULL)
		found = devinfo_foreach_device_child(root, acpi0_check, NULL);
	devinfo_free();
	return found;
}

int
acpi_detect(void)
{
	struct ACPIrsdp *rp;
	struct ACPIsdt *rsdp;
	u_long addr;
	size_t len;

	if (!acpi0_present()) {
		warnx("no acpi0 device located");
		return -1;
	}

	acpi_user_init();

	/* Attempt to use sysctl to find RSD PTR record. */
	len = sizeof(addr);
	if (sysctlbyname(machdep_acpi_root, &addr, &len, NULL, 0) != 0) {
		warnx("cannot find ACPI information");
		return -1;
	}
	rp = acpi_get_rsdp(addr);
	if (rp == NULL) {
		warnx("cannot find ACPI information: sysctl %s does not point to RSDP",
			machdep_acpi_root);
		return -1;
	}
	if (rp->revision < 2) {
		rsdp = (struct ACPIsdt *)acpi_map_sdt(rp->rsdt_addr);
		if (memcmp(rsdp->signature, "RSDT", 4) != 0 ||
		    acpi_checksum(rsdp, rsdp->len) != 0)
			errx(1, "RSDT is corrupted");
		addr_size = sizeof(uint32_t);
	} else {
		rsdp = (struct ACPIsdt *)acpi_map_sdt(rp->xsdt_addr);
		if (memcmp(rsdp->signature, "XSDT", 4) != 0 ||
		    acpi_checksum(rsdp, rsdp->len) != 0)
			errx(1, "XSDT is corrupted");
		addr_size = sizeof(uint64_t);
	}
	ncpu = 0;
	acpi_handle_rsdt(rsdp);
	return (ncpu == 0 ? 1 : ncpu);
}
