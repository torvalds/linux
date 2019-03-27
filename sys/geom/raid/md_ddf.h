/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Alexander Motin <mav@FreeBSD.org>
 * Copyright (c) 2008 Scott Long
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef MD_DDF_H
#define MD_DDF_H

/* Definitions from the SNIA DDF spec, rev 1.2/2.0 */

#define	DDF_HEADER_LENGTH		512
struct ddf_header {
	uint32_t	Signature;
#define	DDF_HEADER_SIGNATURE	0xde11de11
	uint32_t	CRC;
	uint8_t		DDF_Header_GUID[24];
	uint8_t		DDF_rev[8];
	uint32_t	Sequence_Number;
	uint32_t	TimeStamp;
	uint8_t		Open_Flag;
#define	DDF_HEADER_CLOSED	0x00
#define	DDF_HEADER_OPENED_MASK	0x0f
#define	DDF_HEADER_OPEN_ANCHOR	0xff
	uint8_t		Foreign_Flag;
	uint8_t		Diskgrouping;
	uint8_t		pad1[13];
	uint8_t		Header_ext[32];
	uint64_t	Primary_Header_LBA;
	uint64_t	Secondary_Header_LBA;
	uint8_t		Header_Type;
#define	DDF_HEADER_ANCHOR	0x00
#define	DDF_HEADER_PRIMARY	0x01
#define	DDF_HEADER_SECONDARY	0x02
	uint8_t		pad2[3];
	uint32_t	WorkSpace_Length;
	uint64_t	WorkSpace_LBA;
	uint16_t	Max_PD_Entries;
	uint16_t	Max_VD_Entries;
	uint16_t	Max_Partitions;
	uint16_t	Configuration_Record_Length;
	uint16_t	Max_Primary_Element_Entries;
	uint32_t	Max_Mapped_Block_Entries; /* DDF 2.0 */
	uint8_t		pad3[50];
	uint32_t	cd_section;	/* Controller_Data_Section */
	uint32_t	cd_length;	/* Controller_Data_Section_Length */
	uint32_t	pdr_section;	/* Physical_Drive_Records_Section */
	uint32_t	pdr_length;	/* Physical_Drive_Records_Length */
	uint32_t	vdr_section;	/* Virtual_Drive_Records_Section */
	uint32_t	vdr_length;	/* Virtual_Drive_Records_Length */
	uint32_t	cr_section;	/* Configuration_Records_Section */
	uint32_t	cr_length;	/* Configuration_Records_Length */
	uint32_t	pdd_section;	/* Physical_Drive_Data_Section */
	uint32_t	pdd_length;	/* Physical_Drive_Data_Length */
	uint32_t	bbmlog_section;	/* BBM_Log_Section */
	uint32_t	bbmlog_length;	/* BBM_Log_Section_Length */
	uint32_t	Diagnostic_Space;
	uint32_t	Diagnostic_Space_Length;
	uint32_t	Vendor_Specific_Logs;
	uint32_t	Vendor_Specific_Logs_Length;
	uint8_t		pad4[256];
} __packed;

struct ddf_cd_record {
	uint32_t	Signature;
#define	DDF_CONTROLLER_DATA_SIGNATURE	0xad111111
	uint32_t	CRC;
	uint8_t		Controller_GUID[24];
	struct {
		uint16_t	Vendor_ID;
		uint16_t	Device_ID;
		uint16_t	SubVendor_ID;
		uint16_t	SubDevice_ID;
	} Controller_Type __packed;
	uint8_t		Product_ID[16];
	uint8_t		pad1[8];
	uint8_t		Controller_Data[448];
} __packed;

struct ddf_device_scsi {
	uint8_t		Lun;
	uint8_t		Id;
	uint8_t		Channel;
	uint8_t		Path_Flags;
#define DDF_DEVICE_SCSI_FLAG_BROKEN	(1 << 7)
} __packed;

struct ddf_device_sas {
	uint64_t	Initiator_Path;
} __packed;

union ddf_pathinfo {
	struct {
		struct ddf_device_scsi	Path0;
		struct ddf_device_scsi	Path1;
		uint8_t			pad[10];
	} __packed scsi;
	struct {
		struct ddf_device_sas	Path0;
		struct ddf_device_sas	Path1;
		uint8_t			Path0_Flags;
		uint8_t			Path1_Flags;
#define DDF_DEVICE_SAS_PHY_ID		0x7f
#define DDF_DEVICE_SAS_FLAG_BROKEN	(1 << 7)
	} __packed sas;
} __packed;

