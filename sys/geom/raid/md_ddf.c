/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Alexander Motin <mav@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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
#include <sys/bio.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/clock.h>
#include <sys/disk.h>
#include <geom/geom.h>
#include "geom/raid/g_raid.h"
#include "geom/raid/md_ddf.h"
#include "g_raid_md_if.h"

static MALLOC_DEFINE(M_MD_DDF, "md_ddf_data", "GEOM_RAID DDF metadata");

#define	DDF_MAX_DISKS_HARD	128

#define	DDF_MAX_DISKS	16
#define	DDF_MAX_VDISKS	7
#define	DDF_MAX_PARTITIONS	1

#define DECADE (3600*24*(365*10+2))	/* 10 years in seconds. */

struct ddf_meta {
	u_int	sectorsize;
	u_int	bigendian;
	struct ddf_header *hdr;
	struct ddf_cd_record *cdr;
	struct ddf_pd_record *pdr;
	struct ddf_vd_record *vdr;
	void *cr;
	struct ddf_pdd_record *pdd;
	struct ddf_bbm_log *bbm;
};

struct ddf_vol_meta {
	u_int	sectorsize;
	u_int	bigendian;
	struct ddf_header *hdr;
	struct ddf_cd_record *cdr;
	struct ddf_vd_entry *vde;
	struct ddf_vdc_record *vdc;
	struct ddf_vdc_record *bvdc[DDF_MAX_DISKS_HARD];
};

struct g_raid_md_ddf_perdisk {
	struct ddf_meta	 pd_meta;
};

struct g_raid_md_ddf_pervolume {
	struct ddf_vol_meta		 pv_meta;
	int				 pv_started;
	struct callout			 pv_start_co;	/* STARTING state timer. */
};

struct g_raid_md_ddf_object {
	struct g_raid_md_object	 mdio_base;
	u_int			 mdio_bigendian;
	struct ddf_meta		 mdio_meta;
	int			 mdio_starting;
	struct callout		 mdio_start_co;	/* STARTING state timer. */
	int			 mdio_started;
	struct root_hold_token	*mdio_rootmount; /* Root mount delay token. */
};

static g_raid_md_create_req_t g_raid_md_create_req_ddf;
static g_raid_md_taste_t g_raid_md_taste_ddf;
static g_raid_md_event_t g_raid_md_event_ddf;
static g_raid_md_volume_event_t g_raid_md_volume_event_ddf;
static g_raid_md_ctl_t g_raid_md_ctl_ddf;
static g_raid_md_write_t g_raid_md_write_ddf;
static g_raid_md_fail_disk_t g_raid_md_fail_disk_ddf;
static g_raid_md_free_disk_t g_raid_md_free_disk_ddf;
static g_raid_md_free_volume_t g_raid_md_free_volume_ddf;
static g_raid_md_free_t g_raid_md_free_ddf;

static kobj_method_t g_raid_md_ddf_methods[] = {
	KOBJMETHOD(g_raid_md_create_req,	g_raid_md_create_req_ddf),
	KOBJMETHOD(g_raid_md_taste,	g_raid_md_taste_ddf),
	KOBJMETHOD(g_raid_md_event,	g_raid_md_event_ddf),
	KOBJMETHOD(g_raid_md_volume_event,	g_raid_md_volume_event_ddf),
	KOBJMETHOD(g_raid_md_ctl,	g_raid_md_ctl_ddf),
	KOBJMETHOD(g_raid_md_write,	g_raid_md_write_ddf),
	KOBJMETHOD(g_raid_md_fail_disk,	g_raid_md_fail_disk_ddf),
	KOBJMETHOD(g_raid_md_free_disk,	g_raid_md_free_disk_ddf),
	KOBJMETHOD(g_raid_md_free_volume,	g_raid_md_free_volume_ddf),
	KOBJMETHOD(g_raid_md_free,	g_raid_md_free_ddf),
	{ 0, 0 }
};

static struct g_raid_md_class g_raid_md_ddf_class = {
	"DDF",
	g_raid_md_ddf_methods,
	sizeof(struct g_raid_md_ddf_object),
	.mdc_enable = 1,
	.mdc_priority = 100
};

#define GET8(m, f)	((m)->f)
#define GET16(m, f)	((m)->bigendian ? be16dec(&(m)->f) : le16dec(&(m)->f))
#define GET32(m, f)	((m)->bigendian ? be32dec(&(m)->f) : le32dec(&(m)->f))
#define GET64(m, f)	((m)->bigendian ? be64dec(&(m)->f) : le64dec(&(m)->f))
#define GET8D(m, f)	(f)
#define GET16D(m, f)	((m)->bigendian ? be16dec(&f) : le16dec(&f))
#define GET32D(m, f)	((m)->bigendian ? be32dec(&f) : le32dec(&f))
#define GET64D(m, f)	((m)->bigendian ? be64dec(&f) : le64dec(&f))
#define GET8P(m, f)	(*(f))
#define GET16P(m, f)	((m)->bigendian ? be16dec(f) : le16dec(f))
#define GET32P(m, f)	((m)->bigendian ? be32dec(f) : le32dec(f))
#define GET64P(m, f)	((m)->bigendian ? be64dec(f) : le64dec(f))

#define SET8P(m, f, v)							\
	(*(f) = (v))
#define SET16P(m, f, v)							\
	do {								\
		if ((m)->bigendian)					\
			be16enc((f), (v));				\
		else							\
			le16enc((f), (v));				\
	} while (0)
#define SET32P(m, f, v)							\
	do {								\
		if ((m)->bigendian)					\
			be32enc((f), (v));				\
		else							\
			le32enc((f), (v));				\
	} while (0)
#define SET64P(m, f, v)							\
	do {								\
		if ((m)->bigendian)					\
			be64enc((f), (v));				\
		else							\
			le64enc((f), (v));				\
	} while (0)
#define SET8(m, f, v)	SET8P((m), &((m)->f), (v))
#define SET16(m, f, v)	SET16P((m), &((m)->f), (v))
#define SET32(m, f, v)	SET32P((m), &((m)->f), (v))
#define SET64(m, f, v)	SET64P((m), &((m)->f), (v))
#define SET8D(m, f, v)	SET8P((m), &(f), (v))
#define SET16D(m, f, v)	SET16P((m), &(f), (v))
#define SET32D(m, f, v)	SET32P((m), &(f), (v))
#define SET64D(m, f, v)	SET64P((m), &(f), (v))

#define GETCRNUM(m)	(GET32((m), hdr->cr_length) /			\
	GET16((m), hdr->Configuration_Record_Length))

#define GETVDCPTR(m, n)	((struct ddf_vdc_record *)((uint8_t *)(m)->cr +	\
	(n) * GET16((m), hdr->Configuration_Record_Length) *		\
	(m)->sectorsize))

#define GETSAPTR(m, n)	((struct ddf_sa_record *)((uint8_t *)(m)->cr +	\
	(n) * GET16((m), hdr->Configuration_Record_Length) *		\
	(m)->sectorsize))

static int
isff(uint8_t *buf, int size)
{
	int i;

	for (i = 0; i < size; i++)
		if (buf[i] != 0xff)
			return (0);
	return (1);
}

static void
print_guid(uint8_t *buf)
{
	int i, ascii;

	ascii = 1;
	for (i = 0; i < 24; i++) {
		if (buf[i] != 0 && (buf[i] < ' ' || buf[i] > 127)) {
			ascii = 0;
			break;
		}
	}
	if (ascii) {
		printf("'%.24s'", buf);
	} else {
		for (i = 0; i < 24; i++)
			printf("%02x", buf[i]);
	}
}

static void
g_raid_md_ddf_print(struct ddf_meta *meta)
{
	struct ddf_vdc_record *vdc;
	struct ddf_vuc_record *vuc;
	struct ddf_sa_record *sa;
	uint64_t *val2;
	uint32_t val;
	int i, j, k, num, num2;

	if (g_raid_debug < 1)
		return;

	printf("********* DDF Metadata *********\n");
	printf("**** Header ****\n");
	printf("DDF_Header_GUID      ");
	print_guid(meta->hdr->DDF_Header_GUID);
	printf("\n");
	printf("DDF_rev              %8.8s\n", (char *)&meta->hdr->DDF_rev[0]);
	printf("Sequence_Number      0x%08x\n", GET32(meta, hdr->Sequence_Number));
	printf("TimeStamp            0x%08x\n", GET32(meta, hdr->TimeStamp));
	printf("Open_Flag            0x%02x\n", GET16(meta, hdr->Open_Flag));
	printf("Foreign_Flag         0x%02x\n", GET16(meta, hdr->Foreign_Flag));
	printf("Diskgrouping         0x%02x\n", GET16(meta, hdr->Diskgrouping));
	printf("Primary_Header_LBA   %ju\n", GET64(meta, hdr->Primary_Header_LBA));
	printf("Secondary_Header_LBA %ju\n", GET64(meta, hdr->Secondary_Header_LBA));
	printf("WorkSpace_Length     %u\n", GET32(meta, hdr->WorkSpace_Length));
	printf("WorkSpace_LBA        %ju\n", GET64(meta, hdr->WorkSpace_LBA));
	printf("Max_PD_Entries       %u\n", GET16(meta, hdr->Max_PD_Entries));
	printf("Max_VD_Entries       %u\n", GET16(meta, hdr->Max_VD_Entries));
	printf("Max_Partitions       %u\n", GET16(meta, hdr->Max_Partitions));
	printf("Configuration_Record_Length %u\n", GET16(meta, hdr->Configuration_Record_Length));
	printf("Max_Primary_Element_Entries %u\n", GET16(meta, hdr->Max_Primary_Element_Entries));
	printf("Controller Data      %u:%u\n", GET32(meta, hdr->cd_section), GET32(meta, hdr->cd_length));
	printf("Physical Disk        %u:%u\n", GET32(meta, hdr->pdr_section), GET32(meta, hdr->pdr_length));
	printf("Virtual Disk         %u:%u\n", GET32(meta, hdr->vdr_section), GET32(meta, hdr->vdr_length));
	printf("Configuration Recs   %u:%u\n", GET32(meta, hdr->cr_section), GET32(meta, hdr->cr_length));
	printf("Physical Disk Recs   %u:%u\n", GET32(meta, hdr->pdd_section), GET32(meta, hdr->pdd_length));
	printf("BBM Log              %u:%u\n", GET32(meta, hdr->bbmlog_section), GET32(meta, hdr->bbmlog_length));
	printf("Diagnostic Space     %u:%u\n", GET32(meta, hdr->Diagnostic_Space), GET32(meta, hdr->Diagnostic_Space_Length));
	printf("Vendor_Specific_Logs %u:%u\n", GET32(meta, hdr->Vendor_Specific_Logs), GET32(meta, hdr->Vendor_Specific_Logs_Length));
	printf("**** Controller Data ****\n");
	printf("Controller_GUID      ");
	print_guid(meta->cdr->Controller_GUID);
	printf("\n");
	printf("Controller_Type      0x%04x%04x 0x%04x%04x\n",
	    GET16(meta, cdr->Controller_Type.Vendor_ID),
	    GET16(meta, cdr->Controller_Type.Device_ID),
	    GET16(meta, cdr->Controller_Type.SubVendor_ID),
	    GET16(meta, cdr->Controller_Type.SubDevice_ID));
	printf("Product_ID           '%.16s'\n", (char *)&meta->cdr->Product_ID[0]);
	printf("**** Physical Disk Records ****\n");
	printf("Populated_PDEs       %u\n", GET16(meta, pdr->Populated_PDEs));
	printf("Max_PDE_Supported    %u\n", GET16(meta, pdr->Max_PDE_Supported));
	for (j = 0; j < GET16(meta, pdr->Populated_PDEs); j++) {
		if (isff(meta->pdr->entry[j].PD_GUID, 24))
			continue;
		if (GET32(meta, pdr->entry[j].PD_Reference) == 0xffffffff)
			continue;
		printf("PD_GUID              ");
		print_guid(meta->pdr->entry[j].PD_GUID);
		printf("\n");
		printf("PD_Reference         0x%08x\n",
		    GET32(meta, pdr->entry[j].PD_Reference));
		printf("PD_Type              0x%04x\n",
		    GET16(meta, pdr->entry[j].PD_Type));
		printf("PD_State             0x%04x\n",
		    GET16(meta, pdr->entry[j].PD_State));
		printf("Configured_Size      %ju\n",
		    GET64(meta, pdr->entry[j].Configured_Size));
		printf("Block_Size           %u\n",
		    GET16(meta, pdr->entry[j].Block_Size));
	}
	printf("**** Virtual Disk Records ****\n");
	printf("Populated_VDEs       %u\n", GET16(meta, vdr->Populated_VDEs));
	printf("Max_VDE_Supported    %u\n", GET16(meta, vdr->Max_VDE_Supported));
	for (j = 0; j < GET16(meta, vdr->Populated_VDEs); j++) {
		if (isff(meta->vdr->entry[j].VD_GUID, 24))
			continue;
		printf("VD_GUID              ");
		print_guid(meta->vdr->entry[j].VD_GUID);
		printf("\n");
		printf("VD_Number            0x%04x\n",
		    GET16(meta, vdr->entry[j].VD_Number));
		printf("VD_Type              0x%04x\n",
		    GET16(meta, vdr->entry[j].VD_Type));
		printf("VD_State             0x%02x\n",
		    GET8(meta, vdr->entry[j].VD_State));
		printf("Init_State           0x%02x\n",
		    GET8(meta, vdr->entry[j].Init_State));
		printf("Drive_Failures_Remaining %u\n",
		    GET8(meta, vdr->entry[j].Drive_Failures_Remaining));
		printf("VD_Name              '%.16s'\n",
		    (char *)&meta->vdr->entry[j].VD_Name);
	}
	printf("**** Configuration Records ****\n");
	num = GETCRNUM(meta);
	for (j = 0; j < num; j++) {
		vdc = GETVDCPTR(meta, j);
		val = GET32D(meta, vdc->Signature);
		switch (val) {
		case DDF_VDCR_SIGNATURE:
			printf("** Virtual Disk Configuration **\n");
			printf("VD_GUID              ");
			print_guid(vdc->VD_GUID);
			printf("\n");
			printf("Timestamp            0x%08x\n",
			    GET32D(meta, vdc->Timestamp));
			printf("Sequence_Number      0x%08x\n",
			    GET32D(meta, vdc->Sequence_Number));
			printf("Primary_Element_Count %u\n",
			    GET16D(meta, vdc->Primary_Element_Count));
			printf("Stripe_Size          %u\n",
			    GET8D(meta, vdc->Stripe_Size));
			printf("Primary_RAID_Level   0x%02x\n",
			    GET8D(meta, vdc->Primary_RAID_Level));
			printf("RLQ                  0x%02x\n",
			    GET8D(meta, vdc->RLQ));
			printf("Secondary_Element_Count %u\n",
			    GET8D(meta, vdc->Secondary_Element_Count));
			printf("Secondary_Element_Seq %u\n",
			    GET8D(meta, vdc->Secondary_Element_Seq));
			printf("Secondary_RAID_Level 0x%02x\n",
			    GET8D(meta, vdc->Secondary_RAID_Level));
			printf("Block_Count          %ju\n",
			    GET64D(meta, vdc->Block_Count));
			printf("VD_Size              %ju\n",
			    GET64D(meta, vdc->VD_Size));
			printf("Block_Size           %u\n",
			    GET16D(meta, vdc->Block_Size));
			printf("Rotate_Parity_count  %u\n",
			    GET8D(meta, vdc->Rotate_Parity_count));
			printf("Associated_Spare_Disks");
			for (i = 0; i < 8; i++) {
				if (GET32D(meta, vdc->Associated_Spares[i]) != 0xffffffff)
					printf(" 0x%08x", GET32D(meta, vdc->Associated_Spares[i]));
			}
			printf("\n");
			printf("Cache_Flags          %016jx\n",
			    GET64D(meta, vdc->Cache_Flags));
			printf("BG_Rate              %u\n",
			    GET8D(meta, vdc->BG_Rate));
			printf("MDF_Parity_Disks     %u\n",
			    GET8D(meta, vdc->MDF_Parity_Disks));
			printf("MDF_Parity_Generator_Polynomial 0x%04x\n",
			    GET16D(meta, vdc->MDF_Parity_Generator_Polynomial));
			printf("MDF_Constant_Generation_Method 0x%02x\n",
			    GET8D(meta, vdc->MDF_Constant_Generation_Method));
			printf("Physical_Disks      ");
			num2 = GET16D(meta, vdc->Primary_Element_Count);
			val2 = (uint64_t *)&(vdc->Physical_Disk_Sequence[GET16(meta, hdr->Max_Primary_Element_Entries)]);
			for (i = 0; i < num2; i++)
				printf(" 0x%08x @ %ju",
				    GET32D(meta, vdc->Physical_Disk_Sequence[i]),
				    GET64P(meta, val2 + i));
			printf("\n");
			break;
		case DDF_VUCR_SIGNATURE:
			printf("** Vendor Unique Configuration **\n");
			vuc = (struct ddf_vuc_record *)vdc;
			printf("VD_GUID              ");
			print_guid(vuc->VD_GUID);
			printf("\n");
			break;
		case DDF_SA_SIGNATURE:
			printf("** Spare Assignment Configuration **\n");
			sa = (struct ddf_sa_record *)vdc;
			printf("Timestamp            0x%08x\n",
			    GET32D(meta, sa->Timestamp));
			printf("Spare_Type           0x%02x\n",
			    GET8D(meta, sa->Spare_Type));
			printf("Populated_SAEs       %u\n",
			    GET16D(meta, sa->Populated_SAEs));
			printf("MAX_SAE_Supported    %u\n",
			    GET16D(meta, sa->MAX_SAE_Supported));
			for (i = 0; i < GET16D(meta, sa->Populated_SAEs); i++) {
				if (isff(sa->entry[i].VD_GUID, 24))
					continue;
				printf("VD_GUID             ");
				for (k = 0; k < 24; k++)
					printf("%02x", sa->entry[i].VD_GUID[k]);
				printf("\n");
				printf("Secondary_Element   %u\n",
				    GET16D(meta, sa->entry[i].Secondary_Element));
			}
			break;
		case 0x00000000:
		case 0xFFFFFFFF:
			break;
		default:
			printf("Unknown configuration signature %08x\n", val);
			break;
		}
	}
	printf("**** Physical Disk Data ****\n");
	printf("PD_GUID              ");
	print_guid(meta->pdd->PD_GUID);
	printf("\n");
	printf("PD_Reference         0x%08x\n",
	    GET32(meta, pdd->PD_Reference));
	printf("Forced_Ref_Flag      0x%02x\n",
	    GET8(meta, pdd->Forced_Ref_Flag));
	printf("Forced_PD_GUID_Flag  0x%02x\n",
	    GET8(meta, pdd->Forced_PD_GUID_Flag));
}

