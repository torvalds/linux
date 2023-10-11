/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BPF_CGROUP_H
#define _BPF_CGROUP_H

#include <linux/bpf.h>
#include <linux/bpf-cgroup-defs.h>
#include <linux/errno.h>
#include <linux/jump_label.h>
#include <linux/percpu.h>
#include <linux/rbtree.h>
#include <net/sock.h>
#include <uapi/linux/bpf.h>

struct sock;
struct sockaddr;
struct cgroup;
struct sk_buff;
struct bpf_map;
struct bpf_prog;
struct bpf_sock_ops_kern;
struct bpf_cgroup_storage;
struct ctl_table;
struct ctl_table_header;
struct task_struct;

unsigned int __cgroup_bpf_run_lsm_sock(const void *ctx,
				       const struct bpf_insn *insn);
unsigned int __cgroup_bpf_run_lsm_socket(const void *ctx,
					 const struct bpf_insn *insn);
unsigned int __cgroup_bpf_run_lsm_current(const void *ctx,
					  const struct bpf_insn *insn);

#ifdef CONFIG_CGROUP_BPF

#define CGROUP_ATYPE(type) \
	case BPF_##type: return type

static inline enum cgroup_bpf_attach_type
to_cgroup_bpf_attach_type(enum bpf_attach_type attach_type)
{
	switch (attach_type) {
	CGROUP_ATYPE(CGROUP_INET_INGRESS);
	CGROUP_ATYPE(CGROUP_INET_EGRESS);
	CGROUP_ATYPE(CGROUP_INET_SOCK_CREATE);
	CGROUP_ATYPE(CGROUP_SOCK_OPS);
	CGROUP_ATYPE(CGROUP_DEVICE);
	CGROUP_ATYPE(CGROUP_INET4_BIND);
	CGROUP_ATYPE(CGROUP_INET6_BIND);
	CGROUP_ATYPE(CGROUP_INET4_CONNECT);
	CGROUP_ATYPE(CGROUP_INET6_CONNECT);
	CGROUP_ATYPE(CGROUP_INET4_POST_BIND);
	CGROUP_ATYPE(CGROUP_INET6_POST_BIND);
	CGROUP_ATYPE(CGROUP_UDP4_SENDMSG);
	CGROUP_ATYPE(CGROUP_UDP6_SENDMSG);
	CGROUP_ATYPE(CGROUP_SYSCTL);
	CGROUP_ATYPE(CGROUP_UDP4_RECVMSG);
	CGROUP_ATYPE(CGROUP_UDP6_RECVMSG);
	CGROUP_ATYPE(CGROUP_GETSOCKOPT);
	CGROUP_ATYPE(CGROUP_SETSOCKOPT);
	CGROUP_ATYPE(CGROUP_INET4_GETPEERNAME);
	CGROUP_ATYPE(CGROUP_INET6_GETPEERNAME);
	CGROUP_ATYPE(CGROUP_INET4_GETSOCKNAME);
	CGROUP_ATYPE(CGROUP_INET6_GETSOCKNAME);
	CGROUP_ATYPE(CGROUP_INET_SOCK_RELEASE);
	default:
		return CGROUP_BPF_ATTACH_TYPE_INVALID;
	}
}

#undef CGROUP_ATYPE

extern struct static_key_false cgroup_bpf_enabled_key[MAX_CGROUP_BPF_ATTACH_TYPE];
#define cgroup_bpf_enabled(atype) static_branch_unlikely(&cgroup_bpf_enabled_key[atype])

#define for_each_cgroup_storage_type(stype) \
	for (stype = 0; stype < MAX_BPF_CGROUP_STORAGE_TYPE; stype++)

struct bpf_cgroup_storage_map;

struct bpf_storage_buffer {
	struct rcu_head rcu;
	char data[];
};

