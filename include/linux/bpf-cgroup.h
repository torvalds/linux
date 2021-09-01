/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BPF_CGROUP_H
#define _BPF_CGROUP_H

#include <linux/bpf.h>
#include <linux/errno.h>
#include <linux/jump_label.h>
#include <linux/percpu.h>
#include <linux/percpu-refcount.h>
#include <linux/rbtree.h>
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

#ifdef CONFIG_CGROUP_BPF

extern struct static_key_false cgroup_bpf_enabled_key[MAX_BPF_ATTACH_TYPE];
#define cgroup_bpf_enabled(type) static_branch_unlikely(&cgroup_bpf_enabled_key[type])

#define BPF_CGROUP_STORAGE_NEST_MAX	8

struct bpf_cgroup_storage_info {
	struct task_struct *task;
	struct bpf_cgroup_storage *storage[MAX_BPF_CGROUP_STORAGE_TYPE];
};

/* For each cpu, permit maximum BPF_CGROUP_STORAGE_NEST_MAX number of tasks
 * to use bpf cgroup storage simultaneously.
 */
DECLARE_PER_CPU(struct bpf_cgroup_storage_info,
		bpf_cgroup_storage_info[BPF_CGROUP_STORAGE_NEST_MAX]);

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
	struct list_head node;
	struct bpf_prog *prog;
	struct bpf_cgroup_link *link;
	struct bpf_cgroup_storage *storage[MAX_BPF_CGROUP_STORAGE_TYPE];
};

struct bpf_prog_array;

struct cgroup_bpf {
	/* array of effective progs in this cgroup */
	struct bpf_prog_array __rcu *effective[MAX_BPF_ATTACH_TYPE];

	/* attached progs to this cgroup and attach flags
	 * when flags == 0 or BPF_F_ALLOW_OVERRIDE the progs list will
	 * have either zero or one element
	 * when BPF_F_ALLOW_MULTI the list can have up to BPF_CGROUP_MAX_PROGS
	 */
	struct list_head progs[MAX_BPF_ATTACH_TYPE];
	u32 flags[MAX_BPF_ATTACH_TYPE];

	/* list of cgroup shared storages */
	struct list_head storages;

	/* temp storage for effective prog array used by prog_attach/detach */
	struct bpf_prog_array *inactive;

	/* reference counter used to detach bpf programs after cgroup removal */
	struct percpu_ref refcnt;

	/* cgroup_bpf is released using a work queue */
	struct work_struct release_work;
};

int cgroup_bpf_inherit(struct cgroup *cgrp);
void cgroup_bpf_offline(struct cgroup *cgrp);

int __cgroup_bpf_attach(struct cgroup *cgrp,
			struct bpf_prog *prog, struct bpf_prog *replace_prog,
			struct bpf_cgroup_link *link,
			enum bpf_attach_type type, u32 flags);
int __cgroup_bpf_detach(struct cgroup *cgrp, struct bpf_prog *prog,
			struct bpf_cgroup_link *link,
			enum bpf_attach_type type);
int __cgroup_bpf_query(struct cgroup *cgrp, const union bpf_attr *attr,
		       union bpf_attr __user *uattr);

/* Wrapper for __cgroup_bpf_*() protected by cgroup_mutex */
int cgroup_bpf_attach(struct cgroup *cgrp,
		      struct bpf_prog *prog, struct bpf_prog *replace_prog,
		      struct bpf_cgroup_link *link, enum bpf_attach_type type,
		      u32 flags);
int cgroup_bpf_detach(struct cgroup *cgrp, struct bpf_prog *prog,
		      enum bpf_attach_type type);
int cgroup_bpf_query(struct cgroup *cgrp, const union bpf_attr *attr,
		     union bpf_attr __user *uattr);

int __cgroup_bpf_run_filter_skb(struct sock *sk,
				struct sk_buff *skb,
				enum bpf_attach_type type);

int __cgroup_bpf_run_filter_sk(struct sock *sk,
			       enum bpf_attach_type type);

int __cgroup_bpf_run_filter_sock_addr(struct sock *sk,
				      struct sockaddr *uaddr,
				      enum bpf_attach_type type,
				      void *t_ctx,
				      u32 *flags);

