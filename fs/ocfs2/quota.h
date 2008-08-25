/*
 * quota.h for OCFS2
 *
 * On disk quota structures for local and global quota file, in-memory
 * structures.
 *
 */

#ifndef _OCFS2_QUOTA_H
#define _OCFS2_QUOTA_H

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/quota.h>
#include <linux/list.h>
#include <linux/dqblk_qtree.h>

#include "ocfs2.h"

/* Common stuff */
/* id number of quota format */
#define QFMT_OCFS2 3

/*
 * In-memory structures
 */
struct ocfs2_dquot {
	struct dquot dq_dquot;	/* Generic VFS dquot */
	loff_t dq_local_off;	/* Offset in the local quota file */
	struct ocfs2_quota_chunk *dq_chunk;	/* Chunk dquot is in */
	unsigned int dq_use_count;	/* Number of nodes having reference to this entry in global quota file */
	s64 dq_origspace;	/* Last globally synced space usage */
	s64 dq_originodes;	/* Last globally synced inode usage */
};

/* In-memory structure with quota header information */
struct ocfs2_mem_dqinfo {
	unsigned int dqi_type;		/* Quota type this structure describes */
	unsigned int dqi_chunks;	/* Number of chunks in local quota file */
	unsigned int dqi_blocks;	/* Number of blocks allocated for local quota file */
	unsigned int dqi_syncms;	/* How often should we sync with other nodes */
	struct list_head dqi_chunk;	/* List of chunks */
	struct inode *dqi_gqinode;	/* Global quota file inode */
	struct ocfs2_lock_res dqi_gqlock;	/* Lock protecting quota information structure */
	struct buffer_head *dqi_gqi_bh;	/* Buffer head with global quota file inode - set only if inode lock is obtained */
	int dqi_gqi_count;		/* Number of holders of dqi_gqi_bh */
	struct buffer_head *dqi_lqi_bh;	/* Buffer head with local quota file inode */
	struct buffer_head *dqi_ibh;	/* Buffer with information header */
	struct qtree_mem_dqinfo dqi_gi;	/* Info about global file */
};

static inline struct ocfs2_dquot *OCFS2_DQUOT(struct dquot *dquot)
{
	return container_of(dquot, struct ocfs2_dquot, dq_dquot);
}

struct ocfs2_quota_chunk {
	struct list_head qc_chunk;	/* List of quotafile chunks */
	int qc_num;			/* Number of quota chunk */
	struct buffer_head *qc_headerbh;	/* Buffer head with chunk header */
};

extern struct kmem_cache *ocfs2_dquot_cachep;
extern struct kmem_cache *ocfs2_qf_chunk_cachep;

extern struct qtree_fmt_operations ocfs2_global_ops;

ssize_t ocfs2_quota_read(struct super_block *sb, int type, char *data,
			 size_t len, loff_t off);
ssize_t ocfs2_quota_write(struct super_block *sb, int type,
			  const char *data, size_t len, loff_t off);
int ocfs2_global_read_info(struct super_block *sb, int type);
int ocfs2_global_write_info(struct super_block *sb, int type);
int ocfs2_global_read_dquot(struct dquot *dquot);
int __ocfs2_sync_dquot(struct dquot *dquot, int freeing);
static inline int ocfs2_sync_dquot(struct dquot *dquot)
{
	return __ocfs2_sync_dquot(dquot, 0);
}
static inline int ocfs2_global_release_dquot(struct dquot *dquot)
{
	return __ocfs2_sync_dquot(dquot, 1);
}

int ocfs2_lock_global_qf(struct ocfs2_mem_dqinfo *oinfo, int ex);
void ocfs2_unlock_global_qf(struct ocfs2_mem_dqinfo *oinfo, int ex);
struct buffer_head *ocfs2_read_quota_block(struct inode *inode,
					   int block, int *err);

extern struct dquot_operations ocfs2_quota_operations;
extern struct quota_format_type ocfs2_quota_format;

#endif /* _OCFS2_QUOTA_H */