struct bpf_cgroup_storage {
	union {
		struct bpf_storage_buffer *buf;
		void __percpu *percpu_buf;
	};
	struct bpf_cgroup_storage_map *map;
	struct bpf_cgroup_storage_key key;
	struct list_head list_map;
	struct list_head list_cg;
	struct rb_node node;
	struct rcu_head rcu;
};

struct bpf_cgroup_link {
	struct bpf_link link;
	struct cgroup *cgroup;
	enum bpf_attach_type type;
};

struct bpf_prog_list {
	struct hlist_node node;
	struct bpf_prog *prog;
	struct bpf_cgroup_link *link;
	struct bpf_cgroup_storage *storage[MAX_BPF_CGROUP_STORAGE_TYPE];
};

int cgroup_bpf_inherit(struct cgroup *cgrp);
void cgroup_bpf_offline(struct cgroup *cgrp);

int __cgroup_bpf_run_filter_skb(struct sock *sk,
				struct sk_buff *skb,
				enum cgroup_bpf_attach_type atype);

int __cgroup_bpf_run_filter_sk(struct sock *sk,
			       enum cgroup_bpf_attach_type atype);

int __cgroup_bpf_run_filter_sock_addr(struct sock *sk,
				      struct sockaddr *uaddr,
				      int *uaddrlen,
				      enum cgroup_bpf_attach_type atype,
				      void *t_ctx,
				      u32 *flags);

int __cgroup_bpf_run_filter_sock_ops(struct sock *sk,
				     struct bpf_sock_ops_kern *sock_ops,
				     enum cgroup_bpf_attach_type atype);

int __cgroup_bpf_check_dev_permission(short dev_type, u32 major, u32 minor,
				      short access, enum cgroup_bpf_attach_type atype);

int __cgroup_bpf_run_filter_sysctl(struct ctl_table_header *head,
				   struct ctl_table *table, int write,
				   char **buf, size_t *pcount, loff_t *ppos,
				   enum cgroup_bpf_attach_type atype);

int __cgroup_bpf_run_filter_setsockopt(struct sock *sock, int *level,
				       int *optname, char __user *optval,
				       int *optlen, char **kernel_optval);
int __cgroup_bpf_run_filter_getsockopt(struct sock *sk, int level,
				       int optname, char __user *optval,
				       int __user *optlen, int max_optlen,
				       int retval);

int __cgroup_bpf_run_filter_getsockopt_kern(struct sock *sk, int level,
					    int optname, void *optval,
					    int *optlen, int retval);

static inline enum bpf_cgroup_storage_type cgroup_storage_type(
	struct bpf_map *map)
{
	if (map->map_type == BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE)
		return BPF_CGROUP_STORAGE_PERCPU;

	return BPF_CGROUP_STORAGE_SHARED;
}

struct bpf_cgroup_storage *
cgroup_storage_lookup(struct bpf_cgroup_storage_map *map,
		      void *key, bool locked);
struct bpf_cgroup_storage *bpf_cgroup_storage_alloc(struct bpf_prog *prog,
					enum bpf_cgroup_storage_type stype);
void bpf_cgroup_storage_free(struct bpf_cgroup_storage *storage);
void bpf_cgroup_storage_link(struct bpf_cgroup_storage *storage,
			     struct cgroup *cgroup,
			     enum bpf_attach_type type);
void bpf_cgroup_storage_unlink(struct bpf_cgroup_storage *storage);
int bpf_cgroup_storage_assign(struct bpf_prog_aux *aux, struct bpf_map *map);

int bpf_percpu_cgroup_storage_copy(struct bpf_map *map, void *key, void *value);
int bpf_percpu_cgroup_storage_update(struct bpf_map *map, void *key,
				     void *value, u64 flags);

/* Opportunistic check to see whether we have any BPF program attached*/
static inline bool cgroup_bpf_sock_enabled(struct sock *sk,
					   enum cgroup_bpf_attach_type type)
{
	struct cgroup *cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	struct bpf_prog_array *array;

	array = rcu_access_pointer(cgrp->bpf.effective[type]);
	return array != &bpf_empty_prog_array.hdr;
}

