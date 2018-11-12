/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_OPTS_H
#define _BCACHEFS_OPTS_H

#include <linux/bug.h>
#include <linux/log2.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include "bcachefs_format.h"

extern const char * const bch2_error_actions[];
extern const char * const bch2_csum_types[];
extern const char * const bch2_compression_types[];
extern const char * const bch2_str_hash_types[];
extern const char * const bch2_data_types[];
extern const char * const bch2_cache_replacement_policies[];
extern const char * const bch2_cache_modes[];
extern const char * const bch2_dev_state[];

/*
 * Mount options; we also store defaults in the superblock.
 *
 * Also exposed via sysfs: if an option is writeable, and it's also stored in
 * the superblock, changing it via sysfs (currently? might change this) also
 * updates the superblock.
 *
 * We store options as signed integers, where -1 means undefined. This means we
 * can pass the mount options to bch2_fs_alloc() as a whole struct, and then only
 * apply the options from that struct that are defined.
 */

/* dummy option, for options that aren't stored in the superblock */
LE64_BITMASK(NO_SB_OPT,		struct bch_sb, flags[0], 0, 0);

enum opt_mode {
	OPT_INTERNAL,
	OPT_FORMAT,
	OPT_MOUNT,
	OPT_RUNTIME,
};

enum opt_type {
	BCH_OPT_BOOL,
	BCH_OPT_UINT,
	BCH_OPT_STR,
	BCH_OPT_FN,
};

/**
 * BCH_OPT(name, type, in mem type, mode, sb_opt)
 *
 * @name	- name of mount option, sysfs attribute, and struct bch_opts
 *		  member
 *
 * @mode	- when opt may be set
 *
 * @sb_option	- name of corresponding superblock option
 *
 * @type	- one of OPT_BOOL, OPT_UINT, OPT_STR
 */

/*
 * XXX: add fields for
 *  - default value
 *  - helptext
 */

