/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/fou.yaml */
/* YNL-GEN user header */

#ifndef _LINUX_FOU_GEN_H
#define _LINUX_FOU_GEN_H

#include <stdlib.h>
#include <string.h>
#include <linux/types.h>
#include <linux/fou.h>

struct ynl_sock;

extern const struct ynl_family ynl_fou_family;

/* Enums */
const char *fou_op_str(int op);
const char *fou_encap_type_str(int value);

/* Common nested types */
/* ============== FOU_CMD_ADD ============== */
/* FOU_CMD_ADD - do */
struct fou_add_req {
	struct {
		__u32 port:1;
		__u32 ipproto:1;
		__u32 type:1;
		__u32 remcsum_nopartial:1;
		__u32 local_v4:1;
		__u32 peer_v4:1;
		__u32 local_v6_len;
		__u32 peer_v6_len;
		__u32 peer_port:1;
		__u32 ifindex:1;
	} _present;

	__u16 port /* big-endian */;
	__u8 ipproto;
	__u8 type;
	__u32 local_v4;
	__u32 peer_v4;
	void *local_v6;
	void *peer_v6;
	__u16 peer_port /* big-endian */;
	__s32 ifindex;
};

static inline struct fou_add_req *fou_add_req_alloc(void)
{
	return calloc(1, sizeof(struct fou_add_req));
}
void fou_add_req_free(struct fou_add_req *req);

static inline void
fou_add_req_set_port(struct fou_add_req *req, __u16 port /* big-endian */)
{
	req->_present.port = 1;
	req->port = port;
}
static inline void
fou_add_req_set_ipproto(struct fou_add_req *req, __u8 ipproto)
{
	req->_present.ipproto = 1;
	req->ipproto = ipproto;
}
static inline void fou_add_req_set_type(struct fou_add_req *req, __u8 type)
{
	req->_present.type = 1;
	req->type = type;
}
static inline void fou_add_req_set_remcsum_nopartial(struct fou_add_req *req)
{
	req->_present.remcsum_nopartial = 1;
}
static inline void
fou_add_req_set_local_v4(struct fou_add_req *req, __u32 local_v4)
{
	req->_present.local_v4 = 1;
	req->local_v4 = local_v4;
}
static inline void
fou_add_req_set_peer_v4(struct fou_add_req *req, __u32 peer_v4)
{
	req->_present.peer_v4 = 1;
	req->peer_v4 = peer_v4;
}
static inline void
fou_add_req_set_local_v6(struct fou_add_req *req, const void *local_v6,
			 size_t len)
{
	free(req->local_v6);
	req->_present.local_v6_len = len;
	req->local_v6 = malloc(req->_present.local_v6_len);
	memcpy(req->local_v6, local_v6, req->_present.local_v6_len);
}
static inline void
fou_add_req_set_peer_v6(struct fou_add_req *req, const void *peer_v6,
			size_t len)
{
	free(req->peer_v6);
	req->_present.peer_v6_len = len;
	req->peer_v6 = malloc(req->_present.peer_v6_len);
	memcpy(req->peer_v6, peer_v6, req->_present.peer_v6_len);
}
static inline void
fou_add_req_set_peer_port(struct fou_add_req *req,
			  __u16 peer_port /* big-endian */)
{
	req->_present.peer_port = 1;
	req->peer_port = peer_port;
}
static inline void
fou_add_req_set_ifindex(struct fou_add_req *req, __s32 ifindex)
{
	req->_present.ifindex = 1;
	req->ifindex = ifindex;
}

/*
 * Add port.
 */
int fou_add(struct ynl_sock *ys, struct fou_add_req *req);

/* ============== FOU_CMD_DEL ============== */
/* FOU_CMD_DEL - do */
struct fou_del_req {
	struct {
		__u32 af:1;
		__u32 ifindex:1;
		__u32 port:1;
		__u32 peer_port:1;
		__u32 local_v4:1;
		__u32 peer_v4:1;
		__u32 local_v6_len;
		__u32 peer_v6_len;
	} _present;

	__u8 af;
	__s32 ifindex;
	__u16 port /* big-endian */;
	__u16 peer_port /* big-endian */;
	__u32 local_v4;
	__u32 peer_v4;
	void *local_v6;
	void *peer_v6;
};

static inline struct fou_del_req *fou_del_req_alloc(void)
{
	return calloc(1, sizeof(struct fou_del_req));
}
void fou_del_req_free(struct fou_del_req *req);