/* Wrappers for __cgroup_bpf_run_filter_skb() guarded by cgroup_bpf_enabled. */
#define BPF_CGROUP_RUN_PROG_INET_INGRESS(sk, skb)			      \
({									      \
	int __ret = 0;							      \
	if (cgroup_bpf_enabled(CGROUP_INET_INGRESS) &&			      \
	    cgroup_bpf_sock_enabled(sk, CGROUP_INET_INGRESS))		      \
		__ret = __cgroup_bpf_run_filter_skb(sk, skb,		      \
						    CGROUP_INET_INGRESS); \
									      \
	__ret;								      \
})

#define BPF_CGROUP_RUN_PROG_INET_EGRESS(sk, skb)			       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(CGROUP_INET_EGRESS) && sk) {		       \
		typeof(sk) __sk = sk_to_full_sk(sk);			       \
		if (sk_fullsock(__sk) && __sk == skb_to_full_sk(skb) &&	       \
		    cgroup_bpf_sock_enabled(__sk, CGROUP_INET_EGRESS))	       \
			__ret = __cgroup_bpf_run_filter_skb(__sk, skb,	       \
						      CGROUP_INET_EGRESS); \
	}								       \
	__ret;								       \
})

#define BPF_CGROUP_RUN_SK_PROG(sk, atype)				       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(atype)) {					       \
		__ret = __cgroup_bpf_run_filter_sk(sk, atype);		       \
	}								       \
	__ret;								       \
})

#define BPF_CGROUP_RUN_PROG_INET_SOCK(sk)				       \
	BPF_CGROUP_RUN_SK_PROG(sk, CGROUP_INET_SOCK_CREATE)

#define BPF_CGROUP_RUN_PROG_INET_SOCK_RELEASE(sk)			       \
	BPF_CGROUP_RUN_SK_PROG(sk, CGROUP_INET_SOCK_RELEASE)

#define BPF_CGROUP_RUN_PROG_INET4_POST_BIND(sk)				       \
	BPF_CGROUP_RUN_SK_PROG(sk, CGROUP_INET4_POST_BIND)

#define BPF_CGROUP_RUN_PROG_INET6_POST_BIND(sk)				       \
	BPF_CGROUP_RUN_SK_PROG(sk, CGROUP_INET6_POST_BIND)

#define BPF_CGROUP_RUN_SA_PROG(sk, uaddr, uaddrlen, atype)		       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(atype))					       \
		__ret = __cgroup_bpf_run_filter_sock_addr(sk, uaddr, uaddrlen, \
							  atype, NULL, NULL);  \
	__ret;								       \
})

#define BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, uaddrlen, atype, t_ctx)	       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(atype))	{				       \
		lock_sock(sk);						       \
		__ret = __cgroup_bpf_run_filter_sock_addr(sk, uaddr, uaddrlen, \
							  atype, t_ctx, NULL); \
		release_sock(sk);					       \
	}								       \
	__ret;								       \
})

/* BPF_CGROUP_INET4_BIND and BPF_CGROUP_INET6_BIND can return extra flags
 * via upper bits of return code. The only flag that is supported
 * (at bit position 0) is to indicate CAP_NET_BIND_SERVICE capability check
 * should be bypassed (BPF_RET_BIND_NO_CAP_NET_BIND_SERVICE).
 */
#define BPF_CGROUP_RUN_PROG_INET_BIND_LOCK(sk, uaddr, uaddrlen, atype, bind_flags) \
({									       \
	u32 __flags = 0;						       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(atype))	{				       \
		lock_sock(sk);						       \
		__ret = __cgroup_bpf_run_filter_sock_addr(sk, uaddr, uaddrlen, \
							  atype, NULL, &__flags); \
		release_sock(sk);					       \
		if (__flags & BPF_RET_BIND_NO_CAP_NET_BIND_SERVICE)	       \
			*bind_flags |= BIND_NO_CAP_NET_BIND_SERVICE;	       \
	}								       \
	__ret;								       \
})

