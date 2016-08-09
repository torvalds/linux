/*
 * CXL Flash Device Driver
 *
 * Written by: Manoj N. Kumar <manoj@linux.vnet.ibm.com>, IBM Corporation
 *             Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2015 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _CXLFLASH_IOCTL_H
#define _CXLFLASH_IOCTL_H

#include <linux/types.h>

/*
 * Structure and flag definitions CXL Flash superpipe ioctls
 */

#define DK_CXLFLASH_VERSION_0	0

struct dk_cxlflash_hdr {
	__u16 version;			/* Version data */
	__u16 rsvd[3];			/* Reserved for future use */
	__u64 flags;			/* Input flags */
	__u64 return_flags;		/* Returned flags */
};

/*
 * Return flag definitions available to all ioctls
 *
 * Similar to the input flags, these are grown from the bottom-up with the
 * intention that ioctl-specific return flag definitions would grow from the
 * top-down, allowing the two sets to co-exist. While not required/enforced
 * at this time, this provides future flexibility.
 */
#define DK_CXLFLASH_ALL_PORTS_ACTIVE	0x0000000000000001ULL
#define DK_CXLFLASH_APP_CLOSE_ADAP_FD	0x0000000000000002ULL

/*
 * General Notes:
 * -------------
 * The 'context_id' field of all ioctl structures contains the context
 * identifier for a context in the lower 32-bits (upper 32-bits are not
 * to be used when identifying a context to the AFU). That said, the value
 * in its entirety (all 64-bits) is to be treated as an opaque cookie and
 * should be presented as such when issuing ioctls.
 */

/*
 * DK_CXLFLASH_ATTACH Notes:
 * ------------------------
 * Read/write access permissions are specified via the O_RDONLY, O_WRONLY,
 * and O_RDWR flags defined in the fcntl.h header file.
 *
 * A valid adapter file descriptor (fd >= 0) is only returned on the initial
 * attach (successful) of a context. When a context is shared(reused), the user
 * is expected to already 'know' the adapter file descriptor associated with the
 * context.
 */
#define DK_CXLFLASH_ATTACH_REUSE_CONTEXT	0x8000000000000000ULL

struct dk_cxlflash_attach {
	struct dk_cxlflash_hdr hdr;	/* Common fields */
	__u64 num_interrupts;		/* Requested number of interrupts */
	__u64 context_id;		/* Returned context */
	__u64 mmio_size;		/* Returned size of MMIO area */
	__u64 block_size;		/* Returned block size, in bytes */
	__u64 adap_fd;			/* Returned adapter file descriptor */
	__u64 last_lba;			/* Returned last LBA on the device */
	__u64 max_xfer;			/* Returned max transfer size, blocks */
	__u64 reserved[8];		/* Reserved for future use */
};

struct dk_cxlflash_detach {
	struct dk_cxlflash_hdr hdr;	/* Common fields */
	__u64 context_id;		/* Context to detach */
	__u64 reserved[8];		/* Reserved for future use */
};

struct dk_cxlflash_udirect {
	struct dk_cxlflash_hdr hdr;	/* Common fields */
	__u64 context_id;		/* Context to own physical resources */
	__u64 rsrc_handle;		/* Returned resource handle */
	__u64 last_lba;			/* Returned last LBA on the device */
	__u64 reserved[8];		/* Reserved for future use */
};

#define DK_CXLFLASH_UVIRTUAL_NEED_WRITE_SAME	0x8000000000000000ULL

struct dk_cxlflash_uvirtual {
	struct dk_cxlflash_hdr hdr;	/* Common fields */
	__u64 context_id;		/* Context to own virtual resources */
	__u64 lun_size;			/* Requested size, in 4K blocks */
	__u64 rsrc_handle;		/* Returned resource handle */
	__u64 last_lba;			/* Returned last LBA of LUN */
	__u64 reserved[8];		/* Reserved for future use */
};

struct dk_cxlflash_release {
	struct dk_cxlflash_hdr hdr;	/* Common fields */
	__u64 context_id;		/* Context owning resources */
	__u64 rsrc_handle;		/* Resource handle to release */
	__u64 reserved[8];		/* Reserved for future use */
};

struct dk_cxlflash_resize {
	struct dk_cxlflash_hdr hdr;	/* Common fields */
	__u64 context_id;		/* Context owning resources */
	__u64 rsrc_handle;		/* Resource handle of LUN to resize */
	__u64 req_size;			/* New requested size, in 4K blocks */
	__u64 last_lba;			/* Returned last LBA of LUN */
	__u64 reserved[8];		/* Reserved for future use */
};