static int
ddf_meta_find_pd(struct ddf_meta *meta, uint8_t *GUID, uint32_t PD_Reference)
{
	int i;

	for (i = 0; i < GET16(meta, pdr->Populated_PDEs); i++) {
		if (GUID != NULL) {
			if (memcmp(meta->pdr->entry[i].PD_GUID, GUID, 24) == 0)
				return (i);
		} else if (PD_Reference != 0xffffffff) {
			if (GET32(meta, pdr->entry[i].PD_Reference) == PD_Reference)
				return (i);
		} else
			if (isff(meta->pdr->entry[i].PD_GUID, 24))
				return (i);
	}
	if (GUID == NULL && PD_Reference == 0xffffffff) {
		if (i >= GET16(meta, pdr->Max_PDE_Supported))
			return (-1);
		SET16(meta, pdr->Populated_PDEs, i + 1);
		return (i);
	}
	return (-1);
}

static int
ddf_meta_find_vd(struct ddf_meta *meta, uint8_t *GUID)
{
	int i;

	for (i = 0; i < GET16(meta, vdr->Populated_VDEs); i++) {
		if (GUID != NULL) {
			if (memcmp(meta->vdr->entry[i].VD_GUID, GUID, 24) == 0)
				return (i);
		} else
			if (isff(meta->vdr->entry[i].VD_GUID, 24))
				return (i);
	}
	if (GUID == NULL) {
		if (i >= GET16(meta, vdr->Max_VDE_Supported))
			return (-1);
		SET16(meta, vdr->Populated_VDEs, i + 1);
		return (i);
	}
	return (-1);
}

static struct ddf_vdc_record *
ddf_meta_find_vdc(struct ddf_meta *meta, uint8_t *GUID)
{
	struct ddf_vdc_record *vdc;
	int i, num;

	num = GETCRNUM(meta);
	for (i = 0; i < num; i++) {
		vdc = GETVDCPTR(meta, i);
		if (GUID != NULL) {
			if (GET32D(meta, vdc->Signature) == DDF_VDCR_SIGNATURE &&
			    memcmp(vdc->VD_GUID, GUID, 24) == 0)
				return (vdc);
		} else
			if (GET32D(meta, vdc->Signature) == 0xffffffff ||
			    GET32D(meta, vdc->Signature) == 0)
				return (vdc);
	}
	return (NULL);
}

static int
ddf_meta_count_vdc(struct ddf_meta *meta, uint8_t *GUID)
{
	struct ddf_vdc_record *vdc;
	int i, num, cnt;

	cnt = 0;
	num = GETCRNUM(meta);
	for (i = 0; i < num; i++) {
		vdc = GETVDCPTR(meta, i);
		if (GET32D(meta, vdc->Signature) != DDF_VDCR_SIGNATURE)
			continue;
		if (GUID == NULL || memcmp(vdc->VD_GUID, GUID, 24) == 0)
			cnt++;
	}
	return (cnt);
}

static int
ddf_meta_find_disk(struct ddf_vol_meta *vmeta, uint32_t PD_Reference,
    int *bvdp, int *posp)
{
	int i, bvd, pos;

	i = 0;
	for (bvd = 0; bvd < GET8(vmeta, vdc->Secondary_Element_Count); bvd++) {
		if (vmeta->bvdc[bvd] == NULL) {
			i += GET16(vmeta, vdc->Primary_Element_Count); // XXX
			continue;
		}
		for (pos = 0; pos < GET16(vmeta, bvdc[bvd]->Primary_Element_Count);
		    pos++, i++) {
			if (GET32(vmeta, bvdc[bvd]->Physical_Disk_Sequence[pos]) ==
			    PD_Reference) {
				if (bvdp != NULL)
					*bvdp = bvd;
				if (posp != NULL)
					*posp = pos;
				return (i);
			}
		}
	}
	return (-1);
}

static struct ddf_sa_record *
ddf_meta_find_sa(struct ddf_meta *meta, int create)
{
	struct ddf_sa_record *sa;
	int i, num;

	num = GETCRNUM(meta);
	for (i = 0; i < num; i++) {
		sa = GETSAPTR(meta, i);
		if (GET32D(meta, sa->Signature) == DDF_SA_SIGNATURE)
			return (sa);
	}
	if (create) {
		for (i = 0; i < num; i++) {
			sa = GETSAPTR(meta, i);
			if (GET32D(meta, sa->Signature) == 0xffffffff ||
			    GET32D(meta, sa->Signature) == 0)
				return (sa);
		}
	}
	return (NULL);
}

static void
ddf_meta_create(struct g_raid_disk *disk, struct ddf_meta *sample)
{
	struct timespec ts;
	struct clocktime ct;
	struct g_raid_md_ddf_perdisk *pd;
	struct g_raid_md_ddf_object *mdi;
	struct ddf_meta *meta;
	struct ddf_pd_entry *pde;
	off_t anchorlba;
	u_int ss, pos, size;
	int len, error;
	char serial_buffer[DISK_IDENT_SIZE];

	if (sample->hdr == NULL)
		sample = NULL;

	mdi = (struct g_raid_md_ddf_object *)disk->d_softc->sc_md;
	pd = (struct g_raid_md_ddf_perdisk *)disk->d_md_data;
	meta = &pd->pd_meta;
	ss = disk->d_consumer->provider->sectorsize;
	anchorlba = disk->d_consumer->provider->mediasize / ss - 1;

	meta->sectorsize = ss;
	meta->bigendian = sample ? sample->bigendian : mdi->mdio_bigendian;
	getnanotime(&ts);
	clock_ts_to_ct(&ts, &ct);

	/* Header */
	meta->hdr = malloc(ss, M_MD_DDF, M_WAITOK);
	memset(meta->hdr, 0xff, ss);
	if (sample) {
		memcpy(meta->hdr, sample->hdr, sizeof(struct ddf_header));
		if (ss != sample->sectorsize) {
			SET32(meta, hdr->WorkSpace_Length,
			    howmany(GET32(sample, hdr->WorkSpace_Length) *
			        sample->sectorsize, ss));
			SET16(meta, hdr->Configuration_Record_Length,
			    howmany(GET16(sample,
			        hdr->Configuration_Record_Length) *
				sample->sectorsize, ss));
			SET32(meta, hdr->cd_length,
			    howmany(GET32(sample, hdr->cd_length) *
			        sample->sectorsize, ss));
			SET32(meta, hdr->pdr_length,
			    howmany(GET32(sample, hdr->pdr_length) *
			        sample->sectorsize, ss));
			SET32(meta, hdr->vdr_length,
			    howmany(GET32(sample, hdr->vdr_length) *
			        sample->sectorsize, ss));
			SET32(meta, hdr->cr_length,
			    howmany(GET32(sample, hdr->cr_length) *
			        sample->sectorsize, ss));
			SET32(meta, hdr->pdd_length,
			    howmany(GET32(sample, hdr->pdd_length) *
			        sample->sectorsize, ss));
			SET32(meta, hdr->bbmlog_length,
			    howmany(GET32(sample, hdr->bbmlog_length) *
			        sample->sectorsize, ss));
			SET32(meta, hdr->Diagnostic_Space,
			    howmany(GET32(sample, hdr->bbmlog_length) *
			        sample->sectorsize, ss));
			SET32(meta, hdr->Vendor_Specific_Logs,
			    howmany(GET32(sample, hdr->bbmlog_length) *
			        sample->sectorsize, ss));
		}
	} else {
		SET32(meta, hdr->Signature, DDF_HEADER_SIGNATURE);
		snprintf(meta->hdr->DDF_Header_GUID, 25, "FreeBSD %08x%08x",
		    (u_int)(ts.tv_sec - DECADE), arc4random());
		memcpy(meta->hdr->DDF_rev, "02.00.00", 8);
		SET32(meta, hdr->TimeStamp, (ts.tv_sec - DECADE));
		SET32(meta, hdr->WorkSpace_Length, 16 * 1024 * 1024 / ss);
		SET16(meta, hdr->Max_PD_Entries, DDF_MAX_DISKS - 1);
		SET16(meta, hdr->Max_VD_Entries, DDF_MAX_VDISKS);
		SET16(meta, hdr->Max_Partitions, DDF_MAX_PARTITIONS);
		SET16(meta, hdr->Max_Primary_Element_Entries, DDF_MAX_DISKS);
		SET16(meta, hdr->Configuration_Record_Length,
		    howmany(sizeof(struct ddf_vdc_record) + (4 + 8) *
		        GET16(meta, hdr->Max_Primary_Element_Entries), ss));
		SET32(meta, hdr->cd_length,
		    howmany(sizeof(struct ddf_cd_record), ss));
		SET32(meta, hdr->pdr_length,
		    howmany(sizeof(struct ddf_pd_record) +
		        sizeof(struct ddf_pd_entry) * GET16(meta,
			hdr->Max_PD_Entries), ss));
		SET32(meta, hdr->vdr_length,
		    howmany(sizeof(struct ddf_vd_record) +
		        sizeof(struct ddf_vd_entry) *
			GET16(meta, hdr->Max_VD_Entries), ss));
		SET32(meta, hdr->cr_length,
		    GET16(meta, hdr->Configuration_Record_Length) *
		    (GET16(meta, hdr->Max_Partitions) + 1));
		SET32(meta, hdr->pdd_length,
		    howmany(sizeof(struct ddf_pdd_record), ss));
		SET32(meta, hdr->bbmlog_length, 0);
		SET32(meta, hdr->Diagnostic_Space_Length, 0);
		SET32(meta, hdr->Vendor_Specific_Logs_Length, 0);
	}
	pos = 1;
	SET32(meta, hdr->cd_section, pos);
	pos += GET32(meta, hdr->cd_length);
	SET32(meta, hdr->pdr_section, pos);
	pos += GET32(meta, hdr->pdr_length);
	SET32(meta, hdr->vdr_section, pos);
	pos += GET32(meta, hdr->vdr_length);
	SET32(meta, hdr->cr_section, pos);
	pos += GET32(meta, hdr->cr_length);
	SET32(meta, hdr->pdd_section, pos);
	pos += GET32(meta, hdr->pdd_length);
	SET32(meta, hdr->bbmlog_section,
	    GET32(meta, hdr->bbmlog_length) != 0 ? pos : 0xffffffff);
	pos += GET32(meta, hdr->bbmlog_length);
	SET32(meta, hdr->Diagnostic_Space,
	    GET32(meta, hdr->Diagnostic_Space_Length) != 0 ? pos : 0xffffffff);
	pos += GET32(meta, hdr->Diagnostic_Space_Length);
	SET32(meta, hdr->Vendor_Specific_Logs,
	    GET32(meta, hdr->Vendor_Specific_Logs_Length) != 0 ? pos : 0xffffffff);
	pos += min(GET32(meta, hdr->Vendor_Specific_Logs_Length), 1);
	SET64(meta, hdr->Primary_Header_LBA,
	    anchorlba - pos);
	SET64(meta, hdr->Secondary_Header_LBA,
	    0xffffffffffffffffULL);
	SET64(meta, hdr->WorkSpace_LBA,
	    anchorlba + 1 - 32 * 1024 * 1024 / ss);

	/* Controller Data */
	size = GET32(meta, hdr->cd_length) * ss;
	meta->cdr = malloc(size, M_MD_DDF, M_WAITOK);
	memset(meta->cdr, 0xff, size);
	SET32(meta, cdr->Signature, DDF_CONTROLLER_DATA_SIGNATURE);
	memcpy(meta->cdr->Controller_GUID, "FreeBSD GEOM RAID SERIAL", 24);
	memcpy(meta->cdr->Product_ID, "FreeBSD GEOMRAID", 16);

	/* Physical Drive Records. */
	size = GET32(meta, hdr->pdr_length) * ss;
	meta->pdr = malloc(size, M_MD_DDF, M_WAITOK);
	memset(meta->pdr, 0xff, size);
	SET32(meta, pdr->Signature, DDF_PDR_SIGNATURE);
	SET16(meta, pdr->Populated_PDEs, 1);
	SET16(meta, pdr->Max_PDE_Supported,
	    GET16(meta, hdr->Max_PD_Entries));

	pde = &meta->pdr->entry[0];
	len = sizeof(serial_buffer);
	error = g_io_getattr("GEOM::ident", disk->d_consumer, &len, serial_buffer);
	if (error == 0 && (len = strlen (serial_buffer)) >= 6 && len <= 20)
		snprintf(pde->PD_GUID, 25, "DISK%20s", serial_buffer);
	else
		snprintf(pde->PD_GUID, 25, "DISK%04d%02d%02d%08x%04x",
		    ct.year, ct.mon, ct.day,
		    arc4random(), arc4random() & 0xffff);
	SET32D(meta, pde->PD_Reference, arc4random());
	SET16D(meta, pde->PD_Type, DDF_PDE_GUID_FORCE);
	SET16D(meta, pde->PD_State, 0);
	SET64D(meta, pde->Configured_Size,
	    anchorlba + 1 - 32 * 1024 * 1024 / ss);
	SET16D(meta, pde->Block_Size, ss);

	/* Virtual Drive Records. */
	size = GET32(meta, hdr->vdr_length) * ss;
	meta->vdr = malloc(size, M_MD_DDF, M_WAITOK);
	memset(meta->vdr, 0xff, size);
	SET32(meta, vdr->Signature, DDF_VD_RECORD_SIGNATURE);
	SET32(meta, vdr->Populated_VDEs, 0);
	SET16(meta, vdr->Max_VDE_Supported,
	    GET16(meta, hdr->Max_VD_Entries));

	/* Configuration Records. */
	size = GET32(meta, hdr->cr_length) * ss;
	meta->cr = malloc(size, M_MD_DDF, M_WAITOK);
	memset(meta->cr, 0xff, size);

	/* Physical Disk Data. */
	size = GET32(meta, hdr->pdd_length) * ss;
	meta->pdd = malloc(size, M_MD_DDF, M_WAITOK);
	memset(meta->pdd, 0xff, size);
	SET32(meta, pdd->Signature, DDF_PDD_SIGNATURE);
	memcpy(meta->pdd->PD_GUID, pde->PD_GUID, 24);
	SET32(meta, pdd->PD_Reference, GET32D(meta, pde->PD_Reference));
	SET8(meta, pdd->Forced_Ref_Flag, DDF_PDD_FORCED_REF);
	SET8(meta, pdd->Forced_PD_GUID_Flag, DDF_PDD_FORCED_GUID);

	/* Bad Block Management Log. */
	if (GET32(meta, hdr->bbmlog_length) != 0) {
		size = GET32(meta, hdr->bbmlog_length) * ss;
		meta->bbm = malloc(size, M_MD_DDF, M_WAITOK);
		memset(meta->bbm, 0xff, size);
		SET32(meta, bbm->Signature, DDF_BBML_SIGNATURE);
		SET32(meta, bbm->Entry_Count, 0);
		SET32(meta, bbm->Spare_Block_Count, 0);
	}
}