#define BPF_CGROUP_PRE_CONNECT_ENABLED(sk)				       \
	((cgroup_bpf_enabled(CGROUP_INET4_CONNECT) ||		       \
	  cgroup_bpf_enabled(CGROUP_INET6_CONNECT)) &&		       \
	 (sk)->sk_prot->pre_connect)

#define BPF_CGROUP_RUN_PROG_INET4_CONNECT(sk, uaddr, uaddrlen)			\
	BPF_CGROUP_RUN_SA_PROG(sk, uaddr, uaddrlen, CGROUP_INET4_CONNECT)

#define BPF_CGROUP_RUN_PROG_INET6_CONNECT(sk, uaddr, uaddrlen)			\
	BPF_CGROUP_RUN_SA_PROG(sk, uaddr, uaddrlen, CGROUP_INET6_CONNECT)

#define BPF_CGROUP_RUN_PROG_INET4_CONNECT_LOCK(sk, uaddr, uaddrlen)		\
	BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, uaddrlen, CGROUP_INET4_CONNECT, NULL)

#define BPF_CGROUP_RUN_PROG_INET6_CONNECT_LOCK(sk, uaddr, uaddrlen)		\
	BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, uaddrlen, CGROUP_INET6_CONNECT, NULL)

#define BPF_CGROUP_RUN_PROG_UDP4_SENDMSG_LOCK(sk, uaddr, uaddrlen, t_ctx)	\
	BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, uaddrlen, CGROUP_UDP4_SENDMSG, t_ctx)

#define BPF_CGROUP_RUN_PROG_UDP6_SENDMSG_LOCK(sk, uaddr, uaddrlen, t_ctx)	\
	BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, uaddrlen, CGROUP_UDP6_SENDMSG, t_ctx)

#define BPF_CGROUP_RUN_PROG_UDP4_RECVMSG_LOCK(sk, uaddr, uaddrlen)		\
	BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, uaddrlen, CGROUP_UDP4_RECVMSG, NULL)

#define BPF_CGROUP_RUN_PROG_UDP6_RECVMSG_LOCK(sk, uaddr, uaddrlen)		\
	BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, uaddrlen, CGROUP_UDP6_RECVMSG, NULL)

/* The SOCK_OPS"_SK" macro should be used when sock_ops->sk is not a
 * fullsock and its parent fullsock cannot be traced by
 * sk_to_full_sk().
 *
 * e.g. sock_ops->sk is a request_sock and it is under syncookie mode.
 * Its listener-sk is not attached to the rsk_listener.
 * In this case, the caller holds the listener-sk (unlocked),
 * set its sock_ops->sk to req_sk, and call this SOCK_OPS"_SK" with
 * the listener-sk such that the cgroup-bpf-progs of the
 * listener-sk will be run.
 *
 * Regardless of syncookie mode or not,
 * calling bpf_setsockopt on listener-sk will not make sense anyway,
 * so passing 'sock_ops->sk == req_sk' to the bpf prog is appropriate here.
 */
#define BPF_CGROUP_RUN_PROG_SOCK_OPS_SK(sock_ops, sk)			\
({									\
	int __ret = 0;							\
	if (cgroup_bpf_enabled(CGROUP_SOCK_OPS))			\
		__ret = __cgroup_bpf_run_filter_sock_ops(sk,		\
							 sock_ops,	\
							 CGROUP_SOCK_OPS); \
	__ret;								\
})

#define BPF_CGROUP_RUN_PROG_SOCK_OPS(sock_ops)				       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(CGROUP_SOCK_OPS) && (sock_ops)->sk) {       \
		typeof(sk) __sk = sk_to_full_sk((sock_ops)->sk);	       \
		if (__sk && sk_fullsock(__sk))				       \
			__ret = __cgroup_bpf_run_filter_sock_ops(__sk,	       \
								 sock_ops,     \
							 CGROUP_SOCK_OPS); \
	}								       \
	__ret;								       \
})

