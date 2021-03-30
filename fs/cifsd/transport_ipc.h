/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __KSMBD_TRANSPORT_IPC_H__
#define __KSMBD_TRANSPORT_IPC_H__

#include <linux/wait.h>

#define KSMBD_IPC_MAX_PAYLOAD	4096

struct ksmbd_login_response *
ksmbd_ipc_login_request(const char *account);

struct ksmbd_session;
struct ksmbd_share_config;
struct ksmbd_tree_connect;
struct sockaddr;

struct ksmbd_tree_connect_response *
ksmbd_ipc_tree_connect_request(struct ksmbd_session *sess,
		struct ksmbd_share_config *share,
		struct ksmbd_tree_connect *tree_conn,
		struct sockaddr *peer_addr);

int ksmbd_ipc_tree_disconnect_request(unsigned long long session_id,
				      unsigned long long connect_id);
int ksmbd_ipc_logout_request(const char *account);

struct ksmbd_share_config_response *
ksmbd_ipc_share_config_request(const char *name);

struct ksmbd_spnego_authen_response *
ksmbd_ipc_spnego_authen_request(const char *spnego_blob, int blob_len);

int ksmbd_ipc_id_alloc(void);
void ksmbd_rpc_id_free(int handle);

struct ksmbd_rpc_command *ksmbd_rpc_open(struct ksmbd_session *sess, int handle);
struct ksmbd_rpc_command *ksmbd_rpc_close(struct ksmbd_session *sess, int handle);

struct ksmbd_rpc_command *ksmbd_rpc_write(struct ksmbd_session *sess, int handle,
		void *payload, size_t payload_sz);
struct ksmbd_rpc_command *ksmbd_rpc_read(struct ksmbd_session *sess, int handle);
struct ksmbd_rpc_command *ksmbd_rpc_ioctl(struct ksmbd_session *sess, int handle,
		void *payload, size_t payload_sz);
struct ksmbd_rpc_command *ksmbd_rpc_rap(struct ksmbd_session *sess, void *payload,
		size_t payload_sz);

void ksmbd_ipc_release(void);
void ksmbd_ipc_soft_reset(void);
int ksmbd_ipc_init(void);
#endif /* __KSMBD_TRANSPORT_IPC_H__ */
