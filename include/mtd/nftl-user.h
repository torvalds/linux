/*
 * Parts of NFTL headers shared with userspace
 *
 */

#ifndef __MTD_NFTL_USER_H__
#define __MTD_NFTL_USER_H__

/* Block Control Information */

struct nftl_bci {
	unsigned char ECCSig[6];
	uint8_t Status;
	uint8_t Status1;
}__attribute__((packed));

/* Unit Control Information */

struct nftl_uci0 {
	uint16_t VirtUnitNum;
	uint16_t ReplUnitNum;
	uint16_t SpareVirtUnitNum;
	uint16_t SpareReplUnitNum;
} __attribute__((packed));

struct nftl_uci1 {
	uint32_t WearInfo;
	uint16_t EraseMark;
	uint16_t EraseMark1;
} __attribute__((packed));

struct nftl_uci2 {
        uint16_t FoldMark;
        uint16_t FoldMark1;
	uint32_t unused;
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
	uint16_t NumEraseUnits;
	uint16_t FirstPhysicalEUN;
	uint32_t FormattedSize;
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
