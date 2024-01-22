/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#ifndef __YNL_C_PRIV_H
#define __YNL_C_PRIV_H 1

#include <stddef.h>
#include <libmnl/libmnl.h>
#include <linux/types.h>

/*
 * YNL internals / low level stuff
 */

/* Generic mnl helper code */

enum ynl_policy_type {
	YNL_PT_REJECT = 1,
	YNL_PT_IGNORE,
	YNL_PT_NEST,
	YNL_PT_FLAG,
	YNL_PT_BINARY,
	YNL_PT_U8,
	YNL_PT_U16,
	YNL_PT_U32,
	YNL_PT_U64,
	YNL_PT_UINT,
	YNL_PT_NUL_STR,
	YNL_PT_BITFIELD32,
};

struct ynl_policy_attr {
	enum ynl_policy_type type;
	unsigned int len;
	const char *name;
	struct ynl_policy_nest *nest;
};

struct ynl_policy_nest {
	unsigned int max_attr;
	struct ynl_policy_attr *table;
};

struct ynl_parse_arg {
	struct ynl_sock *ys;
	struct ynl_policy_nest *rsp_policy;
	void *data;
};

struct ynl_dump_list_type {
	struct ynl_dump_list_type *next;
	unsigned char data[] __attribute__((aligned(8)));
};
extern struct ynl_dump_list_type *YNL_LIST_END;

static inline bool ynl_dump_obj_is_last(void *obj)
{
	unsigned long uptr = (unsigned long)obj;

	uptr -= offsetof(struct ynl_dump_list_type, data);
	return uptr == (unsigned long)YNL_LIST_END;
}

static inline void *ynl_dump_obj_next(void *obj)
{
	unsigned long uptr = (unsigned long)obj;
	struct ynl_dump_list_type *list;

	uptr -= offsetof(struct ynl_dump_list_type, data);
	list = (void *)uptr;
	uptr = (unsigned long)list->next;
	uptr += offsetof(struct ynl_dump_list_type, data);

	return (void *)uptr;
}

struct ynl_ntf_base_type {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct ynl_ntf_base_type *ntf);
	unsigned char data[] __attribute__((aligned(8)));
};

extern mnl_cb_t ynl_cb_array[NLMSG_MIN_TYPE];

struct nlmsghdr *
ynl_gemsg_start_req(struct ynl_sock *ys, __u32 id, __u8 cmd, __u8 version);
struct nlmsghdr *
ynl_gemsg_start_dump(struct ynl_sock *ys, __u32 id, __u8 cmd, __u8 version);

int ynl_attr_validate(struct ynl_parse_arg *yarg, const struct nlattr *attr);

int ynl_recv_ack(struct ynl_sock *ys, int ret);
int ynl_cb_null(const struct nlmsghdr *nlh, void *data);

/* YNL specific helpers used by the auto-generated code */

struct ynl_req_state {
	struct ynl_parse_arg yarg;
	mnl_cb_t cb;
	__u32 rsp_cmd;
};

struct ynl_dump_state {
	struct ynl_sock *ys;
	struct ynl_policy_nest *rsp_policy;
	void *first;
	struct ynl_dump_list_type *last;
	size_t alloc_sz;
	mnl_cb_t cb;
	__u32 rsp_cmd;
};

struct ynl_ntf_info {
	struct ynl_policy_nest *policy;
	mnl_cb_t cb;
	size_t alloc_sz;
	void (*free)(struct ynl_ntf_base_type *ntf);
};

int ynl_exec(struct ynl_sock *ys, struct nlmsghdr *req_nlh,
	     struct ynl_req_state *yrs);
int ynl_exec_dump(struct ynl_sock *ys, struct nlmsghdr *req_nlh,
		  struct ynl_dump_state *yds);

void ynl_error_unknown_notification(struct ynl_sock *ys, __u8 cmd);
int ynl_error_parse(struct ynl_parse_arg *yarg, const char *msg);

#ifndef MNL_HAS_AUTO_SCALARS
static inline uint64_t mnl_attr_get_uint(const struct nlattr *attr)
{
	if (mnl_attr_get_payload_len(attr) == 4)
		return mnl_attr_get_u32(attr);
	return mnl_attr_get_u64(attr);
}

static inline void
mnl_attr_put_uint(struct nlmsghdr *nlh, uint16_t type, uint64_t data)
{
	if ((uint32_t)data == (uint64_t)data)
		return mnl_attr_put_u32(nlh, type, data);
	return mnl_attr_put_u64(nlh, type, data);
}
#endif
#endif
