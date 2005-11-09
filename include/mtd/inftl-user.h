/*
 * $Id: inftl-user.h,v 1.2 2005/11/07 11:14:56 gleixner Exp $
 *
 * Parts of INFTL headers shared with userspace
 *
 */

#ifndef __MTD_INFTL_USER_H__
#define __MTD_INFTL_USER_H__

#define	OSAK_VERSION	0x5120
#define	PERCENTUSED	98

#define	SECTORSIZE	512

/* Block Control Information */

struct inftl_bci {
	uint8_t ECCsig[6];
	uint8_t Status;
	uint8_t Status1;
} __attribute__((packed));

struct inftl_unithead1 {
	uint16_t virtualUnitNo;
	uint16_t prevUnitNo;
	uint8_t ANAC;
	uint8_t NACs;
	uint8_t parityPerField;
	uint8_t discarded;
} __attribute__((packed));

struct inftl_unithead2 {
	uint8_t parityPerField;
	uint8_t ANAC;
	uint16_t prevUnitNo;
	uint16_t virtualUnitNo;
	uint8_t NACs;
	uint8_t discarded;
} __attribute__((packed));

struct inftl_unittail {
	uint8_t Reserved[4];
	uint16_t EraseMark;
	uint16_t EraseMark1;
} __attribute__((packed));

union inftl_uci {
	struct inftl_unithead1 a;
	struct inftl_unithead2 b;
	struct inftl_unittail c;
};

struct inftl_oob {
	struct inftl_bci b;
	union inftl_uci u;
};


/* INFTL Media Header */

struct INFTLPartition {
	__u32 virtualUnits;
	__u32 firstUnit;
	__u32 lastUnit;
	__u32 flags;
	__u32 spareUnits;
	__u32 Reserved0;
	__u32 Reserved1;
} __attribute__((packed));

struct INFTLMediaHeader {
	char bootRecordID[8];
	__u32 NoOfBootImageBlocks;
	__u32 NoOfBinaryPartitions;
	__u32 NoOfBDTLPartitions;
	__u32 BlockMultiplierBits;
	__u32 FormatFlags;
	__u32 OsakVersion;
	__u32 PercentUsed;
	struct INFTLPartition Partitions[4];
} __attribute__((packed));

/* Partition flag types */
#define	INFTL_BINARY	0x20000000
#define	INFTL_BDTL	0x40000000
#define	INFTL_LAST	0x80000000

#endif /* __MTD_INFTL_USER_H__ */