#define BCH_OPTS()							\
	BCH_OPT(block_size,		u16,	OPT_FORMAT,		\
		OPT_UINT(1, 128),					\
		BCH_SB_BLOCK_SIZE,		8)			\
	BCH_OPT(btree_node_size,	u16,	OPT_FORMAT,		\
		OPT_UINT(1, 128),					\
		BCH_SB_BTREE_NODE_SIZE,		512)			\
	BCH_OPT(errors,			u8,	OPT_RUNTIME,		\
		OPT_STR(bch2_error_actions),				\
		BCH_SB_ERROR_ACTION,		BCH_ON_ERROR_RO)	\
	BCH_OPT(metadata_replicas,	u8,	OPT_RUNTIME,		\
		OPT_UINT(1, BCH_REPLICAS_MAX),				\
		BCH_SB_META_REPLICAS_WANT,	1)			\
	BCH_OPT(data_replicas,		u8,	OPT_RUNTIME,		\
		OPT_UINT(1, BCH_REPLICAS_MAX),				\
		BCH_SB_DATA_REPLICAS_WANT,	1)			\
	BCH_OPT(metadata_replicas_required, u8,	OPT_MOUNT,		\
		OPT_UINT(1, BCH_REPLICAS_MAX),				\
		BCH_SB_META_REPLICAS_REQ,	1)			\
	BCH_OPT(data_replicas_required, u8,	OPT_MOUNT,		\
		OPT_UINT(1, BCH_REPLICAS_MAX),				\
		BCH_SB_DATA_REPLICAS_REQ,	1)			\
	BCH_OPT(metadata_checksum,	u8,	OPT_RUNTIME,		\
		OPT_STR(bch2_csum_types),				\
		BCH_SB_META_CSUM_TYPE,		BCH_CSUM_OPT_CRC32C)	\
	BCH_OPT(data_checksum,		u8,	OPT_RUNTIME,		\
		OPT_STR(bch2_csum_types),				\
		BCH_SB_DATA_CSUM_TYPE,		BCH_CSUM_OPT_CRC32C)	\
	BCH_OPT(compression,		u8,	OPT_RUNTIME,		\
		OPT_STR(bch2_compression_types),			\
		BCH_SB_COMPRESSION_TYPE,	BCH_COMPRESSION_OPT_NONE)\
	BCH_OPT(background_compression,	u8,	OPT_RUNTIME,		\
		OPT_STR(bch2_compression_types),			\
		BCH_SB_BACKGROUND_COMPRESSION_TYPE,BCH_COMPRESSION_OPT_NONE)\
	BCH_OPT(str_hash,		u8,	OPT_RUNTIME,		\
		OPT_STR(bch2_str_hash_types),				\
		BCH_SB_STR_HASH_TYPE,		BCH_STR_HASH_SIPHASH)	\
	BCH_OPT(foreground_target,	u16,	OPT_RUNTIME,		\
		OPT_FN(bch2_opt_target),				\
		BCH_SB_FOREGROUND_TARGET,	0)			\
	BCH_OPT(background_target,	u16,	OPT_RUNTIME,		\
		OPT_FN(bch2_opt_target),				\
		BCH_SB_BACKGROUND_TARGET,	0)			\
	BCH_OPT(promote_target,		u16,	OPT_RUNTIME,		\
		OPT_FN(bch2_opt_target),				\
		BCH_SB_PROMOTE_TARGET,	0)				\
	BCH_OPT(inodes_32bit,		u8,	OPT_RUNTIME,		\
		OPT_BOOL(),						\
		BCH_SB_INODE_32BIT,		false)			\
	BCH_OPT(gc_reserve_percent,	u8,	OPT_RUNTIME,		\
		OPT_UINT(5, 21),					\
		BCH_SB_GC_RESERVE,		8)			\
	BCH_OPT(gc_reserve_bytes,	u64,	OPT_RUNTIME,		\
		OPT_UINT(0, U64_MAX),					\
		BCH_SB_GC_RESERVE_BYTES,	0)			\
	BCH_OPT(root_reserve_percent,	u8,	OPT_MOUNT,		\
		OPT_UINT(0, 100),					\
		BCH_SB_ROOT_RESERVE,		0)			\
	BCH_OPT(wide_macs,		u8,	OPT_RUNTIME,		\
		OPT_BOOL(),						\
		BCH_SB_128_BIT_MACS,		false)			\
	BCH_OPT(acl,			u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		BCH_SB_POSIX_ACL,		true)			\
	BCH_OPT(usrquota,		u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		BCH_SB_USRQUOTA,		false)			\
	BCH_OPT(grpquota,		u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		BCH_SB_GRPQUOTA,		false)			\
	BCH_OPT(prjquota,		u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		BCH_SB_PRJQUOTA,		false)			\
	BCH_OPT(degraded,		u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		NO_SB_OPT,			false)			\
	BCH_OPT(discard,		u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		NO_SB_OPT,			false)			\
	BCH_OPT(verbose_recovery,	u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		NO_SB_OPT,			false)			\
	BCH_OPT(verbose_init,		u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		NO_SB_OPT,			false)			\
	BCH_OPT(journal_flush_disabled, u8,	OPT_RUNTIME,		\
		OPT_BOOL(),						\
		NO_SB_OPT,			false)			\
	BCH_OPT(fsck,			u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		NO_SB_OPT,			true)			\
	BCH_OPT(fix_errors,		u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		NO_SB_OPT,			false)			\
	BCH_OPT(nochanges,		u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		NO_SB_OPT,			false)			\
	BCH_OPT(noreplay,		u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		NO_SB_OPT,			false)			\
	BCH_OPT(norecovery,		u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		NO_SB_OPT,			false)			\
	BCH_OPT(noexcl,			u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		NO_SB_OPT,			false)			\
	BCH_OPT(sb,			u64,	OPT_MOUNT,		\
		OPT_UINT(0, S64_MAX),					\
		NO_SB_OPT,			BCH_SB_SECTOR)		\
	BCH_OPT(read_only,		u8,	OPT_INTERNAL,		\
		OPT_BOOL(),						\
		NO_SB_OPT,			false)			\
	BCH_OPT(nostart,		u8,	OPT_INTERNAL,		\
		OPT_BOOL(),						\
		NO_SB_OPT,			false)			\
	BCH_OPT(no_data_io,		u8,	OPT_MOUNT,		\
		OPT_BOOL(),						\
		NO_SB_OPT,			false)

