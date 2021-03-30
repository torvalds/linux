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

	/* Read data buffer */
	void                            *aux_payload_buf;

	/* Next cmd hdr in compound req buf*/
	int                             next_smb2_rcv_hdr_off;
	/* Next cmd hdr in compound rsp buf*/
	int                             next_smb2_rsp_hdr_off;

	/*
	 * Current Local FID assigned compound response if SMB2 CREATE
	 * command is present in compound request
	 */
	unsigned int                    compound_fid;
	unsigned int                    compound_pfid;
	unsigned int                    compound_sid;

	const struct cred		*saved_cred;

	/* Number of granted credits */
	unsigned int			credits_granted;

	/* response smb header size */
	unsigned int                    resp_hdr_sz;
	unsigned int                    response_sz;
	/* Read data count */
	unsigned int                    aux_payload_sz;

	void				*tr_buf;

	unsigned char			state;
	/* Multiple responses for one request e.g. SMB ECHO */
	bool                            multiRsp:1;
	/* No response for cancelled request */
	bool                            send_no_response:1;
	/* Request is encrypted */
	bool                            encrypted:1;
	/* Is this SYNC or ASYNC ksmbd_work */
	bool                            syncronous:1;
	bool                            need_invalidate_rkey:1;
	bool                            set_trans_buf:1;
	bool                            set_read_buf:1;

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

#define WORK_CANCELLED(w)	((w)->state == KSMBD_WORK_CANCELLED)
#define WORK_CLOSED(w)		((w)->state == KSMBD_WORK_CLOSED)
#define WORK_ACTIVE(w)		((w)->state == KSMBD_WORK_ACTIVE)

#define RESPONSE_BUF_NEXT(w)	\
	(((w)->response_buf + (w)->next_smb2_rsp_hdr_off))
#define REQUEST_BUF_NEXT(w)	\
	(((w)->request_buf + (w)->next_smb2_rcv_hdr_off))

struct ksmbd_work *ksmbd_alloc_work_struct(void);
void ksmbd_free_work_struct(struct ksmbd_work *work);

void ksmbd_work_pool_destroy(void);
int ksmbd_work_pool_init(void);

int ksmbd_workqueue_init(void);
void ksmbd_workqueue_destroy(void);
bool ksmbd_queue_work(struct ksmbd_work *work);

#endif /* __KSMBD_WORK_H__ */
