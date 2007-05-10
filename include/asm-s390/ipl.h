/*
 * s390 (re)ipl support
 *
 * Copyright IBM Corp. 2007
 */

#ifndef _ASM_S390_IPL_H
#define _ASM_S390_IPL_H

#include <asm/types.h>
#include <asm/cio.h>
#include <asm/setup.h>

#define IPL_PARMBLOCK_ORIGIN	0x2000

#define IPL_PARM_BLK_FCP_LEN (sizeof(struct ipl_list_hdr) + \
			      sizeof(struct ipl_block_fcp))

#define IPL_PARM_BLK0_FCP_LEN (sizeof(struct ipl_block_fcp) + 8)

#define IPL_PARM_BLK_CCW_LEN (sizeof(struct ipl_list_hdr) + \
			      sizeof(struct ipl_block_ccw))

#define IPL_PARM_BLK0_CCW_LEN (sizeof(struct ipl_block_ccw) + 8)

#define IPL_MAX_SUPPORTED_VERSION (0)

#define IPL_PARMBLOCK_START	((struct ipl_parameter_block *) \
				 IPL_PARMBLOCK_ORIGIN)
#define IPL_PARMBLOCK_SIZE	(IPL_PARMBLOCK_START->hdr.len)

struct ipl_list_hdr {
	u32 len;
	u8  reserved1[3];
	u8  version;
	u32 blk0_len;
	u8  pbt;
	u8  flags;
	u16 reserved2;
} __attribute__((packed));

struct ipl_block_fcp {
	u8  reserved1[313-1];
	u8  opt;
	u8  reserved2[3];
	u16 reserved3;
	u16 devno;
	u8  reserved4[4];
	u64 wwpn;
	u64 lun;
	u32 bootprog;
	u8  reserved5[12];
	u64 br_lba;
	u32 scp_data_len;
	u8  reserved6[260];
	u8  scp_data[];
} __attribute__((packed));

struct ipl_block_ccw {
	u8  load_param[8];
	u8  reserved1[84];
	u8  reserved2[2];
	u16 devno;
	u8  vm_flags;
	u8  reserved3[3];
	u32 vm_parm_len;
	u8  reserved4[80];
} __attribute__((packed));

struct ipl_parameter_block {
	struct ipl_list_hdr hdr;
	union {
		struct ipl_block_fcp fcp;
		struct ipl_block_ccw ccw;
	} ipl_info;
} __attribute__((packed));

/*
 * IPL validity flags
 */
extern u32 ipl_flags;
extern u32 dump_prefix_page;
extern unsigned int zfcpdump_prefix_array[];

extern void do_reipl(void);
extern void ipl_save_parameters(void);

enum {
	IPL_DEVNO_VALID		= 1,
	IPL_PARMBLOCK_VALID	= 2,
	IPL_NSS_VALID		= 4,
};

enum ipl_type {
	IPL_TYPE_UNKNOWN	= 1,
	IPL_TYPE_CCW		= 2,
	IPL_TYPE_FCP		= 4,
	IPL_TYPE_FCP_DUMP	= 8,
	IPL_TYPE_NSS		= 16,
};

struct ipl_info
{
	enum ipl_type type;
	union {
		struct {
			struct ccw_dev_id dev_id;
		} ccw;
		struct {
			struct ccw_dev_id dev_id;
			u64 wwpn;
			u64 lun;
		} fcp;
		struct {
			char name[NSS_NAME_SIZE + 1];
		} nss;
	} data;
};

extern struct ipl_info ipl_info;
extern void setup_ipl_info(void);

/*
 * DIAG 308 support
 */
enum diag308_subcode  {
	DIAG308_REL_HSA	= 2,
	DIAG308_IPL	= 3,
	DIAG308_DUMP	= 4,
	DIAG308_SET	= 5,
	DIAG308_STORE	= 6,
};

enum diag308_ipl_type {
	DIAG308_IPL_TYPE_FCP	= 0,
	DIAG308_IPL_TYPE_CCW	= 2,
};

enum diag308_opt {
	DIAG308_IPL_OPT_IPL	= 0x10,
	DIAG308_IPL_OPT_DUMP	= 0x20,
};

enum diag308_rc {
	DIAG308_RC_OK	= 1,
};

extern int diag308(unsigned long subcode, void *addr);

#endif /* _ASM_S390_IPL_H */
