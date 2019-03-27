/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 Yahoo!, Inc.
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/types.h>

#include <err.h>
#include <stdio.h>
#include <strings.h>
#include <sys/agpio.h>
#include <sys/pciio.h>

#include <dev/agp/agpreg.h>
#include <dev/pci/pcireg.h>

#include "pciconf.h"

static void	list_ecaps(int fd, struct pci_conf *p);

static void
cap_power(int fd, struct pci_conf *p, uint8_t ptr)
{
	uint16_t cap, status;

	cap = read_config(fd, &p->pc_sel, ptr + PCIR_POWER_CAP, 2);
	status = read_config(fd, &p->pc_sel, ptr + PCIR_POWER_STATUS, 2);
	printf("powerspec %d  supports D0%s%s D3  current D%d",
	    cap & PCIM_PCAP_SPEC,
	    cap & PCIM_PCAP_D1SUPP ? " D1" : "",
	    cap & PCIM_PCAP_D2SUPP ? " D2" : "",
	    status & PCIM_PSTAT_DMASK);
}

static void
cap_agp(int fd, struct pci_conf *p, uint8_t ptr)
{
	uint32_t status, command;

	status = read_config(fd, &p->pc_sel, ptr + AGP_STATUS, 4);
	command = read_config(fd, &p->pc_sel, ptr + AGP_CAPID, 4);
	printf("AGP ");
	if (AGP_MODE_GET_MODE_3(status)) {
		printf("v3 ");
		if (AGP_MODE_GET_RATE(status) & AGP_MODE_V3_RATE_8x)
			printf("8x ");
		if (AGP_MODE_GET_RATE(status) & AGP_MODE_V3_RATE_4x)
			printf("4x ");
	} else {
		if (AGP_MODE_GET_RATE(status) & AGP_MODE_V2_RATE_4x)
			printf("4x ");
		if (AGP_MODE_GET_RATE(status) & AGP_MODE_V2_RATE_2x)
			printf("2x ");
		if (AGP_MODE_GET_RATE(status) & AGP_MODE_V2_RATE_1x)
			printf("1x ");
	}
	if (AGP_MODE_GET_SBA(status))
		printf("SBA ");
	if (AGP_MODE_GET_AGP(command)) {
		printf("enabled at ");
		if (AGP_MODE_GET_MODE_3(command)) {
			printf("v3 ");
			switch (AGP_MODE_GET_RATE(command)) {
			case AGP_MODE_V3_RATE_8x:
				printf("8x ");
				break;
			case AGP_MODE_V3_RATE_4x:
				printf("4x ");
				break;
			}
		} else
			switch (AGP_MODE_GET_RATE(command)) {
			case AGP_MODE_V2_RATE_4x:
				printf("4x ");
				break;
			case AGP_MODE_V2_RATE_2x:
				printf("2x ");
				break;
			case AGP_MODE_V2_RATE_1x:
				printf("1x ");
				break;
			}
		if (AGP_MODE_GET_SBA(command))
			printf("SBA ");
	} else
		printf("disabled");
}

static void
cap_vpd(int fd __unused, struct pci_conf *p __unused, uint8_t ptr __unused)
{

	printf("VPD");
}

static void
cap_msi(int fd, struct pci_conf *p, uint8_t ptr)
{
	uint16_t ctrl;
	int msgnum;

	ctrl = read_config(fd, &p->pc_sel, ptr + PCIR_MSI_CTRL, 2);
	msgnum = 1 << ((ctrl & PCIM_MSICTRL_MMC_MASK) >> 1);
	printf("MSI supports %d message%s%s%s ", msgnum,
	    (msgnum == 1) ? "" : "s",
	    (ctrl & PCIM_MSICTRL_64BIT) ? ", 64 bit" : "",
	    (ctrl & PCIM_MSICTRL_VECTOR) ? ", vector masks" : "");
	if (ctrl & PCIM_MSICTRL_MSI_ENABLE) {
		msgnum = 1 << ((ctrl & PCIM_MSICTRL_MME_MASK) >> 4);
		printf("enabled with %d message%s", msgnum,
		    (msgnum == 1) ? "" : "s");
	}
}

