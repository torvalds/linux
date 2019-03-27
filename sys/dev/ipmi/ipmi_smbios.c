/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 IronPort Systems Inc. <ambrisko@ironport.com>
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
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/selinfo.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/pc/bios.h>

#ifdef LOCAL_MODULE
#include <ipmi.h>
#include <ipmivars.h>
#else
#include <sys/ipmi.h>
#include <dev/ipmi/ipmivars.h>
#endif

struct ipmi_entry {
	uint8_t		type;
	uint8_t		length;
	uint16_t	handle;
	uint8_t		interface_type;
	uint8_t		spec_revision;
	uint8_t		i2c_slave_address;
	uint8_t		NV_storage_device_address;
	uint64_t	base_address;
	uint8_t		base_address_modifier;
	uint8_t		interrupt_number;
};

/* Fields in the base_address field of an IPMI entry. */
#define	IPMI_BAR_MODE(ba)	((ba) & 0x0000000000000001)
#define	IPMI_BAR_ADDR(ba)	((ba) & 0xfffffffffffffffe)

/* Fields in the base_address_modifier field of an IPMI entry. */
#define	IPMI_BAM_IRQ_TRIGGER	0x01
#define	IPMI_BAM_IRQ_POLARITY	0x02
#define	IPMI_BAM_IRQ_VALID	0x08
#define	IPMI_BAM_ADDR_LSB(bam)	(((bam) & 0x10) >> 4)
#define	IPMI_BAM_REG_SPACING(bam) (((bam) & 0xc0) >> 6)
#define	SPACING_8		0x0
#define	SPACING_32		0x1
#define	SPACING_16		0x2

typedef void (*smbios_callback_t)(struct smbios_structure_header *, void *);

static struct ipmi_get_info ipmi_info;
static int ipmi_probed;
static struct mtx ipmi_info_mtx;
MTX_SYSINIT(ipmi_info, &ipmi_info_mtx, "ipmi info", MTX_DEF);

static void	ipmi_smbios_probe(struct ipmi_get_info *);
static int	smbios_cksum(struct smbios_eps *);
static void	smbios_walk_table(uint8_t *, int, smbios_callback_t,
		    void *);
static void	smbios_ipmi_info(struct smbios_structure_header *, void *);

static void
smbios_ipmi_info(struct smbios_structure_header *h, void *arg)
{
	struct ipmi_get_info *info;
	struct ipmi_entry *s;

	if (h->type != 38 || h->length <
	    offsetof(struct ipmi_entry, interrupt_number))
		return;
	s = (struct ipmi_entry *)h;
	info = arg;
	bzero(info, sizeof(struct ipmi_get_info));
	switch (s->interface_type) {
	case KCS_MODE:
	case SMIC_MODE:
		info->address = IPMI_BAR_ADDR(s->base_address) |
		    IPMI_BAM_ADDR_LSB(s->base_address_modifier);
		info->io_mode = IPMI_BAR_MODE(s->base_address);
		switch (IPMI_BAM_REG_SPACING(s->base_address_modifier)) {
		case SPACING_8:
			info->offset = 1;
			break;
		case SPACING_32:
			info->offset = 4;
			break;
		case SPACING_16:
			info->offset = 2;
			break;
		default:
			printf("SMBIOS: Invalid register spacing\n");
			return;
		}
		break;
	case SSIF_MODE:
		if ((s->base_address & 0xffffffffffffff00) != 0) {
			printf("SMBIOS: Invalid SSIF SMBus address, using BMC I2C slave address instead\n");
			info->address = s->i2c_slave_address;
			break;
		}
		info->address = IPMI_BAR_ADDR(s->base_address);
		break;
	default:
		return;
	}
	if (s->length > offsetof(struct ipmi_entry, interrupt_number)) {
		if (s->interrupt_number > 15)
			printf("SMBIOS: Non-ISA IRQ %d for IPMI\n",
			    s->interrupt_number);
		else
			info->irq = s->interrupt_number;
	}
	info->iface_type = s->interface_type;
}

static void
smbios_walk_table(uint8_t *p, int entries, smbios_callback_t cb, void *arg)
{
	struct smbios_structure_header *s;

	while (entries--) {
		s = (struct smbios_structure_header *)p;
		cb(s, arg);

		/*
		 * Look for a double-nul after the end of the
		 * formatted area of this structure.
		 */
		p += s->length;
		while (!(p[0] == 0 && p[1] == 0))
			p++;

		/*
		 * Skip over the double-nul to the start of the next
		 * structure.
		 */
		p += 2;
	}
}

/*
 * Walk the SMBIOS table looking for an IPMI (type 38) entry.  If we find
 * one, return the parsed data in the passed in ipmi_get_info structure and
 * return true.  If we don't find one, return false.
 */
static void
ipmi_smbios_probe(struct ipmi_get_info *info)
{
	struct smbios_eps *header;
	void *table;
	u_int32_t addr;

	bzero(info, sizeof(struct ipmi_get_info));

	/* Find the SMBIOS table header. */
	addr = bios_sigsearch(SMBIOS_START, SMBIOS_SIG, SMBIOS_LEN,
			      SMBIOS_STEP, SMBIOS_OFF);
	if (addr == 0)
		return;

	/*
	 * Map the header.  We first map a fixed size to get the actual
	 * length and then map it a second time with the actual length so
	 * we can verify the checksum.
	 */
	header = pmap_mapbios(addr, sizeof(struct smbios_eps));
	table = pmap_mapbios(addr, header->length);
	pmap_unmapbios((vm_offset_t)header, sizeof(struct smbios_eps));
	header = table;
	if (smbios_cksum(header) != 0) {
		pmap_unmapbios((vm_offset_t)header, header->length);
		return;
	}

	/* Now map the actual table and walk it looking for an IPMI entry. */
	table = pmap_mapbios(header->structure_table_address,
	    header->structure_table_length);
	smbios_walk_table(table, header->number_structures, smbios_ipmi_info,
	    info);

	/* Unmap everything. */
	pmap_unmapbios((vm_offset_t)table, header->structure_table_length);
	pmap_unmapbios((vm_offset_t)header, header->length);
}

/*
 * Return the SMBIOS IPMI table entry info to the caller.  If we haven't
 * searched the IPMI table yet, search it.  Otherwise, return a cached
 * copy of the data.
 */
int
ipmi_smbios_identify(struct ipmi_get_info *info)
{

	mtx_lock(&ipmi_info_mtx);
	switch (ipmi_probed) {
	case 0:
		/* Need to probe the SMBIOS table. */
		ipmi_probed++;
		mtx_unlock(&ipmi_info_mtx);
		ipmi_smbios_probe(&ipmi_info);
		mtx_lock(&ipmi_info_mtx);
		ipmi_probed++;
		wakeup(&ipmi_info);
		break;
	case 1:
		/* Another thread is currently probing the table, so wait. */
		while (ipmi_probed == 1)
			msleep(&ipmi_info, &ipmi_info_mtx, 0, "ipmi info", 0);
		break;
	default:
		/* The cached data is available. */
		break;
	}

	bcopy(&ipmi_info, info, sizeof(ipmi_info));
	mtx_unlock(&ipmi_info_mtx);

	return (info->iface_type != 0);
}

static int
smbios_cksum(struct smbios_eps *e)
{
	u_int8_t *ptr;
	u_int8_t cksum;
	int i;

	ptr = (u_int8_t *)e;
	cksum = 0;
	for (i = 0; i < e->length; i++) {
		cksum += ptr[i];
	}

	return (cksum);
}