struct ddf_pd_entry {
	uint8_t			PD_GUID[24];
	uint32_t		PD_Reference;
	uint16_t		PD_Type;
#define	DDF_PDE_GUID_FORCE	(1 << 0)
#define	DDF_PDE_PARTICIPATING	(1 << 1)
#define	DDF_PDE_GLOBAL_SPARE	(1 << 2)
#define	DDF_PDE_CONFIG_SPARE	(1 << 3)
#define	DDF_PDE_FOREIGN		(1 << 4)
#define	DDF_PDE_LEGACY		(1 << 5)
#define DDF_PDE_TYPE_MASK	(0x0f << 12)
#define DDF_PDE_UNKNOWN		(0x00 << 12)
#define DDF_PDE_SCSI		(0x01 << 12)
#define DDF_PDE_SAS		(0x02 << 12)
#define DDF_PDE_SATA		(0x03 << 12)
#define DDF_PDE_FC		(0x04 << 12)
	uint16_t		PD_State;
#define	DDF_PDE_ONLINE		(1 << 0)
#define	DDF_PDE_FAILED		(1 << 1)
#define	DDF_PDE_REBUILD		(1 << 2)
#define	DDF_PDE_TRANSITION	(1 << 3)
#define	DDF_PDE_PFA		(1 << 4)
#define	DDF_PDE_UNRECOVERED	(1 << 5)
#define	DDF_PDE_MISSING		(1 << 6)
	uint64_t		Configured_Size;
	union ddf_pathinfo	Path_Information;
	uint16_t		Block_Size;	/* DDF 2.0 */
	uint8_t			pad1[4];
} __packed;

struct ddf_pd_record {
	uint32_t		Signature;
#define	DDF_PDR_SIGNATURE	0x22222222
	uint32_t		CRC;
	uint16_t		Populated_PDEs;
	uint16_t		Max_PDE_Supported;
	uint8_t			pad1[52];
	struct ddf_pd_entry	entry[0];
} __packed;

struct ddf_vd_entry {
	uint8_t			VD_GUID[24];
	uint16_t		VD_Number;
	uint8_t			pad1[2];
	uint16_t		VD_Type;
#define DDF_VDE_SHARED		(1 << 0)
#define	DDF_VDE_ENFORCE_GROUP	(1 << 1)
#define	DDF_VDE_UNICODE_NAME	(1 << 2)
#define	DDF_VDE_OWNER_ID_VALID	(1 << 3)
	uint16_t		Controller_GUID_CRC;
	uint8_t			VD_State;
#define	DDF_VDE_OPTIMAL		0x00
#define	DDF_VDE_DEGRADED	0x01
#define	DDF_VDE_DELETED		0x02
#define	DDF_VDE_MISSING		0x03
#define	DDF_VDE_FAILED		0x04
#define DDF_VDE_PARTIAL		0x05
#define DDF_VDE_OFFLINE		0x06
#define	DDF_VDE_STATE_MASK	0x07
#define	DDF_VDE_MORPH		(1 << 3)
#define	DDF_VDE_DIRTY		(1 << 4)
	uint8_t			Init_State;
#define	DDF_VDE_UNINTIALIZED	0x00
#define	DDF_VDE_INIT_QUICK	0x01
#define	DDF_VDE_INIT_FULL	0x02
#define	DDF_VDE_INIT_MASK	0x03
#define	DDF_VDE_UACCESS_RW	0x00
#define	DDF_VDE_UACCESS_RO	0x80
#define	DDF_VDE_UACCESS_BLOCKED	0xc0
#define	DDF_VDE_UACCESS_MASK	0xc0
	uint8_t			Drive_Failures_Remaining;	/* DDF 2.0 */
	uint8_t			pad2[13];
	uint8_t			VD_Name[16];
} __packed;

struct ddf_vd_record {
	uint32_t		Signature;
#define	DDF_VD_RECORD_SIGNATURE	0xdddddddd
	uint32_t		CRC;
	uint16_t		Populated_VDEs;
	uint16_t		Max_VDE_Supported;
	uint8_t			pad1[52];
	struct ddf_vd_entry	entry[0];
} __packed;

#define DDF_CR_INVALID		0xffffffff

