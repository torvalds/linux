/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef _LINUX_FSERROR_H__
#define _LINUX_FSERROR_H__

void fserror_mount(struct super_block *sb);
void fserror_unmount(struct super_block *sb);

enum fserror_type {
	/* pagecache I/O failed */
	FSERR_BUFFERED_READ,
	FSERR_BUFFERED_WRITE,

	/* direct I/O failed */
	FSERR_DIRECTIO_READ,
	FSERR_DIRECTIO_WRITE,

	/* out of band media error reported */
	FSERR_DATA_LOST,

	/* filesystem metadata */
	FSERR_METADATA,
};

struct fserror_event {
	struct work_struct work;
	struct super_block *sb;
	struct inode *inode;
	loff_t pos;
	u64 len;
	enum fserror_type type;

	/* negative error number */
	int error;
};

void fserror_report(struct super_block *sb, struct inode *inode,
		    enum fserror_type type, loff_t pos, u64 len, int error,
		    gfp_t gfp);

static inline void fserror_report_io(struct inode *inode,
				     enum fserror_type type, loff_t pos,
				     u64 len, int error, gfp_t gfp)
{
	fserror_report(inode->i_sb, inode, type, pos, len, error, gfp);
}

static inline void fserror_report_data_lost(struct inode *inode, loff_t pos,
					    u64 len, gfp_t gfp)
{
	fserror_report(inode->i_sb, inode, FSERR_DATA_LOST, pos, len, -EIO,
		       gfp);
}

static inline void fserror_report_file_metadata(struct inode *inode, int error,
						gfp_t gfp)
{
	fserror_report(inode->i_sb, inode, FSERR_METADATA, 0, 0, error, gfp);
}

static inline void fserror_report_metadata(struct super_block *sb, int error,
					   gfp_t gfp)
{
	fserror_report(sb, NULL, FSERR_METADATA, 0, 0, error, gfp);
}

static inline void fserror_report_shutdown(struct super_block *sb, gfp_t gfp)
{
	fserror_report(sb, NULL, FSERR_METADATA, 0, 0, -ESHUTDOWN, gfp);
}

#endif /* _LINUX_FSERROR_H__ */
