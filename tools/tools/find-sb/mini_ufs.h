/*
 * This program, created 2002-10-03 by Garrett A. Wollman
 * <wollman@FreeBSD.org>, is in the public domain.  Use at your own risk.
 *
 * $FreeBSD$
 */

/*
 * Small extract from ufs/ffs/fs.h to get definition of superblock
 * in order to make this tool portable to other unix-like systems.
 *
 * Based upon:
 *
 * FreeBSD: src/sys/ufs/ffs/fs.h,v 1.48 2005/02/20 08:02:15 delphij
 * FreeBSD: src/sys/ufs/ufs/dinode.h,v 1.15 2005/01/07 02:29:26 imp
 */

#include <sys/types.h>

#define	SBLOCKSIZE	8192
#define	DEV_BSIZE	(1<<9)
#define	FS_UFS1_MAGIC	0x011954
#define	FS_UFS2_MAGIC	0x19540119
#define	SBLOCK_UFS1	8192
#define	SBLOCK_UFS2	65536
#define	MAXMNTLEN	468
#define	MAXVOLLEN	32
#define	NOCSPTRS	((128 / sizeof(void *)) - 4)
#define	FSMAXSNAP	20

typedef int64_t ufs_time_t;
typedef int64_t ufs2_daddr_t;

struct csum {
        int32_t cs_ndir;                /* number of directories */
        int32_t cs_nbfree;              /* number of free blocks */
        int32_t cs_nifree;              /* number of free inodes */
        int32_t cs_nffree;              /* number of free frags */
};

struct csum_total {
        int64_t cs_ndir;                /* number of directories */
        int64_t cs_nbfree;              /* number of free blocks */
        int64_t cs_nifree;              /* number of free inodes */
        int64_t cs_nffree;              /* number of free frags */
        int64_t cs_numclusters;         /* number of free clusters */
        int64_t cs_spare[3];            /* future expansion */
};

/*
 * Super block for an FFS filesystem.
 */
