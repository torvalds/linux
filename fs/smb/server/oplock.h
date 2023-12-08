/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __KSMBD_OPLOCK_H
#define __KSMBD_OPLOCK_H

#include "smb_common.h"

#define OPLOCK_WAIT_TIME	(35 * HZ)

/* SMB2 Oplock levels */
#define SMB2_OPLOCK_LEVEL_NONE          0x00
#define SMB2_OPLOCK_LEVEL_II            0x01
#define SMB2_OPLOCK_LEVEL_EXCLUSIVE     0x08
#define SMB2_OPLOCK_LEVEL_BATCH         0x09
#define SMB2_OPLOCK_LEVEL_LEASE         0xFF

/* Oplock states */
#define OPLOCK_STATE_NONE	0x00
#define OPLOCK_ACK_WAIT		0x01
#define OPLOCK_CLOSING		0x02

#define OPLOCK_WRITE_TO_READ		0x01
#define OPLOCK_READ_HANDLE_TO_READ	0x02
#define OPLOCK_WRITE_TO_NONE		0x04
#define OPLOCK_READ_TO_NONE		0x08

struct lease_ctx_info {
	__u8			lease_key[SMB2_LEASE_KEY_SIZE];
	__le32			req_state;
	__le32			flags;
	__le64			duration;
	__u8			parent_lease_key[SMB2_LEASE_KEY_SIZE];
	__le16			epoch;
	int			version;
	bool			is_dir;
};

struct lease_table {
	char			client_guid[SMB2_CLIENT_GUID_SIZE];
	struct list_head	lease_list;
	struct list_head	l_entry;
	spinlock_t		lb_lock;
};

struct lease {
	__u8			lease_key[SMB2_LEASE_KEY_SIZE];
	__le32			state;
	__le32			new_state;
	__le32			flags;
	__le64			duration;
	__u8			parent_lease_key[SMB2_LEASE_KEY_SIZE];
	int			version;
	unsigned short		epoch;
	bool			is_dir;
	struct lease_table	*l_lb;
};

struct oplock_info {
	struct ksmbd_conn	*conn;
	struct ksmbd_session	*sess;
	struct ksmbd_work	*work;
	struct ksmbd_file	*o_fp;
	int                     level;
	int                     op_state;
	unsigned long		pending_break;
	u64			fid;
	atomic_t		breaking_cnt;
	atomic_t		refcount;
	__u16                   Tid;
	bool			is_lease;
	bool			open_trunc;	/* truncate on open */
	struct lease		*o_lease;
	struct list_head        interim_list;
	struct list_head        op_entry;
	struct list_head        lease_entry;
	wait_queue_head_t oplock_q; /* Other server threads */
	wait_queue_head_t oplock_brk; /* oplock breaking wait */
	struct rcu_head		rcu_head;
};

struct lease_break_info {
	__le32			curr_state;
	__le32			new_state;
	__le16			epoch;
	char			lease_key[SMB2_LEASE_KEY_SIZE];
};

struct oplock_break_info {
	int level;
	int open_trunc;
	int fid;
};

int smb_grant_oplock(struct ksmbd_work *work, int req_op_level,
		     u64 pid, struct ksmbd_file *fp, __u16 tid,
		     struct lease_ctx_info *lctx, int share_ret);
void smb_break_all_levII_oplock(struct ksmbd_work *work,
				struct ksmbd_file *fp, int is_trunc);
int opinfo_write_to_read(struct oplock_info *opinfo);
int opinfo_read_handle_to_read(struct oplock_info *opinfo);
int opinfo_write_to_none(struct oplock_info *opinfo);
int opinfo_read_to_none(struct oplock_info *opinfo);
void close_id_del_oplock(struct ksmbd_file *fp);
void smb_break_all_oplock(struct ksmbd_work *work, struct ksmbd_file *fp);
struct oplock_info *opinfo_get(struct ksmbd_file *fp);
void opinfo_put(struct oplock_info *opinfo);

/* Lease related functions */
void create_lease_buf(u8 *rbuf, struct lease *lease);
struct lease_ctx_info *parse_lease_state(void *open_req, bool is_dir);
__u8 smb2_map_lease_to_oplock(__le32 lease_state);
int lease_read_to_write(struct oplock_info *opinfo);

/* Durable related functions */
void create_durable_rsp_buf(char *cc);
void create_durable_v2_rsp_buf(char *cc, struct ksmbd_file *fp);
void create_mxac_rsp_buf(char *cc, int maximal_access);
void create_disk_id_rsp_buf(char *cc, __u64 file_id, __u64 vol_id);
void create_posix_rsp_buf(char *cc, struct ksmbd_file *fp);
struct create_context *smb2_find_context_vals(void *open_req, const char *tag, int tag_len);
struct oplock_info *lookup_lease_in_table(struct ksmbd_conn *conn,
					  char *lease_key);
int find_same_lease_key(struct ksmbd_session *sess, struct ksmbd_inode *ci,
			struct lease_ctx_info *lctx);
void destroy_lease_table(struct ksmbd_conn *conn);
void smb_send_parent_lease_break_noti(struct ksmbd_file *fp,
				      struct lease_ctx_info *lctx);
#endif /* __KSMBD_OPLOCK_H */