static void
ddf_meta_copy(struct ddf_meta *dst, struct ddf_meta *src)
{
	u_int ss;

	dst->bigendian = src->bigendian;
	ss = dst->sectorsize = src->sectorsize;
	dst->hdr = malloc(ss, M_MD_DDF, M_WAITOK);
	memcpy(dst->hdr, src->hdr, ss);
	dst->cdr = malloc(GET32(src, hdr->cd_length) * ss, M_MD_DDF, M_WAITOK);
	memcpy(dst->cdr, src->cdr, GET32(src, hdr->cd_length) * ss);
	dst->pdr = malloc(GET32(src, hdr->pdr_length) * ss, M_MD_DDF, M_WAITOK);
	memcpy(dst->pdr, src->pdr, GET32(src, hdr->pdr_length) * ss);
	dst->vdr = malloc(GET32(src, hdr->vdr_length) * ss, M_MD_DDF, M_WAITOK);
	memcpy(dst->vdr, src->vdr, GET32(src, hdr->vdr_length) * ss);
	dst->cr = malloc(GET32(src, hdr->cr_length) * ss, M_MD_DDF, M_WAITOK);
	memcpy(dst->cr, src->cr, GET32(src, hdr->cr_length) * ss);
	dst->pdd = malloc(GET32(src, hdr->pdd_length) * ss, M_MD_DDF, M_WAITOK);
	memcpy(dst->pdd, src->pdd, GET32(src, hdr->pdd_length) * ss);
	if (src->bbm != NULL) {
		dst->bbm = malloc(GET32(src, hdr->bbmlog_length) * ss, M_MD_DDF, M_WAITOK);
		memcpy(dst->bbm, src->bbm, GET32(src, hdr->bbmlog_length) * ss);
	}
}

static void
ddf_meta_update(struct ddf_meta *meta, struct ddf_meta *src)
{
	struct ddf_pd_entry *pde, *spde;
	int i, j;

	for (i = 0; i < GET16(src, pdr->Populated_PDEs); i++) {
		spde = &src->pdr->entry[i];
		if (isff(spde->PD_GUID, 24))
			continue;
		j = ddf_meta_find_pd(meta, NULL,
		    GET32(src, pdr->entry[i].PD_Reference));
		if (j < 0) {
			j = ddf_meta_find_pd(meta, NULL, 0xffffffff);
			pde = &meta->pdr->entry[j];
			memcpy(pde, spde, sizeof(*pde));
		} else {
			pde = &meta->pdr->entry[j];
			SET16D(meta, pde->PD_State,
			    GET16D(meta, pde->PD_State) |
			    GET16D(src, pde->PD_State));
		}
	}
}

static void
ddf_meta_free(struct ddf_meta *meta)
{

	if (meta->hdr != NULL) {
		free(meta->hdr, M_MD_DDF);
		meta->hdr = NULL;
	}
	if (meta->cdr != NULL) {
		free(meta->cdr, M_MD_DDF);
		meta->cdr = NULL;
	}
	if (meta->pdr != NULL) {
		free(meta->pdr, M_MD_DDF);
		meta->pdr = NULL;
	}
	if (meta->vdr != NULL) {
		free(meta->vdr, M_MD_DDF);
		meta->vdr = NULL;
	}
	if (meta->cr != NULL) {
		free(meta->cr, M_MD_DDF);
		meta->cr = NULL;
	}
	if (meta->pdd != NULL) {
		free(meta->pdd, M_MD_DDF);
		meta->pdd = NULL;
	}
	if (meta->bbm != NULL) {
		free(meta->bbm, M_MD_DDF);
		meta->bbm = NULL;
	}
}

static void
ddf_vol_meta_create(struct ddf_vol_meta *meta, struct ddf_meta *sample)
{
	struct timespec ts;
	struct clocktime ct;
	u_int ss, size;

	meta->bigendian = sample->bigendian;
	ss = meta->sectorsize = sample->sectorsize;
	meta->hdr = malloc(ss, M_MD_DDF, M_WAITOK);
	memcpy(meta->hdr, sample->hdr, ss);
	meta->cdr = malloc(GET32(sample, hdr->cd_length) * ss, M_MD_DDF, M_WAITOK);
	memcpy(meta->cdr, sample->cdr, GET32(sample, hdr->cd_length) * ss);
	meta->vde = malloc(sizeof(struct ddf_vd_entry), M_MD_DDF, M_WAITOK);
	memset(meta->vde, 0xff, sizeof(struct ddf_vd_entry));
	getnanotime(&ts);
	clock_ts_to_ct(&ts, &ct);
	snprintf(meta->vde->VD_GUID, 25, "FreeBSD%04d%02d%02d%08x%01x",
	    ct.year, ct.mon, ct.day,
	    arc4random(), arc4random() & 0xf);
	size = GET16(sample, hdr->Configuration_Record_Length) * ss;
	meta->vdc = malloc(size, M_MD_DDF, M_WAITOK);
	memset(meta->vdc, 0xff, size);
	SET32(meta, vdc->Signature, DDF_VDCR_SIGNATURE);
	memcpy(meta->vdc->VD_GUID, meta->vde->VD_GUID, 24);
	SET32(meta, vdc->Sequence_Number, 0);
}

static void
ddf_vol_meta_update(struct ddf_vol_meta *dst, struct ddf_meta *src,
    uint8_t *GUID, int started)
{
	struct ddf_vd_entry *vde;
	struct ddf_vdc_record *vdc;
	int vnew, bvnew, bvd, size;
	u_int ss;

	vde = &src->vdr->entry[ddf_meta_find_vd(src, GUID)];
	vdc = ddf_meta_find_vdc(src, GUID);
	if (GET8D(src, vdc->Secondary_Element_Count) == 1)
		bvd = 0;
	else
		bvd = GET8D(src, vdc->Secondary_Element_Seq);
	size = GET16(src, hdr->Configuration_Record_Length) * src->sectorsize;

	if (dst->vdc == NULL ||
	    (!started && ((int32_t)(GET32D(src, vdc->Sequence_Number) -
	    GET32(dst, vdc->Sequence_Number))) > 0))
		vnew = 1;
	else
		vnew = 0;

	if (dst->bvdc[bvd] == NULL ||
	    (!started && ((int32_t)(GET32D(src, vdc->Sequence_Number) -
	    GET32(dst, bvdc[bvd]->Sequence_Number))) > 0))
		bvnew = 1;
	else
		bvnew = 0;

	if (vnew) {
		dst->bigendian = src->bigendian;
		ss = dst->sectorsize = src->sectorsize;
		if (dst->hdr != NULL)
			free(dst->hdr, M_MD_DDF);
		dst->hdr = malloc(ss, M_MD_DDF, M_WAITOK);
		memcpy(dst->hdr, src->hdr, ss);
		if (dst->cdr != NULL)
			free(dst->cdr, M_MD_DDF);
		dst->cdr = malloc(GET32(src, hdr->cd_length) * ss, M_MD_DDF, M_WAITOK);
		memcpy(dst->cdr, src->cdr, GET32(src, hdr->cd_length) * ss);
		if (dst->vde != NULL)
			free(dst->vde, M_MD_DDF);
		dst->vde = malloc(sizeof(struct ddf_vd_entry), M_MD_DDF, M_WAITOK);
		memcpy(dst->vde, vde, sizeof(struct ddf_vd_entry));
		if (dst->vdc != NULL)
			free(dst->vdc, M_MD_DDF);
		dst->vdc = malloc(size, M_MD_DDF, M_WAITOK);
		memcpy(dst->vdc, vdc, size);
	}
	if (bvnew) {
		if (dst->bvdc[bvd] != NULL)
			free(dst->bvdc[bvd], M_MD_DDF);
		dst->bvdc[bvd] = malloc(size, M_MD_DDF, M_WAITOK);
		memcpy(dst->bvdc[bvd], vdc, size);
	}
}

static void
ddf_vol_meta_free(struct ddf_vol_meta *meta)
{
	int i;

	if (meta->hdr != NULL) {
		free(meta->hdr, M_MD_DDF);
		meta->hdr = NULL;
	}
	if (meta->cdr != NULL) {
		free(meta->cdr, M_MD_DDF);
		meta->cdr = NULL;
	}
	if (meta->vde != NULL) {
		free(meta->vde, M_MD_DDF);
		meta->vde = NULL;
	}
	if (meta->vdc != NULL) {
		free(meta->vdc, M_MD_DDF);
		meta->vdc = NULL;
	}
	for (i = 0; i < DDF_MAX_DISKS_HARD; i++) {
		if (meta->bvdc[i] != NULL) {
			free(meta->bvdc[i], M_MD_DDF);
			meta->bvdc[i] = NULL;
		}
	}
}

static int
ddf_meta_unused_range(struct ddf_meta *meta, off_t *off, off_t *size)
{
	struct ddf_vdc_record *vdc;
	off_t beg[32], end[32], beg1, end1;
	uint64_t *offp;
	int i, j, n, num, pos;
	uint32_t ref;

	*off = 0;
	*size = 0;
	ref = GET32(meta, pdd->PD_Reference);
	pos = ddf_meta_find_pd(meta, NULL, ref);
	beg[0] = 0;
	end[0] = GET64(meta, pdr->entry[pos].Configured_Size);
	n = 1;
	num = GETCRNUM(meta);
	for (i = 0; i < num; i++) {
		vdc = GETVDCPTR(meta, i);
		if (GET32D(meta, vdc->Signature) != DDF_VDCR_SIGNATURE)
			continue;
		for (pos = 0; pos < GET16D(meta, vdc->Primary_Element_Count); pos++)
			if (GET32D(meta, vdc->Physical_Disk_Sequence[pos]) == ref)
				break;
		if (pos == GET16D(meta, vdc->Primary_Element_Count))
			continue;
		offp = (uint64_t *)&(vdc->Physical_Disk_Sequence[
		    GET16(meta, hdr->Max_Primary_Element_Entries)]);
		beg1 = GET64P(meta, offp + pos);
		end1 = beg1 + GET64D(meta, vdc->Block_Count);
		for (j = 0; j < n; j++) {
			if (beg[j] >= end1 || end[j] <= beg1 )
				continue;
			if (beg[j] < beg1 && end[j] > end1) {
				beg[n] = end1;
				end[n] = end[j];
				end[j] = beg1;
				n++;
			} else if (beg[j] < beg1)
				end[j] = beg1;
			else
				beg[j] = end1;
		}
	}
	for (j = 0; j < n; j++) {
		if (end[j] - beg[j] > *size) {
			*off = beg[j];
			*size = end[j] - beg[j];
		}
	}
	return ((*size > 0) ? 1 : 0);
}

static void
ddf_meta_get_name(struct ddf_meta *meta, int num, char *buf)
{
	const char *b;
	int i;

	b = meta->vdr->entry[num].VD_Name;
	for (i = 15; i >= 0; i--)
		if (b[i] != 0x20)
			break;
	memcpy(buf, b, i + 1);
	buf[i + 1] = 0;
}

static void
ddf_meta_put_name(struct ddf_vol_meta *meta, char *buf)
{
	int len;

	len = min(strlen(buf), 16);
	memset(meta->vde->VD_Name, 0x20, 16);
	memcpy(meta->vde->VD_Name, buf, len);
}

