/*	$OpenBSD: bpb.h,v 1.7 2015/10/23 10:45:31 krw Exp $	*/
/*	$NetBSD: bpb.h,v 1.6 1997/10/17 11:23:35 ws Exp $	*/

/*
 * Written by Paul Popelka (paulp@uts.amdahl.com)
 *
 * You can do anything you want with this software, just don't say you wrote
 * it, and don't remove this notice.
 *
 * This software is provided "as is".
 *
 * The author supplies this software to be publicly redistributed on the
 * understanding that the author is not responsible for the correct
 * functioning of this software in any circumstances and is not liable for
 * any damages caused by this software.
 *
 * October 1992
 */

/*
 * BIOS Parameter Block (BPB) for DOS 3.3
 */
struct bpb33 {
	u_int16_t	bpbBytesPerSec;	/* bytes per sector */
	u_int8_t	bpbSecPerClust;	/* sectors per cluster */
	u_int16_t	bpbResSectors;	/* number of reserved sectors */
	u_int8_t	bpbFATs;	/* number of FATs */
	u_int16_t	bpbRootDirEnts;	/* number of root directory entries */
	u_int16_t	bpbSectors;	/* total number of sectors */
	u_int8_t	bpbMedia;	/* media descriptor */
	u_int16_t	bpbFATsecs;	/* number of sectors per FAT */
	u_int16_t	bpbSecPerTrack;	/* sectors per track */
	u_int16_t	bpbHeads;	/* number of heads */
	u_int16_t	bpbHiddenSecs;	/* number of hidden sectors */
};

/*
 * BPB for DOS 5.0 The difference is bpbHiddenSecs is a short for DOS 3.3,
 * and bpbHugeSectors is not in the 3.3 bpb.
 */
struct bpb50 {
	u_int16_t	bpbBytesPerSec;	/* bytes per sector */
	u_int8_t	bpbSecPerClust;	/* sectors per cluster */
	u_int16_t	bpbResSectors;	/* number of reserved sectors */
	u_int8_t	bpbFATs;	/* number of FATs */
	u_int16_t	bpbRootDirEnts;	/* number of root directory entries */
	u_int16_t	bpbSectors;	/* total number of sectors */
	u_int8_t	bpbMedia;	/* media descriptor */
	u_int16_t	bpbFATsecs;	/* number of sectors per FAT */
	u_int16_t	bpbSecPerTrack;	/* sectors per track */
	u_int16_t	bpbHeads;	/* number of heads */
	u_int32_t	bpbHiddenSecs;	/* # of hidden sectors */
	u_int32_t	bpbHugeSectors;	/* # of sectors if bpbSectors == 0 */
};

/*
 * BPB for DOS 7.10 (FAT32).  This one has a few extensions to bpb50.
 */
struct bpb710 {
	u_int16_t	bpbBytesPerSec;	/* bytes per sector */
	u_int8_t	bpbSecPerClust;	/* sectors per cluster */
	u_int16_t	bpbResSectors;	/* number of reserved sectors */
	u_int8_t	bpbFATs;	/* number of FATs */
	u_int16_t	bpbRootDirEnts;	/* number of root directory entries */
	u_int16_t	bpbSectors;	/* total number of sectors */
	u_int8_t	bpbMedia;	/* media descriptor */
	u_int16_t	bpbFATsecs;	/* number of sectors per FAT */
	u_int16_t	bpbSecPerTrack;	/* sectors per track */
	u_int16_t	bpbHeads;	/* number of heads */
	u_int32_t	bpbHiddenSecs;	/* # of hidden sectors */
	u_int32_t	bpbHugeSectors;	/* # of sectors if bpbSectors == 0 */
	u_int32_t	bpbBigFATsecs;	/* like bpbFATsecs for FAT32 */
	u_int16_t	bpbExtFlags;	/* extended flags: */
#define	FATNUM		0xf		/* mask for numbering active FAT */
#define	FATMIRROR	0x80		/* FAT is mirrored (like it always was) */
	u_int16_t	bpbFSVers;	/* filesystem version */
#define	FSVERS		0		/* currently only 0 is understood */
	u_int32_t	bpbRootClust;	/* start cluster for root directory */
	u_int16_t	bpbFSInfo;	/* filesystem info structure sector */
	u_int16_t	bpbBackup;	/* backup boot sector */
	/* There is a 12 byte filler here, but we ignore it */
};

/*
 * The following structures represent how the bpb's look on disk.  shorts
 * and longs are just character arrays of the appropriate length.  This is
 * because the compiler forces shorts and longs to align on word or
 * halfword boundaries.
 *
 * XXX The little-endian code here assumes that the processor can access
 * 16-bit and 32-bit quantities on byte boundaries.  If this is not true,
 * use the macros for the big-endian case.
 */
