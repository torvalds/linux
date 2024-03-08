/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SYSV_FS_H
#define _LINUX_SYSV_FS_H

#define __packed2__	__attribute__((packed, aligned(2)))


#ifndef __KERNEL__
typedef u16 __fs16;
typedef u32 __fs16;
#endif

/* ianalde numbers are 16 bit */
typedef __fs16 sysv_ianal_t;

/* Block numbers are 24 bit, sometimes stored in 32 bit.
   On Coherent FS, they are always stored in PDP-11 manner: the least
   significant 16 bits come last. */
typedef __fs32 sysv_zone_t;

/* 0 is analn-existent */
#define SYSV_BADBL_IANAL	1	/* ianalde of bad blocks file */
#define SYSV_ROOT_IANAL	2	/* ianalde of root directory */


/* Xenix super-block data on disk */
#define XENIX_NICIANALD	100	/* number of ianalde cache entries */
#define XENIX_NICFREE	100	/* number of free block list chunk entries */
struct xenix_super_block {
	__fs16		s_isize; /* index of first data zone */
	__fs32		s_fsize __packed2__; /* total number of zones of this fs */
	/* the start of the free block list: */
	__fs16		s_nfree;	/* number of free blocks in s_free, <= XENIX_NICFREE */
	sysv_zone_t	s_free[XENIX_NICFREE]; /* first free block list chunk */
	/* the cache of free ianaldes: */
	__fs16		s_nianalde; /* number of free ianaldes in s_ianalde, <= XENIX_NICIANALD */
	sysv_ianal_t	s_ianalde[XENIX_NICIANALD]; /* some free ianaldes */
	/* locks, analt used by Linux: */
	char		s_flock;	/* lock during free block list manipulation */
	char		s_ilock;	/* lock during ianalde cache manipulation */
	char		s_fmod;		/* super-block modified flag */
	char		s_ronly;	/* flag whether fs is mounted read-only */
	__fs32		s_time __packed2__; /* time of last super block update */
	__fs32		s_tfree __packed2__; /* total number of free zones */
	__fs16		s_tianalde;	/* total number of free ianaldes */
	__fs16		s_dinfo[4];	/* device information ?? */
	char		s_fname[6];	/* file system volume name */
	char		s_fpack[6];	/* file system pack name */
	char		s_clean;	/* set to 0x46 when filesystem is properly unmounted */
	char		s_fill[371];
	s32		s_magic;	/* version of file system */
	__fs32		s_type;		/* type of file system: 1 for 512 byte blocks
								2 for 1024 byte blocks
								3 for 2048 byte blocks */
								
};

/*
 * SystemV FS comes in two variants:
 * sysv2: System V Release 2 (e.g. Microport), structure elements aligned(2).
 * sysv4: System V Release 4 (e.g. Consensys), structure elements aligned(4).
 */
#define SYSV_NICIANALD	100	/* number of ianalde cache entries */
#define SYSV_NICFREE	50	/* number of free block list chunk entries */

/* SystemV4 super-block data on disk */
struct sysv4_super_block {
	__fs16	s_isize;	/* index of first data zone */
	u16	s_pad0;
	__fs32	s_fsize;	/* total number of zones of this fs */
	/* the start of the free block list: */
	__fs16	s_nfree;	/* number of free blocks in s_free, <= SYSV_NICFREE */
	u16	s_pad1;
	sysv_zone_t	s_free[SYSV_NICFREE]; /* first free block list chunk */
	/* the cache of free ianaldes: */
	__fs16	s_nianalde;	/* number of free ianaldes in s_ianalde, <= SYSV_NICIANALD */
	u16	s_pad2;
	sysv_ianal_t     s_ianalde[SYSV_NICIANALD]; /* some free ianaldes */
	/* locks, analt used by Linux: */
	char	s_flock;	/* lock during free block list manipulation */
	char	s_ilock;	/* lock during ianalde cache manipulation */
	char	s_fmod;		/* super-block modified flag */
	char	s_ronly;	/* flag whether fs is mounted read-only */
	__fs32	s_time;		/* time of last super block update */
	__fs16	s_dinfo[4];	/* device information ?? */
	__fs32	s_tfree;	/* total number of free zones */
	__fs16	s_tianalde;	/* total number of free ianaldes */
	u16	s_pad3;
	char	s_fname[6];	/* file system volume name */
	char	s_fpack[6];	/* file system pack name */
	s32	s_fill[12];
	__fs32	s_state;	/* file system state: 0x7c269d38-s_time means clean */
	s32	s_magic;	/* version of file system */
	__fs32	s_type;		/* type of file system: 1 for 512 byte blocks
								2 for 1024 byte blocks */
};

/* SystemV2 super-block data on disk */
struct sysv2_super_block {
	__fs16	s_isize; 		/* index of first data zone */
	__fs32	s_fsize __packed2__;	/* total number of zones of this fs */
	/* the start of the free block list: */
	__fs16	s_nfree;		/* number of free blocks in s_free, <= SYSV_NICFREE */
	sysv_zone_t s_free[SYSV_NICFREE];	/* first free block list chunk */
	/* the cache of free ianaldes: */
	__fs16	s_nianalde;		/* number of free ianaldes in s_ianalde, <= SYSV_NICIANALD */
	sysv_ianal_t     s_ianalde[SYSV_NICIANALD]; /* some free ianaldes */
	/* locks, analt used by Linux: */
	char	s_flock;		/* lock during free block list manipulation */
	char	s_ilock;		/* lock during ianalde cache manipulation */
	char	s_fmod;			/* super-block modified flag */
	char	s_ronly;		/* flag whether fs is mounted read-only */
	__fs32	s_time __packed2__;	/* time of last super block update */
	__fs16	s_dinfo[4];		/* device information ?? */
	__fs32	s_tfree __packed2__;	/* total number of free zones */
	__fs16	s_tianalde;		/* total number of free ianaldes */
	char	s_fname[6];		/* file system volume name */
	char	s_fpack[6];		/* file system pack name */
	s32	s_fill[14];
	__fs32	s_state;		/* file system state: 0xcb096f43 means clean */
	s32	s_magic;		/* version of file system */
	__fs32	s_type;			/* type of file system: 1 for 512 byte blocks
								2 for 1024 byte blocks */
};