struct fs {
        int32_t  fs_firstfield;         /* historic filesystem linked list, */
        int32_t  fs_unused_1;           /*     used for incore super blocks */
        int32_t  fs_sblkno;             /* offset of super-block in filesys */
        int32_t  fs_cblkno;             /* offset of cyl-block in filesys */
        int32_t  fs_iblkno;             /* offset of inode-blocks in filesys */
        int32_t  fs_dblkno;             /* offset of first data after cg */
        int32_t  fs_old_cgoffset;       /* cylinder group offset in cylinder */
        int32_t  fs_old_cgmask;         /* used to calc mod fs_ntrak */
        int32_t  fs_old_time;           /* last time written */
        int32_t  fs_old_size;           /* number of blocks in fs */
        int32_t  fs_old_dsize;          /* number of data blocks in fs */
        int32_t  fs_ncg;                /* number of cylinder groups */
        int32_t  fs_bsize;              /* size of basic blocks in fs */
        int32_t  fs_fsize;              /* size of frag blocks in fs */
        int32_t  fs_frag;               /* number of frags in a block in fs */
/* these are configuration parameters */
        int32_t  fs_minfree;            /* minimum percentage of free blocks */
        int32_t  fs_old_rotdelay;       /* num of ms for optimal next block */
        int32_t  fs_old_rps;            /* disk revolutions per second */
/* these fields can be computed from the others */
        int32_t  fs_bmask;              /* ``blkoff'' calc of blk offsets */
        int32_t  fs_fmask;              /* ``fragoff'' calc of frag offsets */
        int32_t  fs_bshift;             /* ``lblkno'' calc of logical blkno */
        int32_t  fs_fshift;             /* ``numfrags'' calc number of frags */
/* these are configuration parameters */
        int32_t  fs_maxcontig;          /* max number of contiguous blks */
        int32_t  fs_maxbpg;             /* max number of blks per cyl group */
/* these fields can be computed from the others */
        int32_t  fs_fragshift;          /* block to frag shift */
        int32_t  fs_fsbtodb;            /* fsbtodb and dbtofsb shift constant */
        int32_t  fs_sbsize;             /* actual size of super block */
        int32_t  fs_spare1[2];          /* old fs_csmask */
                                        /* old fs_csshift */
        int32_t  fs_nindir;             /* value of NINDIR */
        int32_t  fs_inopb;              /* value of INOPB */
        int32_t  fs_old_nspf;           /* value of NSPF */
/* yet another configuration parameter */
        int32_t  fs_optim;              /* optimization preference, see below */
        int32_t  fs_old_npsect;         /* # sectors/track including spares */
        int32_t  fs_old_interleave;     /* hardware sector interleave */
        int32_t  fs_old_trackskew;      /* sector 0 skew, per track */
        int32_t  fs_id[2];              /* unique filesystem id */
/* sizes determined by number of cylinder groups and their sizes */
        int32_t  fs_old_csaddr;         /* blk addr of cyl grp summary area */
        int32_t  fs_cssize;             /* size of cyl grp summary area */
        int32_t  fs_cgsize;             /* cylinder group size */
        int32_t  fs_spare2;             /* old fs_ntrak */
        int32_t  fs_old_nsect;          /* sectors per track */
        int32_t  fs_old_spc;            /* sectors per cylinder */
        int32_t  fs_old_ncyl;           /* cylinders in filesystem */
        int32_t  fs_old_cpg;            /* cylinders per group */
        int32_t  fs_ipg;                /* inodes per group */
        int32_t  fs_fpg;                /* blocks per group * fs_frag */
/* this data must be re-computed after crashes */
        struct  csum fs_old_cstotal;    /* cylinder summary information */
/* these fields are cleared at mount time */
        int8_t   fs_fmod;               /* super block modified flag */
        int8_t   fs_clean;              /* filesystem is clean flag */
        int8_t   fs_ronly;              /* mounted read-only flag */
        int8_t   fs_old_flags;          /* old FS_ flags */
        u_char   fs_fsmnt[MAXMNTLEN];   /* name mounted on */
        u_char   fs_volname[MAXVOLLEN]; /* volume name */
        u_int64_t fs_swuid;             /* system-wide uid */
        int32_t  fs_pad;                /* due to alignment of fs_swuid */
/* these fields retain the current block allocation info */
        int32_t  fs_cgrotor;            /* last cg searched */
        void    *fs_ocsp[NOCSPTRS];     /* padding; was list of fs_cs buffers */
        u_int8_t *fs_contigdirs;        /* (u) # of contig. allocated dirs */
        struct  csum *fs_csp;           /* (u) cg summary info buffer */
        int32_t *fs_maxcluster;         /* (u) max cluster in each cyl group */
        u_int   *fs_active;             /* (u) used by snapshots to track fs */
        int32_t  fs_old_cpc;            /* cyl per cycle in postbl */
        int32_t  fs_maxbsize;           /* maximum blocking factor permitted */
        int64_t  fs_unrefs;             /* number of unreferenced inodes */
        int64_t  fs_sparecon64[16];     /* old rotation block list head */
        int64_t  fs_sblockloc;          /* byte offset of standard superblock */
        struct  csum_total fs_cstotal;  /* (u) cylinder summary information */
        ufs_time_t fs_time;             /* last time written */
        int64_t  fs_size;               /* number of blocks in fs */
        int64_t  fs_dsize;              /* number of data blocks in fs */
        ufs2_daddr_t fs_csaddr;         /* blk addr of cyl grp summary area */
        int64_t  fs_pendingblocks;      /* (u) blocks being freed */
        int32_t  fs_pendinginodes;      /* (u) inodes being freed */
        int32_t  fs_snapinum[FSMAXSNAP];/* list of snapshot inode numbers */
        int32_t  fs_avgfilesize;        /* expected average file size */
        int32_t  fs_avgfpdir;           /* expected # of files per directory */
        int32_t  fs_save_cgsize;        /* save real cg size to use fs_bsize */
        int32_t  fs_sparecon32[26];     /* reserved for future constants */
        int32_t  fs_flags;              /* see FS_ flags below */
        int32_t  fs_contigsumsize;      /* size of cluster summary array */ 
        int32_t  fs_maxsymlinklen;      /* max length of an internal symlink */
        int32_t  fs_old_inodefmt;       /* format of on-disk inodes */
        u_int64_t fs_maxfilesize;       /* maximum representable file size */
        int64_t  fs_qbmask;             /* ~fs_bmask for use with 64-bit size */
        int64_t  fs_qfmask;             /* ~fs_fmask for use with 64-bit size */
        int32_t  fs_state;              /* validate fs_clean field */
        int32_t  fs_old_postblformat;   /* format of positional layout tables */
        int32_t  fs_old_nrpos;          /* number of rotational positions */
        int32_t  fs_spare5[2];          /* old fs_postbloff */
                                        /* old fs_rotbloff */
        int32_t  fs_magic;              /* magic number */
};
