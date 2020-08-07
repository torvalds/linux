/*
 * osta_udf.h
 *
 * This file is based on OSTA UDF(tm) 2.60 (March 1, 2005)
 * http://www.osta.org
 *
 * Copyright (c) 2001-2004  Ben Fennema
 * Copyright (c) 2017-2019  Pali Rohár <pali@kernel.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * @file
 * OSTA-UDF defines and structure definitions
 */

#include "ecma_167.h"

#ifndef _OSTA_UDF_H
#define _OSTA_UDF_H 1

/* OSTA CS0 Charspec (UDF 2.60 2.1.2) */
#define UDF_CHAR_SET_TYPE		0
#define UDF_CHAR_SET_INFO		"OSTA Compressed Unicode"

/* Entity Identifier (UDF 2.60 2.1.5) */
/* Identifiers (UDF 2.60 2.1.5.2) */
/* Implementation Use Extended Attribute (UDF 2.60 3.3.4.5) */
/* Virtual Allocation Table (UDF 1.50 2.2.10) */
/* Logical Volume Extended Information (UDF 1.50 Errata, DCN 5003, 3.3.4.5.1.3) */
/* OS2EA (UDF 1.50 3.3.4.5.3.1) */
/* MacUniqueIDTable (UDF 1.50 3.3.4.5.4.3) */
/* MacResourceFork (UDF 1.50 3.3.4.5.4.4) */
#define UDF_ID_DEVELOPER		"*Linux UDFFS"
#define	UDF_ID_COMPLIANT		"*OSTA UDF Compliant"
#define UDF_ID_LV_INFO			"*UDF LV Info"
#define UDF_ID_FREE_EA			"*UDF FreeEASpace"
#define UDF_ID_FREE_APP_EA		"*UDF FreeAppEASpace"
#define UDF_ID_DVD_CGMS			"*UDF DVD CGMS Info"
#define UDF_ID_VAT_LVEXTENSION		"*UDF VAT LVExtension"
#define UDF_ID_OS2_EA			"*UDF OS/2 EA"
#define UDF_ID_OS2_EA_LENGTH		"*UDF OS/2 EALength"
#define UDF_ID_MAC_VOLUME		"*UDF Mac VolumeInfo"
#define UDF_ID_MAC_FINDER		"*UDF Mac FinderInfo"
#define UDF_ID_MAC_UNIQUE		"*UDF Mac UniqueIDTable"
#define UDF_ID_MAC_RESOURCE		"*UDF Mac ResourceFork"
#define UDF_ID_OS400_DIRINFO		"*UDF OS/400 DirInfo"
#define UDF_ID_VIRTUAL			"*UDF Virtual Partition"
#define UDF_ID_SPARABLE			"*UDF Sparable Partition"
#define UDF_ID_ALLOC			"*UDF Virtual Alloc Tbl"
#define UDF_ID_SPARING			"*UDF Sparing Table"
#define UDF_ID_METADATA			"*UDF Metadata Partition"

/* Identifier Suffix (UDF 2.60 2.1.5.3) */
#define DOMAIN_FLAGS_HARD_WRITE_PROTECT	0x01
#define DOMAIN_FLAGS_SOFT_WRITE_PROTECT	0x02

struct domainIdentSuffix {
	__le16		UDFRevision;
	uint8_t		domainFlags;
	uint8_t		reserved[5];
} __packed;

struct UDFIdentSuffix {
	__le16		UDFRevision;
	uint8_t		OSClass;
	uint8_t		OSIdentifier;
	uint8_t		reserved[4];
} __packed;

struct impIdentSuffix {
	uint8_t		OSClass;
	uint8_t		OSIdentifier;
	uint8_t		impUse[6];
} __packed;

struct appIdentSuffix {
	uint8_t		impUse[8];
} __packed;

/* Logical Volume Integrity Descriptor (UDF 2.60 2.2.6) */
/* Implementation Use (UDF 2.60 2.2.6.4) */
struct logicalVolIntegrityDescImpUse {
	struct regid	impIdent;
	__le32		numFiles;
	__le32		numDirs;
	__le16		minUDFReadRev;
	__le16		minUDFWriteRev;
	__le16		maxUDFWriteRev;
	uint8_t		impUse[0];
} __packed;