int __cgroup_bpf_run_filter_sock_ops(struct sock *sk,
				     struct bpf_sock_ops_kern *sock_ops,
				     enum bpf_attach_type type);

int __cgroup_bpf_check_dev_permission(short dev_type, u32 major, u32 minor,
				      short access, enum bpf_attach_type type);

int __cgroup_bpf_run_filter_sysctl(struct ctl_table_header *head,
				   struct ctl_table *table, int write,
				   char **buf, size_t *pcount, loff_t *ppos,
				   enum bpf_attach_type type);

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

static inline int bpf_cgroup_storage_set(struct bpf_cgroup_storage
					 *storage[MAX_BPF_CGROUP_STORAGE_TYPE])
{
	enum bpf_cgroup_storage_type stype;
	int i, err = 0;

	preempt_disable();
	for (i = 0; i < BPF_CGROUP_STORAGE_NEST_MAX; i++) {
		if (unlikely(this_cpu_read(bpf_cgroup_storage_info[i].task) != NULL))
			continue;

		this_cpu_write(bpf_cgroup_storage_info[i].task, current);
		for_each_cgroup_storage_type(stype)
			this_cpu_write(bpf_cgroup_storage_info[i].storage[stype],
				       storage[stype]);
		goto out;
	}
	err = -EBUSY;
	WARN_ON_ONCE(1);

out:
	preempt_enable();
	return err;
}

static inline void bpf_cgroup_storage_unset(void)
{
	int i;

	for (i = BPF_CGROUP_STORAGE_NEST_MAX - 1; i >= 0; i--) {
		if (likely(this_cpu_read(bpf_cgroup_storage_info[i].task) != current))
			continue;

		this_cpu_write(bpf_cgroup_storage_info[i].task, NULL);
		return;
	}
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

/* Wrappers for __cgroup_bpf_run_filter_skb() guarded by cgroup_bpf_enabled. */
#define BPF_CGROUP_RUN_PROG_INET_INGRESS(sk, skb)			      \
({									      \
	int __ret = 0;							      \
	if (cgroup_bpf_enabled(BPF_CGROUP_INET_INGRESS))		      \
		__ret = __cgroup_bpf_run_filter_skb(sk, skb,		      \
						    BPF_CGROUP_INET_INGRESS); \
									      \
	__ret;								      \
})

#define BPF_CGROUP_RUN_PROG_INET_EGRESS(sk, skb)			       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(BPF_CGROUP_INET_EGRESS) && sk && sk == skb->sk) { \
		typeof(sk) __sk = sk_to_full_sk(sk);			       \
		if (sk_fullsock(__sk))					       \
			__ret = __cgroup_bpf_run_filter_skb(__sk, skb,	       \
						      BPF_CGROUP_INET_EGRESS); \
	}								       \
	__ret;								       \
})

#define BPF_CGROUP_RUN_SK_PROG(sk, type)				       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(type)) {					       \
		__ret = __cgroup_bpf_run_filter_sk(sk, type);		       \
	}								       \
	__ret;								       \
})

#define BPF_CGROUP_RUN_PROG_INET_SOCK(sk)				       \
	BPF_CGROUP_RUN_SK_PROG(sk, BPF_CGROUP_INET_SOCK_CREATE)

#define BPF_CGROUP_RUN_PROG_INET_SOCK_RELEASE(sk)			       \
	BPF_CGROUP_RUN_SK_PROG(sk, BPF_CGROUP_INET_SOCK_RELEASE)

#define BPF_CGROUP_RUN_PROG_INET4_POST_BIND(sk)				       \
	BPF_CGROUP_RUN_SK_PROG(sk, BPF_CGROUP_INET4_POST_BIND)

#define BPF_CGROUP_RUN_PROG_INET6_POST_BIND(sk)				       \
	BPF_CGROUP_RUN_SK_PROG(sk, BPF_CGROUP_INET6_POST_BIND)