static int
ddf_meta_read(struct g_consumer *cp, struct ddf_meta *meta)
{
	struct g_provider *pp;
	struct ddf_header *ahdr, *hdr;
	char *abuf, *buf;
	off_t plba, slba, lba;
	int error, len, i;
	u_int ss;
	uint32_t val;

	ddf_meta_free(meta);
	pp = cp->provider;
	ss = meta->sectorsize = pp->sectorsize;
	/* Read anchor block. */
	abuf = g_read_data(cp, pp->mediasize - ss, ss, &error);
	if (abuf == NULL) {
		G_RAID_DEBUG(1, "Cannot read metadata from %s (error=%d).",
		    pp->name, error);
		return (error);
	}
	ahdr = (struct ddf_header *)abuf;

	/* Check if this is an DDF RAID struct */
	if (be32dec(&ahdr->Signature) == DDF_HEADER_SIGNATURE)
		meta->bigendian = 1;
	else if (le32dec(&ahdr->Signature) == DDF_HEADER_SIGNATURE)
		meta->bigendian = 0;
	else {
		G_RAID_DEBUG(1, "DDF signature check failed on %s", pp->name);
		error = EINVAL;
		goto done;
	}
	if (ahdr->Header_Type != DDF_HEADER_ANCHOR) {
		G_RAID_DEBUG(1, "DDF header type check failed on %s", pp->name);
		error = EINVAL;
		goto done;
	}
	meta->hdr = ahdr;
	plba = GET64(meta, hdr->Primary_Header_LBA);
	slba = GET64(meta, hdr->Secondary_Header_LBA);
	val = GET32(meta, hdr->CRC);
	SET32(meta, hdr->CRC, 0xffffffff);
	meta->hdr = NULL;
	if (crc32(ahdr, ss) != val) {
		G_RAID_DEBUG(1, "DDF CRC mismatch on %s", pp->name);
		error = EINVAL;
		goto done;
	}
	if ((plba + 6) * ss >= pp->mediasize) {
		G_RAID_DEBUG(1, "DDF primary header LBA is wrong on %s", pp->name);
		error = EINVAL;
		goto done;
	}
	if (slba != -1 && (slba + 6) * ss >= pp->mediasize) {
		G_RAID_DEBUG(1, "DDF secondary header LBA is wrong on %s", pp->name);
		error = EINVAL;
		goto done;
	}
	lba = plba;

doread:
	error = 0;
	ddf_meta_free(meta);

	/* Read header block. */
	buf = g_read_data(cp, lba * ss, ss, &error);
	if (buf == NULL) {
readerror:
		G_RAID_DEBUG(1, "DDF %s metadata read error on %s (error=%d).",
		    (lba == plba) ? "primary" : "secondary", pp->name, error);
		if (lba == plba && slba != -1) {
			lba = slba;
			goto doread;
		}
		G_RAID_DEBUG(1, "DDF metadata read error on %s.", pp->name);
		goto done;
	}
	meta->hdr = malloc(ss, M_MD_DDF, M_WAITOK);
	memcpy(meta->hdr, buf, ss);
	g_free(buf);
	hdr = meta->hdr;
	val = GET32(meta, hdr->CRC);
	SET32(meta, hdr->CRC, 0xffffffff);
	if (hdr->Signature != ahdr->Signature ||
	    crc32(meta->hdr, ss) != val ||
	    memcmp(hdr->DDF_Header_GUID, ahdr->DDF_Header_GUID, 24) ||
	    GET64(meta, hdr->Primary_Header_LBA) != plba ||
	    GET64(meta, hdr->Secondary_Header_LBA) != slba) {
hdrerror:
		G_RAID_DEBUG(1, "DDF %s metadata check failed on %s",
		    (lba == plba) ? "primary" : "secondary", pp->name);
		if (lba == plba && slba != -1) {
			lba = slba;
			goto doread;
		}
		G_RAID_DEBUG(1, "DDF metadata check failed on %s", pp->name);
		error = EINVAL;
		goto done;
	}
	if ((lba == plba && hdr->Header_Type != DDF_HEADER_PRIMARY) ||
	    (lba == slba && hdr->Header_Type != DDF_HEADER_SECONDARY))
		goto hdrerror;
	len = 1;
	len = max(len, GET32(meta, hdr->cd_section) + GET32(meta, hdr->cd_length));
	len = max(len, GET32(meta, hdr->pdr_section) + GET32(meta, hdr->pdr_length));
	len = max(len, GET32(meta, hdr->vdr_section) + GET32(meta, hdr->vdr_length));
	len = max(len, GET32(meta, hdr->cr_section) + GET32(meta, hdr->cr_length));
	len = max(len, GET32(meta, hdr->pdd_section) + GET32(meta, hdr->pdd_length));
	if ((val = GET32(meta, hdr->bbmlog_section)) != 0xffffffff)
		len = max(len, val + GET32(meta, hdr->bbmlog_length));
	if ((val = GET32(meta, hdr->Diagnostic_Space)) != 0xffffffff)
		len = max(len, val + GET32(meta, hdr->Diagnostic_Space_Length));
	if ((val = GET32(meta, hdr->Vendor_Specific_Logs)) != 0xffffffff)
		len = max(len, val + GET32(meta, hdr->Vendor_Specific_Logs_Length));
	if ((plba + len) * ss >= pp->mediasize)
		goto hdrerror;
	if (slba != -1 && (slba + len) * ss >= pp->mediasize)
		goto hdrerror;
	/* Workaround for Adaptec implementation. */
	if (GET16(meta, hdr->Max_Primary_Element_Entries) == 0xffff) {
		SET16(meta, hdr->Max_Primary_Element_Entries,
		    min(GET16(meta, hdr->Max_PD_Entries),
		    (GET16(meta, hdr->Configuration_Record_Length) * ss - 512) / 12));
	}

	if (GET32(meta, hdr->cd_length) * ss >= MAXPHYS ||
	    GET32(meta, hdr->pdr_length) * ss >= MAXPHYS ||
	    GET32(meta, hdr->vdr_length) * ss >= MAXPHYS ||
	    GET32(meta, hdr->cr_length) * ss >= MAXPHYS ||
	    GET32(meta, hdr->pdd_length) * ss >= MAXPHYS ||
	    GET32(meta, hdr->bbmlog_length) * ss >= MAXPHYS) {
		G_RAID_DEBUG(1, "%s: Blocksize is too big.", pp->name);
		goto hdrerror;
	}

	/* Read controller data. */
	buf = g_read_data(cp, (lba + GET32(meta, hdr->cd_section)) * ss,
	    GET32(meta, hdr->cd_length) * ss, &error);
	if (buf == NULL)
		goto readerror;
	meta->cdr = malloc(GET32(meta, hdr->cd_length) * ss, M_MD_DDF, M_WAITOK);
	memcpy(meta->cdr, buf, GET32(meta, hdr->cd_length) * ss);
	g_free(buf);
	if (GET32(meta, cdr->Signature) != DDF_CONTROLLER_DATA_SIGNATURE)
		goto hdrerror;

	/* Read physical disk records. */
	buf = g_read_data(cp, (lba + GET32(meta, hdr->pdr_section)) * ss,
	    GET32(meta, hdr->pdr_length) * ss, &error);
	if (buf == NULL)
		goto readerror;
	meta->pdr = malloc(GET32(meta, hdr->pdr_length) * ss, M_MD_DDF, M_WAITOK);
	memcpy(meta->pdr, buf, GET32(meta, hdr->pdr_length) * ss);
	g_free(buf);
	if (GET32(meta, pdr->Signature) != DDF_PDR_SIGNATURE)
		goto hdrerror;
	/*
	 * Workaround for reading metadata corrupted due to graid bug.
	 * XXX: Remove this before we have disks above 128PB. :)
	 */
	if (meta->bigendian) {
		for (i = 0; i < GET16(meta, pdr->Populated_PDEs); i++) {
			if (isff(meta->pdr->entry[i].PD_GUID, 24))
				continue;
			if (GET32(meta, pdr->entry[i].PD_Reference) ==
			    0xffffffff)
				continue;
			if (GET64(meta, pdr->entry[i].Configured_Size) >=
			     (1ULL << 48)) {
				SET16(meta, pdr->entry[i].PD_State,
				    GET16(meta, pdr->entry[i].PD_State) &
				    ~DDF_PDE_FAILED);
				SET64(meta, pdr->entry[i].Configured_Size,
				    GET64(meta, pdr->entry[i].Configured_Size) &
				    ((1ULL << 48) - 1));
			}
		}
	}

	/* Read virtual disk records. */
	buf = g_read_data(cp, (lba + GET32(meta, hdr->vdr_section)) * ss,
	    GET32(meta, hdr->vdr_length) * ss, &error);
	if (buf == NULL)
		goto readerror;
	meta->vdr = malloc(GET32(meta, hdr->vdr_length) * ss, M_MD_DDF, M_WAITOK);
	memcpy(meta->vdr, buf, GET32(meta, hdr->vdr_length) * ss);
	g_free(buf);
	if (GET32(meta, vdr->Signature) != DDF_VD_RECORD_SIGNATURE)
		goto hdrerror;

	/* Read configuration records. */
	buf = g_read_data(cp, (lba + GET32(meta, hdr->cr_section)) * ss,
	    GET32(meta, hdr->cr_length) * ss, &error);
	if (buf == NULL)
		goto readerror;
	meta->cr = malloc(GET32(meta, hdr->cr_length) * ss, M_MD_DDF, M_WAITOK);
	memcpy(meta->cr, buf, GET32(meta, hdr->cr_length) * ss);
	g_free(buf);

	/* Read physical disk data. */
	buf = g_read_data(cp, (lba + GET32(meta, hdr->pdd_section)) * ss,
	    GET32(meta, hdr->pdd_length) * ss, &error);
	if (buf == NULL)
		goto readerror;
	meta->pdd = malloc(GET32(meta, hdr->pdd_length) * ss, M_MD_DDF, M_WAITOK);
	memcpy(meta->pdd, buf, GET32(meta, hdr->pdd_length) * ss);
	g_free(buf);
	if (GET32(meta, pdd->Signature) != DDF_PDD_SIGNATURE)
		goto hdrerror;
	i = ddf_meta_find_pd(meta, NULL, GET32(meta, pdd->PD_Reference));
	if (i < 0)
		goto hdrerror;

	/* Read BBM Log. */
	if (GET32(meta, hdr->bbmlog_section) != 0xffffffff &&
	    GET32(meta, hdr->bbmlog_length) != 0) {
		buf = g_read_data(cp, (lba + GET32(meta, hdr->bbmlog_section)) * ss,
		    GET32(meta, hdr->bbmlog_length) * ss, &error);
		if (buf == NULL)
			goto readerror;
		meta->bbm = malloc(GET32(meta, hdr->bbmlog_length) * ss, M_MD_DDF, M_WAITOK);
		memcpy(meta->bbm, buf, GET32(meta, hdr->bbmlog_length) * ss);
		g_free(buf);
		if (GET32(meta, bbm->Signature) != DDF_BBML_SIGNATURE)
			goto hdrerror;
	}

done:
	g_free(abuf);
	if (error != 0)
		ddf_meta_free(meta);
	return (error);
}

static int
ddf_meta_write(struct g_consumer *cp, struct ddf_meta *meta)
{
	struct g_provider *pp;
	struct ddf_vdc_record *vdc;
	off_t alba, plba, slba, lba;
	u_int ss, size;
	int error, i, num;

	pp = cp->provider;
	ss = pp->sectorsize;
	lba = alba = pp->mediasize / ss - 1;
	plba = GET64(meta, hdr->Primary_Header_LBA);
	slba = GET64(meta, hdr->Secondary_Header_LBA);

next:
	SET8(meta, hdr->Header_Type, (lba == alba) ? DDF_HEADER_ANCHOR :
	    (lba == plba) ? DDF_HEADER_PRIMARY : DDF_HEADER_SECONDARY);
	SET32(meta, hdr->CRC, 0xffffffff);
	SET32(meta, hdr->CRC, crc32(meta->hdr, ss));
	error = g_write_data(cp, lba * ss, meta->hdr, ss);
	if (error != 0) {
err:
		G_RAID_DEBUG(1, "Cannot write metadata to %s (error=%d).",
		    pp->name, error);
		if (lba != alba)
			goto done;
	}
	if (lba == alba) {
		lba = plba;
		goto next;
	}

	size = GET32(meta, hdr->cd_length) * ss;
	SET32(meta, cdr->CRC, 0xffffffff);
	SET32(meta, cdr->CRC, crc32(meta->cdr, size));
	error = g_write_data(cp, (lba + GET32(meta, hdr->cd_section)) * ss,
	    meta->cdr, size);
	if (error != 0)
		goto err;

	size = GET32(meta, hdr->pdr_length) * ss;
	SET32(meta, pdr->CRC, 0xffffffff);
	SET32(meta, pdr->CRC, crc32(meta->pdr, size));
	error = g_write_data(cp, (lba + GET32(meta, hdr->pdr_section)) * ss,
	    meta->pdr, size);
	if (error != 0)
		goto err;

	size = GET32(meta, hdr->vdr_length) * ss;
	SET32(meta, vdr->CRC, 0xffffffff);
	SET32(meta, vdr->CRC, crc32(meta->vdr, size));
	error = g_write_data(cp, (lba + GET32(meta, hdr->vdr_section)) * ss,
	    meta->vdr, size);
	if (error != 0)
		goto err;

	size = GET16(meta, hdr->Configuration_Record_Length) * ss;
	num = GETCRNUM(meta);
	for (i = 0; i < num; i++) {
		vdc = GETVDCPTR(meta, i);
		SET32D(meta, vdc->CRC, 0xffffffff);
		SET32D(meta, vdc->CRC, crc32(vdc, size));
	}
	error = g_write_data(cp, (lba + GET32(meta, hdr->cr_section)) * ss,
	    meta->cr, size * num);
	if (error != 0)
		goto err;

	size = GET32(meta, hdr->pdd_length) * ss;
	SET32(meta, pdd->CRC, 0xffffffff);
	SET32(meta, pdd->CRC, crc32(meta->pdd, size));
	error = g_write_data(cp, (lba + GET32(meta, hdr->pdd_section)) * ss,
	    meta->pdd, size);
	if (error != 0)
		goto err;

	if (GET32(meta, hdr->bbmlog_length) != 0) {
		size = GET32(meta, hdr->bbmlog_length) * ss;
		SET32(meta, bbm->CRC, 0xffffffff);
		SET32(meta, bbm->CRC, crc32(meta->bbm, size));
		error = g_write_data(cp,
		    (lba + GET32(meta, hdr->bbmlog_section)) * ss,
		    meta->bbm, size);
		if (error != 0)
			goto err;
	}

done:
	if (lba == plba && slba != -1) {
		lba = slba;
		goto next;
	}

	return (error);
}

static int
ddf_meta_erase(struct g_consumer *cp)
{
	struct g_provider *pp;
	char *buf;
	int error;

	pp = cp->provider;
	buf = malloc(pp->sectorsize, M_MD_DDF, M_WAITOK | M_ZERO);
	error = g_write_data(cp, pp->mediasize - pp->sectorsize,
	    buf, pp->sectorsize);
	if (error != 0) {
		G_RAID_DEBUG(1, "Cannot erase metadata on %s (error=%d).",
		    pp->name, error);
	}
	free(buf, M_MD_DDF);
	return (error);
}

static struct g_raid_volume *
g_raid_md_ddf_get_volume(struct g_raid_softc *sc, uint8_t *GUID)
{
	struct g_raid_volume	*vol;
	struct g_raid_md_ddf_pervolume *pv;

	TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
		pv = vol->v_md_data;
		if (memcmp(pv->pv_meta.vde->VD_GUID, GUID, 24) == 0)
			break;
	}
	return (vol);
}

static struct g_raid_disk *
g_raid_md_ddf_get_disk(struct g_raid_softc *sc, uint8_t *GUID, uint32_t id)
{
	struct g_raid_disk	*disk;
	struct g_raid_md_ddf_perdisk *pd;
	struct ddf_meta *meta;

	TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
		pd = (struct g_raid_md_ddf_perdisk *)disk->d_md_data;
		meta = &pd->pd_meta;
		if (GUID != NULL) {
			if (memcmp(meta->pdd->PD_GUID, GUID, 24) == 0)
				break;
		} else {
			if (GET32(meta, pdd->PD_Reference) == id)
				break;
		}
	}
	return (disk);
}

static int
g_raid_md_ddf_purge_volumes(struct g_raid_softc *sc)
{
	struct g_raid_volume	*vol, *tvol;
	int i, res;

	res = 0;
	TAILQ_FOREACH_SAFE(vol, &sc->sc_volumes, v_next, tvol) {
		if (vol->v_stopping)
			continue;
		for (i = 0; i < vol->v_disks_count; i++) {
			if (vol->v_subdisks[i].sd_state != G_RAID_SUBDISK_S_NONE)
				break;
		}
		if (i >= vol->v_disks_count) {
			g_raid_destroy_volume(vol);
			res = 1;
		}
	}
	return (res);
}

static int
g_raid_md_ddf_purge_disks(struct g_raid_softc *sc)
{
#if 0
	struct g_raid_disk	*disk, *tdisk;
	struct g_raid_volume	*vol;
	struct g_raid_md_ddf_perdisk *pd;
	int i, j, res;

	res = 0;
	TAILQ_FOREACH_SAFE(disk, &sc->sc_disks, d_next, tdisk) {
		if (disk->d_state == G_RAID_DISK_S_SPARE)
			continue;
		pd = (struct g_raid_md_ddf_perdisk *)disk->d_md_data;

		/* Scan for deleted volumes. */
		for (i = 0; i < pd->pd_subdisks; ) {
			vol = g_raid_md_ddf_get_volume(sc,
			    pd->pd_meta[i]->volume_id);
			if (vol != NULL && !vol->v_stopping) {
				i++;
				continue;
			}
			free(pd->pd_meta[i], M_MD_DDF);
			for (j = i; j < pd->pd_subdisks - 1; j++)
				pd->pd_meta[j] = pd->pd_meta[j + 1];
			pd->pd_meta[DDF_MAX_SUBDISKS - 1] = NULL;
			pd->pd_subdisks--;
			pd->pd_updated = 1;
		}

		/* If there is no metadata left - erase and delete disk. */
		if (pd->pd_subdisks == 0) {
			ddf_meta_erase(disk->d_consumer);
			g_raid_destroy_disk(disk);
			res = 1;
		}
	}
	return (res);
#endif
	return (0);
}

