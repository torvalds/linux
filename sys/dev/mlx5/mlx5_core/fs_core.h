/*-
 * Copyright (c) 2013-2017, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MLX5_FS_CORE_
#define _MLX5_FS_CORE_

#include <asm/atomic.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <dev/mlx5/fs.h>

enum fs_type {
	FS_TYPE_NAMESPACE,
	FS_TYPE_PRIO,
	FS_TYPE_FLOW_TABLE,
	FS_TYPE_FLOW_GROUP,
	FS_TYPE_FLOW_ENTRY,
	FS_TYPE_FLOW_DEST
};

enum fs_ft_type {
	FS_FT_NIC_RX          = 0x0,
	FS_FT_ESW_EGRESS_ACL  = 0x2,
	FS_FT_ESW_INGRESS_ACL = 0x3,
	FS_FT_FDB             = 0X4,
	FS_FT_SNIFFER_RX      = 0x5,
	FS_FT_SNIFFER_TX      = 0x6
};

enum fs_fte_status {
	FS_FTE_STATUS_EXISTING = 1UL << 0,
};

/* Should always be the first variable in the struct */
struct fs_base {
	struct list_head			list;
	struct fs_base			*parent;
	enum fs_type			type;
	struct kref			refcount;
	/* lock the node for writing and traversing */
	struct mutex		lock;
	struct completion			complete;
	atomic_t			users_refcount;
	const char			*name;
};

struct mlx5_flow_rule {
	struct fs_base				base;
	struct mlx5_flow_destination		dest_attr;
	struct list_head			clients_data;
	/*protect clients lits*/
	struct mutex			clients_lock;
};

struct fs_fte {
	struct fs_base				base;
	u32					val[MLX5_ST_SZ_DW(fte_match_param)];
	uint32_t				dests_size;
	uint32_t				flow_tag;
	struct list_head			dests;
	uint32_t				index; /* index in ft */
	u8					action; /* MLX5_FLOW_CONTEXT_ACTION */
	enum fs_fte_status			status;
};

struct fs_star_rule {
	struct	mlx5_flow_group	 *fg;
	struct	fs_fte		*fte;
};

struct mlx5_flow_table {
	struct fs_base			base;
	/* sorted list by start_index */
	struct list_head		fgs;
	struct {
		bool			active;
		unsigned int		max_types;
		unsigned int		num_types;
	} autogroup;
	unsigned int			max_fte;
	unsigned int			level;
	uint32_t			id;
	u16                             vport;
	enum fs_ft_type			type;
	struct fs_star_rule		star_rule;
	unsigned int			shared_refcount;
};

enum fs_prio_flags {
	MLX5_CORE_FS_PRIO_SHARED = 1
};

struct fs_prio {
	struct fs_base			base;
	struct list_head		objs; /* each object is a namespace or ft */
	unsigned int			max_ft;
	unsigned int			num_ft;
	unsigned int			max_ns;
	unsigned int			prio;
	/*When create shared flow table, this lock should be taken*/
	struct mutex		shared_lock;
	u8				flags;
};

struct mlx5_flow_namespace {
	/* parent == NULL => root ns */
	struct	fs_base			base;
	/* sorted by priority number */
	struct	list_head		prios; /* list of fs_prios */
	struct  list_head		list_notifiers;
	struct	rw_semaphore		notifiers_rw_sem;
	struct  rw_semaphore		dests_rw_sem;
};

struct mlx5_flow_root_namespace {
	struct mlx5_flow_namespace	ns;
	struct mlx5_flow_table		*ft_level_0;
	enum   fs_ft_type		table_type;
	struct mlx5_core_dev		*dev;
	struct mlx5_flow_table		*root_ft;
	/* When chaining flow-tables, this lock should be taken */
	struct mutex		fs_chain_lock;
};

struct mlx5_flow_group {
	struct fs_base			base;
	struct list_head		ftes;
	struct mlx5_core_fs_mask	mask;
	uint32_t			start_index;
	uint32_t			max_ftes;
	uint32_t			num_ftes;
	uint32_t			id;
};

struct mlx5_flow_handler {
	struct list_head list;
	rule_event_fn add_dst_cb;
	rule_event_fn del_dst_cb;
	void *client_context;
	struct mlx5_flow_namespace *ns;
};

struct fs_client_priv_data {
	struct mlx5_flow_handler *fs_handler;
	struct list_head list;
	void   *client_dst_data;
};

void _fs_remove_node(struct kref *kref);
#define fs_get_obj(v, _base)  {v = container_of((_base), typeof(*v), base); }
#define fs_get_parent(v, child)  {v = (child)->base.parent ?		     \
				  container_of((child)->base.parent,	     \
					       typeof(*v), base) : NULL; }

#define fs_list_for_each_entry(pos, cond, root)		\
	list_for_each_entry(pos, root, base.list)	\
		if (!(cond)) {} else

#define fs_list_for_each_entry_continue(pos, cond, root)	\
	list_for_each_entry_continue(pos, root, base.list)	\
		if (!(cond)) {} else

#define fs_list_for_each_entry_reverse(pos, cond, root)		\
	list_for_each_entry_reverse(pos, root, base.list)	\
		if (!(cond)) {} else