#define BPF_CGROUP_RUN_PROG_DEVICE_CGROUP(atype, major, minor, access)	      \
({									      \
	int __ret = 0;							      \
	if (cgroup_bpf_enabled(CGROUP_DEVICE))			      \
		__ret = __cgroup_bpf_check_dev_permission(atype, major, minor, \
							  access,	      \
							  CGROUP_DEVICE); \
									      \
	__ret;								      \
})


#define BPF_CGROUP_RUN_PROG_SYSCTL(head, table, write, buf, count, pos)  \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(CGROUP_SYSCTL))			       \
		__ret = __cgroup_bpf_run_filter_sysctl(head, table, write,     \
						       buf, count, pos,        \
						       CGROUP_SYSCTL);     \
	__ret;								       \
})

#define BPF_CGROUP_RUN_PROG_SETSOCKOPT(sock, level, optname, optval, optlen,   \
				       kernel_optval)			       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(CGROUP_SETSOCKOPT) &&			       \
	    cgroup_bpf_sock_enabled(sock, CGROUP_SETSOCKOPT))		       \
		__ret = __cgroup_bpf_run_filter_setsockopt(sock, level,	       \
							   optname, optval,    \
							   optlen,	       \
							   kernel_optval);     \
	__ret;								       \
})

#define BPF_CGROUP_GETSOCKOPT_MAX_OPTLEN(optlen)			       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(CGROUP_GETSOCKOPT))			       \
		get_user(__ret, optlen);				       \
	__ret;								       \
})

#define BPF_CGROUP_RUN_PROG_GETSOCKOPT(sock, level, optname, optval, optlen,   \
				       max_optlen, retval)		       \
({									       \
	int __ret = retval;						       \
	if (cgroup_bpf_enabled(CGROUP_GETSOCKOPT) &&			       \
	    cgroup_bpf_sock_enabled(sock, CGROUP_GETSOCKOPT))		       \
		if (!(sock)->sk_prot->bpf_bypass_getsockopt ||		       \
		    !INDIRECT_CALL_INET_1((sock)->sk_prot->bpf_bypass_getsockopt, \
					tcp_bpf_bypass_getsockopt,	       \
					level, optname))		       \
			__ret = __cgroup_bpf_run_filter_getsockopt(	       \
				sock, level, optname, optval, optlen,	       \
				max_optlen, retval);			       \
	__ret;								       \
})

#define BPF_CGROUP_RUN_PROG_GETSOCKOPT_KERN(sock, level, optname, optval,      \
					    optlen, retval)		       \
({									       \
	int __ret = retval;						       \
	if (cgroup_bpf_enabled(CGROUP_GETSOCKOPT))			       \
		__ret = __cgroup_bpf_run_filter_getsockopt_kern(	       \
			sock, level, optname, optval, optlen, retval);	       \
	__ret;								       \
})

int cgroup_bpf_prog_attach(const union bpf_attr *attr,
			   enum bpf_prog_type ptype, struct bpf_prog *prog);
int cgroup_bpf_prog_detach(const union bpf_attr *attr,
			   enum bpf_prog_type ptype);
int cgroup_bpf_link_attach(const union bpf_attr *attr, struct bpf_prog *prog);
int cgroup_bpf_prog_query(const union bpf_attr *attr,
			  union bpf_attr __user *uattr);

const struct bpf_func_proto *
cgroup_common_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog);
const struct bpf_func_proto *
cgroup_current_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog);
#else

static inline int cgroup_bpf_inherit(struct cgroup *cgrp) { return 0; }
static inline void cgroup_bpf_offline(struct cgroup *cgrp) {}

static inline int cgroup_bpf_prog_attach(const union bpf_attr *attr,
					 enum bpf_prog_type ptype,
					 struct bpf_prog *prog)
{
	return -EINVAL;
}

static inline int cgroup_bpf_prog_detach(const union bpf_attr *attr,
					 enum bpf_prog_type ptype)
{
	return -EINVAL;
}