#include <sys/endian.h>
#if (BYTE_ORDER == LITTLE_ENDIAN) && !defined(__STRICT_ALIGNMENT)
#define	getushort(x)	*((u_int16_t *)(x))
#define	getulong(x)	*((u_int32_t *)(x))
#define	putushort(p, v)	(*((u_int16_t *)(p)) = (v))
#define	putulong(p, v)	(*((u_int32_t *)(p)) = (v))
#else
#define getushort(x)	(((u_int8_t *)(x))[0] + (((u_int8_t *)(x))[1] << 8))
#define getulong(x)	(((u_int8_t *)(x))[0] + (((u_int8_t *)(x))[1] << 8) \
			 + (((u_int8_t *)(x))[2] << 16)	\
			 + (((u_int8_t *)(x))[3] << 24))
#define putushort(p, v)	(((u_int8_t *)(p))[0] = (v),	\
			 ((u_int8_t *)(p))[1] = (v) >> 8)
#define putulong(p, v)	(((u_int8_t *)(p))[0] = (v),	\
			 ((u_int8_t *)(p))[1] = (v) >> 8, \
			 ((u_int8_t *)(p))[2] = (v) >> 16,\
			 ((u_int8_t *)(p))[3] = (v) >> 24)
#endif

/*
 * BIOS Parameter Block (BPB) for DOS 3.3
 */
struct byte_bpb33 {
	int8_t bpbBytesPerSec[2];	/* bytes per sector */
	int8_t bpbSecPerClust;		/* sectors per cluster */
	int8_t bpbResSectors[2];	/* number of reserved sectors */
	int8_t bpbFATs;			/* number of FATs */
	int8_t bpbRootDirEnts[2];	/* number of root directory entries */
	int8_t bpbSectors[2];		/* total number of sectors */
	int8_t bpbMedia;		/* media descriptor */
	int8_t bpbFATsecs[2];		/* number of sectors per FAT */
	int8_t bpbSecPerTrack[2];	/* sectors per track */
	int8_t bpbHeads[2];		/* number of heads */
	int8_t bpbHiddenSecs[2];	/* number of hidden sectors */
};

/*
 * BPB for DOS 5.0 The difference is bpbHiddenSecs is a short for DOS 3.3,
 * and bpbHugeSectors is not in the 3.3 bpb.
 */
struct byte_bpb50 {
	int8_t bpbBytesPerSec[2];	/* bytes per sector */
	int8_t bpbSecPerClust;		/* sectors per cluster */
	int8_t bpbResSectors[2];	/* number of reserved sectors */
	int8_t bpbFATs;			/* number of FATs */
	int8_t bpbRootDirEnts[2];	/* number of root directory entries */
	int8_t bpbSectors[2];		/* total number of sectors */
	int8_t bpbMedia;		/* media descriptor */
	int8_t bpbFATsecs[2];		/* number of sectors per FAT */
	int8_t bpbSecPerTrack[2];	/* sectors per track */
	int8_t bpbHeads[2];		/* number of heads */
	int8_t bpbHiddenSecs[4];	/* number of hidden sectors */
	int8_t bpbHugeSectors[4];	/* # of sectors if bpbSectors == 0 */
};

/*
 * BPB for DOS 7.10 (FAT32).  This one has a few extensions to bpb50.
 */
struct byte_bpb710 {
	u_int8_t bpbBytesPerSec[2];	/* bytes per sector */
	u_int8_t bpbSecPerClust;	/* sectors per cluster */
	u_int8_t bpbResSectors[2];	/* number of reserved sectors */
	u_int8_t bpbFATs;		/* number of FATs */
	u_int8_t bpbRootDirEnts[2];	/* number of root directory entries */
	u_int8_t bpbSectors[2];		/* total number of sectors */
	u_int8_t bpbMedia;		/* media descriptor */
	u_int8_t bpbFATsecs[2];		/* number of sectors per FAT */
	u_int8_t bpbSecPerTrack[2];	/* sectors per track */
	u_int8_t bpbHeads[2];		/* number of heads */
	u_int8_t bpbHiddenSecs[4];	/* # of hidden sectors */
	u_int8_t bpbHugeSectors[4];	/* # of sectors if bpbSectors == 0 */
	u_int8_t bpbBigFATsecs[4];	/* like bpbFATsecs for FAT32 */
	u_int8_t bpbExtFlags[2];	/* extended flags: */
	u_int8_t bpbFSVers[2];		/* filesystem version */
	u_int8_t bpbRootClust[4];	/* start cluster for root directory */
	u_int8_t bpbFSInfo[2];		/* filesystem info structure sector */
	u_int8_t bpbBackup[2];		/* backup boot sector */
	/* There is a 12 byte filler here, but we ignore it */
};

/*
 * FAT32 FSInfo block.
 */
struct fsinfo {
	u_int8_t fsisig1[4];
	u_int8_t fsifill1[480];
	u_int8_t fsisig2[4];
	u_int8_t fsinfree[4];
	u_int8_t fsinxtfree[4];
	u_int8_t fsifill2[12];
	u_int8_t fsisig3[4];
	u_int8_t fsifill3[508];
	u_int8_t fsisig4[4];
};