/* Implementation Use Volume Descriptor (UDF 2.60 2.2.7) */
/* Implementation Use (UDF 2.60 2.2.7.2) */
struct impUseVolDescImpUse {
	struct charspec	LVICharset;
	dstring		logicalVolIdent[128];
	dstring		LVInfo1[36];
	dstring		LVInfo2[36];
	dstring		LVInfo3[36];
	struct regid	impIdent;
	uint8_t		impUse[128];
} __packed;

struct udfPartitionMap2 {
	uint8_t		partitionMapType;
	uint8_t		partitionMapLength;
	uint8_t		reserved1[2];
	struct regid	partIdent;
	__le16		volSeqNum;
	__le16		partitionNum;
} __packed;

/* Virtual Partition Map (UDF 2.60 2.2.8) */
struct virtualPartitionMap {
	uint8_t		partitionMapType;
	uint8_t		partitionMapLength;
	uint8_t		reserved1[2];
	struct regid	partIdent;
	__le16		volSeqNum;
	__le16		partitionNum;
	uint8_t		reserved2[24];
} __packed;

/* Sparable Partition Map (UDF 2.60 2.2.9) */
struct sparablePartitionMap {
	uint8_t partitionMapType;
	uint8_t partitionMapLength;
	uint8_t reserved1[2];
	struct regid partIdent;
	__le16 volSeqNum;
	__le16 partitionNum;
	__le16 packetLength;
	uint8_t numSparingTables;
	uint8_t reserved2[1];
	__le32 sizeSparingTable;
	__le32 locSparingTable[4];
} __packed;

/* Metadata Partition Map (UDF 2.60 2.2.10) */
struct metadataPartitionMap {
	uint8_t		partitionMapType;
	uint8_t		partitionMapLength;
	uint8_t		reserved1[2];
	struct regid	partIdent;
	__le16		volSeqNum;
	__le16		partitionNum;
	__le32		metadataFileLoc;
	__le32		metadataMirrorFileLoc;
	__le32		metadataBitmapFileLoc;
	__le32		allocUnitSize;
	__le16		alignUnitSize;
	uint8_t		flags;
	uint8_t		reserved2[5];
} __packed;

/* Virtual Allocation Table (UDF 1.5 2.2.10) */
struct virtualAllocationTable15 {
	__le32		vatEntry[0];
	struct regid	vatIdent;
	__le32		previousVATICBLoc;
} __packed;

#define ICBTAG_FILE_TYPE_VAT15		0x00U

/* Virtual Allocation Table (UDF 2.60 2.2.11) */
struct virtualAllocationTable20 {
	__le16		lengthHeader;
	__le16		lengthImpUse;
	dstring		logicalVolIdent[128];
	__le32		previousVATICBLoc;
	__le32		numFiles;
	__le32		numDirs;
	__le16		minUDFReadRev;
	__le16		minUDFWriteRev;
	__le16		maxUDFWriteRev;
	__le16		reserved;
	uint8_t		impUse[0];
	__le32		vatEntry[0];
} __packed;

#define ICBTAG_FILE_TYPE_VAT20		0xF8U

/* Sparing Table (UDF 2.60 2.2.12) */
struct sparingEntry {
	__le32		origLocation;
	__le32		mappedLocation;
} __packed;

struct sparingTable {
	struct tag	descTag;
	struct regid	sparingIdent;
	__le16		reallocationTableLen;
	__le16		reserved;
	__le32		sequenceNum;
	struct sparingEntry
			mapEntry[0];
} __packed;

/* Metadata File (and Metadata Mirror File) (UDF 2.60 2.2.13.1) */
#define ICBTAG_FILE_TYPE_MAIN		0xFA
#define ICBTAG_FILE_TYPE_MIRROR		0xFB
#define ICBTAG_FILE_TYPE_BITMAP		0xFC

/* struct long_ad ICB - ADImpUse (UDF 2.60 2.2.4.3) */
struct allocDescImpUse {
	__le16		flags;
	uint8_t		impUse[4];
} __packed;