struct bch_opts {
#define BCH_OPT(_name, _bits, ...)	unsigned _name##_defined:1;
	BCH_OPTS()
#undef BCH_OPT

#define BCH_OPT(_name, _bits, ...)	_bits	_name;
	BCH_OPTS()
#undef BCH_OPT
};

static const struct bch_opts bch2_opts_default = {
#define BCH_OPT(_name, _bits, _mode, _type, _sb_opt, _default)		\
	._name##_defined = true,					\
	._name = _default,						\

	BCH_OPTS()
#undef BCH_OPT
};

#define opt_defined(_opts, _name)	((_opts)._name##_defined)

#define opt_get(_opts, _name)						\
	(opt_defined(_opts, _name) ? (_opts)._name : bch2_opts_default._name)

#define opt_set(_opts, _name, _v)					\
do {									\
	(_opts)._name##_defined = true;					\
	(_opts)._name = _v;						\
} while (0)

static inline struct bch_opts bch2_opts_empty(void)
{
	return (struct bch_opts) { 0 };
}

void bch2_opts_apply(struct bch_opts *, struct bch_opts);

enum bch_opt_id {
#define BCH_OPT(_name, ...)	Opt_##_name,
	BCH_OPTS()
#undef BCH_OPT
	bch2_opts_nr
};

struct bch_fs;
struct printbuf;

struct bch_option {
	struct attribute	attr;
	void			(*set_sb)(struct bch_sb *, u64);
	enum opt_mode		mode;
	enum opt_type		type;

	union {
	struct {
		u64		min, max;
	};
	struct {
		const char * const *choices;
	};
	struct {
		int (*parse)(struct bch_fs *, const char *, u64 *);
		void (*to_text)(struct printbuf *, struct bch_fs *, u64);
	};
	};

};

extern const struct bch_option bch2_opt_table[];

bool bch2_opt_defined_by_id(const struct bch_opts *, enum bch_opt_id);
u64 bch2_opt_get_by_id(const struct bch_opts *, enum bch_opt_id);
void bch2_opt_set_by_id(struct bch_opts *, enum bch_opt_id, u64);

struct bch_opts bch2_opts_from_sb(struct bch_sb *);

int bch2_opt_lookup(const char *);
int bch2_opt_parse(struct bch_fs *, const struct bch_option *, const char *, u64 *);

#define OPT_SHOW_FULL_LIST	(1 << 0)
#define OPT_SHOW_MOUNT_STYLE	(1 << 1)

void bch2_opt_to_text(struct printbuf *, struct bch_fs *,
		      const struct bch_option *, u64, unsigned);

int bch2_opt_check_may_set(struct bch_fs *, int, u64);
int bch2_parse_mount_opts(struct bch_opts *, char *);

/* inode opts: */

#define BCH_INODE_OPTS()					\
	BCH_INODE_OPT(data_checksum,			8)	\
	BCH_INODE_OPT(compression,			8)	\
	BCH_INODE_OPT(background_compression,		8)	\
	BCH_INODE_OPT(data_replicas,			8)	\
	BCH_INODE_OPT(promote_target,			16)	\
	BCH_INODE_OPT(foreground_target,		16)	\
	BCH_INODE_OPT(background_target,		16)

struct bch_io_opts {
#define BCH_INODE_OPT(_name, _bits)	unsigned _name##_defined:1;
	BCH_INODE_OPTS()
#undef BCH_INODE_OPT

#define BCH_INODE_OPT(_name, _bits)	u##_bits _name;
	BCH_INODE_OPTS()
#undef BCH_INODE_OPT
};

struct bch_io_opts bch2_opts_to_inode_opts(struct bch_opts);
struct bch_opts bch2_inode_opts_to_opts(struct bch_io_opts);
void bch2_io_opts_apply(struct bch_io_opts *, struct bch_io_opts);
bool bch2_opt_is_inode_opt(enum bch_opt_id);

#endif /* _BCACHEFS_OPTS_H */
