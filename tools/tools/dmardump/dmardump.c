/*-
 * Copyright (c) 2016 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
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
#include <sys/endian.h>
#include <sys/pciio.h>
#include <sys/queue.h>
#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <x86/iommu/intel_reg.h>

#include "acpidump.h"

int tflag;
int vflag;

static uint32_t
read_4(char *regs, size_t offset)
{
	return *(uint32_t *)(regs + offset);
}

static uint64_t
read_8(char *regs, size_t offset)
{
	return *(uint64_t *)(regs + offset);
}

static struct pci_conf *
pci_find_conf(int segment, int bus, int slot, int func)
{
	static int pcifd = -1;
	static struct pci_conf conf;
	struct pci_conf_io pc;
	struct pci_match_conf patterns[1];

	if (pcifd == -1) {
		pcifd = open("/dev/pci", O_RDONLY);
		if (pcifd < 0)
			err(1, "Failed to open /dev/pci");
	}

	bzero(&pc, sizeof(pc));
	pc.match_buf_len = sizeof(conf);
	pc.matches = &conf;
	bzero(&patterns, sizeof(patterns));
	patterns[0].pc_sel.pc_domain = segment;
	patterns[0].pc_sel.pc_bus = bus;
	patterns[0].pc_sel.pc_dev = slot;
	patterns[0].pc_sel.pc_func = func;
	patterns[0].flags = PCI_GETCONF_MATCH_DOMAIN |
	    PCI_GETCONF_MATCH_BUS | PCI_GETCONF_MATCH_DEV |
	    PCI_GETCONF_MATCH_FUNC;
	pc.num_patterns = 1;
	pc.pat_buf_len = sizeof(patterns);
	pc.patterns = patterns;
	if (ioctl(pcifd, PCIOCGETCONF, &pc) == -1)
		err(1, "ioctl(PCIOCGETCONF)");

	if (pc.status != PCI_GETCONF_LAST_DEVICE ||
	    pc.num_matches == 0)
		return (NULL);

	return (&conf);
}

static void
dump_context_table(int segment, int bus, uint64_t base_addr)
{
	struct dmar_ctx_entry *ctx;
	struct pci_conf *conf;
	bool printed;
	int idx;

	printed = false;
	ctx = acpi_map_physical(base_addr, DMAR_PAGE_SIZE);
	for (idx = 0; idx < DMAR_CTX_CNT; idx++) {
		if (!(ctx[idx].ctx1 & DMAR_CTX1_P))
			continue;
		if (!printed) {
			printf("\tPCI bus %d:\n", bus);
			printed = true;
		}

		/* Check for ARI device first. */
		conf = pci_find_conf(segment, bus, 0, idx);
		if (conf == NULL)
			conf = pci_find_conf(segment, bus, idx >> 3, idx & 7);
		if (conf != NULL) {
			printf("\t    { %d,%d }", conf->pc_sel.pc_dev,
			    conf->pc_sel.pc_func);
			if (conf->pd_name[0] != '\0')
				printf(" (%s%lu)", conf->pd_name,
				    conf->pd_unit);
		} else
			printf("\t    { %d,%d } (absent)", idx >> 3,
			    idx & 7);
		if (ctx[idx].ctx1 & DMAR_CTX1_FPD)
			printf(" FPD");
		switch (ctx[idx].ctx1 & 0xc) {
		case DMAR_CTX1_T_UNTR:
			printf(" UNTR");
			break;
		case DMAR_CTX1_T_TR:
			printf(" TR");
			break;
		case DMAR_CTX1_T_PASS:
			printf(" PASS");
			break;
		default:
			printf(" TT3?");
			break;
		}
		printf(" SLPT %#jx", (uintmax_t)(ctx[idx].ctx1 &
		    DMAR_CTX1_ASR_MASK));
		printf(" domain %d", (int)DMAR_CTX2_GET_DID(ctx[idx].ctx2));
		printf("\n");
	}
}

