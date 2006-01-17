/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */ 
#ifndef _H_JFS_INCORE
#define _H_JFS_INCORE

#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include "jfs_types.h"
#include "jfs_xtree.h"
#include "jfs_dtree.h"

/*
 * JFS magic number
 */
#define JFS_SUPER_MAGIC 0x3153464a /* "JFS1" */

/*
 * JFS-private inode information
 */
struct jfs_inode_info {
	int	fileset;	/* fileset number (always 16)*/
	uint	mode2;		/* jfs-specific mode		*/
	pxd_t   ixpxd;		/* inode extent descriptor	*/
	dxd_t	acl;		/* dxd describing acl	*/
	dxd_t	ea;		/* dxd describing ea	*/
	time_t	otime;		/* time created	*/
	uint	next_index;	/* next available directory entry index */
	int	acltype;	/* Type of ACL	*/
	short	btorder;	/* access order	*/
	short	btindex;	/* btpage entry index*/
	struct inode *ipimap;	/* inode map			*/
	long	cflag;		/* commit flags		*/
	u16	bxflag;		/* xflag of pseudo buffer?	*/
	unchar	agno;		/* ag number			*/
	signed char active_ag;	/* ag currently allocating from	*/
	lid_t	blid;		/* lid of pseudo buffer?	*/
	lid_t	atlhead;	/* anonymous tlock list head	*/
	lid_t	atltail;	/* anonymous tlock list tail	*/
	spinlock_t ag_lock;	/* protects active_ag		*/
	struct list_head anon_inode_list; /* inodes having anonymous txns */
	/*
	 * rdwrlock serializes xtree between reads & writes and synchronizes
	 * changes to special inodes.  It's use would be redundant on
	 * directories since the i_mutex taken in the VFS is sufficient.
	 */
	struct rw_semaphore rdwrlock;
	/*
	 * commit_sem serializes transaction processing on an inode.
	 * It must be taken after beginning a transaction (txBegin), since
	 * dirty inodes may be committed while a new transaction on the
	 * inode is blocked in txBegin or TxBeginAnon
	 */
	struct semaphore commit_sem;
	/* xattr_sem allows us to access the xattrs without taking i_mutex */
	struct rw_semaphore xattr_sem;
	lid_t	xtlid;		/* lid of xtree lock on directory */
#ifdef CONFIG_JFS_POSIX_ACL
	struct posix_acl *i_acl;
	struct posix_acl *i_default_acl;
#endif
	union {
		struct {
			xtpage_t _xtroot;	/* 288: xtree root */
			struct inomap *_imap;	/* 4: inode map header	*/
		} file;
		struct {
			struct dir_table_slot _table[12]; /* 96: dir index */
			dtroot_t _dtroot;	/* 288: dtree root */
		} dir;
		struct {
			unchar _unused[16];	/* 16: */
			dxd_t _dxd;		/* 16: */
			unchar _inline[128];	/* 128: inline symlink */
			/* _inline_ea may overlay the last part of
			 * file._xtroot if maxentry = XTROOTINITSLOT
			 */
			unchar _inline_ea[128];	/* 128: inline extended attr */
		} link;
	} u;
	u32 dev;	/* will die when we get wide dev_t */
	struct inode	vfs_inode;
};
#define i_xtroot u.file._xtroot
#define i_imap u.file._imap
#define i_dirtable u.dir._table
#define i_dtroot u.dir._dtroot
#define i_inline u.link._inline
#define i_inline_ea u.link._inline_ea

#define JFS_ACL_NOT_CACHED ((void *)-1)

#define IREAD_LOCK(ip)		down_read(&JFS_IP(ip)->rdwrlock)
#define IREAD_UNLOCK(ip)	up_read(&JFS_IP(ip)->rdwrlock)
#define IWRITE_LOCK(ip)		down_write(&JFS_IP(ip)->rdwrlock)
#define IWRITE_UNLOCK(ip)	up_write(&JFS_IP(ip)->rdwrlock)

/*
 * cflag
 */
enum cflags {
	COMMIT_Nolink,		/* inode committed with zero link count */
	COMMIT_Inlineea,	/* commit inode inline EA */
	COMMIT_Freewmap,	/* free WMAP at iClose() */
	COMMIT_Dirty,		/* Inode is really dirty */
	COMMIT_Dirtable,	/* commit changes to di_dirtable */
	COMMIT_Stale,		/* data extent is no longer valid */
	COMMIT_Synclist,	/* metadata pages on group commit synclist */
};

#define set_cflag(flag, ip)	set_bit(flag, &(JFS_IP(ip)->cflag))
#define clear_cflag(flag, ip)	clear_bit(flag, &(JFS_IP(ip)->cflag))
#define test_cflag(flag, ip)	test_bit(flag, &(JFS_IP(ip)->cflag))
#define test_and_clear_cflag(flag, ip) \
	test_and_clear_bit(flag, &(JFS_IP(ip)->cflag))
/*
 * JFS-private superblock information.
 */
struct jfs_sb_info {
	struct super_block *sb;		/* Point back to vfs super block */
	unsigned long	mntflag;	/* aggregate attributes	*/
	struct inode	*ipbmap;	/* block map inode		*/
	struct inode	*ipaimap;	/* aggregate inode map inode	*/
	struct inode	*ipaimap2;	/* secondary aimap inode	*/
	struct inode	*ipimap;	/* aggregate inode map inode	*/
	struct jfs_log	*log;		/* log			*/
	struct list_head log_list;	/* volumes associated with a journal */
	short		bsize;		/* logical block size	*/
	short		l2bsize;	/* log2 logical block size	*/
	short		nbperpage;	/* blocks per page		*/
	short		l2nbperpage;	/* log2 blocks per page	*/
	short		l2niperblk;	/* log2 inodes per page	*/
	dev_t		logdev;		/* external log device	*/
	uint		aggregate;	/* volume identifier in log record */
	pxd_t		logpxd;		/* pxd describing log	*/
	pxd_t		fsckpxd;	/* pxd describing fsck wkspc */
	pxd_t		ait2;		/* pxd describing AIT copy	*/
	char		uuid[16];	/* 128-bit uuid for volume	*/
	char		loguuid[16];	/* 128-bit uuid for log	*/
	/*
	 * commit_state is used for synchronization of the jfs_commit
	 * threads.  It is protected by LAZY_LOCK().
	 */
	int		commit_state;	/* commit state */
	/* Formerly in ipimap */
	uint		gengen;		/* inode generation generator*/
	uint		inostamp;	/* shows inode belongs to fileset*/

        /* Formerly in ipbmap */
	struct bmap	*bmap;		/* incore bmap descriptor	*/
	struct nls_table *nls_tab;	/* current codepage		*/
	struct inode *direct_inode;	/* metadata inode */
	uint		state;		/* mount/recovery state	*/
	unsigned long	flag;		/* mount time flags */
	uint		p_state;	/* state prior to going no integrity */
};

/* jfs_sb_info commit_state */
#define IN_LAZYCOMMIT 1

static inline struct jfs_inode_info *JFS_IP(struct inode *inode)
{
	return list_entry(inode, struct jfs_inode_info, vfs_inode);
}

static inline int jfs_dirtable_inline(struct inode *inode)
{
	return (JFS_IP(inode)->next_index <= (MAX_INLINE_DIRTABLE_ENTRY + 1));
}

static inline struct jfs_sb_info *JFS_SBI(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline int isReadOnly(struct inode *inode)
{
	if (JFS_SBI(inode->i_sb)->log)
		return 0;
	return 1;
}
#endif /* _H_JFS_INCORE */
