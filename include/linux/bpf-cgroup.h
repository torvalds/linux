/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BPF_CGROUP_H
#define _BPF_CGROUP_H

#include <linux/bpf.h>
#include <linux/errno.h>
#include <linux/jump_label.h>
#include <linux/percpu.h>
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

#ifdef CONFIG_CGROUP_BPF

extern struct static_key_false cgroup_bpf_enabled_key;
#define cgroup_bpf_enabled static_branch_unlikely(&cgroup_bpf_enabled_key)

DECLARE_PER_CPU(struct bpf_cgroup_storage*,
		bpf_cgroup_storage[MAX_BPF_CGROUP_STORAGE_TYPE]);

#define for_each_cgroup_storage_type(stype) \
	for (stype = 0; stype < MAX_BPF_CGROUP_STORAGE_TYPE; stype++)

struct bpf_cgroup_storage_map;

struct bpf_storage_buffer {
	struct rcu_head rcu;
	char data[0];
};

struct bpf_cgroup_storage {
	union {
		struct bpf_storage_buffer *buf;
		void __percpu *percpu_buf;
	};
	struct bpf_cgroup_storage_map *map;
	struct bpf_cgroup_storage_key key;
	struct list_head list;
	struct rb_node node;
	struct rcu_head rcu;
};

struct bpf_prog_list {
	struct list_head node;
	struct bpf_prog *prog;
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

	/* temp storage for effective prog array used by prog_attach/detach */
	struct bpf_prog_array __rcu *inactive;
};

void cgroup_bpf_put(struct cgroup *cgrp);
int cgroup_bpf_inherit(struct cgroup *cgrp);

int __cgroup_bpf_attach(struct cgroup *cgrp, struct bpf_prog *prog,
			enum bpf_attach_type type, u32 flags);
int __cgroup_bpf_detach(struct cgroup *cgrp, struct bpf_prog *prog,
			enum bpf_attach_type type);
int __cgroup_bpf_query(struct cgroup *cgrp, const union bpf_attr *attr,
		       union bpf_attr __user *uattr);

/* Wrapper for __cgroup_bpf_*() protected by cgroup_mutex */
int cgroup_bpf_attach(struct cgroup *cgrp, struct bpf_prog *prog,
		      enum bpf_attach_type type, u32 flags);
int cgroup_bpf_detach(struct cgroup *cgrp, struct bpf_prog *prog,
		      enum bpf_attach_type type, u32 flags);
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
				      void *t_ctx);

int __cgroup_bpf_run_filter_sock_ops(struct sock *sk,
				     struct bpf_sock_ops_kern *sock_ops,
				     enum bpf_attach_type type);

int __cgroup_bpf_check_dev_permission(short dev_type, u32 major, u32 minor,
				      short access, enum bpf_attach_type type);

int __cgroup_bpf_run_filter_sysctl(struct ctl_table_header *head,
				   struct ctl_table *table, int write,
				   void __user *buf, size_t *pcount,
				   loff_t *ppos, void **new_buf,
				   enum bpf_attach_type type);

static inline enum bpf_cgroup_storage_type cgroup_storage_type(
	struct bpf_map *map)
{
	if (map->map_type == BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE)
		return BPF_CGROUP_STORAGE_PERCPU;

	return BPF_CGROUP_STORAGE_SHARED;
}

static inline void bpf_cgroup_storage_set(struct bpf_cgroup_storage
					  *storage[MAX_BPF_CGROUP_STORAGE_TYPE])
{
	enum bpf_cgroup_storage_type stype;

	for_each_cgroup_storage_type(stype)
		this_cpu_write(bpf_cgroup_storage[stype], storage[stype]);
}

struct bpf_cgroup_storage *bpf_cgroup_storage_alloc(struct bpf_prog *prog,
					enum bpf_cgroup_storage_type stype);
void bpf_cgroup_storage_free(struct bpf_cgroup_storage *storage);
void bpf_cgroup_storage_link(struct bpf_cgroup_storage *storage,
			     struct cgroup *cgroup,
			     enum bpf_attach_type type);
void bpf_cgroup_storage_unlink(struct bpf_cgroup_storage *storage);
int bpf_cgroup_storage_assign(struct bpf_prog *prog, struct bpf_map *map);
void bpf_cgroup_storage_release(struct bpf_prog *prog, struct bpf_map *map);

int bpf_percpu_cgroup_storage_copy(struct bpf_map *map, void *key, void *value);
int bpf_percpu_cgroup_storage_update(struct bpf_map *map, void *key,
				     void *value, u64 flags);

/* Wrappers for __cgroup_bpf_run_filter_skb() guarded by cgroup_bpf_enabled. */
#define BPF_CGROUP_RUN_PROG_INET_INGRESS(sk, skb)			      \
({									      \
	int __ret = 0;							      \
	if (cgroup_bpf_enabled)						      \
		__ret = __cgroup_bpf_run_filter_skb(sk, skb,		      \
						    BPF_CGROUP_INET_INGRESS); \
									      \
	__ret;								      \
})

