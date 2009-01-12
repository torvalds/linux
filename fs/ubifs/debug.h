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

#ifdef CONFIG_UBIFS_FS_DEBUG

/**
 * ubifs_debug_info - per-FS debugging information.
 * @buf: a buffer of LEB size, used for various purposes
 * @old_zroot: old index root - used by 'dbg_check_old_index()'
 * @old_zroot_level: old index root level - used by 'dbg_check_old_index()'
 * @old_zroot_sqnum: old index root sqnum - used by 'dbg_check_old_index()'
 * @failure_mode: failure mode for recovery testing
 * @fail_delay: 0=>don't delay, 1=>delay a time, 2=>delay a number of calls
 * @fail_timeout: time in jiffies when delay of failure mode expires
 * @fail_cnt: current number of calls to failure mode I/O functions
 * @fail_cnt_max: number of calls by which to delay failure mode
 * @chk_lpt_sz: used by LPT tree size checker
 * @chk_lpt_sz2: used by LPT tree size checker
 * @chk_lpt_wastage: used by LPT tree size checker
 * @chk_lpt_lebs: used by LPT tree size checker
 * @new_nhead_offs: used by LPT tree size checker
 * @new_ihead_lnum: used by debugging to check ihead_lnum
 * @new_ihead_offs: used by debugging to check ihead_offs
 *
 * debugfs_dir_name: name of debugfs directory containing this file-system's
 *                   files
 * debugfs_dir: direntry object of the file-system debugfs directory
 * dump_lprops: "dump lprops" debugfs knob
 * dump_budg: "dump budgeting information" debugfs knob
 * dump_tnc: "dump TNC" debugfs knob
 */
struct ubifs_debug_info {
	void *buf;
	struct ubifs_zbranch old_zroot;
	int old_zroot_level;
	unsigned long long old_zroot_sqnum;
	int failure_mode;
	int fail_delay;
	unsigned long fail_timeout;
	unsigned int fail_cnt;
	unsigned int fail_cnt_max;
	long long chk_lpt_sz;
	long long chk_lpt_sz2;
	long long chk_lpt_wastage;
	int chk_lpt_lebs;
	int new_nhead_offs;
	int new_ihead_lnum;
	int new_ihead_offs;

	char debugfs_dir_name[100];
	struct dentry *debugfs_dir;
	struct dentry *dump_lprops;
	struct dentry *dump_budg;
	struct dentry *dump_tnc;
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

#define dbg_dump_stack() do {                                                  \
	if (!dbg_failure_mode)                                                 \
		dump_stack();                                                  \
} while (0)

/* Generic debugging messages */
#define dbg_msg(fmt, ...) do {                                                 \
	spin_lock(&dbg_lock);                                                  \
	printk(KERN_DEBUG "UBIFS DBG (pid %d): %s: " fmt "\n", current->pid,   \
	       __func__, ##__VA_ARGS__);                                       \
	spin_unlock(&dbg_lock);                                                \
} while (0)

#define dbg_do_msg(typ, fmt, ...) do {                                         \
	if (ubifs_msg_flags & typ)                                             \
		dbg_msg(fmt, ##__VA_ARGS__);                                   \
} while (0)

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
 * DBGKEY macros require @dbg_lock to be held, which it is in the dbg message
 * macros.
 */
#define DBGKEY(key) dbg_key_str0(c, (key))
#define DBGKEY1(key) dbg_key_str1(c, (key))

/* General messages */
#define dbg_gen(fmt, ...)   dbg_do_msg(UBIFS_MSG_GEN, fmt, ##__VA_ARGS__)

/* Additional journal messages */
#define dbg_jnl(fmt, ...)   dbg_do_msg(UBIFS_MSG_JNL, fmt, ##__VA_ARGS__)

/* Additional TNC messages */
#define dbg_tnc(fmt, ...)   dbg_do_msg(UBIFS_MSG_TNC, fmt, ##__VA_ARGS__)