static int
g_raid_md_ddf_supported(int level, int qual, int disks, int force)
{

	if (disks > DDF_MAX_DISKS_HARD)
		return (0);
	switch (level) {
	case G_RAID_VOLUME_RL_RAID0:
		if (qual != G_RAID_VOLUME_RLQ_NONE)
			return (0);
		if (disks < 1)
			return (0);
		if (!force && disks < 2)
			return (0);
		break;
	case G_RAID_VOLUME_RL_RAID1:
		if (disks < 1)
			return (0);
		if (qual == G_RAID_VOLUME_RLQ_R1SM) {
			if (!force && disks != 2)
				return (0);
		} else if (qual == G_RAID_VOLUME_RLQ_R1MM) {
			if (!force && disks != 3)
				return (0);
		} else 
			return (0);
		break;
	case G_RAID_VOLUME_RL_RAID3:
		if (qual != G_RAID_VOLUME_RLQ_R3P0 &&
		    qual != G_RAID_VOLUME_RLQ_R3PN)
			return (0);
		if (disks < 3)
			return (0);
		break;
	case G_RAID_VOLUME_RL_RAID4:
		if (qual != G_RAID_VOLUME_RLQ_R4P0 &&
		    qual != G_RAID_VOLUME_RLQ_R4PN)
			return (0);
		if (disks < 3)
			return (0);
		break;
	case G_RAID_VOLUME_RL_RAID5:
		if (qual != G_RAID_VOLUME_RLQ_R5RA &&
		    qual != G_RAID_VOLUME_RLQ_R5RS &&
		    qual != G_RAID_VOLUME_RLQ_R5LA &&
		    qual != G_RAID_VOLUME_RLQ_R5LS)
			return (0);
		if (disks < 3)
			return (0);
		break;
	case G_RAID_VOLUME_RL_RAID6:
		if (qual != G_RAID_VOLUME_RLQ_R6RA &&
		    qual != G_RAID_VOLUME_RLQ_R6RS &&
		    qual != G_RAID_VOLUME_RLQ_R6LA &&
		    qual != G_RAID_VOLUME_RLQ_R6LS)
			return (0);
		if (disks < 4)
			return (0);
		break;
	case G_RAID_VOLUME_RL_RAIDMDF:
		if (qual != G_RAID_VOLUME_RLQ_RMDFRA &&
		    qual != G_RAID_VOLUME_RLQ_RMDFRS &&
		    qual != G_RAID_VOLUME_RLQ_RMDFLA &&
		    qual != G_RAID_VOLUME_RLQ_RMDFLS)
			return (0);
		if (disks < 4)
			return (0);
		break;
	case G_RAID_VOLUME_RL_RAID1E:
		if (qual != G_RAID_VOLUME_RLQ_R1EA &&
		    qual != G_RAID_VOLUME_RLQ_R1EO)
			return (0);
		if (disks < 3)
			return (0);
		break;
	case G_RAID_VOLUME_RL_SINGLE:
		if (qual != G_RAID_VOLUME_RLQ_NONE)
			return (0);
		if (disks != 1)
			return (0);
		break;
	case G_RAID_VOLUME_RL_CONCAT:
		if (qual != G_RAID_VOLUME_RLQ_NONE)
			return (0);
		if (disks < 2)
			return (0);
		break;
	case G_RAID_VOLUME_RL_RAID5E:
		if (qual != G_RAID_VOLUME_RLQ_R5ERA &&
		    qual != G_RAID_VOLUME_RLQ_R5ERS &&
		    qual != G_RAID_VOLUME_RLQ_R5ELA &&
		    qual != G_RAID_VOLUME_RLQ_R5ELS)
			return (0);
		if (disks < 4)
			return (0);
		break;
	case G_RAID_VOLUME_RL_RAID5EE:
		if (qual != G_RAID_VOLUME_RLQ_R5EERA &&
		    qual != G_RAID_VOLUME_RLQ_R5EERS &&
		    qual != G_RAID_VOLUME_RLQ_R5EELA &&
		    qual != G_RAID_VOLUME_RLQ_R5EELS)
			return (0);
		if (disks < 4)
			return (0);
		break;
	case G_RAID_VOLUME_RL_RAID5R:
		if (qual != G_RAID_VOLUME_RLQ_R5RRA &&
		    qual != G_RAID_VOLUME_RLQ_R5RRS &&
		    qual != G_RAID_VOLUME_RLQ_R5RLA &&
		    qual != G_RAID_VOLUME_RLQ_R5RLS)
			return (0);
		if (disks < 3)
			return (0);
		break;
	default:
		return (0);
	}
	return (1);
}

static int
g_raid_md_ddf_start_disk(struct g_raid_disk *disk, struct g_raid_volume *vol)
{
	struct g_raid_softc *sc;
	struct g_raid_subdisk *sd;
	struct g_raid_md_ddf_perdisk *pd;
	struct g_raid_md_ddf_pervolume *pv;
	struct g_raid_md_ddf_object *mdi;
	struct ddf_vol_meta *vmeta;
	struct ddf_meta *pdmeta, *gmeta;
	struct ddf_vdc_record *vdc1;
	struct ddf_sa_record *sa;
	off_t size, eoff = 0, esize = 0;
	uint64_t *val2;
	int disk_pos, md_disk_bvd = -1, md_disk_pos = -1, md_pde_pos;
	int i, resurrection = 0;
	uint32_t reference;

	sc = disk->d_softc;
	mdi = (struct g_raid_md_ddf_object *)sc->sc_md;
	pd = (struct g_raid_md_ddf_perdisk *)disk->d_md_data;
	pdmeta = &pd->pd_meta;
	reference = GET32(&pd->pd_meta, pdd->PD_Reference);

	pv = vol->v_md_data;
	vmeta = &pv->pv_meta;
	gmeta = &mdi->mdio_meta;

	/* Find disk position in metadata by its reference. */
	disk_pos = ddf_meta_find_disk(vmeta, reference,
	    &md_disk_bvd, &md_disk_pos);
	md_pde_pos = ddf_meta_find_pd(gmeta, NULL, reference);

	if (disk_pos < 0) {
		G_RAID_DEBUG1(1, sc,
		    "Disk %s is not a present part of the volume %s",
		    g_raid_get_diskname(disk), vol->v_name);

		/* Failed stale disk is useless for us. */
		if ((GET16(gmeta, pdr->entry[md_pde_pos].PD_State) & DDF_PDE_PFA) != 0) {
			g_raid_change_disk_state(disk, G_RAID_DISK_S_STALE_FAILED);
			return (0);
		}

		/* If disk has some metadata for this volume - erase. */
		if ((vdc1 = ddf_meta_find_vdc(pdmeta, vmeta->vdc->VD_GUID)) != NULL)
			SET32D(pdmeta, vdc1->Signature, 0xffffffff);

		/* If we are in the start process, that's all for now. */
		if (!pv->pv_started)
			goto nofit;
		/*
		 * If we have already started - try to get use of the disk.
		 * Try to replace OFFLINE disks first, then FAILED.
		 */
		if (ddf_meta_count_vdc(&pd->pd_meta, NULL) >=
			GET16(&pd->pd_meta, hdr->Max_Partitions)) {
			G_RAID_DEBUG1(1, sc, "No free partitions on disk %s",
			    g_raid_get_diskname(disk));
			goto nofit;
		}
		ddf_meta_unused_range(&pd->pd_meta, &eoff, &esize);
		if (esize == 0) {
			G_RAID_DEBUG1(1, sc, "No free space on disk %s",
			    g_raid_get_diskname(disk));
			goto nofit;
		}
		eoff *= pd->pd_meta.sectorsize;
		esize *= pd->pd_meta.sectorsize;
		size = INT64_MAX;
		for (i = 0; i < vol->v_disks_count; i++) {
			sd = &vol->v_subdisks[i];
			if (sd->sd_state != G_RAID_SUBDISK_S_NONE)
				size = sd->sd_size;
			if (sd->sd_state <= G_RAID_SUBDISK_S_FAILED &&
			    (disk_pos < 0 ||
			     vol->v_subdisks[i].sd_state < sd->sd_state))
				disk_pos = i;
		}
		if (disk_pos >= 0 &&
		    vol->v_raid_level != G_RAID_VOLUME_RL_CONCAT &&
		    esize < size) {
			G_RAID_DEBUG1(1, sc, "Disk %s free space "
			    "is too small (%ju < %ju)",
			    g_raid_get_diskname(disk), esize, size);
			disk_pos = -1;
		}
		if (disk_pos >= 0) {
			if (vol->v_raid_level != G_RAID_VOLUME_RL_CONCAT)
				esize = size;
			md_disk_bvd = disk_pos / GET16(vmeta, vdc->Primary_Element_Count); // XXX
			md_disk_pos = disk_pos % GET16(vmeta, vdc->Primary_Element_Count); // XXX
		} else {
nofit:
			if (disk->d_state == G_RAID_DISK_S_NONE)
				g_raid_change_disk_state(disk,
				    G_RAID_DISK_S_STALE);
			return (0);
		}

		/*
		 * If spare is committable, delete spare record.
		 * Othersize, mark it active and leave there.
		 */
		sa = ddf_meta_find_sa(&pd->pd_meta, 0);
		if (sa != NULL) {
			if ((GET8D(&pd->pd_meta, sa->Spare_Type) &
			    DDF_SAR_TYPE_REVERTIBLE) == 0) {
				SET32D(&pd->pd_meta, sa->Signature, 0xffffffff);
			} else {
				SET8D(&pd->pd_meta, sa->Spare_Type,
				    GET8D(&pd->pd_meta, sa->Spare_Type) |
				    DDF_SAR_TYPE_ACTIVE);
			}
		}

		G_RAID_DEBUG1(1, sc, "Disk %s takes pos %d in the volume %s",
		    g_raid_get_diskname(disk), disk_pos, vol->v_name);
		resurrection = 1;
	}

	sd = &vol->v_subdisks[disk_pos];

	if (resurrection && sd->sd_disk != NULL) {
		g_raid_change_disk_state(sd->sd_disk,
		    G_RAID_DISK_S_STALE_FAILED);
		TAILQ_REMOVE(&sd->sd_disk->d_subdisks,
		    sd, sd_next);
	}
	vol->v_subdisks[disk_pos].sd_disk = disk;
	TAILQ_INSERT_TAIL(&disk->d_subdisks, sd, sd_next);

	/* Welcome the new disk. */
	if (resurrection)
		g_raid_change_disk_state(disk, G_RAID_DISK_S_ACTIVE);
	else if (GET16(gmeta, pdr->entry[md_pde_pos].PD_State) & DDF_PDE_PFA)
		g_raid_change_disk_state(disk, G_RAID_DISK_S_FAILED);
	else
		g_raid_change_disk_state(disk, G_RAID_DISK_S_ACTIVE);

	if (resurrection) {
		sd->sd_offset = eoff;
		sd->sd_size = esize;
	} else if (pdmeta->cr != NULL &&
	    (vdc1 = ddf_meta_find_vdc(pdmeta, vmeta->vdc->VD_GUID)) != NULL) {
		val2 = (uint64_t *)&(vdc1->Physical_Disk_Sequence[GET16(vmeta, hdr->Max_Primary_Element_Entries)]);
		sd->sd_offset = (off_t)GET64P(pdmeta, val2 + md_disk_pos) * 512;
		sd->sd_size = (off_t)GET64D(pdmeta, vdc1->Block_Count) * 512;
	}

	if (resurrection) {
		/* Stale disk, almost same as new. */
		g_raid_change_subdisk_state(sd,
		    G_RAID_SUBDISK_S_NEW);
	} else if (GET16(gmeta, pdr->entry[md_pde_pos].PD_State) & DDF_PDE_PFA) {
		/* Failed disk. */
		g_raid_change_subdisk_state(sd,
		    G_RAID_SUBDISK_S_FAILED);
	} else if ((GET16(gmeta, pdr->entry[md_pde_pos].PD_State) &
	     (DDF_PDE_FAILED | DDF_PDE_REBUILD)) != 0) {
		/* Rebuilding disk. */
		g_raid_change_subdisk_state(sd,
		    G_RAID_SUBDISK_S_REBUILD);
		sd->sd_rebuild_pos = 0;
	} else if ((GET8(vmeta, vde->VD_State) & DDF_VDE_DIRTY) != 0 ||
	    (GET8(vmeta, vde->Init_State) & DDF_VDE_INIT_MASK) !=
	     DDF_VDE_INIT_FULL) {
		/* Stale disk or dirty volume (unclean shutdown). */
		g_raid_change_subdisk_state(sd,
		    G_RAID_SUBDISK_S_STALE);
	} else {
		/* Up to date disk. */
		g_raid_change_subdisk_state(sd,
		    G_RAID_SUBDISK_S_ACTIVE);
	}
	g_raid_event_send(sd, G_RAID_SUBDISK_E_NEW,
	    G_RAID_EVENT_SUBDISK);

	return (resurrection);
}

static void
g_raid_md_ddf_refill(struct g_raid_softc *sc)
{
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct g_raid_disk *disk;
	struct g_raid_md_object *md;
	struct g_raid_md_ddf_perdisk *pd;
	struct g_raid_md_ddf_pervolume *pv;
	int update, updated, i, bad;

	md = sc->sc_md;
restart:
	updated = 0;
	TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
		pv = vol->v_md_data;
		if (!pv->pv_started || vol->v_stopping)
			continue;

		/* Search for subdisk that needs replacement. */
		bad = 0;
		for (i = 0; i < vol->v_disks_count; i++) {
			sd = &vol->v_subdisks[i];
			if (sd->sd_state == G_RAID_SUBDISK_S_NONE ||
			    sd->sd_state == G_RAID_SUBDISK_S_FAILED)
			        bad = 1;
		}
		if (!bad)
			continue;

		G_RAID_DEBUG1(1, sc, "Volume %s is not complete, "
		    "trying to refill.", vol->v_name);

		TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
			/* Skip failed. */
			if (disk->d_state < G_RAID_DISK_S_SPARE)
				continue;
			/* Skip already used by this volume. */
			for (i = 0; i < vol->v_disks_count; i++) {
				sd = &vol->v_subdisks[i];
				if (sd->sd_disk == disk)
					break;
			}
			if (i < vol->v_disks_count)
				continue;

			/* Try to use disk if it has empty extents. */
			pd = disk->d_md_data;
			if (ddf_meta_count_vdc(&pd->pd_meta, NULL) <
			    GET16(&pd->pd_meta, hdr->Max_Partitions)) {
				update = g_raid_md_ddf_start_disk(disk, vol);
			} else
				update = 0;
			if (update) {
				updated = 1;
				g_raid_md_write_ddf(md, vol, NULL, disk);
				break;
			}
		}
	}
	if (updated)
		goto restart;
}