#define BPF_CGROUP_RUN_PROG_INET_EGRESS(sk, skb)			       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled && sk && sk == skb->sk) {		       \
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
	if (cgroup_bpf_enabled) {					       \
		__ret = __cgroup_bpf_run_filter_sk(sk, type);		       \
	}								       \
	__ret;								       \
})

#define BPF_CGROUP_RUN_PROG_INET_SOCK(sk)				       \
	BPF_CGROUP_RUN_SK_PROG(sk, BPF_CGROUP_INET_SOCK_CREATE)

#define BPF_CGROUP_RUN_PROG_INET4_POST_BIND(sk)				       \
	BPF_CGROUP_RUN_SK_PROG(sk, BPF_CGROUP_INET4_POST_BIND)

#define BPF_CGROUP_RUN_PROG_INET6_POST_BIND(sk)				       \
	BPF_CGROUP_RUN_SK_PROG(sk, BPF_CGROUP_INET6_POST_BIND)

#define BPF_CGROUP_RUN_SA_PROG(sk, uaddr, type)				       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled)						       \
		__ret = __cgroup_bpf_run_filter_sock_addr(sk, uaddr, type,     \
							  NULL);	       \
	__ret;								       \
})

#define BPF_CGROUP_RUN_SA_PROG_LOCK(sk, uaddr, type, t_ctx)		       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled)	{					       \
		lock_sock(sk);						       \
		__ret = __cgroup_bpf_run_filter_sock_addr(sk, uaddr, type,     \
							  t_ctx);	       \
		release_sock(sk);					       \
	}								       \
	__ret;								       \
})

#define BPF_CGROUP_RUN_PROG_INET4_BIND(sk, uaddr)			       \
	BPF_CGROUP_RUN_SA_PROG(sk, uaddr, BPF_CGROUP_INET4_BIND)

#define BPF_CGROUP_RUN_PROG_INET6_BIND(sk, uaddr)			       \
	BPF_CGROUP_RUN_SA_PROG(sk, uaddr, BPF_CGROUP_INET6_BIND)

#define BPF_CGROUP_PRE_CONNECT_ENABLED(sk) (cgroup_bpf_enabled && \
					    sk->sk_prot->pre_connect)

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

#define BPF_CGROUP_RUN_PROG_SOCK_OPS(sock_ops)				       \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled && (sock_ops)->sk) {	       \
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
	if (cgroup_bpf_enabled)						      \
		__ret = __cgroup_bpf_check_dev_permission(type, major, minor, \
							  access,	      \
							  BPF_CGROUP_DEVICE); \
									      \
	__ret;								      \
})


#define BPF_CGROUP_RUN_PROG_SYSCTL(head, table, write, buf, count, pos, nbuf)  \
({									       \
	int __ret = 0;							       \
	if (cgroup_bpf_enabled)						       \
		__ret = __cgroup_bpf_run_filter_sysctl(head, table, write,     \
						       buf, count, pos, nbuf,  \
						       BPF_CGROUP_SYSCTL);     \
	__ret;								       \
})

int cgroup_bpf_prog_attach(const union bpf_attr *attr,
			   enum bpf_prog_type ptype, struct bpf_prog *prog);
int cgroup_bpf_prog_detach(const union bpf_attr *attr,
			   enum bpf_prog_type ptype);
int cgroup_bpf_prog_query(const union bpf_attr *attr,
			  union bpf_attr __user *uattr);
#else

struct bpf_prog;
struct cgroup_bpf {};
static inline void cgroup_bpf_put(struct cgroup *cgrp) {}
static inline int cgroup_bpf_inherit(struct cgroup *cgrp) { return 0; }

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

static inline int cgroup_bpf_prog_query(const union bpf_attr *attr,
					union bpf_attr __user *uattr)
{
	return -EINVAL;
}

static inline void bpf_cgroup_storage_set(
	struct bpf_cgroup_storage *storage[MAX_BPF_CGROUP_STORAGE_TYPE]) {}
static inline int bpf_cgroup_storage_assign(struct bpf_prog *prog,
					    struct bpf_map *map) { return 0; }
static inline void bpf_cgroup_storage_release(struct bpf_prog *prog,
					      struct bpf_map *map) {}
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

#define cgroup_bpf_enabled (0)
#define BPF_CGROUP_PRE_CONNECT_ENABLED(sk) (0)
#define BPF_CGROUP_RUN_PROG_INET_INGRESS(sk,skb) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET_EGRESS(sk,skb) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET_SOCK(sk) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET4_BIND(sk, uaddr) ({ 0; })
#define BPF_CGROUP_RUN_PROG_INET6_BIND(sk, uaddr) ({ 0; })
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
#define BPF_CGROUP_RUN_PROG_SYSCTL(head,table,write,buf,count,pos,nbuf) ({ 0; })

#define for_each_cgroup_storage_type(stype) for (; false; )

#endif /* CONFIG_CGROUP_BPF */

#endif /* _BPF_CGROUP_H */