/* Additional lprops messages */
#define dbg_lp(fmt, ...)    dbg_do_msg(UBIFS_MSG_LP, fmt, ##__VA_ARGS__)

/* Additional LEB find messages */
#define dbg_find(fmt, ...)  dbg_do_msg(UBIFS_MSG_FIND, fmt, ##__VA_ARGS__)

/* Additional mount messages */
#define dbg_mnt(fmt, ...)   dbg_do_msg(UBIFS_MSG_MNT, fmt, ##__VA_ARGS__)

/* Additional I/O messages */
#define dbg_io(fmt, ...)    dbg_do_msg(UBIFS_MSG_IO, fmt, ##__VA_ARGS__)

/* Additional commit messages */
#define dbg_cmt(fmt, ...)   dbg_do_msg(UBIFS_MSG_CMT, fmt, ##__VA_ARGS__)

/* Additional budgeting messages */
#define dbg_budg(fmt, ...)  dbg_do_msg(UBIFS_MSG_BUDG, fmt, ##__VA_ARGS__)

/* Additional log messages */
#define dbg_log(fmt, ...)   dbg_do_msg(UBIFS_MSG_LOG, fmt, ##__VA_ARGS__)

/* Additional gc messages */
#define dbg_gc(fmt, ...)    dbg_do_msg(UBIFS_MSG_GC, fmt, ##__VA_ARGS__)

/* Additional scan messages */
#define dbg_scan(fmt, ...)  dbg_do_msg(UBIFS_MSG_SCAN, fmt, ##__VA_ARGS__)

/* Additional recovery messages */
#define dbg_rcvry(fmt, ...) dbg_do_msg(UBIFS_MSG_RCVRY, fmt, ##__VA_ARGS__)

/*
 * Debugging message type flags (must match msg_type_names in debug.c).
 *
 * UBIFS_MSG_GEN: general messages
 * UBIFS_MSG_JNL: journal messages
 * UBIFS_MSG_MNT: mount messages
 * UBIFS_MSG_CMT: commit messages
 * UBIFS_MSG_FIND: LEB find messages
 * UBIFS_MSG_BUDG: budgeting messages
 * UBIFS_MSG_GC: garbage collection messages
 * UBIFS_MSG_TNC: TNC messages
 * UBIFS_MSG_LP: lprops messages
 * UBIFS_MSG_IO: I/O messages
 * UBIFS_MSG_LOG: log messages
 * UBIFS_MSG_SCAN: scan messages
 * UBIFS_MSG_RCVRY: recovery messages
 */
enum {
	UBIFS_MSG_GEN   = 0x1,
	UBIFS_MSG_JNL   = 0x2,
	UBIFS_MSG_MNT   = 0x4,
	UBIFS_MSG_CMT   = 0x8,
	UBIFS_MSG_FIND  = 0x10,
	UBIFS_MSG_BUDG  = 0x20,
	UBIFS_MSG_GC    = 0x40,
	UBIFS_MSG_TNC   = 0x80,
	UBIFS_MSG_LP    = 0x100,
	UBIFS_MSG_IO    = 0x200,
	UBIFS_MSG_LOG   = 0x400,
	UBIFS_MSG_SCAN  = 0x800,
	UBIFS_MSG_RCVRY = 0x1000,
};

/* Debugging message type flags for each default debug message level */
#define UBIFS_MSG_LVL_0 0
#define UBIFS_MSG_LVL_1 0x1
#define UBIFS_MSG_LVL_2 0x7f
#define UBIFS_MSG_LVL_3 0xffff

/*
 * Debugging check flags (must match chk_names in debug.c).
 *
 * UBIFS_CHK_GEN: general checks
 * UBIFS_CHK_TNC: check TNC
 * UBIFS_CHK_IDX_SZ: check index size
 * UBIFS_CHK_ORPH: check orphans
 * UBIFS_CHK_OLD_IDX: check the old index
 * UBIFS_CHK_LPROPS: check lprops
 * UBIFS_CHK_FS: check the file-system
 */
