/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#ifndef __YNL_C_PRIV_H
#define __YNL_C_PRIV_H 1

#include <stdbool.h>
#include <stddef.h>
#include <linux/types.h>

struct ynl_parse_arg;

/*
 * YNL internals / low level stuff
 */

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

enum ynl_parse_result {
	YNL_PARSE_CB_ERROR = -1,
	YNL_PARSE_CB_STOP = 0,
	YNL_PARSE_CB_OK = 1,
};

#define YNL_SOCKET_BUFFER_SIZE		(1 << 17)

#define YNL_ARRAY_SIZE(array)	(sizeof(array) ?			\
				 sizeof(array) / sizeof(array[0]) : 0)

typedef int (*ynl_parse_cb_t)(const struct nlmsghdr *nlh,
			      struct ynl_parse_arg *yarg);

struct ynl_policy_attr {
	enum ynl_policy_type type;
	unsigned int len;
	const char *name;
	const struct ynl_policy_nest *nest;
};

struct ynl_policy_nest {
	unsigned int max_attr;
	const struct ynl_policy_attr *table;
};

struct ynl_parse_arg {
	struct ynl_sock *ys;
	const struct ynl_policy_nest *rsp_policy;
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
	list = (struct ynl_dump_list_type *)uptr;
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

struct nlmsghdr *ynl_msg_start_req(struct ynl_sock *ys, __u32 id);
struct nlmsghdr *ynl_msg_start_dump(struct ynl_sock *ys, __u32 id);

struct nlmsghdr *
ynl_gemsg_start_req(struct ynl_sock *ys, __u32 id, __u8 cmd, __u8 version);
struct nlmsghdr *
ynl_gemsg_start_dump(struct ynl_sock *ys, __u32 id, __u8 cmd, __u8 version);

int ynl_attr_validate(struct ynl_parse_arg *yarg, const struct nlattr *attr);

/* YNL specific helpers used by the auto-generated code */

struct ynl_req_state {
	struct ynl_parse_arg yarg;
	ynl_parse_cb_t cb;
	__u32 rsp_cmd;
};

struct ynl_dump_state {
	struct ynl_parse_arg yarg;
	void *first;
	struct ynl_dump_list_type *last;
	size_t alloc_sz;
	ynl_parse_cb_t cb;
	__u32 rsp_cmd;
};

struct ynl_ntf_info {
	const struct ynl_policy_nest *policy;
	ynl_parse_cb_t cb;
	size_t alloc_sz;
	void (*free)(struct ynl_ntf_base_type *ntf);
};

int ynl_exec(struct ynl_sock *ys, struct nlmsghdr *req_nlh,
	     struct ynl_req_state *yrs);
int ynl_exec_dump(struct ynl_sock *ys, struct nlmsghdr *req_nlh,
		  struct ynl_dump_state *yds);

void ynl_error_unknown_notification(struct ynl_sock *ys, __u8 cmd);
int ynl_error_parse(struct ynl_parse_arg *yarg, const char *msg);

/* Netlink message handling helpers */

#define YNL_MSG_OVERFLOW	1

static inline struct nlmsghdr *ynl_nlmsg_put_header(void *buf)
{
	struct nlmsghdr *nlh = (struct nlmsghdr *)buf;

	memset(nlh, 0, sizeof(*nlh));
	nlh->nlmsg_len = NLMSG_HDRLEN;

	return nlh;
}

static inline unsigned int ynl_nlmsg_data_len(const struct nlmsghdr *nlh)
{
	return nlh->nlmsg_len - NLMSG_HDRLEN;
}

static inline void *ynl_nlmsg_data(const struct nlmsghdr *nlh)
{
	return (unsigned char *)nlh + NLMSG_HDRLEN;
}

static inline void *
ynl_nlmsg_data_offset(const struct nlmsghdr *nlh, unsigned int offset)
{
	return (unsigned char *)nlh + NLMSG_HDRLEN + offset;
}

static inline void *ynl_nlmsg_end_addr(const struct nlmsghdr *nlh)
{
	return (char *)nlh + nlh->nlmsg_len;
}

static inline void *
ynl_nlmsg_put_extra_header(struct nlmsghdr *nlh, unsigned int size)
{
	void *tail = ynl_nlmsg_end_addr(nlh);

	nlh->nlmsg_len += NLMSG_ALIGN(size);
	return tail;
}

/* Netlink attribute helpers */

static inline unsigned int ynl_attr_type(const struct nlattr *attr)
{
	return attr->nla_type & NLA_TYPE_MASK;
}

static inline unsigned int ynl_attr_data_len(const struct nlattr *attr)
{
	return attr->nla_len - NLA_HDRLEN;
}

static inline void *ynl_attr_data(const struct nlattr *attr)
{
	return (unsigned char *)attr + NLA_HDRLEN;
}

static inline void *ynl_attr_data_end(const struct nlattr *attr)
{
	return (char *)ynl_attr_data(attr) + ynl_attr_data_len(attr);
}

#define ynl_attr_for_each(attr, nlh, fixed_hdr_sz)			\
	for ((attr) = ynl_attr_first(nlh, (nlh)->nlmsg_len,		\
				     NLMSG_HDRLEN + fixed_hdr_sz); attr; \
	     (attr) = ynl_attr_next(ynl_nlmsg_end_addr(nlh), attr))

#define ynl_attr_for_each_nested(attr, outer)				\
	for ((attr) = ynl_attr_first(outer, outer->nla_len,		\
				     sizeof(struct nlattr)); attr;	\
	     (attr) = ynl_attr_next(ynl_attr_data_end(outer), attr))

#define ynl_attr_for_each_payload(start, len, attr)			\
	for ((attr) = ynl_attr_first(start, len, 0); attr;		\
	     (attr) = ynl_attr_next(start + len, attr))

static inline struct nlattr *
ynl_attr_if_good(const void *end, struct nlattr *attr)
{
	if (attr + 1 > (const struct nlattr *)end)
		return NULL;
	if (ynl_attr_data_end(attr) > end)
		return NULL;
	return attr;
}

static inline struct nlattr *
ynl_attr_next(const void *end, const struct nlattr *prev)
{
	struct nlattr *attr;

	attr = (struct nlattr *)((char *)prev + NLA_ALIGN(prev->nla_len));
	return ynl_attr_if_good(end, attr);
}

static inline struct nlattr *
ynl_attr_first(const void *start, size_t len, size_t skip)
{
	struct nlattr *attr;

	attr = (struct nlattr *)((char *)start + NLMSG_ALIGN(skip));
	return ynl_attr_if_good((char *)start + len, attr);
}

static inline bool
__ynl_attr_put_overflow(struct nlmsghdr *nlh, size_t size)
{
	bool o;

	/* ynl_msg_start() stashed buffer length in nlmsg_pid. */
	o = nlh->nlmsg_len + NLA_HDRLEN + NLMSG_ALIGN(size) > nlh->nlmsg_pid;
	if (o)
		/* YNL_MSG_OVERFLOW is < NLMSG_HDRLEN, all subsequent checks
		 * are guaranteed to fail.
		 */
		nlh->nlmsg_pid = YNL_MSG_OVERFLOW;
	return o;
}

static inline struct nlattr *
ynl_attr_nest_start(struct nlmsghdr *nlh, unsigned int attr_type)
{
	struct nlattr *attr;

	if (__ynl_attr_put_overflow(nlh, 0))
		return (struct nlattr *)ynl_nlmsg_end_addr(nlh) - 1;

	attr = (struct nlattr *)ynl_nlmsg_end_addr(nlh);
	attr->nla_type = attr_type | NLA_F_NESTED;
	nlh->nlmsg_len += NLA_HDRLEN;

	return attr;
}

static inline void
ynl_attr_nest_end(struct nlmsghdr *nlh, struct nlattr *attr)
{
	attr->nla_len = (char *)ynl_nlmsg_end_addr(nlh) - (char *)attr;
}

static inline void
ynl_attr_put(struct nlmsghdr *nlh, unsigned int attr_type,
	     const void *value, size_t size)
{
	struct nlattr *attr;

	if (__ynl_attr_put_overflow(nlh, size))
		return;

	attr = (struct nlattr *)ynl_nlmsg_end_addr(nlh);
	attr->nla_type = attr_type;
	attr->nla_len = NLA_HDRLEN + size;

	memcpy(ynl_attr_data(attr), value, size);

	nlh->nlmsg_len += NLMSG_ALIGN(attr->nla_len);
}

static inline void
ynl_attr_put_str(struct nlmsghdr *nlh, unsigned int attr_type, const char *str)
{
	struct nlattr *attr;
	size_t len;

	len = strlen(str);
	if (__ynl_attr_put_overflow(nlh, len))
		return;

	attr = (struct nlattr *)ynl_nlmsg_end_addr(nlh);
	attr->nla_type = attr_type;

	strcpy((char *)ynl_attr_data(attr), str);
	attr->nla_len = NLA_HDRLEN + NLA_ALIGN(len);

	nlh->nlmsg_len += NLMSG_ALIGN(attr->nla_len);
}

static inline const char *ynl_attr_get_str(const struct nlattr *attr)
{
	return (const char *)ynl_attr_data(attr);
}

static inline __s8 ynl_attr_get_s8(const struct nlattr *attr)
{
	return *(__s8 *)ynl_attr_data(attr);
}

static inline __s16 ynl_attr_get_s16(const struct nlattr *attr)
{
	return *(__s16 *)ynl_attr_data(attr);
}

static inline __s32 ynl_attr_get_s32(const struct nlattr *attr)
{
	return *(__s32 *)ynl_attr_data(attr);
}

static inline __s64 ynl_attr_get_s64(const struct nlattr *attr)
{
	__s64 tmp;

	memcpy(&tmp, (unsigned char *)(attr + 1), sizeof(tmp));
	return tmp;
}

static inline __u8 ynl_attr_get_u8(const struct nlattr *attr)
{
	return *(__u8 *)ynl_attr_data(attr);
}

static inline __u16 ynl_attr_get_u16(const struct nlattr *attr)
{
	return *(__u16 *)ynl_attr_data(attr);
}

static inline __u32 ynl_attr_get_u32(const struct nlattr *attr)
{
	return *(__u32 *)ynl_attr_data(attr);
}

static inline __u64 ynl_attr_get_u64(const struct nlattr *attr)
{
	__u64 tmp;

	memcpy(&tmp, (unsigned char *)(attr + 1), sizeof(tmp));
	return tmp;
}

static inline void
ynl_attr_put_s8(struct nlmsghdr *nlh, unsigned int attr_type, __s8 value)
{
	ynl_attr_put(nlh, attr_type, &value, sizeof(value));
}

static inline void
ynl_attr_put_s16(struct nlmsghdr *nlh, unsigned int attr_type, __s16 value)
{
	ynl_attr_put(nlh, attr_type, &value, sizeof(value));
}

static inline void
ynl_attr_put_s32(struct nlmsghdr *nlh, unsigned int attr_type, __s32 value)
{
	ynl_attr_put(nlh, attr_type, &value, sizeof(value));
}

static inline void
ynl_attr_put_s64(struct nlmsghdr *nlh, unsigned int attr_type, __s64 value)
{
	ynl_attr_put(nlh, attr_type, &value, sizeof(value));
}

static inline void
ynl_attr_put_u8(struct nlmsghdr *nlh, unsigned int attr_type, __u8 value)
{
	ynl_attr_put(nlh, attr_type, &value, sizeof(value));
}

static inline void
ynl_attr_put_u16(struct nlmsghdr *nlh, unsigned int attr_type, __u16 value)
{
	ynl_attr_put(nlh, attr_type, &value, sizeof(value));
}

static inline void
ynl_attr_put_u32(struct nlmsghdr *nlh, unsigned int attr_type, __u32 value)
{
	ynl_attr_put(nlh, attr_type, &value, sizeof(value));
}

static inline void
ynl_attr_put_u64(struct nlmsghdr *nlh, unsigned int attr_type, __u64 value)
{
	ynl_attr_put(nlh, attr_type, &value, sizeof(value));
}

static inline __u64 ynl_attr_get_uint(const struct nlattr *attr)
{
	switch (ynl_attr_data_len(attr)) {
	case 4:
		return ynl_attr_get_u32(attr);
	case 8:
		return ynl_attr_get_u64(attr);
	default:
		return 0;
	}
}

static inline __s64 ynl_attr_get_sint(const struct nlattr *attr)
{
	switch (ynl_attr_data_len(attr)) {
	case 4:
		return ynl_attr_get_s32(attr);
	case 8:
		return ynl_attr_get_s64(attr);
	default:
		return 0;
	}
}

static inline void
ynl_attr_put_uint(struct nlmsghdr *nlh, __u16 type, __u64 data)
{
	if ((__u32)data == (__u64)data)
		ynl_attr_put_u32(nlh, type, data);
	else
		ynl_attr_put_u64(nlh, type, data);
}

static inline void
ynl_attr_put_sint(struct nlmsghdr *nlh, __u16 type, __s64 data)
{
	if ((__s32)data == (__s64)data)
		ynl_attr_put_s32(nlh, type, data);
	else
		ynl_attr_put_s64(nlh, type, data);
}
#endif
