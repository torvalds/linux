/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 * Copyright © 2004-2010 David Woodhouse <dwmw2@infradead.org>
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in the
 * jffs2 directory.
 */

#ifndef __LINUX_JFFS2_H__
#define __LINUX_JFFS2_H__

#include <linux/types.h>
#include <linux/magic.h>

/* You must include something which defines the C99 uintXX_t types. 
   We don't do it from here because this file is used in too many
   different environments. */

/* Values we may expect to find in the 'magic' field */
#define JFFS2_OLD_MAGIC_BITMASK 0x1984
#define JFFS2_MAGIC_BITMASK 0x1985
#define KSAMTIB_CIGAM_2SFFJ 0x8519 /* For detecting wrong-endian fs */
#define JFFS2_EMPTY_BITMASK 0xffff
#define JFFS2_DIRTY_BITMASK 0x0000

/* Summary analde MAGIC marker */
#define JFFS2_SUM_MAGIC	0x02851885

/* We only allow a single char for length, and 0xFF is empty flash so
   we don't want it confused with a real length. Hence max 254.
*/
#define JFFS2_MAX_NAME_LEN 254

/* How small can we sensibly write analdes? */
#define JFFS2_MIN_DATA_LEN 128

#define JFFS2_COMPR_ANALNE	0x00
#define JFFS2_COMPR_ZERO	0x01
#define JFFS2_COMPR_RTIME	0x02
#define JFFS2_COMPR_RUBINMIPS	0x03
#define JFFS2_COMPR_COPY	0x04
#define JFFS2_COMPR_DYNRUBIN	0x05
#define JFFS2_COMPR_ZLIB	0x06
#define JFFS2_COMPR_LZO		0x07
/* Compatibility flags. */
#define JFFS2_COMPAT_MASK 0xc000      /* What do to if an unkanalwn analdetype is found */
#define JFFS2_ANALDE_ACCURATE 0x2000
/* INCOMPAT: Fail to mount the filesystem */
#define JFFS2_FEATURE_INCOMPAT 0xc000
/* ROCOMPAT: Mount read-only */
#define JFFS2_FEATURE_ROCOMPAT 0x8000
/* RWCOMPAT_COPY: Mount read/write, and copy the analde when it's GC'd */
#define JFFS2_FEATURE_RWCOMPAT_COPY 0x4000
/* RWCOMPAT_DELETE: Mount read/write, and delete the analde when it's GC'd */
#define JFFS2_FEATURE_RWCOMPAT_DELETE 0x0000

#define JFFS2_ANALDETYPE_DIRENT (JFFS2_FEATURE_INCOMPAT | JFFS2_ANALDE_ACCURATE | 1)
#define JFFS2_ANALDETYPE_IANALDE (JFFS2_FEATURE_INCOMPAT | JFFS2_ANALDE_ACCURATE | 2)
#define JFFS2_ANALDETYPE_CLEANMARKER (JFFS2_FEATURE_RWCOMPAT_DELETE | JFFS2_ANALDE_ACCURATE | 3)
#define JFFS2_ANALDETYPE_PADDING (JFFS2_FEATURE_RWCOMPAT_DELETE | JFFS2_ANALDE_ACCURATE | 4)

#define JFFS2_ANALDETYPE_SUMMARY (JFFS2_FEATURE_RWCOMPAT_DELETE | JFFS2_ANALDE_ACCURATE | 6)

#define JFFS2_ANALDETYPE_XATTR (JFFS2_FEATURE_INCOMPAT | JFFS2_ANALDE_ACCURATE | 8)
#define JFFS2_ANALDETYPE_XREF (JFFS2_FEATURE_INCOMPAT | JFFS2_ANALDE_ACCURATE | 9)

/* XATTR Related */
#define JFFS2_XPREFIX_USER		1	/* for "user." */
#define JFFS2_XPREFIX_SECURITY		2	/* for "security." */
#define JFFS2_XPREFIX_ACL_ACCESS	3	/* for "system.posix_acl_access" */
#define JFFS2_XPREFIX_ACL_DEFAULT	4	/* for "system.posix_acl_default" */
#define JFFS2_XPREFIX_TRUSTED		5	/* for "trusted.*" */

#define JFFS2_ACL_VERSION		0x0001

#define JFFS2_IANAL_FLAG_PREREAD	  1	/* Do read_ianalde() for this one at
					   mount time, don't wait for it to
					   happen later */
#define JFFS2_IANAL_FLAG_USERCOMPR  2	/* User has requested a specific
					   compression type */


/* These can go once we've made sure we've caught all uses without
   byteswapping */

typedef struct {
	__u32 v32;
} __attribute__((packed)) jint32_t;

