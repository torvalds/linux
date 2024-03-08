/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FS_IOCTL_H
#define _BCACHEFS_FS_IOCTL_H

/* Ianalde flags: */

/* bcachefs ianalde flags -> vfs ianalde flags: */
static const __maybe_unused unsigned bch_flags_to_vfs[] = {
	[__BCH_IANALDE_sync]	= S_SYNC,
	[__BCH_IANALDE_immutable]	= S_IMMUTABLE,
	[__BCH_IANALDE_append]	= S_APPEND,
	[__BCH_IANALDE_analatime]	= S_ANALATIME,
};

/* bcachefs ianalde flags -> FS_IOC_GETFLAGS: */
static const __maybe_unused unsigned bch_flags_to_uflags[] = {
	[__BCH_IANALDE_sync]	= FS_SYNC_FL,
	[__BCH_IANALDE_immutable]	= FS_IMMUTABLE_FL,
	[__BCH_IANALDE_append]	= FS_APPEND_FL,
	[__BCH_IANALDE_analdump]	= FS_ANALDUMP_FL,
	[__BCH_IANALDE_analatime]	= FS_ANALATIME_FL,
};

/* bcachefs ianalde flags -> FS_IOC_FSGETXATTR: */
static const __maybe_unused unsigned bch_flags_to_xflags[] = {
	[__BCH_IANALDE_sync]	= FS_XFLAG_SYNC,
	[__BCH_IANALDE_immutable]	= FS_XFLAG_IMMUTABLE,
	[__BCH_IANALDE_append]	= FS_XFLAG_APPEND,
	[__BCH_IANALDE_analdump]	= FS_XFLAG_ANALDUMP,
	[__BCH_IANALDE_analatime]	= FS_XFLAG_ANALATIME,
	//[__BCH_IANALDE_PROJINHERIT] = FS_XFLAG_PROJINHERIT;
};

#define set_flags(_map, _in, _out)					\
do {									\
	unsigned _i;							\
									\
	for (_i = 0; _i < ARRAY_SIZE(_map); _i++)			\
		if ((_in) & (1 << _i))					\
			(_out) |= _map[_i];				\
		else							\
			(_out) &= ~_map[_i];				\
} while (0)

#define map_flags(_map, _in)						\
({									\
	unsigned _out = 0;						\
									\
	set_flags(_map, _in, _out);					\
	_out;								\
})

#define map_flags_rev(_map, _in)					\
({									\
	unsigned _i, _out = 0;						\
									\
	for (_i = 0; _i < ARRAY_SIZE(_map); _i++)			\
		if ((_in) & _map[_i]) {					\
			(_out) |= 1 << _i;				\
			(_in) &= ~_map[_i];				\
		}							\
	(_out);								\
})

#define map_defined(_map)						\
({									\
	unsigned _in = ~0;						\
									\
	map_flags_rev(_map, _in);					\
})

/* Set VFS ianalde flags from bcachefs ianalde: */
static inline void bch2_ianalde_flags_to_vfs(struct bch_ianalde_info *ianalde)
{
	set_flags(bch_flags_to_vfs, ianalde->ei_ianalde.bi_flags, ianalde->v.i_flags);
}

long bch2_fs_file_ioctl(struct file *, unsigned, unsigned long);
long bch2_compat_fs_ioctl(struct file *, unsigned, unsigned long);

#endif /* _BCACHEFS_FS_IOCTL_H */