struct dk_cxlflash_clone {
	struct dk_cxlflash_hdr hdr;	/* Common fields */
	__u64 context_id_src;		/* Context to clone from */
	__u64 context_id_dst;		/* Context to clone to */
	__u64 adap_fd_src;		/* Source context adapter fd */
	__u64 reserved[8];		/* Reserved for future use */
};

#define DK_CXLFLASH_VERIFY_SENSE_LEN	18
#define DK_CXLFLASH_VERIFY_HINT_SENSE	0x8000000000000000ULL

struct dk_cxlflash_verify {
	struct dk_cxlflash_hdr hdr;	/* Common fields */
	__u64 context_id;		/* Context owning resources to verify */
	__u64 rsrc_handle;		/* Resource handle of LUN */
	__u64 hint;			/* Reasons for verify */
	__u64 last_lba;			/* Returned last LBA of device */
	__u8 sense_data[DK_CXLFLASH_VERIFY_SENSE_LEN]; /* SCSI sense data */
	__u8 pad[6];			/* Pad to next 8-byte boundary */
	__u64 reserved[8];		/* Reserved for future use */
};

#define DK_CXLFLASH_RECOVER_AFU_CONTEXT_RESET	0x8000000000000000ULL

struct dk_cxlflash_recover_afu {
	struct dk_cxlflash_hdr hdr;	/* Common fields */
	__u64 reason;			/* Reason for recovery request */
	__u64 context_id;		/* Context to recover / updated ID */
	__u64 mmio_size;		/* Returned size of MMIO area */
	__u64 adap_fd;			/* Returned adapter file descriptor */
	__u64 reserved[8];		/* Reserved for future use */
};

#define DK_CXLFLASH_MANAGE_LUN_WWID_LEN			16
#define DK_CXLFLASH_MANAGE_LUN_ENABLE_SUPERPIPE		0x8000000000000000ULL
#define DK_CXLFLASH_MANAGE_LUN_DISABLE_SUPERPIPE	0x4000000000000000ULL
#define DK_CXLFLASH_MANAGE_LUN_ALL_PORTS_ACCESSIBLE	0x2000000000000000ULL

struct dk_cxlflash_manage_lun {
	struct dk_cxlflash_hdr hdr;			/* Common fields */
	__u8 wwid[DK_CXLFLASH_MANAGE_LUN_WWID_LEN];	/* Page83 WWID, NAA-6 */
	__u64 reserved[8];				/* Rsvd, future use */
};

union cxlflash_ioctls {
	struct dk_cxlflash_attach attach;
	struct dk_cxlflash_detach detach;
	struct dk_cxlflash_udirect udirect;
	struct dk_cxlflash_uvirtual uvirtual;
	struct dk_cxlflash_release release;
	struct dk_cxlflash_resize resize;
	struct dk_cxlflash_clone clone;
	struct dk_cxlflash_verify verify;
	struct dk_cxlflash_recover_afu recover_afu;
	struct dk_cxlflash_manage_lun manage_lun;
};

#define MAX_CXLFLASH_IOCTL_SZ	(sizeof(union cxlflash_ioctls))

#define CXL_MAGIC 0xCA
#define CXL_IOWR(_n, _s)	_IOWR(CXL_MAGIC, _n, struct _s)

#define DK_CXLFLASH_ATTACH		CXL_IOWR(0x80, dk_cxlflash_attach)
#define DK_CXLFLASH_USER_DIRECT		CXL_IOWR(0x81, dk_cxlflash_udirect)
#define DK_CXLFLASH_RELEASE		CXL_IOWR(0x82, dk_cxlflash_release)
#define DK_CXLFLASH_DETACH		CXL_IOWR(0x83, dk_cxlflash_detach)
#define DK_CXLFLASH_VERIFY		CXL_IOWR(0x84, dk_cxlflash_verify)
#define DK_CXLFLASH_RECOVER_AFU		CXL_IOWR(0x85, dk_cxlflash_recover_afu)
#define DK_CXLFLASH_MANAGE_LUN		CXL_IOWR(0x86, dk_cxlflash_manage_lun)
#define DK_CXLFLASH_USER_VIRTUAL	CXL_IOWR(0x87, dk_cxlflash_uvirtual)
#define DK_CXLFLASH_VLUN_RESIZE		CXL_IOWR(0x88, dk_cxlflash_resize)
#define DK_CXLFLASH_VLUN_CLONE		CXL_IOWR(0x89, dk_cxlflash_clone)

#endif /* ifndef _CXLFLASH_IOCTL_H */
