/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2019 Samsung Electronics Co., Ltd.
 */

#ifndef __VFS_CACHE_H__
#define __VFS_CACHE_H__

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/workqueue.h>

#include "vfs.h"

/* Windows style file permissions for extended response */
#define	FILE_GENERIC_ALL	0x1F01FF
#define	FILE_GENERIC_READ	0x120089
#define	FILE_GENERIC_WRITE	0x120116
#define	FILE_GENERIC_EXECUTE	0X1200a0

#define KSMBD_START_FID		0
#define KSMBD_NO_FID		(UINT_MAX)
#define SMB2_NO_FID		(0xFFFFFFFFFFFFFFFFULL)

#define FP_FILENAME(fp)		fp->filp->f_path.dentry->d_name.name
#define FP_INODE(fp)		d_inode(fp->filp->f_path.dentry)
#define PARENT_INODE(fp)	d_inode(fp->filp->f_path.dentry->d_parent)

#define ATTR_FP(fp) (fp->attrib_only && \
		(fp->cdoption != FILE_OVERWRITE_IF_LE && \
		fp->cdoption != FILE_OVERWRITE_LE && \
		fp->cdoption != FILE_SUPERSEDE_LE))

struct ksmbd_conn;
struct ksmbd_session;

struct ksmbd_lock {
	struct file_lock *fl;
	struct list_head glist;
	struct list_head llist;
	unsigned int flags;
	int cmd;
	int zero_len;
	unsigned long long start;
	unsigned long long end;
};

struct stream {
	char *name;
	ssize_t size;
};

struct ksmbd_inode {
	rwlock_t			m_lock;
	atomic_t			m_count;
	atomic_t			op_count;
	/* opinfo count for streams */
	atomic_t			sop_count;
	struct inode			*m_inode;
	unsigned int			m_flags;
	struct hlist_node		m_hash;
	struct list_head		m_fp_list;
	struct list_head		m_op_list;
	struct oplock_info		*m_opinfo;
	__le32				m_fattr;
};

struct ksmbd_file {
	struct file			*filp;
	char				*filename;
	unsigned int			persistent_id;
	unsigned int			volatile_id;

	spinlock_t			f_lock;

	struct ksmbd_inode		*f_ci;
	struct ksmbd_inode		*f_parent_ci;
	struct oplock_info __rcu	*f_opinfo;
	struct ksmbd_conn		*conn;
	struct ksmbd_tree_connect	*tcon;

	atomic_t			refcount;
	__le32				daccess;
	__le32				saccess;
	__le32				coption;
	__le32				cdoption;
	__u64				create_time;
	__u64				itime;

	bool				is_nt_open;
	bool				attrib_only;

	char				client_guid[16];
	char				create_guid[16];
	char				app_instance_id[16];

	struct stream			stream;
	struct list_head		node;
	struct list_head		blocked_works;

	int				durable_timeout;

	/* for SMB1 */
	int				pid;

	/* conflict lock fail count for SMB1 */
	unsigned int			cflock_cnt;
	/* last lock failure start offset for SMB1 */
	unsigned long long		llock_fstart;

	int				dirent_offset;

	/* if ls is happening on directory, below is valid*/
	struct ksmbd_readdir_data	readdir_data;
	int				dot_dotdot[2];
};

static inline void set_ctx_actor(struct dir_context *ctx,
				 filldir_t actor)
{
	ctx->actor = actor;
}

#define KSMBD_NR_OPEN_DEFAULT BITS_PER_LONG

struct ksmbd_file_table {
	rwlock_t		lock;
	struct idr		*idr;
};

static inline bool HAS_FILE_ID(unsigned long long req)
{
	unsigned int id = (unsigned int)req;

	return id < KSMBD_NO_FID;
}

static inline bool ksmbd_stream_fd(struct ksmbd_file *fp)
{
	return fp->stream.name != NULL;
}

int ksmbd_init_file_table(struct ksmbd_file_table *ft);
void ksmbd_destroy_file_table(struct ksmbd_file_table *ft);
int ksmbd_close_fd(struct ksmbd_work *work, unsigned int id);
struct ksmbd_file *ksmbd_lookup_fd_fast(struct ksmbd_work *work, unsigned int id);
struct ksmbd_file *ksmbd_lookup_foreign_fd(struct ksmbd_work *work, unsigned int id);
struct ksmbd_file *ksmbd_lookup_fd_slow(struct ksmbd_work *work, unsigned int id,
		unsigned int pid);
void ksmbd_fd_put(struct ksmbd_work *work, struct ksmbd_file *fp);
struct ksmbd_file *ksmbd_lookup_durable_fd(unsigned long long id);
struct ksmbd_file *ksmbd_lookup_fd_cguid(char *cguid);
struct ksmbd_file *ksmbd_lookup_fd_inode(struct inode *inode);
unsigned int ksmbd_open_durable_fd(struct ksmbd_file *fp);
struct ksmbd_file *ksmbd_open_fd(struct ksmbd_work *work, struct file *filp);
void ksmbd_close_tree_conn_fds(struct ksmbd_work *work);
void ksmbd_close_session_fds(struct ksmbd_work *work);
int ksmbd_close_inode_fds(struct ksmbd_work *work, struct inode *inode);
int ksmbd_init_global_file_table(void);
void ksmbd_free_global_file_table(void);
int ksmbd_file_table_flush(struct ksmbd_work *work);
void ksmbd_set_fd_limit(unsigned long limit);

/*
 * INODE hash
 */
int __init ksmbd_inode_hash_init(void);
void ksmbd_release_inode_hash(void);

enum KSMBD_INODE_STATUS {
	KSMBD_INODE_STATUS_OK,
	KSMBD_INODE_STATUS_UNKNOWN,
	KSMBD_INODE_STATUS_PENDING_DELETE,
};

int ksmbd_query_inode_status(struct inode *inode);
bool ksmbd_inode_pending_delete(struct ksmbd_file *fp);
void ksmbd_set_inode_pending_delete(struct ksmbd_file *fp);
void ksmbd_clear_inode_pending_delete(struct ksmbd_file *fp);
void ksmbd_fd_set_delete_on_close(struct ksmbd_file *fp,
				  int file_info);
#endif /* __VFS_CACHE_H__ */