static void
cap_pcix(int fd, struct pci_conf *p, uint8_t ptr)
{
	uint32_t status;
	int comma, max_splits, max_burst_read;

	status = read_config(fd, &p->pc_sel, ptr + PCIXR_STATUS, 4);
	printf("PCI-X ");
	if (status & PCIXM_STATUS_64BIT)
		printf("64-bit ");
	if ((p->pc_hdr & PCIM_HDRTYPE) == 1)
		printf("bridge ");
	if ((p->pc_hdr & PCIM_HDRTYPE) != 1 || (status & (PCIXM_STATUS_133CAP |
	    PCIXM_STATUS_266CAP | PCIXM_STATUS_533CAP)) != 0)
		printf("supports");
	comma = 0;
	if (status & PCIXM_STATUS_133CAP) {
		printf(" 133MHz");
		comma = 1;
	}
	if (status & PCIXM_STATUS_266CAP) {
		printf("%s 266MHz", comma ? "," : "");
		comma = 1;
	}
	if (status & PCIXM_STATUS_533CAP) {
		printf("%s 533MHz", comma ? "," : "");
		comma = 1;
	}
	if ((p->pc_hdr & PCIM_HDRTYPE) == 1)
		return;
	max_burst_read = 0;
	switch (status & PCIXM_STATUS_MAX_READ) {
	case PCIXM_STATUS_MAX_READ_512:
		max_burst_read = 512;
		break;
	case PCIXM_STATUS_MAX_READ_1024:
		max_burst_read = 1024;
		break;
	case PCIXM_STATUS_MAX_READ_2048:
		max_burst_read = 2048;
		break;
	case PCIXM_STATUS_MAX_READ_4096:
		max_burst_read = 4096;
		break;
	}
	max_splits = 0;
	switch (status & PCIXM_STATUS_MAX_SPLITS) {
	case PCIXM_STATUS_MAX_SPLITS_1:
		max_splits = 1;
		break;
	case PCIXM_STATUS_MAX_SPLITS_2:
		max_splits = 2;
		break;
	case PCIXM_STATUS_MAX_SPLITS_3:
		max_splits = 3;
		break;
	case PCIXM_STATUS_MAX_SPLITS_4:
		max_splits = 4;
		break;
	case PCIXM_STATUS_MAX_SPLITS_8:
		max_splits = 8;
		break;
	case PCIXM_STATUS_MAX_SPLITS_12:
		max_splits = 12;
		break;
	case PCIXM_STATUS_MAX_SPLITS_16:
		max_splits = 16;
		break;
	case PCIXM_STATUS_MAX_SPLITS_32:
		max_splits = 32;
		break;
	}
	printf("%s %d burst read, %d split transaction%s", comma ? "," : "",
	    max_burst_read, max_splits, max_splits == 1 ? "" : "s");
}

static void
cap_ht(int fd, struct pci_conf *p, uint8_t ptr)
{
	uint32_t reg;
	uint16_t command;

	command = read_config(fd, &p->pc_sel, ptr + PCIR_HT_COMMAND, 2);
	printf("HT ");
	if ((command & 0xe000) == PCIM_HTCAP_SLAVE)
		printf("slave");
	else if ((command & 0xe000) == PCIM_HTCAP_HOST)
		printf("host");
	else
		switch (command & PCIM_HTCMD_CAP_MASK) {
		case PCIM_HTCAP_SWITCH:
			printf("switch");
			break;
		case PCIM_HTCAP_INTERRUPT:
			printf("interrupt");
			break;
		case PCIM_HTCAP_REVISION_ID:
			printf("revision ID");
			break;
		case PCIM_HTCAP_UNITID_CLUMPING:
			printf("unit ID clumping");
			break;
		case PCIM_HTCAP_EXT_CONFIG_SPACE:
			printf("extended config space");
			break;
		case PCIM_HTCAP_ADDRESS_MAPPING:
			printf("address mapping");
			break;
		case PCIM_HTCAP_MSI_MAPPING:
			printf("MSI %saddress window %s at 0x",
			    command & PCIM_HTCMD_MSI_FIXED ? "fixed " : "",
			    command & PCIM_HTCMD_MSI_ENABLE ? "enabled" :
			    "disabled");
			if (command & PCIM_HTCMD_MSI_FIXED)
				printf("fee00000");
			else {
				reg = read_config(fd, &p->pc_sel,
				    ptr + PCIR_HTMSI_ADDRESS_HI, 4);
				if (reg != 0)
					printf("%08x", reg);
				reg = read_config(fd, &p->pc_sel,
				    ptr + PCIR_HTMSI_ADDRESS_LO, 4);
				printf("%08x", reg);
			}
			break;
		case PCIM_HTCAP_DIRECT_ROUTE:
			printf("direct route");
			break;
		case PCIM_HTCAP_VCSET:
			printf("VC set");
			break;
		case PCIM_HTCAP_RETRY_MODE:
			printf("retry mode");
			break;
		case PCIM_HTCAP_X86_ENCODING:
			printf("X86 encoding");
			break;
		case PCIM_HTCAP_GEN3:
			printf("Gen3");
			break;
		case PCIM_HTCAP_FLE:
			printf("function-level extension");
			break;
		case PCIM_HTCAP_PM:
			printf("power management");
			break;
		case PCIM_HTCAP_HIGH_NODE_COUNT:
			printf("high node count");
			break;
		default:
			printf("unknown %02x", command);
			break;
		}
}