static void
handle_drhd(int segment, uint64_t base_addr)
{
	struct dmar_root_entry *root_table;
	char *regs;
	uint64_t rtaddr;
	uint32_t gsts, ver;
	bool extended;
	int bus;

	regs = acpi_map_physical(base_addr, 4096);

	ver = read_4(regs, DMAR_VER_REG);
	gsts = read_4(regs, DMAR_GSTS_REG);
	printf("drhd @ %#jx (version %d.%d) PCI segment %d%s:\n",
	    (uintmax_t)base_addr, DMAR_MAJOR_VER(ver), DMAR_MINOR_VER(ver),
	    segment, gsts & DMAR_GSTS_TES ? "" : " (disabled)");
	if ((gsts & (DMAR_GSTS_TES | DMAR_GSTS_RTPS)) !=
	    (DMAR_GSTS_TES | DMAR_GSTS_RTPS))
		return;
	rtaddr = read_8(regs, DMAR_RTADDR_REG);
	extended = (rtaddr & DMAR_RTADDR_RTT) != 0;
	printf("    %sroot table @ 0x%#jx\n", extended ? "extended " : "",
	    rtaddr & DMAR_RTADDR_RTA_MASK);
	root_table = acpi_map_physical(rtaddr & DMAR_RTADDR_RTA_MASK, 4096);
	for (bus = 0; bus < 255; bus++) {
		if (extended) {
#ifdef notyet
			if (root_table[bus].r1 & DMAR_ROOT_R1_P)
				dump_ext_context_table(segment, bus,
				    root_table[bus].r1 & DMAR_ROOT_R1_CTP_MASK,
				    false);
			if (root_table[bus].r2 & DMAR_ROOT_R1_P)
				dump_ext_context_table(segment, bus,
				    root_table[bus].r2 & DMAR_ROOT_R1_CTP_MASK,
				    true);
#endif
		} else if (root_table[bus].r1 & DMAR_ROOT_R1_P)
			dump_context_table(segment, bus, root_table[bus].r1 &
			    DMAR_ROOT_R1_CTP_MASK);
	}
}

/* Borrowed from acpi.c in acpidump: */

static void
acpi_handle_dmar_drhd(ACPI_DMAR_HARDWARE_UNIT *drhd)
{

	handle_drhd(drhd->Segment, drhd->Address);
}

static int
acpi_handle_dmar_remapping_structure(void *addr, int remaining)
{
	ACPI_DMAR_HEADER *hdr = addr;

	if (remaining < (int)sizeof(ACPI_DMAR_HEADER))
		return (-1);

	if (remaining < hdr->Length)
		return (-1);

	switch (hdr->Type) {
	case ACPI_DMAR_TYPE_HARDWARE_UNIT:
		acpi_handle_dmar_drhd(addr);
		break;
	}
	return (hdr->Length);
}

static void
acpi_handle_dmar(ACPI_TABLE_HEADER *sdp)
{
	char *cp;
	int remaining, consumed;
	ACPI_TABLE_DMAR *dmar;

	dmar = (ACPI_TABLE_DMAR *)sdp;
	remaining = sdp->Length - sizeof(ACPI_TABLE_DMAR);
	while (remaining > 0) {
		cp = (char *)sdp + sdp->Length - remaining;
		consumed = acpi_handle_dmar_remapping_structure(cp, remaining);
		if (consumed <= 0)
			break;
		else
			remaining -= consumed;
	}
}

static ACPI_TABLE_HEADER *
acpi_map_sdt(vm_offset_t pa)
{
	ACPI_TABLE_HEADER *sp;

	sp = acpi_map_physical(pa, sizeof(ACPI_TABLE_HEADER));
	sp = acpi_map_physical(pa, sp->Length);
	return (sp);
}

static void
walk_rsdt(ACPI_TABLE_HEADER *rsdp)
{
	ACPI_TABLE_HEADER *sdp;
	ACPI_TABLE_RSDT *rsdt;
	ACPI_TABLE_XSDT *xsdt;
	vm_offset_t addr;
	int addr_size, entries, i;

	if (memcmp(rsdp->Signature, "RSDT", 4) != 0)
		addr_size = sizeof(uint32_t);
	else
		addr_size = sizeof(uint64_t);
	rsdt = (ACPI_TABLE_RSDT *)rsdp;
	xsdt = (ACPI_TABLE_XSDT *)rsdp;
	entries = (rsdp->Length - sizeof(ACPI_TABLE_HEADER)) / addr_size;
	for (i = 0; i < entries; i++) {
		if (addr_size == 4)
			addr = le32toh(rsdt->TableOffsetEntry[i]);
		else
			addr = le64toh(xsdt->TableOffsetEntry[i]);
		if (addr == 0)
			continue;
		sdp = (ACPI_TABLE_HEADER *)acpi_map_sdt(addr);
		if (acpi_checksum(sdp, sdp->Length)) {
			continue;
		}
		if (!memcmp(sdp->Signature, ACPI_SIG_DMAR, 4))
			acpi_handle_dmar(sdp);
	}
}

int
main(int argc __unused, char *argv[] __unused)
{
	ACPI_TABLE_HEADER *rsdt;

	rsdt = sdt_load_devmem();
	walk_rsdt(rsdt);
	return 0;
}