enum {
	UBIFS_CHK_GEN     = 0x1,
	UBIFS_CHK_TNC     = 0x2,
	UBIFS_CHK_IDX_SZ  = 0x4,
	UBIFS_CHK_ORPH    = 0x8,
	UBIFS_CHK_OLD_IDX = 0x10,
	UBIFS_CHK_LPROPS  = 0x20,
	UBIFS_CHK_FS      = 0x40,
};

/*
 * Special testing flags (must match tst_names in debug.c).
 *
 * UBIFS_TST_FORCE_IN_THE_GAPS: force the use of in-the-gaps method
 * UBIFS_TST_RCVRY: failure mode for recovery testing
 */
enum {
	UBIFS_TST_FORCE_IN_THE_GAPS = 0x2,
	UBIFS_TST_RCVRY             = 0x4,
};

#if CONFIG_UBIFS_FS_DEBUG_MSG_LVL == 1
#define UBIFS_MSG_FLAGS_DEFAULT UBIFS_MSG_LVL_1
#elif CONFIG_UBIFS_FS_DEBUG_MSG_LVL == 2
#define UBIFS_MSG_FLAGS_DEFAULT UBIFS_MSG_LVL_2
#elif CONFIG_UBIFS_FS_DEBUG_MSG_LVL == 3
#define UBIFS_MSG_FLAGS_DEFAULT UBIFS_MSG_LVL_3
#else
#define UBIFS_MSG_FLAGS_DEFAULT UBIFS_MSG_LVL_0
#endif

#ifdef CONFIG_UBIFS_FS_DEBUG_CHKS
#define UBIFS_CHK_FLAGS_DEFAULT 0xffffffff
#else
#define UBIFS_CHK_FLAGS_DEFAULT 0
#endif

extern spinlock_t dbg_lock;

extern unsigned int ubifs_msg_flags;
extern unsigned int ubifs_chk_flags;
extern unsigned int ubifs_tst_flags;

int ubifs_debugging_init(struct ubifs_info *c);
void ubifs_debugging_exit(struct ubifs_info *c);

/* Dump functions */
const char *dbg_ntype(int type);
const char *dbg_cstate(int cmt_state);
const char *dbg_get_key_dump(const struct ubifs_info *c,
			     const union ubifs_key *key);
void dbg_dump_inode(const struct ubifs_info *c, const struct inode *inode);
void dbg_dump_node(const struct ubifs_info *c, const void *node);
void dbg_dump_lpt_node(const struct ubifs_info *c, void *node, int lnum,
		       int offs);
void dbg_dump_budget_req(const struct ubifs_budget_req *req);
void dbg_dump_lstats(const struct ubifs_lp_stats *lst);
void dbg_dump_budg(struct ubifs_info *c);
void dbg_dump_lprop(const struct ubifs_info *c, const struct ubifs_lprops *lp);
void dbg_dump_lprops(struct ubifs_info *c);
void dbg_dump_lpt_info(struct ubifs_info *c);
void dbg_dump_leb(const struct ubifs_info *c, int lnum);
void dbg_dump_znode(const struct ubifs_info *c,
		    const struct ubifs_znode *znode);
void dbg_dump_heap(struct ubifs_info *c, struct ubifs_lpt_heap *heap, int cat);
void dbg_dump_pnode(struct ubifs_info *c, struct ubifs_pnode *pnode,
		    struct ubifs_nnode *parent, int iip);
void dbg_dump_tnc(struct ubifs_info *c);
void dbg_dump_index(struct ubifs_info *c);
void dbg_dump_lpt_lebs(const struct ubifs_info *c);

/* Checking helper functions */
typedef int (*dbg_leaf_callback)(struct ubifs_info *c,
				 struct ubifs_zbranch *zbr, void *priv);
typedef int (*dbg_znode_callback)(struct ubifs_info *c,
				  struct ubifs_znode *znode, void *priv);