static void
cap_vendor(int fd, struct pci_conf *p, uint8_t ptr)
{
	uint8_t length;

	length = read_config(fd, &p->pc_sel, ptr + PCIR_VENDOR_LENGTH, 1);
	printf("vendor (length %d)", length);
	if (p->pc_vendor == 0x8086) {
		/* Intel */
		uint8_t version;

		version = read_config(fd, &p->pc_sel, ptr + PCIR_VENDOR_DATA,
		    1);
		printf(" Intel cap %d version %d", version >> 4, version & 0xf);
		if (version >> 4 == 1 && length == 12) {
			/* Feature Detection */
			uint32_t fvec;
			int comma;

			comma = 0;
			fvec = read_config(fd, &p->pc_sel, ptr +
			    PCIR_VENDOR_DATA + 5, 4);
			printf("\n\t\t features:");
			if (fvec & (1 << 0)) {
				printf(" AMT");
				comma = 1;
			}
			fvec = read_config(fd, &p->pc_sel, ptr +
			    PCIR_VENDOR_DATA + 1, 4);
			if (fvec & (1 << 21)) {
				printf("%s Quick Resume", comma ? "," : "");
				comma = 1;
			}
			if (fvec & (1 << 18)) {
				printf("%s SATA RAID-5", comma ? "," : "");
				comma = 1;
			}
			if (fvec & (1 << 9)) {
				printf("%s Mobile", comma ? "," : "");
				comma = 1;
			}
			if (fvec & (1 << 7)) {
				printf("%s 6 PCI-e x1 slots", comma ? "," : "");
				comma = 1;
			} else {
				printf("%s 4 PCI-e x1 slots", comma ? "," : "");
				comma = 1;
			}
			if (fvec & (1 << 5)) {
				printf("%s SATA RAID-0/1/10", comma ? "," : "");
				comma = 1;
			}
			if (fvec & (1 << 3))
				printf(", SATA AHCI");
		}
	}
}

static void
cap_debug(int fd, struct pci_conf *p, uint8_t ptr)
{
	uint16_t debug_port;

	debug_port = read_config(fd, &p->pc_sel, ptr + PCIR_DEBUG_PORT, 2);
	printf("EHCI Debug Port at offset 0x%x in map 0x%x", debug_port &
	    PCIM_DEBUG_PORT_OFFSET, PCIR_BAR(debug_port >> 13));
}

static void
cap_subvendor(int fd, struct pci_conf *p, uint8_t ptr)
{
	uint32_t id;

	id = read_config(fd, &p->pc_sel, ptr + PCIR_SUBVENDCAP_ID, 4);
	printf("PCI Bridge card=0x%08x", id);
}

#define	MAX_PAYLOAD(field)		(128 << (field))

static const char *
link_speed_string(uint8_t speed)
{

	switch (speed) {
	case 1:
		return ("2.5");
	case 2:
		return ("5.0");
	case 3:
		return ("8.0");
	default:
		return ("undef");
	}
}

static const char *
aspm_string(uint8_t aspm)
{

	switch (aspm) {
	case 1:
		return ("L0s");
	case 2:
		return ("L1");
	case 3:
		return ("L0s/L1");
	default:
		return ("disabled");
	}
}

static int
slot_power(uint32_t cap)
{
	int mwatts;

	mwatts = (cap & PCIEM_SLOT_CAP_SPLV) >> 7;
	switch (cap & PCIEM_SLOT_CAP_SPLS) {
	case 0x0:
		mwatts *= 1000;
		break;
	case 0x1:
		mwatts *= 100;
		break;
	case 0x2:
		mwatts *= 10;
		break;
	default:
		break;
	}
	return (mwatts);
}