static inline void fou_del_req_set_af(struct fou_del_req *req, __u8 af)
{
	req->_present.af = 1;
	req->af = af;
}
static inline void
fou_del_req_set_ifindex(struct fou_del_req *req, __s32 ifindex)
{
	req->_present.ifindex = 1;
	req->ifindex = ifindex;
}
static inline void
fou_del_req_set_port(struct fou_del_req *req, __u16 port /* big-endian */)
{
	req->_present.port = 1;
	req->port = port;
}
static inline void
fou_del_req_set_peer_port(struct fou_del_req *req,
			  __u16 peer_port /* big-endian */)
{
	req->_present.peer_port = 1;
	req->peer_port = peer_port;
}
static inline void
fou_del_req_set_local_v4(struct fou_del_req *req, __u32 local_v4)
{
	req->_present.local_v4 = 1;
	req->local_v4 = local_v4;
}
static inline void
fou_del_req_set_peer_v4(struct fou_del_req *req, __u32 peer_v4)
{
	req->_present.peer_v4 = 1;
	req->peer_v4 = peer_v4;
}
static inline void
fou_del_req_set_local_v6(struct fou_del_req *req, const void *local_v6,
			 size_t len)
{
	free(req->local_v6);
	req->_present.local_v6_len = len;
	req->local_v6 = malloc(req->_present.local_v6_len);
	memcpy(req->local_v6, local_v6, req->_present.local_v6_len);
}
static inline void
fou_del_req_set_peer_v6(struct fou_del_req *req, const void *peer_v6,
			size_t len)
{
	free(req->peer_v6);
	req->_present.peer_v6_len = len;
	req->peer_v6 = malloc(req->_present.peer_v6_len);
	memcpy(req->peer_v6, peer_v6, req->_present.peer_v6_len);
}

/*
 * Delete port.
 */
int fou_del(struct ynl_sock *ys, struct fou_del_req *req);

/* ============== FOU_CMD_GET ============== */
/* FOU_CMD_GET - do */
struct fou_get_req {
	struct {
		__u32 af:1;
		__u32 ifindex:1;
		__u32 port:1;
		__u32 peer_port:1;
		__u32 local_v4:1;
		__u32 peer_v4:1;
		__u32 local_v6_len;
		__u32 peer_v6_len;
	} _present;

	__u8 af;
	__s32 ifindex;
	__u16 port /* big-endian */;
	__u16 peer_port /* big-endian */;
	__u32 local_v4;
	__u32 peer_v4;
	void *local_v6;
	void *peer_v6;
};

static inline struct fou_get_req *fou_get_req_alloc(void)
{
	return calloc(1, sizeof(struct fou_get_req));
}
void fou_get_req_free(struct fou_get_req *req);

static inline void fou_get_req_set_af(struct fou_get_req *req, __u8 af)
{
	req->_present.af = 1;
	req->af = af;
}
static inline void
fou_get_req_set_ifindex(struct fou_get_req *req, __s32 ifindex)
{
	req->_present.ifindex = 1;
	req->ifindex = ifindex;
}
static inline void
fou_get_req_set_port(struct fou_get_req *req, __u16 port /* big-endian */)
{
	req->_present.port = 1;
	req->port = port;
}
static inline void
fou_get_req_set_peer_port(struct fou_get_req *req,
			  __u16 peer_port /* big-endian */)
{
	req->_present.peer_port = 1;
	req->peer_port = peer_port;
}
static inline void
fou_get_req_set_local_v4(struct fou_get_req *req, __u32 local_v4)
{
	req->_present.local_v4 = 1;
	req->local_v4 = local_v4;
}
static inline void
fou_get_req_set_peer_v4(struct fou_get_req *req, __u32 peer_v4)
{
	req->_present.peer_v4 = 1;
	req->peer_v4 = peer_v4;
}
static inline void
fou_get_req_set_local_v6(struct fou_get_req *req, const void *local_v6,
			 size_t len)
{
	free(req->local_v6);
	req->_present.local_v6_len = len;
	req->local_v6 = malloc(req->_present.local_v6_len);
	memcpy(req->local_v6, local_v6, req->_present.local_v6_len);
}
static inline void
fou_get_req_set_peer_v6(struct fou_get_req *req, const void *peer_v6,
			size_t len)
{
	free(req->peer_v6);
	req->_present.peer_v6_len = len;
	req->peer_v6 = malloc(req->_present.peer_v6_len);
	memcpy(req->peer_v6, peer_v6, req->_present.peer_v6_len);
}

struct fou_get_rsp {
	struct {
		__u32 port:1;
		__u32 ipproto:1;
		__u32 type:1;
		__u32 remcsum_nopartial:1;
		__u32 local_v4:1;
		__u32 peer_v4:1;
		__u32 local_v6_len;
		__u32 peer_v6_len;
		__u32 peer_port:1;
		__u32 ifindex:1;
	} _present;

	__u16 port /* big-endian */;
	__u8 ipproto;
	__u8 type;
	__u32 local_v4;
	__u32 peer_v4;
	void *local_v6;
	void *peer_v6;
	__u16 peer_port /* big-endian */;
	__s32 ifindex;
};

void fou_get_rsp_free(struct fou_get_rsp *rsp);

/*
 * Get tunnel info.
 */
struct fou_get_rsp *fou_get(struct ynl_sock *ys, struct fou_get_req *req);

/* FOU_CMD_GET - dump */
struct fou_get_list {
	struct fou_get_list *next;
	struct fou_get_rsp obj __attribute__((aligned(8)));
};

void fou_get_list_free(struct fou_get_list *rsp);

struct fou_get_list *fou_get_dump(struct ynl_sock *ys);

#endif /* _LINUX_FOU_GEN_H */
