/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

#ifndef __UBIFS_DEBUG_H__
#define __UBIFS_DEBUG_H__

/* Checking helper functions */
typedef int (*dbg_leaf_callback)(struct ubifs_info *c,
				 struct ubifs_zbranch *zbr, void *priv);
typedef int (*dbg_znode_callback)(struct ubifs_info *c,
				  struct ubifs_znode *znode, void *priv);

#ifdef CONFIG_UBIFS_FS_DEBUG

/*
 * The UBIFS debugfs directory name pattern and maximum name length (3 for "ubi"
 * + 1 for "_" and plus 2x2 for 2 UBI numbers and 1 for the trailing zero byte.
 */
#define UBIFS_DFS_DIR_NAME "ubi%d_%d"
#define UBIFS_DFS_DIR_LEN  (3 + 1 + 2*2 + 1)

/**
 * ubifs_debug_info - per-FS debugging information.
 * @old_zroot: old index root - used by 'dbg_check_old_index()'
 * @old_zroot_level: old index root level - used by 'dbg_check_old_index()'
 * @old_zroot_sqnum: old index root sqnum - used by 'dbg_check_old_index()'
 *
 * @pc_happened: non-zero if an emulated power cut happened
 * @pc_delay: 0=>don't delay, 1=>delay a time, 2=>delay a number of calls
 * @pc_timeout: time in jiffies when delay of failure mode expires
 * @pc_cnt: current number of calls to failure mode I/O functions
 * @pc_cnt_max: number of calls by which to delay failure mode
 *
 * @chk_lpt_sz: used by LPT tree size checker
 * @chk_lpt_sz2: used by LPT tree size checker
 * @chk_lpt_wastage: used by LPT tree size checker
 * @chk_lpt_lebs: used by LPT tree size checker
 * @new_nhead_offs: used by LPT tree size checker
 * @new_ihead_lnum: used by debugging to check @c->ihead_lnum
 * @new_ihead_offs: used by debugging to check @c->ihead_offs
 *
 * @saved_lst: saved lprops statistics (used by 'dbg_save_space_info()')
 * @saved_bi: saved budgeting information
 * @saved_free: saved amount of free space
 * @saved_idx_gc_cnt: saved value of @c->idx_gc_cnt
 *
 * @chk_gen: if general extra checks are enabled
 * @chk_index: if index xtra checks are enabled
 * @chk_orph: if orphans extra checks are enabled
 * @chk_lprops: if lprops extra checks are enabled
 * @chk_fs: if UBIFS contents extra checks are enabled
 * @tst_rcvry: if UBIFS recovery testing mode enabled
 *
 * @dfs_dir_name: name of debugfs directory containing this file-system's files
 * @dfs_dir: direntry object of the file-system debugfs directory
 * @dfs_dump_lprops: "dump lprops" debugfs knob
 * @dfs_dump_budg: "dump budgeting information" debugfs knob
 * @dfs_dump_tnc: "dump TNC" debugfs knob
 * @dfs_chk_gen: debugfs knob to enable UBIFS general extra checks
 * @dfs_chk_index: debugfs knob to enable UBIFS index extra checks
 * @dfs_chk_orph: debugfs knob to enable UBIFS orphans extra checks
 * @dfs_chk_lprops: debugfs knob to enable UBIFS LEP properties extra checks
 * @dfs_chk_fs: debugfs knob to enable UBIFS contents extra checks
 * @dfs_tst_rcvry: debugfs knob to enable UBIFS recovery testing
 */
struct ubifs_debug_info {
	struct ubifs_zbranch old_zroot;
	int old_zroot_level;
	unsigned long long old_zroot_sqnum;

	int pc_happened;
	int pc_delay;
	unsigned long pc_timeout;
	unsigned int pc_cnt;
	unsigned int pc_cnt_max;

	long long chk_lpt_sz;
	long long chk_lpt_sz2;
	long long chk_lpt_wastage;
	int chk_lpt_lebs;
	int new_nhead_offs;
	int new_ihead_lnum;
	int new_ihead_offs;

	struct ubifs_lp_stats saved_lst;
	struct ubifs_budg_info saved_bi;
	long long saved_free;
	int saved_idx_gc_cnt;

	unsigned int chk_gen:1;
	unsigned int chk_index:1;
	unsigned int chk_orph:1;
	unsigned int chk_lprops:1;
	unsigned int chk_fs:1;
	unsigned int tst_rcvry:1;