#define AD_IU_EXT_ERASED		0x0001

/* Real-Time Files (UDF 2.60 6.11) */
#define ICBTAG_FILE_TYPE_REALTIME	0xF9U

/* Implementation Use Extended Attribute (UDF 2.60 3.3.4.5) */
/* FreeEASpace (UDF 2.60 3.3.4.5.1.1) */
struct freeEaSpace {
	__le16		headerChecksum;
	uint8_t		freeEASpace[0];
} __packed;

/* DVD Copyright Management Information (UDF 2.60 3.3.4.5.1.2) */
struct DVDCopyrightImpUse {
	__le16		headerChecksum;
	uint8_t		CGMSInfo;
	uint8_t		dataType;
	uint8_t		protectionSystemInfo[4];
} __packed;

/* Logical Volume Extended Information (UDF 1.50 Errata, DCN 5003, 3.3.4.5.1.3) */
struct LVExtensionEA {
	__le16		headerChecksum;
	__le64		verificationID;
	__le32		numFiles;
	__le32		numDirs;
	dstring		logicalVolIdent[128];
} __packed;

/* Application Use Extended Attribute (UDF 2.60 3.3.4.6) */
/* FreeAppEASpace (UDF 2.60 3.3.4.6.1) */
struct freeAppEASpace {
	__le16		headerChecksum;
	uint8_t		freeEASpace[0];
} __packed;

/* UDF Defined System Stream (UDF 2.60 3.3.7) */
#define UDF_ID_UNIQUE_ID		"*UDF Unique ID Mapping Data"
#define UDF_ID_NON_ALLOC		"*UDF Non-Allocatable Space"
#define UDF_ID_POWER_CAL		"*UDF Power Cal Table"
#define UDF_ID_BACKUP			"*UDF Backup"

/* UDF Defined Non-System Streams (UDF 2.60 3.3.8) */
#define UDF_ID_MAC_RESOURCE_FORK_STREAM	"*UDF Macintosh Resource Fork"
/* #define UDF_ID_OS2_EA		"*UDF OS/2 EA" */
#define UDF_ID_NT_ACL			"*UDF NT ACL"
#define UDF_ID_UNIX_ACL			"*UDF UNIX ACL"

/* Operating System Identifiers (UDF 2.60 6.3) */
#define UDF_OS_CLASS_UNDEF		0x00U
#define UDF_OS_CLASS_DOS		0x01U
#define UDF_OS_CLASS_OS2		0x02U
#define UDF_OS_CLASS_MAC		0x03U
#define UDF_OS_CLASS_UNIX		0x04U
#define UDF_OS_CLASS_WIN9X		0x05U
#define UDF_OS_CLASS_WINNT		0x06U
#define UDF_OS_CLASS_OS400		0x07U
#define UDF_OS_CLASS_BEOS		0x08U
#define UDF_OS_CLASS_WINCE		0x09U

#define UDF_OS_ID_UNDEF			0x00U
#define UDF_OS_ID_DOS			0x00U
#define UDF_OS_ID_OS2			0x00U
#define UDF_OS_ID_MAC			0x00U
#define UDF_OS_ID_MAX_OSX		0x01U
#define UDF_OS_ID_UNIX			0x00U
#define UDF_OS_ID_AIX			0x01U
#define UDF_OS_ID_SOLARIS		0x02U
#define UDF_OS_ID_HPUX			0x03U
#define UDF_OS_ID_IRIX			0x04U
#define UDF_OS_ID_LINUX			0x05U
#define UDF_OS_ID_MKLINUX		0x06U
#define UDF_OS_ID_FREEBSD		0x07U
#define UDF_OS_ID_NETBSD		0x08U
#define UDF_OS_ID_WIN9X			0x00U
#define UDF_OS_ID_WINNT			0x00U
#define UDF_OS_ID_OS400			0x00U
#define UDF_OS_ID_BEOS			0x00U
#define UDF_OS_ID_WINCE			0x00U

#endif /* _OSTA_UDF_H */