static void
cap_express(int fd, struct pci_conf *p, uint8_t ptr)
{
	uint32_t cap;
	uint16_t ctl, flags, sta;
	unsigned int version;

	flags = read_config(fd, &p->pc_sel, ptr + PCIER_FLAGS, 2);
	version = flags & PCIEM_FLAGS_VERSION;
	printf("PCI-Express %u ", version);
	switch (flags & PCIEM_FLAGS_TYPE) {
	case PCIEM_TYPE_ENDPOINT:
		printf("endpoint");
		break;
	case PCIEM_TYPE_LEGACY_ENDPOINT:
		printf("legacy endpoint");
		break;
	case PCIEM_TYPE_ROOT_PORT:
		printf("root port");
		break;
	case PCIEM_TYPE_UPSTREAM_PORT:
		printf("upstream port");
		break;
	case PCIEM_TYPE_DOWNSTREAM_PORT:
		printf("downstream port");
		break;
	case PCIEM_TYPE_PCI_BRIDGE:
		printf("PCI bridge");
		break;
	case PCIEM_TYPE_PCIE_BRIDGE:
		printf("PCI to PCIe bridge");
		break;
	case PCIEM_TYPE_ROOT_INT_EP:
		printf("root endpoint");
		break;
	case PCIEM_TYPE_ROOT_EC:
		printf("event collector");
		break;
	default:
		printf("type %d", (flags & PCIEM_FLAGS_TYPE) >> 4);
		break;
	}
	if (flags & PCIEM_FLAGS_IRQ)
		printf(" MSI %d", (flags & PCIEM_FLAGS_IRQ) >> 9);
	cap = read_config(fd, &p->pc_sel, ptr + PCIER_DEVICE_CAP, 4);
	ctl = read_config(fd, &p->pc_sel, ptr + PCIER_DEVICE_CTL, 2);
	printf(" max data %d(%d)",
	    MAX_PAYLOAD((ctl & PCIEM_CTL_MAX_PAYLOAD) >> 5),
	    MAX_PAYLOAD(cap & PCIEM_CAP_MAX_PAYLOAD));
	if ((cap & PCIEM_CAP_FLR) != 0)
		printf(" FLR");
	if (ctl & PCIEM_CTL_RELAXED_ORD_ENABLE)
		printf(" RO");
	if (ctl & PCIEM_CTL_NOSNOOP_ENABLE)
		printf(" NS");
	if (version >= 2) {
		cap = read_config(fd, &p->pc_sel, ptr + PCIER_DEVICE_CAP2, 4);
		if ((cap & PCIEM_CAP2_ARI) != 0) {
			ctl = read_config(fd, &p->pc_sel,
			    ptr + PCIER_DEVICE_CTL2, 4);
			printf(" ARI %s",
			    (ctl & PCIEM_CTL2_ARI) ? "enabled" : "disabled");
		}
	}
	cap = read_config(fd, &p->pc_sel, ptr + PCIER_LINK_CAP, 4);
	sta = read_config(fd, &p->pc_sel, ptr + PCIER_LINK_STA, 2);
	if (cap == 0 && sta == 0)
		return;
	printf("\n                ");
	printf(" link x%d(x%d)", (sta & PCIEM_LINK_STA_WIDTH) >> 4,
	    (cap & PCIEM_LINK_CAP_MAX_WIDTH) >> 4);
	if ((cap & PCIEM_LINK_CAP_MAX_WIDTH) != 0) {
		printf(" speed %s(%s)", (sta & PCIEM_LINK_STA_WIDTH) == 0 ?
		    "0.0" : link_speed_string(sta & PCIEM_LINK_STA_SPEED),
	    	    link_speed_string(cap & PCIEM_LINK_CAP_MAX_SPEED));
	}
	if ((cap & PCIEM_LINK_CAP_ASPM) != 0) {
		ctl = read_config(fd, &p->pc_sel, ptr + PCIER_LINK_CTL, 2);
		printf(" ASPM %s(%s)", aspm_string(ctl & PCIEM_LINK_CTL_ASPMC),
		    aspm_string((cap & PCIEM_LINK_CAP_ASPM) >> 10));
	}
	if (!(flags & PCIEM_FLAGS_SLOT))
		return;
	cap = read_config(fd, &p->pc_sel, ptr + PCIER_SLOT_CAP, 4);
	sta = read_config(fd, &p->pc_sel, ptr + PCIER_SLOT_STA, 2);
	ctl = read_config(fd, &p->pc_sel, ptr + PCIER_SLOT_CTL, 2);
	printf("\n                ");
	printf(" slot %d", (cap & PCIEM_SLOT_CAP_PSN) >> 19);
	printf(" power limit %d mW", slot_power(cap));
	if (cap & PCIEM_SLOT_CAP_HPC)
		printf(" HotPlug(%s)", sta & PCIEM_SLOT_STA_PDS ? "present" :
		    "empty");
	if (cap & PCIEM_SLOT_CAP_HPS)
		printf(" surprise");
	if (cap & PCIEM_SLOT_CAP_APB)
		printf(" Attn Button");
	if (cap & PCIEM_SLOT_CAP_PCP)
		printf(" PC(%s)", ctl & PCIEM_SLOT_CTL_PCC ? "off" : "on");
	if (cap & PCIEM_SLOT_CAP_MRLSP)
		printf(" MRL(%s)", sta & PCIEM_SLOT_STA_MRLSS ? "open" :
		    "closed");
	if (cap & PCIEM_SLOT_CAP_EIP)
		printf(" EI(%s)", sta & PCIEM_SLOT_STA_EIS ? "engaged" :
		    "disengaged");
}

static void
cap_msix(int fd, struct pci_conf *p, uint8_t ptr)
{
	uint32_t pba_offset, table_offset, val;
	int msgnum, pba_bar, table_bar;
	uint16_t ctrl;

	ctrl = read_config(fd, &p->pc_sel, ptr + PCIR_MSIX_CTRL, 2);
	msgnum = (ctrl & PCIM_MSIXCTRL_TABLE_SIZE) + 1;

	val = read_config(fd, &p->pc_sel, ptr + PCIR_MSIX_TABLE, 4);
	table_bar = PCIR_BAR(val & PCIM_MSIX_BIR_MASK);
	table_offset = val & ~PCIM_MSIX_BIR_MASK;

	val = read_config(fd, &p->pc_sel, ptr + PCIR_MSIX_PBA, 4);
	pba_bar = PCIR_BAR(val & PCIM_MSIX_BIR_MASK);
	pba_offset = val & ~PCIM_MSIX_BIR_MASK;

	printf("MSI-X supports %d message%s%s\n", msgnum,
	    (msgnum == 1) ? "" : "s",
	    (ctrl & PCIM_MSIXCTRL_MSIX_ENABLE) ? ", enabled" : "");

	printf("                 ");
	printf("Table in map 0x%x[0x%x], PBA in map 0x%x[0x%x]",
	    table_bar, table_offset, pba_bar, pba_offset);
}