#define BPF_CGROUP_RUN_SA_PROG(sk, uaddr, type)				       \
({									       \
	u32 __unused_flags;						       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(type))					       \
		__ret = __cgroup_bpf_run_filter_sock_addr(sk, uaddr, type,     \
							  NULL,		       \
							  &__unused_flags);    \
	__ret;								       \
})

#define BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, type, t_ctx)		       \
({									       \
	u32 __unused_flags;						       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(type))	{				       \
		lock_sock(sk);						       \
		__ret = __cgroup_bpf_run_filter_sock_addr(sk, uaddr, type,     \
							  t_ctx,	       \
							  &__unused_flags);    \
		release_sock(sk);					       \
	}								       \
	__ret;								       \
})

/* BPF_CGROUP_INET4_BIND and BPF_CGROUP_INET6_BIND can return extra flags
 * via upper bits of return code. The only flag that is supported
 * (at bit position 0) is to indicate CAP_NET_BIND_SERVICE capability check
 * should be bypassed (BPF_RET_BIND_NO_CAP_NET_BIND_SERVICE).
 */
#define BPF_CGROUP_RUN_PROG_INET_BIND_LOCK(sk, uaddr, type, bind_flags)	       \
({									       \
	u32 __flags = 0;						       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(type))	{				       \
		lock_sock(sk);						       \
		__ret = __cgroup_bpf_run_filter_sock_addr(sk, uaddr, type,     \
							  NULL, &__flags);     \
		release_sock(sk);					       \
		if (__flags & BPF_RET_BIND_NO_CAP_NET_BIND_SERVICE)	       \
			*bind_flags |= BIND_NO_CAP_NET_BIND_SERVICE;	       \
	}								       \
	__ret;								       \
})

#define BPF_CGROUP_PRE_CONNECT_ENABLED(sk)				       \
	((cgroup_bpf_enabled(BPF_CGROUP_INET4_CONNECT) ||		       \
	  cgroup_bpf_enabled(BPF_CGROUP_INET6_CONNECT)) &&		       \
	 (sk)->sk_prot->pre_connect)

#define BPF_CGROUP_RUN_PROG_INET4_CONNECT(sk, uaddr)			       \
	BPF_CGROUP_RUN_SA_PROG(sk, uaddr, BPF_CGROUP_INET4_CONNECT)

#define BPF_CGROUP_RUN_PROG_INET6_CONNECT(sk, uaddr)			       \
	BPF_CGROUP_RUN_SA_PROG(sk, uaddr, BPF_CGROUP_INET6_CONNECT)

#define BPF_CGROUP_RUN_PROG_INET4_CONNECT_LOCK(sk, uaddr)		       \
	BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, BPF_CGROUP_INET4_CONNECT, NULL)

#define BPF_CGROUP_RUN_PROG_INET6_CONNECT_LOCK(sk, uaddr)		       \
	BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, BPF_CGROUP_INET6_CONNECT, NULL)

#define BPF_CGROUP_RUN_PROG_UDP4_SENDMSG_LOCK(sk, uaddr, t_ctx)		       \
	BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, BPF_CGROUP_UDP4_SENDMSG, t_ctx)

#define BPF_CGROUP_RUN_PROG_UDP6_SENDMSG_LOCK(sk, uaddr, t_ctx)		       \
	BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, BPF_CGROUP_UDP6_SENDMSG, t_ctx)

#define BPF_CGROUP_RUN_PROG_UDP4_RECVMSG_LOCK(sk, uaddr)			\
	BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, BPF_CGROUP_UDP4_RECVMSG, NULL)

#define BPF_CGROUP_RUN_PROG_UDP6_RECVMSG_LOCK(sk, uaddr)			\
	BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, BPF_CGROUP_UDP6_RECVMSG, NULL)

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
	if (cgroup_bpf_enabled(BPF_CGROUP_SOCK_OPS))			\
		__ret = __cgroup_bpf_run_filter_sock_ops(sk,		\
							 sock_ops,	\
							 BPF_CGROUP_SOCK_OPS); \
	__ret;								\
})