static void
g_raid_md_ddf_start(struct g_raid_volume *vol)
{
	struct g_raid_softc *sc;
	struct g_raid_subdisk *sd;
	struct g_raid_disk *disk;
	struct g_raid_md_object *md;
	struct g_raid_md_ddf_perdisk *pd;
	struct g_raid_md_ddf_pervolume *pv;
	struct g_raid_md_ddf_object *mdi;
	struct ddf_vol_meta *vmeta;
	uint64_t *val2;
	int i, j, bvd;

	sc = vol->v_softc;
	md = sc->sc_md;
	mdi = (struct g_raid_md_ddf_object *)md;
	pv = vol->v_md_data;
	vmeta = &pv->pv_meta;

	vol->v_raid_level = GET8(vmeta, vdc->Primary_RAID_Level);
	vol->v_raid_level_qualifier = GET8(vmeta, vdc->RLQ);
	if (GET8(vmeta, vdc->Secondary_Element_Count) > 1 &&
	    vol->v_raid_level == G_RAID_VOLUME_RL_RAID1 &&
	    GET8(vmeta, vdc->Secondary_RAID_Level) == 0)
		vol->v_raid_level = G_RAID_VOLUME_RL_RAID1E;
	vol->v_sectorsize = GET16(vmeta, vdc->Block_Size);
	if (vol->v_sectorsize == 0xffff)
		vol->v_sectorsize = vmeta->sectorsize;
	vol->v_strip_size = vol->v_sectorsize << GET8(vmeta, vdc->Stripe_Size);
	vol->v_disks_count = GET16(vmeta, vdc->Primary_Element_Count) *
	    GET8(vmeta, vdc->Secondary_Element_Count);
	vol->v_mdf_pdisks = GET8(vmeta, vdc->MDF_Parity_Disks);
	vol->v_mdf_polynomial = GET16(vmeta, vdc->MDF_Parity_Generator_Polynomial);
	vol->v_mdf_method = GET8(vmeta, vdc->MDF_Constant_Generation_Method);
	if (GET8(vmeta, vdc->Rotate_Parity_count) > 31)
		vol->v_rotate_parity = 1;
	else
		vol->v_rotate_parity = 1 << GET8(vmeta, vdc->Rotate_Parity_count);
	vol->v_mediasize = GET64(vmeta, vdc->VD_Size) * vol->v_sectorsize;
	for (i = 0, j = 0, bvd = 0; i < vol->v_disks_count; i++, j++) {
		if (j == GET16(vmeta, vdc->Primary_Element_Count)) {
			j = 0;
			bvd++;
		}
		sd = &vol->v_subdisks[i];
		if (vmeta->bvdc[bvd] == NULL) {
			sd->sd_offset = 0;
			sd->sd_size = GET64(vmeta, vdc->Block_Count) *
			    vol->v_sectorsize;
			continue;
		}
		val2 = (uint64_t *)&(vmeta->bvdc[bvd]->Physical_Disk_Sequence[
		    GET16(vmeta, hdr->Max_Primary_Element_Entries)]);
		sd->sd_offset = GET64P(vmeta, val2 + j) * vol->v_sectorsize;
		sd->sd_size = GET64(vmeta, bvdc[bvd]->Block_Count) *
		    vol->v_sectorsize;
	}
	g_raid_start_volume(vol);

	/* Make all disks found till the moment take their places. */
	TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
		pd = (struct g_raid_md_ddf_perdisk *)disk->d_md_data;
		if (ddf_meta_find_vdc(&pd->pd_meta, vmeta->vdc->VD_GUID) != NULL)
			g_raid_md_ddf_start_disk(disk, vol);
	}

	pv->pv_started = 1;
	mdi->mdio_starting--;
	callout_stop(&pv->pv_start_co);
	G_RAID_DEBUG1(0, sc, "Volume started.");
	g_raid_md_write_ddf(md, vol, NULL, NULL);

	/* Pickup any STALE/SPARE disks to refill array if needed. */
	g_raid_md_ddf_refill(sc);

	g_raid_event_send(vol, G_RAID_VOLUME_E_START, G_RAID_EVENT_VOLUME);
}

static void
g_raid_ddf_go(void *arg)
{
	struct g_raid_volume *vol;
	struct g_raid_softc *sc;
	struct g_raid_md_ddf_pervolume *pv;

	vol = arg;
	pv = vol->v_md_data;
	sc = vol->v_softc;
	if (!pv->pv_started) {
		G_RAID_DEBUG1(0, sc, "Force volume start due to timeout.");
		g_raid_event_send(vol, G_RAID_VOLUME_E_STARTMD,
		    G_RAID_EVENT_VOLUME);
	}
}

static void
g_raid_md_ddf_new_disk(struct g_raid_disk *disk)
{
	struct g_raid_softc *sc;
	struct g_raid_md_object *md;
	struct g_raid_md_ddf_perdisk *pd;
	struct g_raid_md_ddf_pervolume *pv;
	struct g_raid_md_ddf_object *mdi;
	struct g_raid_volume *vol;
	struct ddf_meta *pdmeta;
	struct ddf_vol_meta *vmeta;
	struct ddf_vdc_record *vdc;
	struct ddf_vd_entry *vde;
	int i, j, k, num, have, need, cnt, spare;
	uint32_t val;
	char buf[17];

	sc = disk->d_softc;
	md = sc->sc_md;
	mdi = (struct g_raid_md_ddf_object *)md;
	pd = (struct g_raid_md_ddf_perdisk *)disk->d_md_data;
	pdmeta = &pd->pd_meta;
	spare = -1;

	if (mdi->mdio_meta.hdr == NULL)
		ddf_meta_copy(&mdi->mdio_meta, pdmeta);
	else
		ddf_meta_update(&mdi->mdio_meta, pdmeta);

	num = GETCRNUM(pdmeta);
	for (j = 0; j < num; j++) {
		vdc = GETVDCPTR(pdmeta, j);
		val = GET32D(pdmeta, vdc->Signature);

		if (val == DDF_SA_SIGNATURE && spare == -1)
			spare = 1;

		if (val != DDF_VDCR_SIGNATURE)
			continue;
		spare = 0;
		k = ddf_meta_find_vd(pdmeta, vdc->VD_GUID);
		if (k < 0)
			continue;
		vde = &pdmeta->vdr->entry[k];

		/* Look for volume with matching ID. */
		vol = g_raid_md_ddf_get_volume(sc, vdc->VD_GUID);
		if (vol == NULL) {
			ddf_meta_get_name(pdmeta, k, buf);
			vol = g_raid_create_volume(sc, buf,
			    GET16D(pdmeta, vde->VD_Number));
			pv = malloc(sizeof(*pv), M_MD_DDF, M_WAITOK | M_ZERO);
			vol->v_md_data = pv;
			callout_init(&pv->pv_start_co, 1);
			callout_reset(&pv->pv_start_co,
			    g_raid_start_timeout * hz,
			    g_raid_ddf_go, vol);
			mdi->mdio_starting++;
		} else
			pv = vol->v_md_data;

		/* If we haven't started yet - check metadata freshness. */
		vmeta = &pv->pv_meta;
		ddf_vol_meta_update(vmeta, pdmeta, vdc->VD_GUID, pv->pv_started);
	}

	if (spare == 1) {
		g_raid_change_disk_state(disk, G_RAID_DISK_S_SPARE);
		g_raid_md_ddf_refill(sc);
	}

	TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
		pv = vol->v_md_data;
		vmeta = &pv->pv_meta;

		if (ddf_meta_find_vdc(pdmeta, vmeta->vdc->VD_GUID) == NULL)
			continue;

		if (pv->pv_started) {
			if (g_raid_md_ddf_start_disk(disk, vol))
				g_raid_md_write_ddf(md, vol, NULL, NULL);
			continue;
		}

		/* If we collected all needed disks - start array. */
		need = 0;
		have = 0;
		for (k = 0; k < GET8(vmeta, vdc->Secondary_Element_Count); k++) {
			if (vmeta->bvdc[k] == NULL) {
				need += GET16(vmeta, vdc->Primary_Element_Count);
				continue;
			}
			cnt = GET16(vmeta, bvdc[k]->Primary_Element_Count);
			need += cnt;
			for (i = 0; i < cnt; i++) {
				val = GET32(vmeta, bvdc[k]->Physical_Disk_Sequence[i]);
				if (g_raid_md_ddf_get_disk(sc, NULL, val) != NULL)
					have++;
			}
		}
		G_RAID_DEBUG1(1, sc, "Volume %s now has %d of %d disks",
		    vol->v_name, have, need);
		if (have == need)
			g_raid_md_ddf_start(vol);
	}
}

static int
g_raid_md_create_req_ddf(struct g_raid_md_object *md, struct g_class *mp,
    struct gctl_req *req, struct g_geom **gp)
{
	struct g_geom *geom;
	struct g_raid_softc *sc;
	struct g_raid_md_ddf_object *mdi, *mdi1;
	char name[16];
	const char *fmtopt;
	int be = 1;

	mdi = (struct g_raid_md_ddf_object *)md;
	fmtopt = gctl_get_asciiparam(req, "fmtopt");
	if (fmtopt == NULL || strcasecmp(fmtopt, "BE") == 0)
		be = 1;
	else if (strcasecmp(fmtopt, "LE") == 0)
		be = 0;
	else {
		gctl_error(req, "Incorrect fmtopt argument.");
		return (G_RAID_MD_TASTE_FAIL);
	}

	/* Search for existing node. */
	LIST_FOREACH(geom, &mp->geom, geom) {
		sc = geom->softc;
		if (sc == NULL)
			continue;
		if (sc->sc_stopping != 0)
			continue;
		if (sc->sc_md->mdo_class != md->mdo_class)
			continue;
		mdi1 = (struct g_raid_md_ddf_object *)sc->sc_md;
		if (mdi1->mdio_bigendian != be)
			continue;
		break;
	}
	if (geom != NULL) {
		*gp = geom;
		return (G_RAID_MD_TASTE_EXISTING);
	}

	/* Create new one if not found. */
	mdi->mdio_bigendian = be;
	snprintf(name, sizeof(name), "DDF%s", be ? "" : "-LE");
	sc = g_raid_create_node(mp, name, md);
	if (sc == NULL)
		return (G_RAID_MD_TASTE_FAIL);
	md->mdo_softc = sc;
	*gp = sc->sc_geom;
	return (G_RAID_MD_TASTE_NEW);
}

static int
g_raid_md_taste_ddf(struct g_raid_md_object *md, struct g_class *mp,
                              struct g_consumer *cp, struct g_geom **gp)
{
	struct g_consumer *rcp;
	struct g_provider *pp;
	struct g_raid_softc *sc;
	struct g_raid_disk *disk;
	struct ddf_meta meta;
	struct g_raid_md_ddf_perdisk *pd;
	struct g_raid_md_ddf_object *mdi;
	struct g_geom *geom;
	int error, result, be;
	char name[16];

	G_RAID_DEBUG(1, "Tasting DDF on %s", cp->provider->name);
	mdi = (struct g_raid_md_ddf_object *)md;
	pp = cp->provider;

	/* Read metadata from device. */
	g_topology_unlock();
	bzero(&meta, sizeof(meta));
	error = ddf_meta_read(cp, &meta);
	g_topology_lock();
	if (error != 0)
		return (G_RAID_MD_TASTE_FAIL);
	be = meta.bigendian;

	/* Metadata valid. Print it. */
	g_raid_md_ddf_print(&meta);

	/* Search for matching node. */
	sc = NULL;
	LIST_FOREACH(geom, &mp->geom, geom) {
		sc = geom->softc;
		if (sc == NULL)
			continue;
		if (sc->sc_stopping != 0)
			continue;
		if (sc->sc_md->mdo_class != md->mdo_class)
			continue;
		mdi = (struct g_raid_md_ddf_object *)sc->sc_md;
		if (mdi->mdio_bigendian != be)
			continue;
		break;
	}

	/* Found matching node. */
	if (geom != NULL) {
		G_RAID_DEBUG(1, "Found matching array %s", sc->sc_name);
		result = G_RAID_MD_TASTE_EXISTING;

	} else { /* Not found matching node -- create one. */
		result = G_RAID_MD_TASTE_NEW;
		mdi->mdio_bigendian = be;
		snprintf(name, sizeof(name), "DDF%s", be ? "" : "-LE");
		sc = g_raid_create_node(mp, name, md);
		md->mdo_softc = sc;
		geom = sc->sc_geom;
	}

	/* There is no return after this point, so we close passed consumer. */
	g_access(cp, -1, 0, 0);

	rcp = g_new_consumer(geom);
	rcp->flags |= G_CF_DIRECT_RECEIVE;
	g_attach(rcp, pp);
	if (g_access(rcp, 1, 1, 1) != 0)
		; //goto fail1;

	g_topology_unlock();
	sx_xlock(&sc->sc_lock);

	pd = malloc(sizeof(*pd), M_MD_DDF, M_WAITOK | M_ZERO);
	pd->pd_meta = meta;
	disk = g_raid_create_disk(sc);
	disk->d_md_data = (void *)pd;
	disk->d_consumer = rcp;
	rcp->private = disk;

	g_raid_get_disk_info(disk);

	g_raid_md_ddf_new_disk(disk);

	sx_xunlock(&sc->sc_lock);
	g_topology_lock();
	*gp = geom;
	return (result);
}

static int
g_raid_md_event_ddf(struct g_raid_md_object *md,
    struct g_raid_disk *disk, u_int event)
{
	struct g_raid_softc *sc;

	sc = md->mdo_softc;
	if (disk == NULL)
		return (-1);
	switch (event) {
	case G_RAID_DISK_E_DISCONNECTED:
		/* Delete disk. */
		g_raid_change_disk_state(disk, G_RAID_DISK_S_NONE);
		g_raid_destroy_disk(disk);
		g_raid_md_ddf_purge_volumes(sc);

		/* Write updated metadata to all disks. */
		g_raid_md_write_ddf(md, NULL, NULL, NULL);

		/* Check if anything left. */
		if (g_raid_ndisks(sc, -1) == 0)
			g_raid_destroy_node(sc, 0);
		else
			g_raid_md_ddf_refill(sc);
		return (0);
	}
	return (-2);
}

static int
g_raid_md_volume_event_ddf(struct g_raid_md_object *md,
    struct g_raid_volume *vol, u_int event)
{
	struct g_raid_md_ddf_pervolume *pv;

	pv = (struct g_raid_md_ddf_pervolume *)vol->v_md_data;
	switch (event) {
	case G_RAID_VOLUME_E_STARTMD:
		if (!pv->pv_started)
			g_raid_md_ddf_start(vol);
		return (0);
	}
	return (-2);
}

static int
g_raid_md_ctl_ddf(struct g_raid_md_object *md,
    struct gctl_req *req)
{
	struct g_raid_softc *sc;
	struct g_raid_volume *vol, *vol1;
	struct g_raid_subdisk *sd;
	struct g_raid_disk *disk, *disks[DDF_MAX_DISKS_HARD];
	struct g_raid_md_ddf_perdisk *pd;
	struct g_raid_md_ddf_pervolume *pv;
	struct g_raid_md_ddf_object *mdi;
	struct ddf_sa_record *sa;
	struct g_consumer *cp;
	struct g_provider *pp;
	char arg[16];
	const char *nodename, *verb, *volname, *levelname, *diskname;
	char *tmp;
	int *nargs, *force;
	off_t size, sectorsize, strip, offs[DDF_MAX_DISKS_HARD], esize;
	intmax_t *sizearg, *striparg;
	int i, numdisks, len, level, qual;
	int error;

	sc = md->mdo_softc;
	mdi = (struct g_raid_md_ddf_object *)md;
	verb = gctl_get_param(req, "verb", NULL);
	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	error = 0;