#define fs_list_for_each_entry_continue_reverse(pos, cond, root)	\
	list_for_each_entry_continue_reverse(pos, root, base.list)	\
		if (!(cond)) {} else

#define fs_for_each_ft(pos, prio)			\
	fs_list_for_each_entry(pos, (pos)->base.type == FS_TYPE_FLOW_TABLE, \
			       &(prio)->objs)

#define fs_for_each_ft_reverse(pos, prio)			\
	fs_list_for_each_entry_reverse(pos,			\
				       (pos)->base.type == FS_TYPE_FLOW_TABLE, \
				       &(prio)->objs)

#define fs_for_each_ns(pos, prio)			\
	fs_list_for_each_entry(pos,			\
			       (pos)->base.type == FS_TYPE_NAMESPACE, \
			       &(prio)->objs)

#define fs_for_each_ns_or_ft_reverse(pos, prio)			\
	list_for_each_entry_reverse(pos, &(prio)->objs, list)		\
		if (!((pos)->type == FS_TYPE_NAMESPACE ||		\
		      (pos)->type == FS_TYPE_FLOW_TABLE)) {} else

#define fs_for_each_ns_or_ft(pos, prio)			\
	list_for_each_entry(pos, &(prio)->objs, list)		\
		if (!((pos)->type == FS_TYPE_NAMESPACE ||	\
		      (pos)->type == FS_TYPE_FLOW_TABLE)) {} else

#define fs_for_each_ns_or_ft_continue_reverse(pos, prio)		\
	list_for_each_entry_continue_reverse(pos, &(prio)->objs, list)	\
		if (!((pos)->type == FS_TYPE_NAMESPACE ||		\
		      (pos)->type == FS_TYPE_FLOW_TABLE)) {} else

#define fs_for_each_ns_or_ft_continue(pos, prio)			\
	list_for_each_entry_continue(pos, &(prio)->objs, list)		\
		if (!((pos)->type == FS_TYPE_NAMESPACE ||		\
		      (pos)->type == FS_TYPE_FLOW_TABLE)) {} else

#define fs_for_each_prio(pos, ns)			\
	fs_list_for_each_entry(pos, (pos)->base.type == FS_TYPE_PRIO, \
			       &(ns)->prios)

#define fs_for_each_prio_reverse(pos, ns)			\
	fs_list_for_each_entry_reverse(pos, (pos)->base.type == FS_TYPE_PRIO, \
				       &(ns)->prios)

#define fs_for_each_prio_continue(pos, ns)			\
	fs_list_for_each_entry_continue(pos, (pos)->base.type == FS_TYPE_PRIO, \
				       &(ns)->prios)

#define fs_for_each_prio_continue_reverse(pos, ns)			\
	fs_list_for_each_entry_continue_reverse(pos,			\
						(pos)->base.type == FS_TYPE_PRIO, \
						&(ns)->prios)

#define fs_for_each_fg(pos, ft)			\
	fs_list_for_each_entry(pos, (pos)->base.type == FS_TYPE_FLOW_GROUP, \
			       &(ft)->fgs)

#define fs_for_each_fte(pos, fg)			\
	fs_list_for_each_entry(pos, (pos)->base.type == FS_TYPE_FLOW_ENTRY, \
			       &(fg)->ftes)
#define fs_for_each_dst(pos, fte)			\
	fs_list_for_each_entry(pos, (pos)->base.type == FS_TYPE_FLOW_DEST, \
			       &(fte)->dests)

int mlx5_cmd_fs_create_ft(struct mlx5_core_dev *dev,
			  u16 vport,
			  enum fs_ft_type type, unsigned int level,
			  unsigned int log_size, unsigned int *table_id);

int mlx5_cmd_fs_destroy_ft(struct mlx5_core_dev *dev,
			   u16 vport,
			   enum fs_ft_type type, unsigned int table_id);

int mlx5_cmd_fs_create_fg(struct mlx5_core_dev *dev,
			  u32 *in,
			  u16 vport,
			  enum fs_ft_type type, unsigned int table_id,
			  unsigned int *group_id);

int mlx5_cmd_fs_destroy_fg(struct mlx5_core_dev *dev,
			   u16 vport,
			   enum fs_ft_type type, unsigned int table_id,
			   unsigned int group_id);


int mlx5_cmd_fs_set_fte(struct mlx5_core_dev *dev,
			u16 vport,
			enum fs_fte_status *fte_status,
			u32 *match_val,
			enum fs_ft_type type, unsigned int table_id,
			unsigned int index, unsigned int group_id,
			unsigned int flow_tag,
			unsigned short action, int dest_size,
			struct list_head *dests);  /* mlx5_flow_desination */

int mlx5_cmd_fs_delete_fte(struct mlx5_core_dev *dev,
			   u16 vport,
			   enum fs_fte_status *fte_status,
			   enum fs_ft_type type, unsigned int table_id,
			   unsigned int index);

int mlx5_cmd_update_root_ft(struct mlx5_core_dev *dev,
			    enum fs_ft_type type,
			    unsigned int id);

int mlx5_init_fs(struct mlx5_core_dev *dev);
void mlx5_cleanup_fs(struct mlx5_core_dev *dev);
#endif