int dbg_walk_index(struct ubifs_info *c, dbg_leaf_callback leaf_cb,
		   dbg_znode_callback znode_cb, void *priv);

/* Checking functions */

int dbg_check_lprops(struct ubifs_info *c);
int dbg_old_index_check_init(struct ubifs_info *c, struct ubifs_zbranch *zroot);
int dbg_check_old_index(struct ubifs_info *c, struct ubifs_zbranch *zroot);
int dbg_check_cats(struct ubifs_info *c);
int dbg_check_ltab(struct ubifs_info *c);
int dbg_chk_lpt_free_spc(struct ubifs_info *c);
int dbg_chk_lpt_sz(struct ubifs_info *c, int action, int len);
int dbg_check_synced_i_size(struct inode *inode);
int dbg_check_dir_size(struct ubifs_info *c, const struct inode *dir);
int dbg_check_tnc(struct ubifs_info *c, int extra);
int dbg_check_idx_size(struct ubifs_info *c, long long idx_size);
int dbg_check_filesystem(struct ubifs_info *c);
void dbg_check_heap(struct ubifs_info *c, struct ubifs_lpt_heap *heap, int cat,
		    int add_pos);
int dbg_check_lprops(struct ubifs_info *c);
int dbg_check_lpt_nodes(struct ubifs_info *c, struct ubifs_cnode *cnode,
			int row, int col);

/* Force the use of in-the-gaps method for testing */

#define dbg_force_in_the_gaps_enabled \
	(ubifs_tst_flags & UBIFS_TST_FORCE_IN_THE_GAPS)

int dbg_force_in_the_gaps(void);

/* Failure mode for recovery testing */

#define dbg_failure_mode (ubifs_tst_flags & UBIFS_TST_RCVRY)

#ifndef UBIFS_DBG_PRESERVE_UBI

#define ubi_leb_read   dbg_leb_read
#define ubi_leb_write  dbg_leb_write
#define ubi_leb_change dbg_leb_change
#define ubi_leb_erase  dbg_leb_erase
#define ubi_leb_unmap  dbg_leb_unmap
#define ubi_is_mapped  dbg_is_mapped
#define ubi_leb_map    dbg_leb_map

#endif

int dbg_leb_read(struct ubi_volume_desc *desc, int lnum, char *buf, int offset,
		 int len, int check);
int dbg_leb_write(struct ubi_volume_desc *desc, int lnum, const void *buf,
		  int offset, int len, int dtype);
int dbg_leb_change(struct ubi_volume_desc *desc, int lnum, const void *buf,
		   int len, int dtype);
int dbg_leb_erase(struct ubi_volume_desc *desc, int lnum);
int dbg_leb_unmap(struct ubi_volume_desc *desc, int lnum);
int dbg_is_mapped(struct ubi_volume_desc *desc, int lnum);
int dbg_leb_map(struct ubi_volume_desc *desc, int lnum, int dtype);

static inline int dbg_read(struct ubi_volume_desc *desc, int lnum, char *buf,
			   int offset, int len)
{
	return dbg_leb_read(desc, lnum, buf, offset, len, 0);
}

static inline int dbg_write(struct ubi_volume_desc *desc, int lnum,
			    const void *buf, int offset, int len)
{
	return dbg_leb_write(desc, lnum, buf, offset, len, UBI_UNKNOWN);
}

static inline int dbg_change(struct ubi_volume_desc *desc, int lnum,
				    const void *buf, int len)
{
	return dbg_leb_change(desc, lnum, buf, len, UBI_UNKNOWN);
}

/* Debugfs-related stuff */
int dbg_debugfs_init(void);
void dbg_debugfs_exit(void);
int dbg_debugfs_init_fs(struct ubifs_info *c);
void dbg_debugfs_exit_fs(struct ubifs_info *c);

#else /* !CONFIG_UBIFS_FS_DEBUG */