	char dfs_dir_name[UBIFS_DFS_DIR_LEN + 1];
	struct dentry *dfs_dir;
	struct dentry *dfs_dump_lprops;
	struct dentry *dfs_dump_budg;
	struct dentry *dfs_dump_tnc;
	struct dentry *dfs_chk_gen;
	struct dentry *dfs_chk_index;
	struct dentry *dfs_chk_orph;
	struct dentry *dfs_chk_lprops;
	struct dentry *dfs_chk_fs;
	struct dentry *dfs_tst_rcvry;
};

/**
 * ubifs_global_debug_info - global (not per-FS) UBIFS debugging information.
 *
 * @chk_gen: if general extra checks are enabled
 * @chk_index: if index xtra checks are enabled
 * @chk_orph: if orphans extra checks are enabled
 * @chk_lprops: if lprops extra checks are enabled
 * @chk_fs: if UBIFS contents extra checks are enabled
 * @tst_rcvry: if UBIFS recovery testing mode enabled
 */
struct ubifs_global_debug_info {
	unsigned int chk_gen:1;
	unsigned int chk_index:1;
	unsigned int chk_orph:1;
	unsigned int chk_lprops:1;
	unsigned int chk_fs:1;
	unsigned int tst_rcvry:1;
};

#define ubifs_assert(expr) do {                                                \
	if (unlikely(!(expr))) {                                               \
		printk(KERN_CRIT "UBIFS assert failed in %s at %u (pid %d)\n", \
		       __func__, __LINE__, current->pid);                      \
		dbg_dump_stack();                                              \
	}                                                                      \
} while (0)

#define ubifs_assert_cmt_locked(c) do {                                        \
	if (unlikely(down_write_trylock(&(c)->commit_sem))) {                  \
		up_write(&(c)->commit_sem);                                    \
		printk(KERN_CRIT "commit lock is not locked!\n");              \
		ubifs_assert(0);                                               \
	}                                                                      \
} while (0)

#define dbg_dump_stack() dump_stack()

#define dbg_err(fmt, ...) do {                                                 \
	spin_lock(&dbg_lock);                                                  \
	ubifs_err(fmt, ##__VA_ARGS__);                                         \
	spin_unlock(&dbg_lock);                                                \
} while (0)

const char *dbg_key_str0(const struct ubifs_info *c,
			 const union ubifs_key *key);
const char *dbg_key_str1(const struct ubifs_info *c,
			 const union ubifs_key *key);

/*
 * TODO: these macros are now broken because there is no locking around them
 * and we use a global buffer for the key string. This means that in case of
 * concurrent execution we will end up with incorrect and messy key strings.
 */
#define DBGKEY(key) dbg_key_str0(c, (key))
#define DBGKEY1(key) dbg_key_str1(c, (key))

extern spinlock_t dbg_lock;

