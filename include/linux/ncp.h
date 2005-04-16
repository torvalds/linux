/*
 *  ncp.h
 *
 *  Copyright (C) 1995 by Volker Lendecke
 *  Modified for sparc by J.F. Chadima
 *  Modified for __constant_ntoh by Frank A. Vorstenbosch
 *
 */

#ifndef _LINUX_NCP_H
#define _LINUX_NCP_H

#include <linux/types.h>

#define NCP_PTYPE                (0x11)
#define NCP_PORT                 (0x0451)

#define NCP_ALLOC_SLOT_REQUEST   (0x1111)
#define NCP_REQUEST              (0x2222)
#define NCP_DEALLOC_SLOT_REQUEST (0x5555)

struct ncp_request_header {
	__u16 type __attribute__((packed));
	__u8 sequence __attribute__((packed));
	__u8 conn_low __attribute__((packed));
	__u8 task __attribute__((packed));
	__u8 conn_high __attribute__((packed));
	__u8 function __attribute__((packed));
	__u8 data[0] __attribute__((packed));
};

#define NCP_REPLY                (0x3333)
#define NCP_WATCHDOG		 (0x3E3E)
#define NCP_POSITIVE_ACK         (0x9999)

struct ncp_reply_header {
	__u16 type __attribute__((packed));
	__u8 sequence __attribute__((packed));
	__u8 conn_low __attribute__((packed));
	__u8 task __attribute__((packed));
	__u8 conn_high __attribute__((packed));
	__u8 completion_code __attribute__((packed));
	__u8 connection_state __attribute__((packed));
	__u8 data[0] __attribute__((packed));
};

#define NCP_VOLNAME_LEN (16)
#define NCP_NUMBER_OF_VOLUMES (256)
struct ncp_volume_info {
	__u32 total_blocks;
	__u32 free_blocks;
	__u32 purgeable_blocks;
	__u32 not_yet_purgeable_blocks;
	__u32 total_dir_entries;
	__u32 available_dir_entries;
	__u8 sectors_per_block;
	char volume_name[NCP_VOLNAME_LEN + 1];
};

#define AR_READ      (cpu_to_le16(1))
#define AR_WRITE     (cpu_to_le16(2))
#define AR_EXCLUSIVE (cpu_to_le16(0x20))

#define NCP_FILE_ID_LEN 6

/* Defines for Name Spaces */
#define NW_NS_DOS     0
#define NW_NS_MAC     1
#define NW_NS_NFS     2
#define NW_NS_FTAM    3
#define NW_NS_OS2     4

/*  Defines for ReturnInformationMask */
#define RIM_NAME	      (cpu_to_le32(1))
#define RIM_SPACE_ALLOCATED   (cpu_to_le32(2))
#define RIM_ATTRIBUTES	      (cpu_to_le32(4))
#define RIM_DATA_SIZE	      (cpu_to_le32(8))
#define RIM_TOTAL_SIZE	      (cpu_to_le32(0x10))
#define RIM_EXT_ATTR_INFO     (cpu_to_le32(0x20))
#define RIM_ARCHIVE	      (cpu_to_le32(0x40))
#define RIM_MODIFY	      (cpu_to_le32(0x80))
#define RIM_CREATION	      (cpu_to_le32(0x100))
#define RIM_OWNING_NAMESPACE  (cpu_to_le32(0x200))
#define RIM_DIRECTORY	      (cpu_to_le32(0x400))
#define RIM_RIGHTS	      (cpu_to_le32(0x800))
#define RIM_ALL 	      (cpu_to_le32(0xFFF))
#define RIM_COMPRESSED_INFO   (cpu_to_le32(0x80000000))

/* Defines for NSInfoBitMask */
#define NSIBM_NFS_NAME		0x0001
#define NSIBM_NFS_MODE		0x0002
#define NSIBM_NFS_GID		0x0004
#define NSIBM_NFS_NLINKS	0x0008
#define NSIBM_NFS_RDEV		0x0010
#define NSIBM_NFS_LINK		0x0020
#define NSIBM_NFS_CREATED	0x0040
#define NSIBM_NFS_UID		0x0080
#define NSIBM_NFS_ACSFLAG	0x0100
#define NSIBM_NFS_MYFLAG	0x0200

/* open/create modes */
#define OC_MODE_OPEN	  0x01
#define OC_MODE_TRUNCATE  0x02
#define OC_MODE_REPLACE   0x02
#define OC_MODE_CREATE	  0x08