static void
cap_sata(int fd __unused, struct pci_conf *p __unused, uint8_t ptr __unused)
{

	printf("SATA Index-Data Pair");
}

static void
cap_pciaf(int fd, struct pci_conf *p, uint8_t ptr)
{
	uint8_t cap;

	cap = read_config(fd, &p->pc_sel, ptr + PCIR_PCIAF_CAP, 1);
	printf("PCI Advanced Features:%s%s",
	    cap & PCIM_PCIAFCAP_FLR ? " FLR" : "",
	    cap & PCIM_PCIAFCAP_TP  ? " TP"  : "");
}

static const char *
ea_bei_to_name(int bei)
{
	static const char *barstr[] = {
		"BAR0", "BAR1", "BAR2", "BAR3", "BAR4", "BAR5"
	};
	static const char *vfbarstr[] = {
		"VFBAR0", "VFBAR1", "VFBAR2", "VFBAR3", "VFBAR4", "VFBAR5"
	};

	if ((bei >= PCIM_EA_BEI_BAR_0) && (bei <= PCIM_EA_BEI_BAR_5))
		return (barstr[bei - PCIM_EA_BEI_BAR_0]);
	if ((bei >= PCIM_EA_BEI_VF_BAR_0) && (bei <= PCIM_EA_BEI_VF_BAR_5))
		return (vfbarstr[bei - PCIM_EA_BEI_VF_BAR_0]);

	switch (bei) {
	case PCIM_EA_BEI_BRIDGE:
		return "BRIDGE";
	case PCIM_EA_BEI_ENI:
		return "ENI";
	case PCIM_EA_BEI_ROM:
		return "ROM";
	case PCIM_EA_BEI_RESERVED:
	default:
		return "RSVD";
	}
}

static const char *
ea_prop_to_name(uint8_t prop)
{

	switch (prop) {
	case PCIM_EA_P_MEM:
		return "Non-Prefetchable Memory";
	case PCIM_EA_P_MEM_PREFETCH:
		return "Prefetchable Memory";
	case PCIM_EA_P_IO:
		return "I/O Space";
	case PCIM_EA_P_VF_MEM_PREFETCH:
		return "VF Prefetchable Memory";
	case PCIM_EA_P_VF_MEM:
		return "VF Non-Prefetchable Memory";
	case PCIM_EA_P_BRIDGE_MEM:
		return "Bridge Non-Prefetchable Memory";
	case PCIM_EA_P_BRIDGE_MEM_PREFETCH:
		return "Bridge Prefetchable Memory";
	case PCIM_EA_P_BRIDGE_IO:
		return "Bridge I/O Space";
	case PCIM_EA_P_MEM_RESERVED:
		return "Reserved Memory";
	case PCIM_EA_P_IO_RESERVED:
		return "Reserved I/O Space";
	case PCIM_EA_P_UNAVAILABLE:
		return "Unavailable";
	default:
		return "Reserved";
	}
}

static void
cap_ea(int fd, struct pci_conf *p, uint8_t ptr)
{
	int num_ent;
	int a, b;
	uint32_t bei;
	uint32_t val;
	int ent_size;
	uint32_t dw[4];
	uint32_t flags, flags_pp, flags_sp;
	uint64_t base, max_offset;
	uint8_t fixed_sub_bus_nr, fixed_sec_bus_nr;

	/* Determine the number of entries */
	num_ent = read_config(fd, &p->pc_sel, ptr + PCIR_EA_NUM_ENT, 2);
	num_ent &= PCIM_EA_NUM_ENT_MASK;

	printf("PCI Enhanced Allocation (%d entries)", num_ent);

	/* Find the first entry to care of */
	ptr += PCIR_EA_FIRST_ENT;

	/* Print BUS numbers for bridges */
	if ((p->pc_hdr & PCIM_HDRTYPE) == PCIM_HDRTYPE_BRIDGE) {
		val = read_config(fd, &p->pc_sel, ptr, 4);

		fixed_sec_bus_nr = PCIM_EA_SEC_NR(val);
		fixed_sub_bus_nr = PCIM_EA_SUB_NR(val);

		printf("\n\t\t BRIDGE, sec bus [%d], sub bus [%d]",
		    fixed_sec_bus_nr, fixed_sub_bus_nr);
		ptr += 4;
	}

	for (a = 0; a < num_ent; a++) {
		/* Read a number of dwords in the entry */
		val = read_config(fd, &p->pc_sel, ptr, 4);
		ptr += 4;
		ent_size = (val & PCIM_EA_ES);

		for (b = 0; b < ent_size; b++) {
			dw[b] = read_config(fd, &p->pc_sel, ptr, 4);
			ptr += 4;
		}

		flags = val;
		flags_pp = (flags & PCIM_EA_PP) >> PCIM_EA_PP_OFFSET;
		flags_sp = (flags & PCIM_EA_SP) >> PCIM_EA_SP_OFFSET;
		bei = (PCIM_EA_BEI & val) >> PCIM_EA_BEI_OFFSET;

		base = dw[0] & PCIM_EA_FIELD_MASK;
		max_offset = dw[1] | ~PCIM_EA_FIELD_MASK;
		b = 2;
		if (((dw[0] & PCIM_EA_IS_64) != 0) && (b < ent_size)) {
			base |= (uint64_t)dw[b] << 32UL;
			b++;
		}
		if (((dw[1] & PCIM_EA_IS_64) != 0)
			&& (b < ent_size)) {
			max_offset |= (uint64_t)dw[b] << 32UL;
			b++;
		}

		printf("\n\t\t [%d] %s, %s, %s, base [0x%jx], size [0x%jx]"
		    "\n\t\t\tPrimary properties [0x%x] (%s)"
		    "\n\t\t\tSecondary properties [0x%x] (%s)",
		    bei, ea_bei_to_name(bei),
		    (flags & PCIM_EA_ENABLE ? "Enabled" : "Disabled"),
		    (flags & PCIM_EA_WRITABLE ? "Writable" : "Read-only"),
		    (uintmax_t)base, (uintmax_t)(max_offset + 1),
		    flags_pp, ea_prop_to_name(flags_pp),
		    flags_sp, ea_prop_to_name(flags_sp));
	}
}