#define ubifs_dbg_msg(type, fmt, ...) \
	pr_debug("UBIFS DBG " type ": " fmt "\n", ##__VA_ARGS__)

/* Just a debugging messages not related to any specific UBIFS subsystem */
#define dbg_msg(fmt, ...)                                                     \
	printk(KERN_DEBUG "UBIFS DBG (pid %d): %s: " fmt "\n", current->pid,  \
	       __func__, ##__VA_ARGS__)

/* General messages */
#define dbg_gen(fmt, ...)   ubifs_dbg_msg("gen", fmt, ##__VA_ARGS__)
/* Additional journal messages */
#define dbg_jnl(fmt, ...)   ubifs_dbg_msg("jnl", fmt, ##__VA_ARGS__)
/* Additional TNC messages */
#define dbg_tnc(fmt, ...)   ubifs_dbg_msg("tnc", fmt, ##__VA_ARGS__)
/* Additional lprops messages */
#define dbg_lp(fmt, ...)    ubifs_dbg_msg("lp", fmt, ##__VA_ARGS__)
/* Additional LEB find messages */
#define dbg_find(fmt, ...)  ubifs_dbg_msg("find", fmt, ##__VA_ARGS__)
/* Additional mount messages */
#define dbg_mnt(fmt, ...)   ubifs_dbg_msg("mnt", fmt, ##__VA_ARGS__)
/* Additional I/O messages */
#define dbg_io(fmt, ...)    ubifs_dbg_msg("io", fmt, ##__VA_ARGS__)
/* Additional commit messages */
#define dbg_cmt(fmt, ...)   ubifs_dbg_msg("cmt", fmt, ##__VA_ARGS__)
/* Additional budgeting messages */
#define dbg_budg(fmt, ...)  ubifs_dbg_msg("budg", fmt, ##__VA_ARGS__)
/* Additional log messages */
#define dbg_log(fmt, ...)   ubifs_dbg_msg("log", fmt, ##__VA_ARGS__)
/* Additional gc messages */
#define dbg_gc(fmt, ...)    ubifs_dbg_msg("gc", fmt, ##__VA_ARGS__)
/* Additional scan messages */
#define dbg_scan(fmt, ...)  ubifs_dbg_msg("scan", fmt, ##__VA_ARGS__)
/* Additional recovery messages */
#define dbg_rcvry(fmt, ...) ubifs_dbg_msg("rcvry", fmt, ##__VA_ARGS__)

extern struct ubifs_global_debug_info ubifs_dbg;

static inline int dbg_is_chk_gen(const struct ubifs_info *c)
{
	return !!(ubifs_dbg.chk_gen || c->dbg->chk_gen);
}
static inline int dbg_is_chk_index(const struct ubifs_info *c)
{
	return !!(ubifs_dbg.chk_index || c->dbg->chk_index);
}
static inline int dbg_is_chk_orph(const struct ubifs_info *c)
{
	return !!(ubifs_dbg.chk_orph || c->dbg->chk_orph);
}
static inline int dbg_is_chk_lprops(const struct ubifs_info *c)
{
	return !!(ubifs_dbg.chk_lprops || c->dbg->chk_lprops);
}
static inline int dbg_is_chk_fs(const struct ubifs_info *c)
{
	return !!(ubifs_dbg.chk_fs || c->dbg->chk_fs);
}
static inline int dbg_is_tst_rcvry(const struct ubifs_info *c)
{
	return !!(ubifs_dbg.tst_rcvry || c->dbg->tst_rcvry);
}
static inline int dbg_is_power_cut(const struct ubifs_info *c)
{
	return !!c->dbg->pc_happened;
}

int ubifs_debugging_init(struct ubifs_info *c);
void ubifs_debugging_exit(struct ubifs_info *c);

/* Dump functions */
const char *dbg_ntype(int type);
const char *dbg_cstate(int cmt_state);
const char *dbg_jhead(int jhead);
const char *dbg_get_key_dump(const struct ubifs_info *c,
			     const union ubifs_key *key);
void dbg_dump_inode(struct ubifs_info *c, const struct inode *inode);
void dbg_dump_node(const struct ubifs_info *c, const void *node);
void dbg_dump_lpt_node(const struct ubifs_info *c, void *node, int lnum,
		       int offs);
void dbg_dump_budget_req(const struct ubifs_budget_req *req);
void dbg_dump_lstats(const struct ubifs_lp_stats *lst);
void dbg_dump_budg(struct ubifs_info *c, const struct ubifs_budg_info *bi);
void dbg_dump_lprop(const struct ubifs_info *c, const struct ubifs_lprops *lp);
void dbg_dump_lprops(struct ubifs_info *c);
void dbg_dump_lpt_info(struct ubifs_info *c);
void dbg_dump_leb(const struct ubifs_info *c, int lnum);
void dbg_dump_sleb(const struct ubifs_info *c,
		   const struct ubifs_scan_leb *sleb, int offs);
void dbg_dump_znode(const struct ubifs_info *c,
		    const struct ubifs_znode *znode);
void dbg_dump_heap(struct ubifs_info *c, struct ubifs_lpt_heap *heap, int cat);
void dbg_dump_pnode(struct ubifs_info *c, struct ubifs_pnode *pnode,
		    struct ubifs_nnode *parent, int iip);
void dbg_dump_tnc(struct ubifs_info *c);
void dbg_dump_index(struct ubifs_info *c);
void dbg_dump_lpt_lebs(const struct ubifs_info *c);

int dbg_walk_index(struct ubifs_info *c, dbg_leaf_callback leaf_cb,
		   dbg_znode_callback znode_cb, void *priv);

/* Checking functions */
void dbg_save_space_info(struct ubifs_info *c);
int dbg_check_space_info(struct ubifs_info *c);
int dbg_check_lprops(struct ubifs_info *c);
int dbg_old_index_check_init(struct ubifs_info *c, struct ubifs_zbranch *zroot);
int dbg_check_old_index(struct ubifs_info *c, struct ubifs_zbranch *zroot);
int dbg_check_cats(struct ubifs_info *c);
int dbg_check_ltab(struct ubifs_info *c);
int dbg_chk_lpt_free_spc(struct ubifs_info *c);
int dbg_chk_lpt_sz(struct ubifs_info *c, int action, int len);
int dbg_check_synced_i_size(const struct ubifs_info *c, struct inode *inode);
int dbg_check_dir(struct ubifs_info *c, const struct inode *dir);
int dbg_check_tnc(struct ubifs_info *c, int extra);
int dbg_check_idx_size(struct ubifs_info *c, long long idx_size);
int dbg_check_filesystem(struct ubifs_info *c);
void dbg_check_heap(struct ubifs_info *c, struct ubifs_lpt_heap *heap, int cat,
		    int add_pos);
int dbg_check_lpt_nodes(struct ubifs_info *c, struct ubifs_cnode *cnode,
			int row, int col);
int dbg_check_inode_size(struct ubifs_info *c, const struct inode *inode,
			 loff_t size);
int dbg_check_data_nodes_order(struct ubifs_info *c, struct list_head *head);
int dbg_check_nondata_nodes_order(struct ubifs_info *c, struct list_head *head);

int dbg_leb_write(struct ubifs_info *c, int lnum, const void *buf, int offs,
		  int len, int dtype);
int dbg_leb_change(struct ubifs_info *c, int lnum, const void *buf, int len,
		   int dtype);
int dbg_leb_unmap(struct ubifs_info *c, int lnum);
int dbg_leb_map(struct ubifs_info *c, int lnum, int dtype);

/* Debugfs-related stuff */
int dbg_debugfs_init(void);
void dbg_debugfs_exit(void);
int dbg_debugfs_init_fs(struct ubifs_info *c);
void dbg_debugfs_exit_fs(struct ubifs_info *c);

#else /* !CONFIG_UBIFS_FS_DEBUG */

/* Use "if (0)" to make compiler check arguments even if debugging is off */
#define ubifs_assert(expr)  do {                                               \
	if (0)                                                                 \
		printk(KERN_CRIT "UBIFS assert failed in %s at %u (pid %d)\n", \
		       __func__, __LINE__, current->pid);                      \
} while (0)

#define dbg_err(fmt, ...)   do {                   \
	if (0)                                     \
		ubifs_err(fmt, ##__VA_ARGS__);     \
} while (0)

#define DBGKEY(key)  ((char *)(key))
#define DBGKEY1(key) ((char *)(key))

#define ubifs_dbg_msg(fmt, ...) do {                        \
	if (0)                                              \
		printk(KERN_DEBUG fmt "\n", ##__VA_ARGS__); \
} while (0)

#define dbg_dump_stack()
#define ubifs_assert_cmt_locked(c)

#define dbg_msg(fmt, ...)   ubifs_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_gen(fmt, ...)   ubifs_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_jnl(fmt, ...)   ubifs_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_tnc(fmt, ...)   ubifs_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_lp(fmt, ...)    ubifs_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_find(fmt, ...)  ubifs_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_mnt(fmt, ...)   ubifs_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_io(fmt, ...)    ubifs_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_cmt(fmt, ...)   ubifs_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_budg(fmt, ...)  ubifs_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_log(fmt, ...)   ubifs_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_gc(fmt, ...)    ubifs_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_scan(fmt, ...)  ubifs_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_rcvry(fmt, ...) ubifs_dbg_msg(fmt, ##__VA_ARGS__)

static inline int ubifs_debugging_init(struct ubifs_info *c)      { return 0; }
static inline void ubifs_debugging_exit(struct ubifs_info *c)     { return; }
static inline const char *dbg_ntype(int type)                     { return ""; }
static inline const char *dbg_cstate(int cmt_state)               { return ""; }
static inline const char *dbg_jhead(int jhead)                    { return ""; }
static inline const char *
dbg_get_key_dump(const struct ubifs_info *c,
		 const union ubifs_key *key)                      { return ""; }
static inline void dbg_dump_inode(struct ubifs_info *c,
				  const struct inode *inode)      { return; }
static inline void dbg_dump_node(const struct ubifs_info *c,
				 const void *node)                { return; }
static inline void dbg_dump_lpt_node(const struct ubifs_info *c,
				     void *node, int lnum,
				     int offs)                    { return; }
static inline void
dbg_dump_budget_req(const struct ubifs_budget_req *req)           { return; }
static inline void
dbg_dump_lstats(const struct ubifs_lp_stats *lst)                 { return; }
static inline void
dbg_dump_budg(struct ubifs_info *c,
	      const struct ubifs_budg_info *bi)                   { return; }
static inline void dbg_dump_lprop(const struct ubifs_info *c,
				  const struct ubifs_lprops *lp)  { return; }
static inline void dbg_dump_lprops(struct ubifs_info *c)          { return; }
static inline void dbg_dump_lpt_info(struct ubifs_info *c)        { return; }
static inline void dbg_dump_leb(const struct ubifs_info *c,
				int lnum)                         { return; }
static inline void
dbg_dump_sleb(const struct ubifs_info *c,
	      const struct ubifs_scan_leb *sleb, int offs)        { return; }
static inline void
dbg_dump_znode(const struct ubifs_info *c,
	       const struct ubifs_znode *znode)                   { return; }
static inline void dbg_dump_heap(struct ubifs_info *c,
				 struct ubifs_lpt_heap *heap,
				 int cat)                         { return; }
static inline void dbg_dump_pnode(struct ubifs_info *c,
				  struct ubifs_pnode *pnode,
				  struct ubifs_nnode *parent,
				  int iip)                        { return; }
static inline void dbg_dump_tnc(struct ubifs_info *c)             { return; }
static inline void dbg_dump_index(struct ubifs_info *c)           { return; }
static inline void dbg_dump_lpt_lebs(const struct ubifs_info *c)  { return; }

static inline int dbg_walk_index(struct ubifs_info *c,
				 dbg_leaf_callback leaf_cb,
				 dbg_znode_callback znode_cb,
				 void *priv)                      { return 0; }
static inline void dbg_save_space_info(struct ubifs_info *c)      { return; }
static inline int dbg_check_space_info(struct ubifs_info *c)      { return 0; }
static inline int dbg_check_lprops(struct ubifs_info *c)          { return 0; }
static inline int
dbg_old_index_check_init(struct ubifs_info *c,
			 struct ubifs_zbranch *zroot)             { return 0; }
static inline int
dbg_check_old_index(struct ubifs_info *c,
		    struct ubifs_zbranch *zroot)                  { return 0; }
static inline int dbg_check_cats(struct ubifs_info *c)            { return 0; }
static inline int dbg_check_ltab(struct ubifs_info *c)            { return 0; }
static inline int dbg_chk_lpt_free_spc(struct ubifs_info *c)      { return 0; }
static inline int dbg_chk_lpt_sz(struct ubifs_info *c,
				 int action, int len)             { return 0; }
static inline int
dbg_check_synced_i_size(const struct ubifs_info *c,
			struct inode *inode)                      { return 0; }
static inline int dbg_check_dir(struct ubifs_info *c,
				const struct inode *dir)          { return 0; }
static inline int dbg_check_tnc(struct ubifs_info *c, int extra)  { return 0; }
static inline int dbg_check_idx_size(struct ubifs_info *c,
				     long long idx_size)          { return 0; }
static inline int dbg_check_filesystem(struct ubifs_info *c)      { return 0; }
static inline void dbg_check_heap(struct ubifs_info *c,
				  struct ubifs_lpt_heap *heap,
				  int cat, int add_pos)           { return; }
static inline int dbg_check_lpt_nodes(struct ubifs_info *c,
	struct ubifs_cnode *cnode, int row, int col)              { return 0; }
static inline int dbg_check_inode_size(struct ubifs_info *c,
				       const struct inode *inode,
				       loff_t size)               { return 0; }
static inline int
dbg_check_data_nodes_order(struct ubifs_info *c,
			   struct list_head *head)                { return 0; }
static inline int
dbg_check_nondata_nodes_order(struct ubifs_info *c,
			      struct list_head *head)             { return 0; }

static inline int dbg_leb_write(struct ubifs_info *c, int lnum,
				const void *buf, int offset,
				int len, int dtype)               { return 0; }
static inline int dbg_leb_change(struct ubifs_info *c, int lnum,
				 const void *buf, int len,
				 int dtype)                       { return 0; }
static inline int dbg_leb_unmap(struct ubifs_info *c, int lnum)   { return 0; }
static inline int dbg_leb_map(struct ubifs_info *c, int lnum,
			      int dtype)                          { return 0; }

static inline int dbg_is_chk_gen(const struct ubifs_info *c)      { return 0; }
static inline int dbg_is_chk_index(const struct ubifs_info *c)    { return 0; }
static inline int dbg_is_chk_orph(const struct ubifs_info *c)     { return 0; }
static inline int dbg_is_chk_lprops(const struct ubifs_info *c)   { return 0; }
static inline int dbg_is_chk_fs(const struct ubifs_info *c)       { return 0; }
static inline int dbg_is_tst_rcvry(const struct ubifs_info *c)    { return 0; }
static inline int dbg_is_power_cut(const struct ubifs_info *c)    { return 0; }

static inline int dbg_debugfs_init(void)                          { return 0; }
static inline void dbg_debugfs_exit(void)                         { return; }
static inline int dbg_debugfs_init_fs(struct ubifs_info *c)       { return 0; }
static inline int dbg_debugfs_exit_fs(struct ubifs_info *c)       { return 0; }

#endif /* !CONFIG_UBIFS_FS_DEBUG */
#endif /* !__UBIFS_DEBUG_H__ */
