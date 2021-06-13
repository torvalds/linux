/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_OPTS_H
#define _BCACHEFS_OPTS_H

#include <linux/bug.h>
#include <linux/log2.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include "bcachefs_format.h"

extern const char * const bch2_error_actions[];
extern const char * const bch2_sb_features[];
extern const char * const bch2_sb_compat[];
extern const char * const bch2_btree_ids[];
extern const char * const bch2_csum_opts[];
extern const char * const bch2_compression_opts[];
extern const char * const bch2_str_hash_types[];
extern const char * const bch2_data_types[];
extern const char * const bch2_cache_replacement_policies[];
extern const char * const bch2_member_states[];
extern const char * const bch2_d_types[];

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

/* When can be set: */
enum opt_mode {
	OPT_FORMAT	= (1 << 0),
	OPT_MOUNT	= (1 << 1),
	OPT_RUNTIME	= (1 << 2),
	OPT_INODE	= (1 << 3),
	OPT_DEVICE	= (1 << 4),
};

enum opt_type {
	BCH_OPT_BOOL,
	BCH_OPT_UINT,
	BCH_OPT_SECTORS,
	BCH_OPT_STR,
	BCH_OPT_FN,
};

/**
 * x(name, shortopt, type, in mem type, mode, sb_opt)
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

#ifdef __KERNEL__
#define RATELIMIT_ERRORS true
#else
#define RATELIMIT_ERRORS false
#endif

#define BCH_OPTS()							\
	x(block_size,			u16,				\
	  OPT_FORMAT,							\
	  OPT_SECTORS(1, 128),						\
	  BCH_SB_BLOCK_SIZE,		8,				\
	  "size",	NULL)						\
	x(btree_node_size,		u16,				\
	  OPT_FORMAT,							\
	  OPT_SECTORS(1, 512),						\
	  BCH_SB_BTREE_NODE_SIZE,	512,				\
	  "size",	"Btree node size, default 256k")		\
	x(errors,			u8,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME,				\
	  OPT_STR(bch2_error_actions),					\
	  BCH_SB_ERROR_ACTION,		BCH_ON_ERROR_ro,		\
	  NULL,		"Action to take on filesystem error")		\
	x(metadata_replicas,		u8,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME,				\
	  OPT_UINT(1, BCH_REPLICAS_MAX),				\
	  BCH_SB_META_REPLICAS_WANT,	1,				\
	  "#",		"Number of metadata replicas")			\
	x(data_replicas,		u8,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME|OPT_INODE,			\
	  OPT_UINT(1, BCH_REPLICAS_MAX),				\
	  BCH_SB_DATA_REPLICAS_WANT,	1,				\
	  "#",		"Number of data replicas")			\
	x(metadata_replicas_required, u8,				\
	  OPT_FORMAT|OPT_MOUNT,						\
	  OPT_UINT(1, BCH_REPLICAS_MAX),				\
	  BCH_SB_META_REPLICAS_REQ,	1,				\
	  "#",		NULL)						\
	x(data_replicas_required,	u8,				\
	  OPT_FORMAT|OPT_MOUNT,						\
	  OPT_UINT(1, BCH_REPLICAS_MAX),				\
	  BCH_SB_DATA_REPLICAS_REQ,	1,				\
	  "#",		NULL)						\
	x(metadata_checksum,		u8,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME,				\
	  OPT_STR(bch2_csum_opts),					\
	  BCH_SB_META_CSUM_TYPE,	BCH_CSUM_OPT_crc32c,		\
	  NULL,		NULL)						\
	x(data_checksum,		u8,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME|OPT_INODE,			\
	  OPT_STR(bch2_csum_opts),					\
	  BCH_SB_DATA_CSUM_TYPE,	BCH_CSUM_OPT_crc32c,		\
	  NULL,		NULL)						\
	x(compression,			u8,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME|OPT_INODE,			\
	  OPT_STR(bch2_compression_opts),				\
	  BCH_SB_COMPRESSION_TYPE,	BCH_COMPRESSION_OPT_none,	\
	  NULL,		NULL)						\
	x(background_compression,	u8,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME|OPT_INODE,			\
	  OPT_STR(bch2_compression_opts),				\
	  BCH_SB_BACKGROUND_COMPRESSION_TYPE,BCH_COMPRESSION_OPT_none,	\
	  NULL,		NULL)						\
	x(str_hash,			u8,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME,				\
	  OPT_STR(bch2_str_hash_types),					\
	  BCH_SB_STR_HASH_TYPE,		BCH_STR_HASH_OPT_siphash,	\
	  NULL,		"Hash function for directory entries and xattrs")\
	x(metadata_target,		u16,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME|OPT_INODE,			\
	  OPT_FN(bch2_opt_target),					\
	  BCH_SB_METADATA_TARGET,	0,				\
	  "(target)",	"Device or disk group for metadata writes")	\
	x(foreground_target,		u16,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME|OPT_INODE,			\
	  OPT_FN(bch2_opt_target),					\
	  BCH_SB_FOREGROUND_TARGET,	0,				\
	  "(target)",	"Device or disk group for foreground writes")	\
	x(background_target,		u16,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME|OPT_INODE,			\
	  OPT_FN(bch2_opt_target),					\
	  BCH_SB_BACKGROUND_TARGET,	0,				\
	  "(target)",	"Device or disk group to move data to in the background")\
	x(promote_target,		u16,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME|OPT_INODE,			\
	  OPT_FN(bch2_opt_target),					\
	  BCH_SB_PROMOTE_TARGET,	0,				\
	  "(target)",	"Device or disk group to promote data to on read")\
	x(erasure_code,			u16,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME|OPT_INODE,			\
	  OPT_BOOL(),							\
	  BCH_SB_ERASURE_CODE,		false,				\
	  NULL,		"Enable erasure coding (DO NOT USE YET)")	\
	x(inodes_32bit,			u8,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME,				\
	  OPT_BOOL(),							\
	  BCH_SB_INODE_32BIT,		true,				\
	  NULL,		"Constrain inode numbers to 32 bits")		\
	x(shard_inode_numbers,		u8,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME,				\
	  OPT_BOOL(),							\
	  BCH_SB_SHARD_INUMS,		false,				\
	  NULL,		"Shard new inode numbers by CPU id")		\
	x(inodes_use_key_cache,	u8,					\
	  OPT_FORMAT|OPT_MOUNT,						\
	  OPT_BOOL(),							\
	  BCH_SB_INODES_USE_KEY_CACHE,	true,				\
	  NULL,		"Use the btree key cache for the inodes btree")	\
	x(gc_reserve_percent,		u8,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME,				\
	  OPT_UINT(5, 21),						\
	  BCH_SB_GC_RESERVE,		8,				\
	  "%",		"Percentage of disk space to reserve for copygc")\
	x(gc_reserve_bytes,		u64,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME,				\
	  OPT_SECTORS(0, U64_MAX),					\
	  BCH_SB_GC_RESERVE_BYTES,	0,				\
	  "%",		"Amount of disk space to reserve for copygc\n"	\
			"Takes precedence over gc_reserve_percent if set")\
	x(root_reserve_percent,		u8,				\
	  OPT_FORMAT|OPT_MOUNT,						\
	  OPT_UINT(0, 100),						\
	  BCH_SB_ROOT_RESERVE,		0,				\
	  "%",		"Percentage of disk space to reserve for superuser")\
	x(wide_macs,			u8,				\
	  OPT_FORMAT|OPT_MOUNT|OPT_RUNTIME,				\
	  OPT_BOOL(),							\
	  BCH_SB_128_BIT_MACS,		false,				\
	  NULL,		"Store full 128 bits of cryptographic MACs, instead of 80")\
	x(inline_data,			u8,				\
	  OPT_MOUNT|OPT_RUNTIME,					\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			true,				\
	  NULL,		"Enable inline data extents")			\
	x(acl,				u8,				\
	  OPT_FORMAT|OPT_MOUNT,						\
	  OPT_BOOL(),							\
	  BCH_SB_POSIX_ACL,		true,				\
	  NULL,		"Enable POSIX acls")				\
	x(usrquota,			u8,				\
	  OPT_FORMAT|OPT_MOUNT,						\
	  OPT_BOOL(),							\
	  BCH_SB_USRQUOTA,		false,				\
	  NULL,		"Enable user quotas")				\
	x(grpquota,			u8,				\
	  OPT_FORMAT|OPT_MOUNT,						\
	  OPT_BOOL(),							\
	  BCH_SB_GRPQUOTA,		false,				\
	  NULL,		"Enable group quotas")				\
	x(prjquota,			u8,				\
	  OPT_FORMAT|OPT_MOUNT,						\
	  OPT_BOOL(),							\
	  BCH_SB_PRJQUOTA,		false,				\
	  NULL,		"Enable project quotas")			\
	x(degraded,			u8,				\
	  OPT_MOUNT,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Allow mounting in degraded mode")		\
	x(very_degraded,		u8,				\
	  OPT_MOUNT,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Allow mounting in when data will be missing")	\
	x(discard,			u8,				\
	  OPT_MOUNT|OPT_DEVICE,						\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Enable discard/TRIM support")			\
	x(verbose,			u8,				\
	  OPT_MOUNT,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Extra debugging information during mount/recovery")\
	x(journal_flush_disabled,	u8,				\
	  OPT_MOUNT|OPT_RUNTIME,					\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Disable journal flush on sync/fsync\n"		\
			"If enabled, writes can be lost, but only since the\n"\
			"last journal write (default 1 second)")	\
	x(fsck,				u8,				\
	  OPT_MOUNT,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Run fsck on mount")				\
	x(fix_errors,			u8,				\
	  OPT_MOUNT,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Fix errors during fsck without asking")	\
	x(ratelimit_errors,		u8,				\
	  OPT_MOUNT,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			RATELIMIT_ERRORS,		\
	  NULL,		"Ratelimit error messages during fsck")		\
	x(nochanges,			u8,				\
	  OPT_MOUNT,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Super read only mode - no writes at all will be issued,\n"\
			"even if we have to replay the journal")	\
	x(norecovery,			u8,				\
	  OPT_MOUNT,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Don't replay the journal")			\
	x(rebuild_replicas,		u8,				\
	  OPT_MOUNT,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Rebuild the superblock replicas section")	\
	x(keep_journal,			u8,				\
	  OPT_MOUNT,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Don't free journal entries/keys after startup")\
	x(read_entire_journal,		u8,				\
	  0,								\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Read all journal entries, not just dirty ones")\
	x(noexcl,			u8,				\
	  OPT_MOUNT,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Don't open device in exclusive mode")		\
	x(sb,				u64,				\
	  OPT_MOUNT,							\
	  OPT_UINT(0, S64_MAX),						\
	  NO_SB_OPT,			BCH_SB_SECTOR,			\
	  "offset",	"Sector offset of superblock")			\
	x(read_only,			u8,				\
	  0,								\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		NULL)						\
	x(nostart,			u8,				\
	  0,								\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Don\'t start filesystem, only open devices")	\
	x(reconstruct_alloc,		u8,				\
	  OPT_MOUNT,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Reconstruct alloc btree")			\
	x(version_upgrade,		u8,				\
	  OPT_MOUNT,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Set superblock to latest version,\n"		\
			"allowing any new features to be used")		\
	x(project,			u8,				\
	  OPT_INODE,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		NULL)						\
	x(no_data_io,			u8,				\
	  OPT_MOUNT,							\
	  OPT_BOOL(),							\
	  NO_SB_OPT,			false,				\
	  NULL,		"Skip submit_bio() for data reads and writes, "	\
			"for performance testing purposes")		\
	x(fs_size,			u64,				\
	  OPT_DEVICE,							\
	  OPT_SECTORS(0, S64_MAX),					\
	  NO_SB_OPT,			0,				\
	  "size",	"Size of filesystem on device")			\
	x(bucket,			u32,				\
	  OPT_DEVICE,							\
	  OPT_SECTORS(0, S64_MAX),					\
	  NO_SB_OPT,			0,				\
	  "size",	"Size of filesystem on device")			\
	x(durability,			u8,				\
	  OPT_DEVICE,							\
	  OPT_UINT(0, BCH_REPLICAS_MAX),				\
	  NO_SB_OPT,			1,				\
	  "n",		"Data written to this device will be considered\n"\
			"to have already been replicated n times")

struct bch_opts {
#define x(_name, _bits, ...)	unsigned _name##_defined:1;
	BCH_OPTS()
#undef x

#define x(_name, _bits, ...)	_bits	_name;
	BCH_OPTS()
#undef x
};

static const struct bch_opts bch2_opts_default = {
#define x(_name, _bits, _mode, _type, _sb_opt, _default, ...)		\
	._name##_defined = true,					\
	._name = _default,						\

	BCH_OPTS()
#undef x
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
#define x(_name, ...)	Opt_##_name,
	BCH_OPTS()
#undef x
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

	const char		*hint;
	const char		*help;

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
int bch2_opts_check_may_set(struct bch_fs *);
int bch2_parse_mount_opts(struct bch_fs *, struct bch_opts *, char *);

/* inode opts: */

struct bch_io_opts {
#define x(_name, _bits)	unsigned _name##_defined:1;
	BCH_INODE_OPTS()
#undef x

#define x(_name, _bits)	u##_bits _name;
	BCH_INODE_OPTS()
#undef x
};

struct bch_io_opts bch2_opts_to_inode_opts(struct bch_opts);
struct bch_opts bch2_inode_opts_to_opts(struct bch_io_opts);
void bch2_io_opts_apply(struct bch_io_opts *, struct bch_io_opts);
bool bch2_opt_is_inode_opt(enum bch_opt_id);

#endif /* _BCACHEFS_OPTS_H */