void
list_caps(int fd, struct pci_conf *p)
{
	int express;
	uint16_t sta;
	uint8_t ptr, cap;

	/* Are capabilities present for this device? */
	sta = read_config(fd, &p->pc_sel, PCIR_STATUS, 2);
	if (!(sta & PCIM_STATUS_CAPPRESENT))
		return;

	switch (p->pc_hdr & PCIM_HDRTYPE) {
	case PCIM_HDRTYPE_NORMAL:
	case PCIM_HDRTYPE_BRIDGE:
		ptr = PCIR_CAP_PTR;
		break;
	case PCIM_HDRTYPE_CARDBUS:
		ptr = PCIR_CAP_PTR_2;
		break;
	default:
		errx(1, "list_caps: bad header type");
	}

	/* Walk the capability list. */
	express = 0;
	ptr = read_config(fd, &p->pc_sel, ptr, 1);
	while (ptr != 0 && ptr != 0xff) {
		cap = read_config(fd, &p->pc_sel, ptr + PCICAP_ID, 1);
		printf("    cap %02x[%02x] = ", cap, ptr);
		switch (cap) {
		case PCIY_PMG:
			cap_power(fd, p, ptr);
			break;
		case PCIY_AGP:
			cap_agp(fd, p, ptr);
			break;
		case PCIY_VPD:
			cap_vpd(fd, p, ptr);
			break;
		case PCIY_MSI:
			cap_msi(fd, p, ptr);
			break;
		case PCIY_PCIX:
			cap_pcix(fd, p, ptr);
			break;
		case PCIY_HT:
			cap_ht(fd, p, ptr);
			break;
		case PCIY_VENDOR:
			cap_vendor(fd, p, ptr);
			break;
		case PCIY_DEBUG:
			cap_debug(fd, p, ptr);
			break;
		case PCIY_SUBVENDOR:
			cap_subvendor(fd, p, ptr);
			break;
		case PCIY_EXPRESS:
			express = 1;
			cap_express(fd, p, ptr);
			break;
		case PCIY_MSIX:
			cap_msix(fd, p, ptr);
			break;
		case PCIY_SATA:
			cap_sata(fd, p, ptr);
			break;
		case PCIY_PCIAF:
			cap_pciaf(fd, p, ptr);
			break;
		case PCIY_EA:
			cap_ea(fd, p, ptr);
			break;
		default:
			printf("unknown");
			break;
		}
		printf("\n");
		ptr = read_config(fd, &p->pc_sel, ptr + PCICAP_NEXTPTR, 1);
	}

	if (express)
		list_ecaps(fd, p);
}

/* From <sys/systm.h>. */
static __inline uint32_t
bitcount32(uint32_t x)
{

	x = (x & 0x55555555) + ((x & 0xaaaaaaaa) >> 1);
	x = (x & 0x33333333) + ((x & 0xcccccccc) >> 2);
	x = (x + (x >> 4)) & 0x0f0f0f0f;
	x = (x + (x >> 8));
	x = (x + (x >> 16)) & 0x000000ff;
	return (x);
}