	if (strcmp(verb, "label") == 0) {

		if (*nargs < 4) {
			gctl_error(req, "Invalid number of arguments.");
			return (-1);
		}
		volname = gctl_get_asciiparam(req, "arg1");
		if (volname == NULL) {
			gctl_error(req, "No volume name.");
			return (-2);
		}
		levelname = gctl_get_asciiparam(req, "arg2");
		if (levelname == NULL) {
			gctl_error(req, "No RAID level.");
			return (-3);
		}
		if (g_raid_volume_str2level(levelname, &level, &qual)) {
			gctl_error(req, "Unknown RAID level '%s'.", levelname);
			return (-4);
		}
		numdisks = *nargs - 3;
		force = gctl_get_paraml(req, "force", sizeof(*force));
		if (!g_raid_md_ddf_supported(level, qual, numdisks,
		    force ? *force : 0)) {
			gctl_error(req, "Unsupported RAID level "
			    "(0x%02x/0x%02x), or number of disks (%d).",
			    level, qual, numdisks);
			return (-5);
		}

		/* Search for disks, connect them and probe. */
		size = INT64_MAX;
		sectorsize = 0;
		bzero(disks, sizeof(disks));
		bzero(offs, sizeof(offs));
		for (i = 0; i < numdisks; i++) {
			snprintf(arg, sizeof(arg), "arg%d", i + 3);
			diskname = gctl_get_asciiparam(req, arg);
			if (diskname == NULL) {
				gctl_error(req, "No disk name (%s).", arg);
				error = -6;
				break;
			}
			if (strcmp(diskname, "NONE") == 0)
				continue;

			TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
				if (disk->d_consumer != NULL && 
				    disk->d_consumer->provider != NULL &&
				    strcmp(disk->d_consumer->provider->name,
				     diskname) == 0)
					break;
			}
			if (disk != NULL) {
				if (disk->d_state != G_RAID_DISK_S_ACTIVE) {
					gctl_error(req, "Disk '%s' is in a "
					    "wrong state (%s).", diskname,
					    g_raid_disk_state2str(disk->d_state));
					error = -7;
					break;
				}
				pd = disk->d_md_data;
				if (ddf_meta_count_vdc(&pd->pd_meta, NULL) >=
				    GET16(&pd->pd_meta, hdr->Max_Partitions)) {
					gctl_error(req, "No free partitions "
					    "on disk '%s'.",
					    diskname);
					error = -7;
					break;
				}
				pp = disk->d_consumer->provider;
				disks[i] = disk;
				ddf_meta_unused_range(&pd->pd_meta,
				    &offs[i], &esize);
				offs[i] *= pp->sectorsize;
				size = MIN(size, (off_t)esize * pp->sectorsize);
				sectorsize = MAX(sectorsize, pp->sectorsize);
				continue;
			}

			g_topology_lock();
			cp = g_raid_open_consumer(sc, diskname);
			if (cp == NULL) {
				gctl_error(req, "Can't open disk '%s'.",
				    diskname);
				g_topology_unlock();
				error = -8;
				break;
			}
			pp = cp->provider;
			pd = malloc(sizeof(*pd), M_MD_DDF, M_WAITOK | M_ZERO);
			disk = g_raid_create_disk(sc);
			disk->d_md_data = (void *)pd;
			disk->d_consumer = cp;
			disks[i] = disk;
			cp->private = disk;
			ddf_meta_create(disk, &mdi->mdio_meta);
			if (mdi->mdio_meta.hdr == NULL)
				ddf_meta_copy(&mdi->mdio_meta, &pd->pd_meta);
			else
				ddf_meta_update(&mdi->mdio_meta, &pd->pd_meta);
			g_topology_unlock();

			g_raid_get_disk_info(disk);

			/* Reserve some space for metadata. */
			size = MIN(size, GET64(&pd->pd_meta,
			    pdr->entry[0].Configured_Size) * pp->sectorsize);
			sectorsize = MAX(sectorsize, pp->sectorsize);
		}
		if (error != 0) {
			for (i = 0; i < numdisks; i++) {
				if (disks[i] != NULL &&
				    disks[i]->d_state == G_RAID_DISK_S_NONE)
					g_raid_destroy_disk(disks[i]);
			}
			return (error);
		}

		if (sectorsize <= 0) {
			gctl_error(req, "Can't get sector size.");
			return (-8);
		}

		/* Handle size argument. */
		len = sizeof(*sizearg);
		sizearg = gctl_get_param(req, "size", &len);
		if (sizearg != NULL && len == sizeof(*sizearg) &&
		    *sizearg > 0) {
			if (*sizearg > size) {
				gctl_error(req, "Size too big %lld > %lld.",
				    (long long)*sizearg, (long long)size);
				return (-9);
			}
			size = *sizearg;
		}

		/* Handle strip argument. */
		strip = 131072;
		len = sizeof(*striparg);
		striparg = gctl_get_param(req, "strip", &len);
		if (striparg != NULL && len == sizeof(*striparg) &&
		    *striparg > 0) {
			if (*striparg < sectorsize) {
				gctl_error(req, "Strip size too small.");
				return (-10);
			}
			if (*striparg % sectorsize != 0) {
				gctl_error(req, "Incorrect strip size.");
				return (-11);
			}
			strip = *striparg;
		}

		/* Round size down to strip or sector. */
		if (level == G_RAID_VOLUME_RL_RAID1 ||
		    level == G_RAID_VOLUME_RL_RAID3 ||
		    level == G_RAID_VOLUME_RL_SINGLE ||
		    level == G_RAID_VOLUME_RL_CONCAT)
			size -= (size % sectorsize);
		else if (level == G_RAID_VOLUME_RL_RAID1E &&
		    (numdisks & 1) != 0)
			size -= (size % (2 * strip));
		else
			size -= (size % strip);
		if (size <= 0) {
			gctl_error(req, "Size too small.");
			return (-13);
		}

		/* We have all we need, create things: volume, ... */
		pv = malloc(sizeof(*pv), M_MD_DDF, M_WAITOK | M_ZERO);
		ddf_vol_meta_create(&pv->pv_meta, &mdi->mdio_meta);
		pv->pv_started = 1;
		vol = g_raid_create_volume(sc, volname, -1);
		vol->v_md_data = pv;
		vol->v_raid_level = level;
		vol->v_raid_level_qualifier = qual;
		vol->v_strip_size = strip;
		vol->v_disks_count = numdisks;
		if (level == G_RAID_VOLUME_RL_RAID0 ||
		    level == G_RAID_VOLUME_RL_CONCAT ||
		    level == G_RAID_VOLUME_RL_SINGLE)
			vol->v_mediasize = size * numdisks;
		else if (level == G_RAID_VOLUME_RL_RAID1)
			vol->v_mediasize = size;
		else if (level == G_RAID_VOLUME_RL_RAID3 ||
		    level == G_RAID_VOLUME_RL_RAID4 ||
		    level == G_RAID_VOLUME_RL_RAID5)
			vol->v_mediasize = size * (numdisks - 1);
		else if (level == G_RAID_VOLUME_RL_RAID5R) {
			vol->v_mediasize = size * (numdisks - 1);
			vol->v_rotate_parity = 1024;
		} else if (level == G_RAID_VOLUME_RL_RAID6 ||
		    level == G_RAID_VOLUME_RL_RAID5E ||
		    level == G_RAID_VOLUME_RL_RAID5EE)
			vol->v_mediasize = size * (numdisks - 2);
		else if (level == G_RAID_VOLUME_RL_RAIDMDF) {
			if (numdisks < 5)
				vol->v_mdf_pdisks = 2;
			else
				vol->v_mdf_pdisks = 3;
			vol->v_mdf_polynomial = 0x11d;
			vol->v_mdf_method = 0x00;
			vol->v_mediasize = size * (numdisks - vol->v_mdf_pdisks);
		} else { /* RAID1E */
			vol->v_mediasize = ((size * numdisks) / strip / 2) *
			    strip;
		}
		vol->v_sectorsize = sectorsize;
		g_raid_start_volume(vol);

		/* , and subdisks. */
		for (i = 0; i < numdisks; i++) {
			disk = disks[i];
			sd = &vol->v_subdisks[i];
			sd->sd_disk = disk;
			sd->sd_offset = offs[i];
			sd->sd_size = size;
			if (disk == NULL)
				continue;
			TAILQ_INSERT_TAIL(&disk->d_subdisks, sd, sd_next);
			g_raid_change_disk_state(disk,
			    G_RAID_DISK_S_ACTIVE);
			g_raid_change_subdisk_state(sd,
			    G_RAID_SUBDISK_S_ACTIVE);
			g_raid_event_send(sd, G_RAID_SUBDISK_E_NEW,
			    G_RAID_EVENT_SUBDISK);
		}

		/* Write metadata based on created entities. */
		G_RAID_DEBUG1(0, sc, "Array started.");
		g_raid_md_write_ddf(md, vol, NULL, NULL);

		/* Pickup any STALE/SPARE disks to refill array if needed. */
		g_raid_md_ddf_refill(sc);

		g_raid_event_send(vol, G_RAID_VOLUME_E_START,
		    G_RAID_EVENT_VOLUME);
		return (0);
	}
	if (strcmp(verb, "add") == 0) {

		gctl_error(req, "`add` command is not applicable, "
		    "use `label` instead.");
		return (-99);
	}
	if (strcmp(verb, "delete") == 0) {

		nodename = gctl_get_asciiparam(req, "arg0");
		if (nodename != NULL && strcasecmp(sc->sc_name, nodename) != 0)
			nodename = NULL;

		/* Full node destruction. */
		if (*nargs == 1 && nodename != NULL) {
			/* Check if some volume is still open. */
			force = gctl_get_paraml(req, "force", sizeof(*force));
			if (force != NULL && *force == 0 &&
			    g_raid_nopens(sc) != 0) {
				gctl_error(req, "Some volume is still open.");
				return (-4);
			}

			TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
				if (disk->d_consumer)
					ddf_meta_erase(disk->d_consumer);
			}
			g_raid_destroy_node(sc, 0);
			return (0);
		}

		/* Destroy specified volume. If it was last - all node. */
		if (*nargs > 2) {
			gctl_error(req, "Invalid number of arguments.");
			return (-1);
		}
		volname = gctl_get_asciiparam(req,
		    nodename != NULL ? "arg1" : "arg0");
		if (volname == NULL) {
			gctl_error(req, "No volume name.");
			return (-2);
		}

		/* Search for volume. */
		TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
			if (strcmp(vol->v_name, volname) == 0)
				break;
			pp = vol->v_provider;
			if (pp == NULL)
				continue;
			if (strcmp(pp->name, volname) == 0)
				break;
			if (strncmp(pp->name, "raid/", 5) == 0 &&
			    strcmp(pp->name + 5, volname) == 0)
				break;
		}
		if (vol == NULL) {
			i = strtol(volname, &tmp, 10);
			if (verb != volname && tmp[0] == 0) {
				TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
					if (vol->v_global_id == i)
						break;
				}
			}
		}
		if (vol == NULL) {
			gctl_error(req, "Volume '%s' not found.", volname);
			return (-3);
		}

		/* Check if volume is still open. */
		force = gctl_get_paraml(req, "force", sizeof(*force));
		if (force != NULL && *force == 0 &&
		    vol->v_provider_open != 0) {
			gctl_error(req, "Volume is still open.");
			return (-4);
		}

		/* Destroy volume and potentially node. */
		i = 0;
		TAILQ_FOREACH(vol1, &sc->sc_volumes, v_next)
			i++;
		if (i >= 2) {
			g_raid_destroy_volume(vol);
			g_raid_md_ddf_purge_disks(sc);
			g_raid_md_write_ddf(md, NULL, NULL, NULL);
		} else {
			TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
				if (disk->d_consumer)
					ddf_meta_erase(disk->d_consumer);
			}
			g_raid_destroy_node(sc, 0);
		}
		return (0);
	}
	if (strcmp(verb, "remove") == 0 ||
	    strcmp(verb, "fail") == 0) {
		if (*nargs < 2) {
			gctl_error(req, "Invalid number of arguments.");
			return (-1);
		}
		for (i = 1; i < *nargs; i++) {
			snprintf(arg, sizeof(arg), "arg%d", i);
			diskname = gctl_get_asciiparam(req, arg);
			if (diskname == NULL) {
				gctl_error(req, "No disk name (%s).", arg);
				error = -2;
				break;
			}
			if (strncmp(diskname, "/dev/", 5) == 0)
				diskname += 5;

			TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
				if (disk->d_consumer != NULL && 
				    disk->d_consumer->provider != NULL &&
				    strcmp(disk->d_consumer->provider->name,
				     diskname) == 0)
					break;
			}
			if (disk == NULL) {
				gctl_error(req, "Disk '%s' not found.",
				    diskname);
				error = -3;
				break;
			}

			if (strcmp(verb, "fail") == 0) {
				g_raid_md_fail_disk_ddf(md, NULL, disk);
				continue;
			}

			/* Erase metadata on deleting disk and destroy it. */
			ddf_meta_erase(disk->d_consumer);
			g_raid_destroy_disk(disk);
		}
		g_raid_md_ddf_purge_volumes(sc);

		/* Write updated metadata to remaining disks. */
		g_raid_md_write_ddf(md, NULL, NULL, NULL);

		/* Check if anything left. */
		if (g_raid_ndisks(sc, -1) == 0)
			g_raid_destroy_node(sc, 0);
		else
			g_raid_md_ddf_refill(sc);
		return (error);
	}
	if (strcmp(verb, "insert") == 0) {
		if (*nargs < 2) {
			gctl_error(req, "Invalid number of arguments.");
			return (-1);
		}
		for (i = 1; i < *nargs; i++) {
			/* Get disk name. */
			snprintf(arg, sizeof(arg), "arg%d", i);
			diskname = gctl_get_asciiparam(req, arg);
			if (diskname == NULL) {
				gctl_error(req, "No disk name (%s).", arg);
				error = -3;
				break;
			}

			/* Try to find provider with specified name. */
			g_topology_lock();
			cp = g_raid_open_consumer(sc, diskname);
			if (cp == NULL) {
				gctl_error(req, "Can't open disk '%s'.",
				    diskname);
				g_topology_unlock();
				error = -4;
				break;
			}
			pp = cp->provider;
			g_topology_unlock();

			pd = malloc(sizeof(*pd), M_MD_DDF, M_WAITOK | M_ZERO);

			disk = g_raid_create_disk(sc);
			disk->d_consumer = cp;
			disk->d_md_data = (void *)pd;
			cp->private = disk;

			g_raid_get_disk_info(disk);

			/* Welcome the "new" disk. */
			g_raid_change_disk_state(disk, G_RAID_DISK_S_SPARE);
			ddf_meta_create(disk, &mdi->mdio_meta);
			sa = ddf_meta_find_sa(&pd->pd_meta, 1);
			if (sa != NULL) {
				SET32D(&pd->pd_meta, sa->Signature,
				    DDF_SA_SIGNATURE);
				SET8D(&pd->pd_meta, sa->Spare_Type, 0);
				SET16D(&pd->pd_meta, sa->Populated_SAEs, 0);
				SET16D(&pd->pd_meta, sa->MAX_SAE_Supported,
				    (GET16(&pd->pd_meta, hdr->Configuration_Record_Length) *
				     pd->pd_meta.sectorsize -
				     sizeof(struct ddf_sa_record)) /
				    sizeof(struct ddf_sa_entry));
			}
			if (mdi->mdio_meta.hdr == NULL)
				ddf_meta_copy(&mdi->mdio_meta, &pd->pd_meta);
			else
				ddf_meta_update(&mdi->mdio_meta, &pd->pd_meta);
			g_raid_md_write_ddf(md, NULL, NULL, NULL);
			g_raid_md_ddf_refill(sc);
		}
		return (error);
	}
	return (-100);
}

static int
g_raid_md_write_ddf(struct g_raid_md_object *md, struct g_raid_volume *tvol,
    struct g_raid_subdisk *tsd, struct g_raid_disk *tdisk)
{
	struct g_raid_softc *sc;
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct g_raid_disk *disk;
	struct g_raid_md_ddf_perdisk *pd;
	struct g_raid_md_ddf_pervolume *pv;
	struct g_raid_md_ddf_object *mdi;
	struct ddf_meta *gmeta;
	struct ddf_vol_meta *vmeta;
	struct ddf_vdc_record *vdc;
	struct ddf_sa_record *sa;
	uint64_t *val2;
	int i, j, pos, bvd, size;

	sc = md->mdo_softc;
	mdi = (struct g_raid_md_ddf_object *)md;
	gmeta = &mdi->mdio_meta;

	if (sc->sc_stopping == G_RAID_DESTROY_HARD)
		return (0);

