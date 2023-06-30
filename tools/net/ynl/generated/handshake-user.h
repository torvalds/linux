/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/handshake.yaml */
/* YNL-GEN user header */

#ifndef _LINUX_HANDSHAKE_GEN_H
#define _LINUX_HANDSHAKE_GEN_H

#include <stdlib.h>
#include <string.h>
#include <linux/types.h>
#include <linux/handshake.h>

struct ynl_sock;

extern const struct ynl_family ynl_handshake_family;

/* Enums */
const char *handshake_op_str(int op);
const char *handshake_handler_class_str(enum handshake_handler_class value);
const char *handshake_msg_type_str(enum handshake_msg_type value);
const char *handshake_auth_str(enum handshake_auth value);

/* Common nested types */
struct handshake_x509 {
	struct {
		__u32 cert:1;
		__u32 privkey:1;
	} _present;

	__u32 cert;
	__u32 privkey;
};

/* ============== HANDSHAKE_CMD_ACCEPT ============== */
/* HANDSHAKE_CMD_ACCEPT - do */
struct handshake_accept_req {
	struct {
		__u32 handler_class:1;
	} _present;

	enum handshake_handler_class handler_class;
};

static inline struct handshake_accept_req *handshake_accept_req_alloc(void)
{
	return calloc(1, sizeof(struct handshake_accept_req));
}
void handshake_accept_req_free(struct handshake_accept_req *req);

static inline void
handshake_accept_req_set_handler_class(struct handshake_accept_req *req,
				       enum handshake_handler_class handler_class)
{
	req->_present.handler_class = 1;
	req->handler_class = handler_class;
}

struct handshake_accept_rsp {
	struct {
		__u32 sockfd:1;
		__u32 message_type:1;
		__u32 timeout:1;
		__u32 auth_mode:1;
		__u32 peername_len;
	} _present;

	__u32 sockfd;
	enum handshake_msg_type message_type;
	__u32 timeout;
	enum handshake_auth auth_mode;
	unsigned int n_peer_identity;
	__u32 *peer_identity;
	unsigned int n_certificate;
	struct handshake_x509 *certificate;
	char *peername;
};

void handshake_accept_rsp_free(struct handshake_accept_rsp *rsp);

/*
 * Handler retrieves next queued handshake request
 */
struct handshake_accept_rsp *
handshake_accept(struct ynl_sock *ys, struct handshake_accept_req *req);

/* HANDSHAKE_CMD_ACCEPT - notify */
struct handshake_accept_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct handshake_accept_ntf *ntf);
	struct handshake_accept_rsp obj __attribute__ ((aligned (8)));
};

void handshake_accept_ntf_free(struct handshake_accept_ntf *rsp);

/* ============== HANDSHAKE_CMD_DONE ============== */
/* HANDSHAKE_CMD_DONE - do */
struct handshake_done_req {
	struct {
		__u32 status:1;
		__u32 sockfd:1;
	} _present;

	__u32 status;
	__u32 sockfd;
	unsigned int n_remote_auth;
	__u32 *remote_auth;
};

static inline struct handshake_done_req *handshake_done_req_alloc(void)
{
	return calloc(1, sizeof(struct handshake_done_req));
}
void handshake_done_req_free(struct handshake_done_req *req);

static inline void
handshake_done_req_set_status(struct handshake_done_req *req, __u32 status)
{
	req->_present.status = 1;
	req->status = status;
}
static inline void
handshake_done_req_set_sockfd(struct handshake_done_req *req, __u32 sockfd)
{
	req->_present.sockfd = 1;
	req->sockfd = sockfd;
}
static inline void
__handshake_done_req_set_remote_auth(struct handshake_done_req *req,
				     __u32 *remote_auth,
				     unsigned int n_remote_auth)
{
	free(req->remote_auth);
	req->remote_auth = remote_auth;
	req->n_remote_auth = n_remote_auth;
}

/*
 * Handler reports handshake completion
 */
int handshake_done(struct ynl_sock *ys, struct handshake_done_req *req);

#endif /* _LINUX_HANDSHAKE_GEN_H */