/* Use "if (0)" to make compiler check arguments even if debugging is off */
#define ubifs_assert(expr)  do {                                               \
	if (0 && (expr))                                                       \
		printk(KERN_CRIT "UBIFS assert failed in %s at %u (pid %d)\n", \
		       __func__, __LINE__, current->pid);                      \
} while (0)

#define dbg_err(fmt, ...)   do {                                               \
	if (0)                                                                 \
		ubifs_err(fmt, ##__VA_ARGS__);                                 \
} while (0)

#define dbg_msg(fmt, ...) do {                                                 \
	if (0)                                                                 \
		printk(KERN_DEBUG "UBIFS DBG (pid %d): %s: " fmt "\n",         \
		       current->pid, __func__, ##__VA_ARGS__);                 \
} while (0)

#define dbg_dump_stack()
#define ubifs_assert_cmt_locked(c)

#define dbg_gen(fmt, ...)   dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_jnl(fmt, ...)   dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_tnc(fmt, ...)   dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_lp(fmt, ...)    dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_find(fmt, ...)  dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_mnt(fmt, ...)   dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_io(fmt, ...)    dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_cmt(fmt, ...)   dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_budg(fmt, ...)  dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_log(fmt, ...)   dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_gc(fmt, ...)    dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_scan(fmt, ...)  dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_rcvry(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)

#define DBGKEY(key)  ((char *)(key))
#define DBGKEY1(key) ((char *)(key))

#define ubifs_debugging_init(c)                0
#define ubifs_debugging_exit(c)                ({})

#define dbg_ntype(type)                        ""
#define dbg_cstate(cmt_state)                  ""
#define dbg_get_key_dump(c, key)               ({})
#define dbg_dump_inode(c, inode)               ({})
#define dbg_dump_node(c, node)                 ({})
#define dbg_dump_lpt_node(c, node, lnum, offs) ({})
#define dbg_dump_budget_req(req)               ({})
#define dbg_dump_lstats(lst)                   ({})
#define dbg_dump_budg(c)                       ({})
#define dbg_dump_lprop(c, lp)                  ({})
#define dbg_dump_lprops(c)                     ({})
#define dbg_dump_lpt_info(c)                   ({})
#define dbg_dump_leb(c, lnum)                  ({})
#define dbg_dump_znode(c, znode)               ({})
#define dbg_dump_heap(c, heap, cat)            ({})
#define dbg_dump_pnode(c, pnode, parent, iip)  ({})
#define dbg_dump_tnc(c)                        ({})
#define dbg_dump_index(c)                      ({})
#define dbg_dump_lpt_lebs(c)                   ({})

#define dbg_walk_index(c, leaf_cb, znode_cb, priv) 0
#define dbg_old_index_check_init(c, zroot)         0
#define dbg_check_old_index(c, zroot)              0
#define dbg_check_cats(c)                          0
#define dbg_check_ltab(c)                          0
#define dbg_chk_lpt_free_spc(c)                    0
#define dbg_chk_lpt_sz(c, action, len)             0
#define dbg_check_synced_i_size(inode)             0
#define dbg_check_dir_size(c, dir)                 0
#define dbg_check_tnc(c, x)                        0
#define dbg_check_idx_size(c, idx_size)            0
#define dbg_check_filesystem(c)                    0
#define dbg_check_heap(c, heap, cat, add_pos)      ({})
#define dbg_check_lprops(c)                        0
#define dbg_check_lpt_nodes(c, cnode, row, col)    0
#define dbg_force_in_the_gaps_enabled              0
#define dbg_force_in_the_gaps()                    0
#define dbg_failure_mode                           0

#define dbg_debugfs_init()                         0
#define dbg_debugfs_exit()
#define dbg_debugfs_init_fs(c)                     0
#define dbg_debugfs_exit_fs(c)                     0

#endif /* !CONFIG_UBIFS_FS_DEBUG */
#endif /* !__UBIFS_DEBUG_H__ */