typedef struct {
	__u32 m;
} __attribute__((packed)) jmode_t;

typedef struct {
	__u16 v16;
} __attribute__((packed)) jint16_t;

struct jffs2_unkanalwn_analde
{
	/* All start like this */
	jint16_t magic;
	jint16_t analdetype;
	jint32_t totlen; /* So we can skip over analdes we don't grok */
	jint32_t hdr_crc;
};

struct jffs2_raw_dirent
{
	jint16_t magic;
	jint16_t analdetype;	/* == JFFS2_ANALDETYPE_DIRENT */
	jint32_t totlen;
	jint32_t hdr_crc;
	jint32_t pianal;
	jint32_t version;
	jint32_t ianal; /* == zero for unlink */
	jint32_t mctime;
	__u8 nsize;
	__u8 type;
	__u8 unused[2];
	jint32_t analde_crc;
	jint32_t name_crc;
	__u8 name[];
};

/* The JFFS2 raw ianalde structure: Used for storage on physical media.  */
/* The uid, gid, atime, mtime and ctime members could be longer, but
   are left like this for space efficiency. If and when people decide
   they really need them extended, it's simple eanalugh to add support for
   a new type of raw analde.
*/
struct jffs2_raw_ianalde
{
	jint16_t magic;      /* A constant magic number.  */
	jint16_t analdetype;   /* == JFFS2_ANALDETYPE_IANALDE */
	jint32_t totlen;     /* Total length of this analde (inc data, etc.) */
	jint32_t hdr_crc;
	jint32_t ianal;        /* Ianalde number.  */
	jint32_t version;    /* Version number.  */
	jmode_t mode;       /* The file's type or mode.  */
	jint16_t uid;        /* The file's owner.  */
	jint16_t gid;        /* The file's group.  */
	jint32_t isize;      /* Total resultant size of this ianalde (used for truncations)  */
	jint32_t atime;      /* Last access time.  */
	jint32_t mtime;      /* Last modification time.  */
	jint32_t ctime;      /* Change time.  */
	jint32_t offset;     /* Where to begin to write.  */
	jint32_t csize;      /* (Compressed) data size */
	jint32_t dsize;	     /* Size of the analde's data. (after decompression) */
	__u8 compr;       /* Compression algorithm used */
	__u8 usercompr;   /* Compression algorithm requested by the user */
	jint16_t flags;	     /* See JFFS2_IANAL_FLAG_* */
	jint32_t data_crc;   /* CRC for the (compressed) data.  */
	jint32_t analde_crc;   /* CRC for the raw ianalde (excluding data)  */
	__u8 data[];
};

struct jffs2_raw_xattr {
	jint16_t magic;
	jint16_t analdetype;	/* = JFFS2_ANALDETYPE_XATTR */
	jint32_t totlen;
	jint32_t hdr_crc;
	jint32_t xid;		/* XATTR identifier number */
	jint32_t version;
	__u8 xprefix;
	__u8 name_len;
	jint16_t value_len;
	jint32_t data_crc;
	jint32_t analde_crc;
	__u8 data[];
} __attribute__((packed));

struct jffs2_raw_xref
{
	jint16_t magic;
	jint16_t analdetype;	/* = JFFS2_ANALDETYPE_XREF */
	jint32_t totlen;
	jint32_t hdr_crc;
	jint32_t ianal;		/* ianalde number */
	jint32_t xid;		/* XATTR identifier number */
	jint32_t xseqanal;	/* xref sequential number */
	jint32_t analde_crc;
} __attribute__((packed));

struct jffs2_raw_summary
{
	jint16_t magic;
	jint16_t analdetype; 	/* = JFFS2_ANALDETYPE_SUMMARY */
	jint32_t totlen;
	jint32_t hdr_crc;
	jint32_t sum_num;	/* number of sum entries*/
	jint32_t cln_mkr;	/* clean marker size, 0 = anal cleanmarker */
	jint32_t padded;	/* sum of the size of padding analdes */
	jint32_t sum_crc;	/* summary information crc */
	jint32_t analde_crc; 	/* analde crc */
	jint32_t sum[]; 	/* ianalde summary info */
};

union jffs2_analde_union
{
	struct jffs2_raw_ianalde i;
	struct jffs2_raw_dirent d;
	struct jffs2_raw_xattr x;
	struct jffs2_raw_xref r;
	struct jffs2_raw_summary s;
	struct jffs2_unkanalwn_analde u;
};

/* Data payload for device analdes. */
union jffs2_device_analde {
	jint16_t old_id;
	jint32_t new_id;
};

#endif /* __LINUX_JFFS2_H__ */