	/*
	 * Clear disk flags to let only really needed ones to be reset.
	 * Do it only if there are no volumes in starting state now,
	 * as they can update disk statuses yet and we may kill innocent.
	 */
	if (mdi->mdio_starting == 0) {
		for (i = 0; i < GET16(gmeta, pdr->Populated_PDEs); i++) {
			if (isff(gmeta->pdr->entry[i].PD_GUID, 24))
				continue;
			SET16(gmeta, pdr->entry[i].PD_Type,
			    GET16(gmeta, pdr->entry[i].PD_Type) &
			    ~(DDF_PDE_PARTICIPATING |
			      DDF_PDE_GLOBAL_SPARE | DDF_PDE_CONFIG_SPARE));
			if ((GET16(gmeta, pdr->entry[i].PD_State) &
			    DDF_PDE_PFA) == 0)
				SET16(gmeta, pdr->entry[i].PD_State, 0);
		}
	}

	/* Generate/update new per-volume metadata. */
	TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
		pv = (struct g_raid_md_ddf_pervolume *)vol->v_md_data;
		if (vol->v_stopping || !pv->pv_started)
			continue;
		vmeta = &pv->pv_meta;

		SET32(vmeta, vdc->Sequence_Number,
		    GET32(vmeta, vdc->Sequence_Number) + 1);
		if (vol->v_raid_level == G_RAID_VOLUME_RL_RAID1E &&
		    vol->v_disks_count % 2 == 0)
			SET16(vmeta, vdc->Primary_Element_Count, 2);
		else
			SET16(vmeta, vdc->Primary_Element_Count,
			    vol->v_disks_count);
		SET8(vmeta, vdc->Stripe_Size,
		    ffs(vol->v_strip_size / vol->v_sectorsize) - 1);
		if (vol->v_raid_level == G_RAID_VOLUME_RL_RAID1E &&
		    vol->v_disks_count % 2 == 0) {
			SET8(vmeta, vdc->Primary_RAID_Level,
			    DDF_VDCR_RAID1);
			SET8(vmeta, vdc->RLQ, 0);
			SET8(vmeta, vdc->Secondary_Element_Count,
			    vol->v_disks_count / 2);
			SET8(vmeta, vdc->Secondary_RAID_Level, 0);
		} else {
			SET8(vmeta, vdc->Primary_RAID_Level,
			    vol->v_raid_level);
			SET8(vmeta, vdc->RLQ,
			    vol->v_raid_level_qualifier);
			SET8(vmeta, vdc->Secondary_Element_Count, 1);
			SET8(vmeta, vdc->Secondary_RAID_Level, 0);
		}
		SET8(vmeta, vdc->Secondary_Element_Seq, 0);
		SET64(vmeta, vdc->Block_Count, 0);
		SET64(vmeta, vdc->VD_Size, vol->v_mediasize / vol->v_sectorsize);
		SET16(vmeta, vdc->Block_Size, vol->v_sectorsize);
		SET8(vmeta, vdc->Rotate_Parity_count,
		    fls(vol->v_rotate_parity) - 1);
		SET8(vmeta, vdc->MDF_Parity_Disks, vol->v_mdf_pdisks);
		SET16(vmeta, vdc->MDF_Parity_Generator_Polynomial,
		    vol->v_mdf_polynomial);
		SET8(vmeta, vdc->MDF_Constant_Generation_Method,
		    vol->v_mdf_method);

		SET16(vmeta, vde->VD_Number, vol->v_global_id);
		if (vol->v_state <= G_RAID_VOLUME_S_BROKEN)
			SET8(vmeta, vde->VD_State, DDF_VDE_FAILED);
		else if (vol->v_state <= G_RAID_VOLUME_S_DEGRADED)
			SET8(vmeta, vde->VD_State, DDF_VDE_DEGRADED);
		else if (vol->v_state <= G_RAID_VOLUME_S_SUBOPTIMAL)
			SET8(vmeta, vde->VD_State, DDF_VDE_PARTIAL);
		else
			SET8(vmeta, vde->VD_State, DDF_VDE_OPTIMAL);
		if (vol->v_dirty ||
		    g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_STALE) > 0 ||
		    g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_RESYNC) > 0)
			SET8(vmeta, vde->VD_State,
			    GET8(vmeta, vde->VD_State) | DDF_VDE_DIRTY);
		SET8(vmeta, vde->Init_State, DDF_VDE_INIT_FULL); // XXX
		ddf_meta_put_name(vmeta, vol->v_name);

		for (i = 0; i < vol->v_disks_count; i++) {
			sd = &vol->v_subdisks[i];
			bvd = i / GET16(vmeta, vdc->Primary_Element_Count);
			pos = i % GET16(vmeta, vdc->Primary_Element_Count);
			disk = sd->sd_disk;
			if (disk != NULL) {
				pd = (struct g_raid_md_ddf_perdisk *)disk->d_md_data;
				if (vmeta->bvdc[bvd] == NULL) {
					size = GET16(vmeta,
					    hdr->Configuration_Record_Length) *
					    vmeta->sectorsize;
					vmeta->bvdc[bvd] = malloc(size,
					    M_MD_DDF, M_WAITOK);
					memset(vmeta->bvdc[bvd], 0xff, size);
				}
				memcpy(vmeta->bvdc[bvd], vmeta->vdc,
				    sizeof(struct ddf_vdc_record));
				SET8(vmeta, bvdc[bvd]->Secondary_Element_Seq, bvd);
				SET64(vmeta, bvdc[bvd]->Block_Count,
				    sd->sd_size / vol->v_sectorsize);
				SET32(vmeta, bvdc[bvd]->Physical_Disk_Sequence[pos],
				    GET32(&pd->pd_meta, pdd->PD_Reference));
				val2 = (uint64_t *)&(vmeta->bvdc[bvd]->Physical_Disk_Sequence[
				    GET16(vmeta, hdr->Max_Primary_Element_Entries)]);
				SET64P(vmeta, val2 + pos,
				    sd->sd_offset / vol->v_sectorsize);
			}
			if (vmeta->bvdc[bvd] == NULL)
				continue;

			j = ddf_meta_find_pd(gmeta, NULL,
			    GET32(vmeta, bvdc[bvd]->Physical_Disk_Sequence[pos]));
			if (j < 0)
				continue;
			SET16(gmeta, pdr->entry[j].PD_Type,
			    GET16(gmeta, pdr->entry[j].PD_Type) |
			    DDF_PDE_PARTICIPATING);
			if (sd->sd_state == G_RAID_SUBDISK_S_NONE)
				SET16(gmeta, pdr->entry[j].PD_State,
				    GET16(gmeta, pdr->entry[j].PD_State) |
				    (DDF_PDE_FAILED | DDF_PDE_MISSING));
			else if (sd->sd_state == G_RAID_SUBDISK_S_FAILED)
				SET16(gmeta, pdr->entry[j].PD_State,
				    GET16(gmeta, pdr->entry[j].PD_State) |
				    (DDF_PDE_FAILED | DDF_PDE_PFA));
			else if (sd->sd_state <= G_RAID_SUBDISK_S_REBUILD)
				SET16(gmeta, pdr->entry[j].PD_State,
				    GET16(gmeta, pdr->entry[j].PD_State) |
				    DDF_PDE_REBUILD);
			else
				SET16(gmeta, pdr->entry[j].PD_State,
				    GET16(gmeta, pdr->entry[j].PD_State) |
				    DDF_PDE_ONLINE);
		}
	}

	/* Mark spare and failed disks as such. */
	TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
		pd = (struct g_raid_md_ddf_perdisk *)disk->d_md_data;
		i = ddf_meta_find_pd(gmeta, NULL,
		    GET32(&pd->pd_meta, pdd->PD_Reference));
		if (i < 0)
			continue;
		if (disk->d_state == G_RAID_DISK_S_FAILED) {
			SET16(gmeta, pdr->entry[i].PD_State,
			    GET16(gmeta, pdr->entry[i].PD_State) |
			    (DDF_PDE_FAILED | DDF_PDE_PFA));
		}
		if (disk->d_state != G_RAID_DISK_S_SPARE)
			continue;
		sa = ddf_meta_find_sa(&pd->pd_meta, 0);
		if (sa == NULL ||
		    (GET8D(&pd->pd_meta, sa->Spare_Type) &
		     DDF_SAR_TYPE_DEDICATED) == 0) {
			SET16(gmeta, pdr->entry[i].PD_Type,
			    GET16(gmeta, pdr->entry[i].PD_Type) |
			    DDF_PDE_GLOBAL_SPARE);
		} else {
			SET16(gmeta, pdr->entry[i].PD_Type,
			    GET16(gmeta, pdr->entry[i].PD_Type) |
			    DDF_PDE_CONFIG_SPARE);
		}
		SET16(gmeta, pdr->entry[i].PD_State,
		    GET16(gmeta, pdr->entry[i].PD_State) |
		    DDF_PDE_ONLINE);
	}

	/* Remove disks without "participating" flag (unused). */
	for (i = 0, j = -1; i < GET16(gmeta, pdr->Populated_PDEs); i++) {
		if (isff(gmeta->pdr->entry[i].PD_GUID, 24))
			continue;
		if ((GET16(gmeta, pdr->entry[i].PD_Type) &
		    (DDF_PDE_PARTICIPATING |
		     DDF_PDE_GLOBAL_SPARE | DDF_PDE_CONFIG_SPARE)) != 0 ||
		    g_raid_md_ddf_get_disk(sc,
		     NULL, GET32(gmeta, pdr->entry[i].PD_Reference)) != NULL)
			j = i;
		else
			memset(&gmeta->pdr->entry[i], 0xff,
			    sizeof(struct ddf_pd_entry));
	}
	SET16(gmeta, pdr->Populated_PDEs, j + 1);

	/* Update per-disk metadata and write them. */
	TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
		pd = (struct g_raid_md_ddf_perdisk *)disk->d_md_data;
		if (disk->d_state != G_RAID_DISK_S_ACTIVE &&
		    disk->d_state != G_RAID_DISK_S_SPARE)
			continue;
		/* Update PDR. */
		memcpy(pd->pd_meta.pdr, gmeta->pdr,
		    GET32(&pd->pd_meta, hdr->pdr_length) *
		    pd->pd_meta.sectorsize);
		/* Update VDR. */
		SET16(&pd->pd_meta, vdr->Populated_VDEs, 0);
		TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
			if (vol->v_stopping)
				continue;
			pv = (struct g_raid_md_ddf_pervolume *)vol->v_md_data;
			i = ddf_meta_find_vd(&pd->pd_meta,
			    pv->pv_meta.vde->VD_GUID);
			if (i < 0)
				i = ddf_meta_find_vd(&pd->pd_meta, NULL);
			if (i >= 0)
				memcpy(&pd->pd_meta.vdr->entry[i],
				    pv->pv_meta.vde,
				    sizeof(struct ddf_vd_entry));
		}
		/* Update VDC. */
		if (mdi->mdio_starting == 0) {
			/* Remove all VDCs to restore needed later. */
			j = GETCRNUM(&pd->pd_meta);
			for (i = 0; i < j; i++) {
				vdc = GETVDCPTR(&pd->pd_meta, i);
				if (GET32D(&pd->pd_meta, vdc->Signature) !=
				    DDF_VDCR_SIGNATURE)
					continue;
				SET32D(&pd->pd_meta, vdc->Signature, 0xffffffff);
			}
		}
		TAILQ_FOREACH(sd, &disk->d_subdisks, sd_next) {
			vol = sd->sd_volume;
			if (vol->v_stopping)
				continue;
			pv = (struct g_raid_md_ddf_pervolume *)vol->v_md_data;
			vmeta = &pv->pv_meta;
			vdc = ddf_meta_find_vdc(&pd->pd_meta,
			    vmeta->vde->VD_GUID);
			if (vdc == NULL)
				vdc = ddf_meta_find_vdc(&pd->pd_meta, NULL);
			if (vdc != NULL) {
				bvd = sd->sd_pos / GET16(vmeta,
				    vdc->Primary_Element_Count);
				memcpy(vdc, vmeta->bvdc[bvd],
				    GET16(&pd->pd_meta,
				    hdr->Configuration_Record_Length) *
				    pd->pd_meta.sectorsize);
			}
		}
		G_RAID_DEBUG(1, "Writing DDF metadata to %s",
		    g_raid_get_diskname(disk));
		g_raid_md_ddf_print(&pd->pd_meta);
		ddf_meta_write(disk->d_consumer, &pd->pd_meta);
	}
	return (0);
}

static int
g_raid_md_fail_disk_ddf(struct g_raid_md_object *md,
    struct g_raid_subdisk *tsd, struct g_raid_disk *tdisk)
{
	struct g_raid_softc *sc;
	struct g_raid_md_ddf_perdisk *pd;
	struct g_raid_subdisk *sd;
	int i;

	sc = md->mdo_softc;
	pd = (struct g_raid_md_ddf_perdisk *)tdisk->d_md_data;

	/* We can't fail disk that is not a part of array now. */
	if (tdisk->d_state != G_RAID_DISK_S_ACTIVE)
		return (-1);

	/*
	 * Mark disk as failed in metadata and try to write that metadata
	 * to the disk itself to prevent it's later resurrection as STALE.
	 */
	G_RAID_DEBUG(1, "Writing DDF metadata to %s",
	    g_raid_get_diskname(tdisk));
	i = ddf_meta_find_pd(&pd->pd_meta, NULL, GET32(&pd->pd_meta, pdd->PD_Reference));
	SET16(&pd->pd_meta, pdr->entry[i].PD_State, DDF_PDE_FAILED | DDF_PDE_PFA);
	if (tdisk->d_consumer != NULL)
		ddf_meta_write(tdisk->d_consumer, &pd->pd_meta);

	/* Change states. */
	g_raid_change_disk_state(tdisk, G_RAID_DISK_S_FAILED);
	TAILQ_FOREACH(sd, &tdisk->d_subdisks, sd_next) {
		g_raid_change_subdisk_state(sd,
		    G_RAID_SUBDISK_S_FAILED);
		g_raid_event_send(sd, G_RAID_SUBDISK_E_FAILED,
		    G_RAID_EVENT_SUBDISK);
	}

	/* Write updated metadata to remaining disks. */
	g_raid_md_write_ddf(md, NULL, NULL, tdisk);

	g_raid_md_ddf_refill(sc);
	return (0);
}

static int
g_raid_md_free_disk_ddf(struct g_raid_md_object *md,
    struct g_raid_disk *disk)
{
	struct g_raid_md_ddf_perdisk *pd;

	pd = (struct g_raid_md_ddf_perdisk *)disk->d_md_data;
	ddf_meta_free(&pd->pd_meta);
	free(pd, M_MD_DDF);
	disk->d_md_data = NULL;
	return (0);
}

static int
g_raid_md_free_volume_ddf(struct g_raid_md_object *md,
    struct g_raid_volume *vol)
{
	struct g_raid_md_ddf_object *mdi;
	struct g_raid_md_ddf_pervolume *pv;

	mdi = (struct g_raid_md_ddf_object *)md;
	pv = (struct g_raid_md_ddf_pervolume *)vol->v_md_data;
	ddf_vol_meta_free(&pv->pv_meta);
	if (!pv->pv_started) {
		pv->pv_started = 1;
		mdi->mdio_starting--;
		callout_stop(&pv->pv_start_co);
	}
	free(pv, M_MD_DDF);
	vol->v_md_data = NULL;
	return (0);
}

static int
g_raid_md_free_ddf(struct g_raid_md_object *md)
{
	struct g_raid_md_ddf_object *mdi;

	mdi = (struct g_raid_md_ddf_object *)md;
	if (!mdi->mdio_started) {
		mdi->mdio_started = 0;
		callout_stop(&mdi->mdio_start_co);
		G_RAID_DEBUG1(1, md->mdo_softc,
		    "root_mount_rel %p", mdi->mdio_rootmount);
		root_mount_rel(mdi->mdio_rootmount);
		mdi->mdio_rootmount = NULL;
	}
	ddf_meta_free(&mdi->mdio_meta);
	return (0);
}

G_RAID_MD_DECLARE(ddf, "DDF");
