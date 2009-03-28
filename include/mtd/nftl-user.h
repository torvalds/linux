/*
 * Parts of NFTL headers shared with userspace
 *
 */

#ifndef __MTD_NFTL_USER_H__
#define __MTD_NFTL_USER_H__

#include <linux/types.h>

/* Block Control Information */

struct nftl_bci {
	unsigned char ECCSig[6];
	__u8 Status;
	__u8 Status1;
}__attribute__((packed));

/* Unit Control Information */

struct nftl_uci0 {
	__u16 VirtUnitNum;
	__u16 ReplUnitNum;
	__u16 SpareVirtUnitNum;
	__u16 SpareReplUnitNum;
} __attribute__((packed));

struct nftl_uci1 {
	__u32 WearInfo;
	__u16 EraseMark;
	__u16 EraseMark1;
} __attribute__((packed));

struct nftl_uci2 {
        __u16 FoldMark;
        __u16 FoldMark1;
	__u32 unused;
} __attribute__((packed));

union nftl_uci {
	struct nftl_uci0 a;
	struct nftl_uci1 b;
	struct nftl_uci2 c;
};

struct nftl_oob {
	struct nftl_bci b;
	union nftl_uci u;
};

/* NFTL Media Header */

struct NFTLMediaHeader {
	char DataOrgID[6];
	__u16 NumEraseUnits;
	__u16 FirstPhysicalEUN;
	__u32 FormattedSize;
	unsigned char UnitSizeFactor;
} __attribute__((packed));

#define MAX_ERASE_ZONES (8192 - 512)

#define ERASE_MARK 0x3c69
#define SECTOR_FREE 0xff
#define SECTOR_USED 0x55
#define SECTOR_IGNORE 0x11
#define SECTOR_DELETED 0x00

#define FOLD_MARK_IN_PROGRESS 0x5555

#define ZONE_GOOD 0xff
#define ZONE_BAD_ORIGINAL 0
#define ZONE_BAD_MARKED 7


#endif /* __MTD_NFTL_USER_H__ */
