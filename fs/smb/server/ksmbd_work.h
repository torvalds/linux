/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2019 Samsung Electronics Co., Ltd.
 */

#ifndef __KSMBD_WORK_H__
#define __KSMBD_WORK_H__

#include <linux/ctype.h>
#include <linux/workqueue.h>

struct ksmbd_conn;
struct ksmbd_session;
struct ksmbd_tree_connect;

enum {
	KSMBD_WORK_ACTIVE = 0,
	KSMBD_WORK_CANCELLED,
	KSMBD_WORK_CLOSED,
};

struct aux_read {
	void *buf;
	struct list_head entry;
};

/* one of these for every pending CIFS request at the connection */
struct ksmbd_work {
	/* Server corresponding to this mid */
	struct ksmbd_conn               *conn;
	struct ksmbd_session            *sess;
	struct ksmbd_tree_connect       *tcon;

	/* Pointer to received SMB header */
	void                            *request_buf;
	/* Response buffer */
	void                            *response_buf;

	struct list_head		aux_read_list;

	struct kvec			*iov;
	int				iov_alloc_cnt;
	int				iov_cnt;
	int				iov_idx;

	/* Next cmd hdr in compound req buf*/
	int                             next_smb2_rcv_hdr_off;
	/* Next cmd hdr in compound rsp buf*/
	int                             next_smb2_rsp_hdr_off;
	/* Current cmd hdr in compound rsp buf*/
	int                             curr_smb2_rsp_hdr_off;

	/*
	 * Current Local FID assigned compound response if SMB2 CREATE
	 * command is present in compound request
	 */
	u64				compound_fid;
	u64				compound_pfid;
	u64				compound_sid;

	const struct cred		*saved_cred;

	/* Number of granted credits */
	unsigned int			credits_granted;

	/* response smb header size */
	unsigned int                    response_sz;

	void				*tr_buf;

	unsigned char			state;
	/* No response for cancelled request */
	bool                            send_no_response:1;
	/* Request is encrypted */
	bool                            encrypted:1;
	/* Is this SYNC or ASYNC ksmbd_work */
	bool                            asynchronous:1;
	bool                            need_invalidate_rkey:1;

	unsigned int                    remote_key;
	/* cancel works */
	int                             async_id;
	void                            **cancel_argv;
	void                            (*cancel_fn)(void **argv);

	struct work_struct              work;
	/* List head at conn->requests */
	struct list_head                request_entry;
	/* List head at conn->async_requests */
	struct list_head                async_request_entry;
	struct list_head                fp_entry;
	struct list_head                interim_entry;
};

/**
 * ksmbd_resp_buf_next - Get next buffer on compound response.
 * @work: smb work containing response buffer
 */
static inline void *ksmbd_resp_buf_next(struct ksmbd_work *work)
{
	return work->response_buf + work->next_smb2_rsp_hdr_off + 4;
}

/**
 * ksmbd_resp_buf_curr - Get current buffer on compound response.
 * @work: smb work containing response buffer
 */
static inline void *ksmbd_resp_buf_curr(struct ksmbd_work *work)
{
	return work->response_buf + work->curr_smb2_rsp_hdr_off + 4;
}

/**
 * ksmbd_req_buf_next - Get next buffer on compound request.
 * @work: smb work containing response buffer
 */
static inline void *ksmbd_req_buf_next(struct ksmbd_work *work)
{
	return work->request_buf + work->next_smb2_rcv_hdr_off + 4;
}

struct ksmbd_work *ksmbd_alloc_work_struct(void);
void ksmbd_free_work_struct(struct ksmbd_work *work);

void ksmbd_work_pool_destroy(void);
int ksmbd_work_pool_init(void);

int ksmbd_workqueue_init(void);
void ksmbd_workqueue_destroy(void);
bool ksmbd_queue_work(struct ksmbd_work *work);
int ksmbd_iov_pin_rsp_read(struct ksmbd_work *work, void *ib, int len,
			   void *aux_buf, unsigned int aux_size);
int ksmbd_iov_pin_rsp(struct ksmbd_work *work, void *ib, int len);
void ksmbd_iov_reset(struct ksmbd_work *work);
#endif /* __KSMBD_WORK_H__ */