static void
ecap_aer(int fd, struct pci_conf *p, uint16_t ptr, uint8_t ver)
{
	uint32_t sta, mask;

	printf("AER %d", ver);
	if (ver < 1)
		return;
	sta = read_config(fd, &p->pc_sel, ptr + PCIR_AER_UC_STATUS, 4);
	mask = read_config(fd, &p->pc_sel, ptr + PCIR_AER_UC_SEVERITY, 4);
	printf(" %d fatal", bitcount32(sta & mask));
	printf(" %d non-fatal", bitcount32(sta & ~mask));
	sta = read_config(fd, &p->pc_sel, ptr + PCIR_AER_COR_STATUS, 4);
	printf(" %d corrected\n", bitcount32(sta));
}

static void
ecap_vc(int fd, struct pci_conf *p, uint16_t ptr, uint8_t ver)
{
	uint32_t cap1;

	printf("VC %d", ver);
	if (ver < 1)
		return;
	cap1 = read_config(fd, &p->pc_sel, ptr + PCIR_VC_CAP1, 4);
	printf(" max VC%d", cap1 & PCIM_VC_CAP1_EXT_COUNT);
	if ((cap1 & PCIM_VC_CAP1_LOWPRI_EXT_COUNT) != 0)
		printf(" lowpri VC0-VC%d",
		    (cap1 & PCIM_VC_CAP1_LOWPRI_EXT_COUNT) >> 4);
	printf("\n");
}

static void
ecap_sernum(int fd, struct pci_conf *p, uint16_t ptr, uint8_t ver)
{
	uint32_t high, low;

	printf("Serial %d", ver);
	if (ver < 1)
		return;
	low = read_config(fd, &p->pc_sel, ptr + PCIR_SERIAL_LOW, 4);
	high = read_config(fd, &p->pc_sel, ptr + PCIR_SERIAL_HIGH, 4);
	printf(" %08x%08x\n", high, low);
}

static void
ecap_vendor(int fd, struct pci_conf *p, uint16_t ptr, uint8_t ver)
{
	uint32_t val;

	printf("Vendor %d", ver);
	if (ver < 1)
		return;
	val = read_config(fd, &p->pc_sel, ptr + 4, 4);
	printf(" ID %d\n", val & 0xffff);
}

static void
ecap_sec_pcie(int fd, struct pci_conf *p, uint16_t ptr, uint8_t ver)
{
	uint32_t val;

	printf("PCIe Sec %d", ver);
	if (ver < 1)
		return;
	val = read_config(fd, &p->pc_sel, ptr + 8, 4);
	printf(" lane errors %#x\n", val);
}

static const char *
check_enabled(int value)
{

	return (value ? "enabled" : "disabled");
}

static void
ecap_sriov(int fd, struct pci_conf *p, uint16_t ptr, uint8_t ver)
{
	const char *comma, *enabled;
	uint16_t iov_ctl, total_vfs, num_vfs, vf_offset, vf_stride, vf_did;
	uint32_t page_caps, page_size, page_shift, size;
	int i;

	printf("SR-IOV %d ", ver);

	iov_ctl = read_config(fd, &p->pc_sel, ptr + PCIR_SRIOV_CTL, 2);
	printf("IOV %s, Memory Space %s, ARI %s\n",
	    check_enabled(iov_ctl & PCIM_SRIOV_VF_EN),
	    check_enabled(iov_ctl & PCIM_SRIOV_VF_MSE),
	    check_enabled(iov_ctl & PCIM_SRIOV_ARI_EN));

	total_vfs = read_config(fd, &p->pc_sel, ptr + PCIR_SRIOV_TOTAL_VFS, 2);
	num_vfs = read_config(fd, &p->pc_sel, ptr + PCIR_SRIOV_NUM_VFS, 2);
	printf("                     ");
	printf("%d VFs configured out of %d supported\n", num_vfs, total_vfs);

	vf_offset = read_config(fd, &p->pc_sel, ptr + PCIR_SRIOV_VF_OFF, 2);
	vf_stride = read_config(fd, &p->pc_sel, ptr + PCIR_SRIOV_VF_STRIDE, 2);
	printf("                     ");
	printf("First VF RID Offset 0x%04x, VF RID Stride 0x%04x\n", vf_offset,
	    vf_stride);

	vf_did = read_config(fd, &p->pc_sel, ptr + PCIR_SRIOV_VF_DID, 2);
	printf("                     VF Device ID 0x%04x\n", vf_did);

	page_caps = read_config(fd, &p->pc_sel, ptr + PCIR_SRIOV_PAGE_CAP, 4);
	page_size = read_config(fd, &p->pc_sel, ptr + PCIR_SRIOV_PAGE_SIZE, 4);
	printf("                     ");
	printf("Page Sizes: ");
	comma = "";
	while (page_caps != 0) {
		page_shift = ffs(page_caps) - 1;

		if (page_caps & page_size)
			enabled = " (enabled)";
		else
			enabled = "";

		size = (1 << (page_shift + PCI_SRIOV_BASE_PAGE_SHIFT));
		printf("%s%d%s", comma, size, enabled);
		comma = ", ";

		page_caps &= ~(1 << page_shift);
	}
	printf("\n");

	for (i = 0; i <= PCIR_MAX_BAR_0; i++)
		print_bar(fd, p, "iov bar  ", ptr + PCIR_SRIOV_BAR(i));
}