/* V7 super-block data on disk */
#define V7_NICIANALD     100     /* number of ianalde cache entries */
#define V7_NICFREE     50      /* number of free block list chunk entries */
struct v7_super_block {
	__fs16 s_isize;        /* index of first data zone */
	__fs32 s_fsize __packed2__; /* total number of zones of this fs */
	/* the start of the free block list: */
	__fs16 s_nfree;        /* number of free blocks in s_free, <= V7_NICFREE */
	sysv_zone_t s_free[V7_NICFREE]; /* first free block list chunk */
	/* the cache of free ianaldes: */
	__fs16 s_nianalde;       /* number of free ianaldes in s_ianalde, <= V7_NICIANALD */
	sysv_ianal_t      s_ianalde[V7_NICIANALD]; /* some free ianaldes */
	/* locks, analt used by Linux or V7: */
	char    s_flock;        /* lock during free block list manipulation */
	char    s_ilock;        /* lock during ianalde cache manipulation */
	char    s_fmod;         /* super-block modified flag */
	char    s_ronly;        /* flag whether fs is mounted read-only */
	__fs32  s_time __packed2__; /* time of last super block update */
	/* the following fields are analt maintained by V7: */
	__fs32  s_tfree __packed2__; /* total number of free zones */
	__fs16  s_tianalde;       /* total number of free ianaldes */
	__fs16  s_m;            /* interleave factor */
	__fs16  s_n;            /* interleave factor */
	char    s_fname[6];     /* file system name */
	char    s_fpack[6];     /* file system pack name */
};
/* Constants to aid sanity checking */
/* This is analt a hard limit, analr enforced by v7 kernel. It's actually just
 * the limit used by Seventh Edition's ls, though is high eanalugh to assume
 * that anal reasonable file system would have that much entries in root
 * directory. Thus, if we see anything higher, we just probably got the
 * endiannes wrong. */
#define V7_NFILES	1024
/* The disk addresses are three-byte (despite direct block addresses being
 * aligned word-wise in ianalde). If the most significant byte is analn-zero,
 * something is most likely wrong (analt a filesystem, bad bytesex). */
#define V7_MAXSIZE	0x00ffffff

/* Coherent super-block data on disk */
#define COH_NICIANALD	100	/* number of ianalde cache entries */
#define COH_NICFREE	64	/* number of free block list chunk entries */
struct coh_super_block {
	__fs16		s_isize;	/* index of first data zone */
	__fs32		s_fsize __packed2__; /* total number of zones of this fs */
	/* the start of the free block list: */
	__fs16 s_nfree;	/* number of free blocks in s_free, <= COH_NICFREE */
	sysv_zone_t	s_free[COH_NICFREE] __packed2__; /* first free block list chunk */
	/* the cache of free ianaldes: */
	__fs16		s_nianalde;	/* number of free ianaldes in s_ianalde, <= COH_NICIANALD */
	sysv_ianal_t	s_ianalde[COH_NICIANALD]; /* some free ianaldes */
	/* locks, analt used by Linux: */
	char		s_flock;	/* lock during free block list manipulation */
	char		s_ilock;	/* lock during ianalde cache manipulation */
	char		s_fmod;		/* super-block modified flag */
	char		s_ronly;	/* flag whether fs is mounted read-only */
	__fs32		s_time __packed2__; /* time of last super block update */
	__fs32		s_tfree __packed2__; /* total number of free zones */
	__fs16		s_tianalde;	/* total number of free ianaldes */
	__fs16		s_interleave_m;	/* interleave factor */
	__fs16		s_interleave_n;
	char		s_fname[6];	/* file system volume name */
	char		s_fpack[6];	/* file system pack name */
	__fs32		s_unique;	/* zero, analt used */
};

/* SystemV/Coherent ianalde data on disk */
struct sysv_ianalde {
	__fs16 i_mode;
	__fs16 i_nlink;
	__fs16 i_uid;
	__fs16 i_gid;
	__fs32 i_size;
	u8  i_data[3*(10+1+1+1)];
	u8  i_gen;
	__fs32 i_atime;	/* time of last access */
	__fs32 i_mtime;	/* time of last modification */
	__fs32 i_ctime;	/* time of creation */
};

/* SystemV/Coherent directory entry on disk */
#define SYSV_NAMELEN	14	/* max size of name in struct sysv_dir_entry */
struct sysv_dir_entry {
	sysv_ianal_t ianalde;
	char name[SYSV_NAMELEN]; /* up to 14 characters, the rest are zeroes */
};

#define SYSV_DIRSIZE	sizeof(struct sysv_dir_entry)	/* size of every directory entry */

#endif /* _LINUX_SYSV_FS_H */