static inline int cgroup_bpf_link_attach(const union bpf_attr *attr,
					 struct bpf_prog *prog)
{
	return -EINVAL;
}

static inline int cgroup_bpf_prog_query(const union bpf_attr *attr,
					union bpf_attr __user *uattr)
{
	return -EINVAL;
}

static inline const struct bpf_func_proto *
cgroup_common_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	return NULL;
}

static inline const struct bpf_func_proto *
cgroup_current_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	return NULL;
}

static inline int bpf_cgroup_storage_assign(struct bpf_prog_aux *aux,
					    struct bpf_map *map) { return 0; }
static inline struct bpf_cgroup_storage *bpf_cgroup_storage_alloc(
	struct bpf_prog *prog, enum bpf_cgroup_storage_type stype) { return NULL; }
static inline void bpf_cgroup_storage_free(
	struct bpf_cgroup_storage *storage) {}
static inline int bpf_percpu_cgroup_storage_copy(struct bpf_map *map, void *key,
						 void *value) {
	return 0;
}
static inline int bpf_percpu_cgroup_storage_update(struct bpf_map *map,
					void *key, void *value, u64 flags) {
	return 0;
}

#define cgroup_bpf_enabled(atype) (0)
#define BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, uaddrlen, atype, t_ctx) ({ 0; })
#define BPF_CGROUP_RUN_SA_PROG(sk, uaddr, uaddrlen, atype) ({ 0; })
#define BPF_CGROUP_PRE_CONNECT_ENABLED(sk) (0)
#define BPF_CGROUP_RUN_PROG_INET_INGRESS(sk,skb) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET_EGRESS(sk,skb) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET_SOCK(sk) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET_SOCK_RELEASE(sk) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET_BIND_LOCK(sk, uaddr, uaddrlen, atype, flags) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET4_POST_BIND(sk) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET6_POST_BIND(sk) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET4_CONNECT(sk, uaddr, uaddrlen) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET4_CONNECT_LOCK(sk, uaddr, uaddrlen) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET6_CONNECT(sk, uaddr, uaddrlen) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET6_CONNECT_LOCK(sk, uaddr, uaddrlen) ({ 0; })
#define BPF_CGROUP_RUN_PROG_UDP4_SENDMSG_LOCK(sk, uaddr, uaddrlen, t_ctx) ({ 0; })
#define BPF_CGROUP_RUN_PROG_UDP6_SENDMSG_LOCK(sk, uaddr, uaddrlen, t_ctx) ({ 0; })
#define BPF_CGROUP_RUN_PROG_UDP4_RECVMSG_LOCK(sk, uaddr, uaddrlen) ({ 0; })
#define BPF_CGROUP_RUN_PROG_UDP6_RECVMSG_LOCK(sk, uaddr, uaddrlen) ({ 0; })
#define BPF_CGROUP_RUN_PROG_SOCK_OPS(sock_ops) ({ 0; })
#define BPF_CGROUP_RUN_PROG_DEVICE_CGROUP(atype, major, minor, access) ({ 0; })
#define BPF_CGROUP_RUN_PROG_SYSCTL(head,table,write,buf,count,pos) ({ 0; })
#define BPF_CGROUP_GETSOCKOPT_MAX_OPTLEN(optlen) ({ 0; })
#define BPF_CGROUP_RUN_PROG_GETSOCKOPT(sock, level, optname, optval, \
				       optlen, max_optlen, retval) ({ retval; })
#define BPF_CGROUP_RUN_PROG_GETSOCKOPT_KERN(sock, level, optname, optval, \
					    optlen, retval) ({ retval; })
#define BPF_CGROUP_RUN_PROG_SETSOCKOPT(sock, level, optname, optval, optlen, \
				       kernel_optval) ({ 0; })

#define for_each_cgroup_storage_type(stype) for (; false; )

#endif /* CONFIG_CGROUP_BPF */

#endif /* _BPF_CGROUP_H */