/* open/create results */
#define OC_ACTION_NONE	   0x00
#define OC_ACTION_OPEN	   0x01
#define OC_ACTION_CREATE   0x02
#define OC_ACTION_TRUNCATE 0x04
#define OC_ACTION_REPLACE  0x04

/* access rights attributes */
#ifndef AR_READ_ONLY
#define AR_READ_ONLY	   0x0001
#define AR_WRITE_ONLY	   0x0002
#define AR_DENY_READ	   0x0004
#define AR_DENY_WRITE	   0x0008
#define AR_COMPATIBILITY   0x0010
#define AR_WRITE_THROUGH   0x0040
#define AR_OPEN_COMPRESSED 0x0100
#endif

struct nw_nfs_info {
	__u32 mode;
	__u32 rdev;
};

struct nw_info_struct {
	__u32 spaceAlloc __attribute__((packed));
	__le32 attributes __attribute__((packed));
	__u16 flags __attribute__((packed));
	__le32 dataStreamSize __attribute__((packed));
	__le32 totalStreamSize __attribute__((packed));
	__u16 numberOfStreams __attribute__((packed));
	__le16 creationTime __attribute__((packed));
	__le16 creationDate __attribute__((packed));
	__u32 creatorID __attribute__((packed));
	__le16 modifyTime __attribute__((packed));
	__le16 modifyDate __attribute__((packed));
	__u32 modifierID __attribute__((packed));
	__le16 lastAccessDate __attribute__((packed));
	__u16 archiveTime __attribute__((packed));
	__u16 archiveDate __attribute__((packed));
	__u32 archiverID __attribute__((packed));
	__u16 inheritedRightsMask __attribute__((packed));
	__le32 dirEntNum __attribute__((packed));
	__le32 DosDirNum __attribute__((packed));
	__u32 volNumber __attribute__((packed));
	__u32 EADataSize __attribute__((packed));
	__u32 EAKeyCount __attribute__((packed));
	__u32 EAKeySize __attribute__((packed));
	__u32 NSCreator __attribute__((packed));
	__u8 nameLen __attribute__((packed));
	__u8 entryName[256] __attribute__((packed));
	/* libncp may depend on there being nothing after entryName */
#ifdef __KERNEL__
	struct nw_nfs_info nfs;
#endif
};

/* modify mask - use with MODIFY_DOS_INFO structure */
#define DM_ATTRIBUTES		  (cpu_to_le32(0x02))
#define DM_CREATE_DATE		  (cpu_to_le32(0x04))
#define DM_CREATE_TIME		  (cpu_to_le32(0x08))
#define DM_CREATOR_ID		  (cpu_to_le32(0x10))
#define DM_ARCHIVE_DATE 	  (cpu_to_le32(0x20))
#define DM_ARCHIVE_TIME 	  (cpu_to_le32(0x40))
#define DM_ARCHIVER_ID		  (cpu_to_le32(0x80))
#define DM_MODIFY_DATE		  (cpu_to_le32(0x0100))
#define DM_MODIFY_TIME		  (cpu_to_le32(0x0200))
#define DM_MODIFIER_ID		  (cpu_to_le32(0x0400))
#define DM_LAST_ACCESS_DATE	  (cpu_to_le32(0x0800))
#define DM_INHERITED_RIGHTS_MASK  (cpu_to_le32(0x1000))
#define DM_MAXIMUM_SPACE	  (cpu_to_le32(0x2000))

struct nw_modify_dos_info {
	__le32 attributes __attribute__((packed));
	__le16 creationDate __attribute__((packed));
	__le16 creationTime __attribute__((packed));
	__u32 creatorID __attribute__((packed));
	__le16 modifyDate __attribute__((packed));
	__le16 modifyTime __attribute__((packed));
	__u32 modifierID __attribute__((packed));
	__u16 archiveDate __attribute__((packed));
	__u16 archiveTime __attribute__((packed));
	__u32 archiverID __attribute__((packed));
	__le16 lastAccessDate __attribute__((packed));
	__u16 inheritanceGrantMask __attribute__((packed));
	__u16 inheritanceRevokeMask __attribute__((packed));
	__u32 maximumSpace __attribute__((packed));
};

struct nw_search_sequence {
	__u8 volNumber __attribute__((packed));
	__u32 dirBase __attribute__((packed));
	__u32 sequence __attribute__((packed));
};

#endif				/* _LINUX_NCP_H */