struct ddf_vdc_record {
	uint32_t	Signature;
#define	DDF_VDCR_SIGNATURE	0xeeeeeeee
	uint32_t	CRC;
	uint8_t		VD_GUID[24];
	uint32_t	Timestamp;
	uint32_t	Sequence_Number;
	uint8_t		pad1[24];
	uint16_t	Primary_Element_Count;
	uint8_t		Stripe_Size;
	uint8_t		Primary_RAID_Level;
#define DDF_VDCR_RAID0		0x00
#define DDF_VDCR_RAID1		0x01
#define DDF_VDCR_RAID3		0x03
#define DDF_VDCR_RAID4		0x04
#define DDF_VDCR_RAID5		0x05
#define DDF_VDCR_RAID6		0x06
#define DDF_VDCR_RAID1E		0x11
#define DDF_VDCR_SINGLE		0x0f
#define DDF_VDCR_CONCAT		0x1f
#define DDF_VDCR_RAID5E		0x15
#define DDF_VDCR_RAID5EE	0x25
	uint8_t		RLQ;
	uint8_t		Secondary_Element_Count;
	uint8_t		Secondary_Element_Seq;
	uint8_t		Secondary_RAID_Level;
	uint64_t	Block_Count;
	uint64_t	VD_Size;
	uint16_t	Block_Size;			/* DDF 2.0 */
	uint8_t		Rotate_Parity_count;		/* DDF 2.0 */
	uint8_t		pad2[5];
	uint32_t	Associated_Spares[8];
	uint64_t	Cache_Flags;
#define DDF_VDCR_CACHE_WB		(1 << 0)
#define	DDF_VDCR_CACHE_WB_ADAPTIVE	(1 << 1)
#define	DDF_VDCR_CACHE_RA		(1 << 2)
#define	DDF_VDCR_CACHE_RA_ADAPTIVE	(1 << 3)
#define	DDF_VDCR_CACHE_WCACHE_NOBATTERY	(1 << 4)
#define	DDF_VDCR_CACHE_WCACHE_ALLOW	(1 << 5)
#define	DDF_VDCR_CACHE_RCACHE_ALLOW	(1 << 6)
#define	DDF_VDCR_CACHE_VENDOR		(1 << 7)
	uint8_t		BG_Rate;
	uint8_t		pad3[3];
	uint8_t		MDF_Parity_Disks;		/* DDF 2.0 */
	uint16_t	MDF_Parity_Generator_Polynomial; /* DDF 2.0 */
	uint8_t		pad4;
	uint8_t		MDF_Constant_Generation_Method; /* DDF 2.0 */
	uint8_t		pad5[47];
	uint8_t		pad6[192];
	uint8_t		V0[32];
	uint8_t		V1[32];
	uint8_t		V2[16];
	uint8_t		V3[16];
	uint8_t		Vendor_Scratch[32];
	uint32_t	Physical_Disk_Sequence[0];
} __packed;

struct ddf_vuc_record {
	uint32_t	Signature;
#define	DDF_VUCR_SIGNATURE	0x88888888
	uint32_t	CRC;
	uint8_t		VD_GUID[24];
} __packed;

struct ddf_sa_entry {
	uint8_t		VD_GUID[24];
	uint16_t	Secondary_Element;
	uint8_t		rsrvd2[6];
} __packed;

struct ddf_sa_record {
	uint32_t	Signature;
#define	DDF_SA_SIGNATURE	0x55555555
	uint32_t	CRC;
	uint32_t	Timestamp;
	uint8_t		pad1[7];
	uint8_t		Spare_Type;
#define	DDF_SAR_TYPE_DEDICATED		(1 << 0)
#define	DDF_SAR_TYPE_REVERTIBLE		(1 << 1)
#define	DDF_SAR_TYPE_ACTIVE		(1 << 2)
#define	DDF_SAR_TYPE_ENCL_AFFINITY	(1 << 3)
	uint16_t	Populated_SAEs;
	uint16_t	MAX_SAE_Supported;
	uint8_t		pad2[8];
	struct ddf_sa_entry	entry[0];
} __packed;

struct ddf_pdd_record {
	uint32_t	Signature;
#define	DDF_PDD_SIGNATURE	0x33333333
	uint32_t	CRC;
	uint8_t		PD_GUID[24];
	uint32_t	PD_Reference;
	uint8_t		Forced_Ref_Flag;
#define	DDF_PDD_FORCED_REF	0x01
	uint8_t		Forced_PD_GUID_Flag;
#define	DDF_PDD_FORCED_GUID	0x01
	uint8_t		Vendor_Scratch[32];
	uint8_t		pad2[442];
} __packed;

struct ddf_bbm_entry {
	uint64_t	Defective_Block_Start;
	uint32_t	Spare_Block_Offset;
	uint16_t	Remapped_Count;
	uint8_t	pad[2];
};

struct ddf_bbm_log {
	uint32_t	Signature;
#define	DDF_BBML_SIGNATURE	0xabadb10c
	uint32_t	CRC;
	uint32_t	Entry_Count;
	uint32_t	Spare_Block_Count;
	uint8_t		pad1[8];
	uint64_t	First_Spare_LBA;
	uint64_t	Mapped_Block_Entry[0];
} __packed;

struct ddf_vendor_log {
	uint32_t	Signature;
#define	DDF_VENDOR_LOG_SIGNATURE	0x01dbeef0
	uint32_t	CRC;
	uint64_t	Log_Owner;
	uint8_t		pad1[16];
} __packed;

#endif