static struct {
	uint16_t id;
	const char *name;
} ecap_names[] = {
	{ PCIZ_PWRBDGT, "Power Budgeting" },
	{ PCIZ_RCLINK_DCL, "Root Complex Link Declaration" },
	{ PCIZ_RCLINK_CTL, "Root Complex Internal Link Control" },
	{ PCIZ_RCEC_ASSOC, "Root Complex Event Collector ASsociation" },
	{ PCIZ_MFVC, "MFVC" },
	{ PCIZ_RCRB, "RCRB" },
	{ PCIZ_ACS, "ACS" },
	{ PCIZ_ARI, "ARI" },
	{ PCIZ_ATS, "ATS" },
	{ PCIZ_MULTICAST, "Multicast" },
	{ PCIZ_RESIZE_BAR, "Resizable BAR" },
	{ PCIZ_DPA, "DPA" },
	{ PCIZ_TPH_REQ, "TPH Requester" },
	{ PCIZ_LTR, "LTR" },
	{ 0, NULL }
};

static void
list_ecaps(int fd, struct pci_conf *p)
{
	const char *name;
	uint32_t ecap;
	uint16_t ptr;
	int i;

	ptr = PCIR_EXTCAP;
	ecap = read_config(fd, &p->pc_sel, ptr, 4);
	if (ecap == 0xffffffff || ecap == 0)
		return;
	for (;;) {
		printf("    ecap %04x[%03x] = ", PCI_EXTCAP_ID(ecap), ptr);
		switch (PCI_EXTCAP_ID(ecap)) {
		case PCIZ_AER:
			ecap_aer(fd, p, ptr, PCI_EXTCAP_VER(ecap));
			break;
		case PCIZ_VC:
			ecap_vc(fd, p, ptr, PCI_EXTCAP_VER(ecap));
			break;
		case PCIZ_SERNUM:
			ecap_sernum(fd, p, ptr, PCI_EXTCAP_VER(ecap));
			break;
		case PCIZ_VENDOR:
			ecap_vendor(fd, p, ptr, PCI_EXTCAP_VER(ecap));
			break;
		case PCIZ_SEC_PCIE:
			ecap_sec_pcie(fd, p, ptr, PCI_EXTCAP_VER(ecap));
			break;
		case PCIZ_SRIOV:
			ecap_sriov(fd, p, ptr, PCI_EXTCAP_VER(ecap));
			break;
		default:
			name = "unknown";
			for (i = 0; ecap_names[i].name != NULL; i++)
				if (ecap_names[i].id == PCI_EXTCAP_ID(ecap)) {
					name = ecap_names[i].name;
					break;
				}
			printf("%s %d\n", name, PCI_EXTCAP_VER(ecap));
			break;
		}
		ptr = PCI_EXTCAP_NEXTPTR(ecap);
		if (ptr == 0)
			break;
		ecap = read_config(fd, &p->pc_sel, ptr, 4);
	}
}

/* Find offset of a specific capability.  Returns 0 on failure. */
uint8_t
pci_find_cap(int fd, struct pci_conf *p, uint8_t id)
{
	uint16_t sta;
	uint8_t ptr, cap;

	/* Are capabilities present for this device? */
	sta = read_config(fd, &p->pc_sel, PCIR_STATUS, 2);
	if (!(sta & PCIM_STATUS_CAPPRESENT))
		return (0);

	switch (p->pc_hdr & PCIM_HDRTYPE) {
	case PCIM_HDRTYPE_NORMAL:
	case PCIM_HDRTYPE_BRIDGE:
		ptr = PCIR_CAP_PTR;
		break;
	case PCIM_HDRTYPE_CARDBUS:
		ptr = PCIR_CAP_PTR_2;
		break;
	default:
		return (0);
	}

	ptr = read_config(fd, &p->pc_sel, ptr, 1);
	while (ptr != 0 && ptr != 0xff) {
		cap = read_config(fd, &p->pc_sel, ptr + PCICAP_ID, 1);
		if (cap == id)
			return (ptr);
		ptr = read_config(fd, &p->pc_sel, ptr + PCICAP_NEXTPTR, 1);
	}
	return (0);
}

/* Find offset of a specific extended capability.  Returns 0 on failure. */
uint16_t
pcie_find_cap(int fd, struct pci_conf *p, uint16_t id)
{
	uint32_t ecap;
	uint16_t ptr;

	ptr = PCIR_EXTCAP;
	ecap = read_config(fd, &p->pc_sel, ptr, 4);
	if (ecap == 0xffffffff || ecap == 0)
		return (0);
	for (;;) {
		if (PCI_EXTCAP_ID(ecap) == id)
			return (ptr);
		ptr = PCI_EXTCAP_NEXTPTR(ecap);
		if (ptr == 0)
			break;
		ecap = read_config(fd, &p->pc_sel, ptr, 4);
	}
	return (0);
}