#define BPF_CGROUP_RUN_PROG_SOCK_OPS(sock_ops)				       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(BPF_CGROUP_SOCK_OPS) && (sock_ops)->sk) {       \
		typeof(sk) __sk = sk_to_full_sk((sock_ops)->sk);	       \
		if (__sk && sk_fullsock(__sk))				       \
			__ret = __cgroup_bpf_run_filter_sock_ops(__sk,	       \
								 sock_ops,     \
							 BPF_CGROUP_SOCK_OPS); \
	}								       \
	__ret;								       \
})

#define BPF_CGROUP_RUN_PROG_DEVICE_CGROUP(type, major, minor, access)	      \
({									      \
	int __ret = 0;							      \
	if (cgroup_bpf_enabled(BPF_CGROUP_DEVICE))			      \
		__ret = __cgroup_bpf_check_dev_permission(type, major, minor, \
							  access,	      \
							  BPF_CGROUP_DEVICE); \
									      \
	__ret;								      \
})


#define BPF_CGROUP_RUN_PROG_SYSCTL(head, table, write, buf, count, pos)  \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(BPF_CGROUP_SYSCTL))			       \
		__ret = __cgroup_bpf_run_filter_sysctl(head, table, write,     \
						       buf, count, pos,        \
						       BPF_CGROUP_SYSCTL);     \
	__ret;								       \
})

#define BPF_CGROUP_RUN_PROG_SETSOCKOPT(sock, level, optname, optval, optlen,   \
				       kernel_optval)			       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(BPF_CGROUP_SETSOCKOPT))			       \
		__ret = __cgroup_bpf_run_filter_setsockopt(sock, level,	       \
							   optname, optval,    \
							   optlen,	       \
							   kernel_optval);     \
	__ret;								       \
})

#define BPF_CGROUP_GETSOCKOPT_MAX_OPTLEN(optlen)			       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled(BPF_CGROUP_GETSOCKOPT))			       \
		get_user(__ret, optlen);				       \
	__ret;								       \
})

#define BPF_CGROUP_RUN_PROG_GETSOCKOPT(sock, level, optname, optval, optlen,   \
				       max_optlen, retval)		       \
({									       \
	int __ret = retval;						       \
	if (cgroup_bpf_enabled(BPF_CGROUP_GETSOCKOPT))			       \
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
	if (cgroup_bpf_enabled(BPF_CGROUP_GETSOCKOPT))			       \
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
#else

struct cgroup_bpf {};
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

static inline int bpf_cgroup_storage_set(
	struct bpf_cgroup_storage *storage[MAX_BPF_CGROUP_STORAGE_TYPE]) { return 0; }
static inline void bpf_cgroup_storage_unset(void) {}
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

#define cgroup_bpf_enabled(type) (0)
#define BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, type, t_ctx) ({ 0; })
#define BPF_CGROUP_PRE_CONNECT_ENABLED(sk) (0)
#define BPF_CGROUP_RUN_PROG_INET_INGRESS(sk,skb) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET_EGRESS(sk,skb) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET_SOCK(sk) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET_SOCK_RELEASE(sk) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET_BIND_LOCK(sk, uaddr, type, flags) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET4_POST_BIND(sk) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET6_POST_BIND(sk) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET4_CONNECT(sk, uaddr) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET4_CONNECT_LOCK(sk, uaddr) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET6_CONNECT(sk, uaddr) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET6_CONNECT_LOCK(sk, uaddr) ({ 0; })
#define BPF_CGROUP_RUN_PROG_UDP4_SENDMSG_LOCK(sk, uaddr, t_ctx) ({ 0; })
#define BPF_CGROUP_RUN_PROG_UDP6_SENDMSG_LOCK(sk, uaddr, t_ctx) ({ 0; })
#define BPF_CGROUP_RUN_PROG_UDP4_RECVMSG_LOCK(sk, uaddr) ({ 0; })
#define BPF_CGROUP_RUN_PROG_UDP6_RECVMSG_LOCK(sk, uaddr) ({ 0; })
#define BPF_CGROUP_RUN_PROG_SOCK_OPS(sock_ops) ({ 0; })
#define BPF_CGROUP_RUN_PROG_DEVICE_CGROUP(type,major,minor,access) ({ 0; })
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
