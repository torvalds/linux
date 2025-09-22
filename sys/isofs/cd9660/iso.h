/*	$OpenBSD: iso.h,v 1.16 2021/03/05 07:01:36 jsg Exp $	*/
/*	$NetBSD: iso.h,v 1.20 1997/07/07 22:45:34 cgd Exp $	*/

/*-
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley
 * by Pace Willisson (pace@blitz.com).  The Rock Ridge Extension
 * Support code is derived from software contributed to Berkeley
 * by Atsushi Murai (amurai@spec.co.jp).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)iso.h	8.4 (Berkeley) 12/5/94
 */

/*
 * Definitions describing ISO9660 file system structure, as well as
 * the functions necessary to access fields of ISO9660 file system
 * structures.
 */

typedef uint32_t cdino_t;

#define ISODCL(from, to) (to - from + 1)

struct iso_volume_descriptor {
	char type[ISODCL(1,1)]; /* 711 */
	char id[ISODCL(2,6)];
	char version[ISODCL(7,7)];
	char data[ISODCL(8,2048)];
};

/* volume descriptor types */
#define ISO_VD_PRIMARY 1
#define ISO_VD_SUPPLEMENTARY 2
#define ISO_VD_END 255

#define ISO_STANDARD_ID "CD001"
#define ISO_ECMA_ID     "CDW01"

struct iso_primary_descriptor {
	char type			[ISODCL (  1,   1)]; /* 711 */
	char id				[ISODCL (  2,   6)];
	char version			[ISODCL (  7,   7)]; /* 711 */
	char unused1			[ISODCL (  8,   8)];
	char system_id			[ISODCL (  9,  40)]; /* achars */
	char volume_id			[ISODCL ( 41,  72)]; /* dchars */
	char unused2			[ISODCL ( 73,  80)];
	char volume_space_size		[ISODCL ( 81,  88)]; /* 733 */
	char unused3			[ISODCL ( 89, 120)];
	char volume_set_size		[ISODCL (121, 124)]; /* 723 */
	char volume_sequence_number	[ISODCL (125, 128)]; /* 723 */
	char logical_block_size		[ISODCL (129, 132)]; /* 723 */
	char path_table_size		[ISODCL (133, 140)]; /* 733 */
	char type_l_path_table		[ISODCL (141, 144)]; /* 731 */
	char opt_type_l_path_table	[ISODCL (145, 148)]; /* 731 */
	char type_m_path_table		[ISODCL (149, 152)]; /* 732 */
	char opt_type_m_path_table	[ISODCL (153, 156)]; /* 732 */
	char root_directory_record	[ISODCL (157, 190)]; /* 9.1 */
	char volume_set_id		[ISODCL (191, 318)]; /* dchars */
	char publisher_id		[ISODCL (319, 446)]; /* achars */
	char preparer_id		[ISODCL (447, 574)]; /* achars */
	char application_id		[ISODCL (575, 702)]; /* achars */
	char copyright_file_id		[ISODCL (703, 739)]; /* 7.5 dchars */
	char abstract_file_id		[ISODCL (740, 776)]; /* 7.5 dchars */
	char bibliographic_file_id	[ISODCL (777, 813)]; /* 7.5 dchars */
	char creation_date		[ISODCL (814, 830)]; /* 8.4.26.1 */
	char modification_date		[ISODCL (831, 847)]; /* 8.4.26.1 */
	char expiration_date		[ISODCL (848, 864)]; /* 8.4.26.1 */
	char effective_date		[ISODCL (865, 881)]; /* 8.4.26.1 */
	char file_structure_version	[ISODCL (882, 882)]; /* 711 */
	char unused4			[ISODCL (883, 883)];
	char application_data		[ISODCL (884, 1395)];
	char unused5			[ISODCL (1396, 2048)];
};
#define ISO_DEFAULT_BLOCK_SHIFT		11
#define ISO_DEFAULT_BLOCK_SIZE		(1<<ISO_DEFAULT_BLOCK_SHIFT)

/*
 * Used by Microsoft Joliet extension to ISO9660. Almost the same
 * as PVD, but byte position 8 is a flag, and 89-120 is for escape.
 */

struct iso_supplementary_descriptor {
	char type			[ISODCL (  1,   1)]; /* 711 */
	char id				[ISODCL (  2,   6)];
	char version			[ISODCL (  7,   7)]; /* 711 */
	char flags			[ISODCL (  8,   8)];
	char system_id			[ISODCL (  9,  40)]; /* achars */
	char volume_id			[ISODCL ( 41,  72)]; /* dchars */
	char unused2			[ISODCL ( 73,  80)];
	char volume_space_size		[ISODCL ( 81,  88)]; /* 733 */
	char escape			[ISODCL ( 89, 120)];
	char volume_set_size		[ISODCL (121, 124)]; /* 723 */
	char volume_sequence_number	[ISODCL (125, 128)]; /* 723 */
	char logical_block_size		[ISODCL (129, 132)]; /* 723 */
	char path_table_size		[ISODCL (133, 140)]; /* 733 */
	char type_l_path_table		[ISODCL (141, 144)]; /* 731 */
	char opt_type_l_path_table	[ISODCL (145, 148)]; /* 731 */
	char type_m_path_table		[ISODCL (149, 152)]; /* 732 */
	char opt_type_m_path_table	[ISODCL (153, 156)]; /* 732 */
	char root_directory_record	[ISODCL (157, 190)]; /* 9.1 */
	char volume_set_id		[ISODCL (191, 318)]; /* dchars */
	char publisher_id		[ISODCL (319, 446)]; /* achars */
	char preparer_id		[ISODCL (447, 574)]; /* achars */
	char application_id		[ISODCL (575, 702)]; /* achars */
	char copyright_file_id		[ISODCL (703, 739)]; /* 7.5 dchars */
	char abstract_file_id		[ISODCL (740, 776)]; /* 7.5 dchars */
	char bibliographic_file_id	[ISODCL (777, 813)]; /* 7.5 dchars */
	char creation_date		[ISODCL (814, 830)]; /* 8.4.26.1 */
	char modification_date		[ISODCL (831, 847)]; /* 8.4.26.1 */
	char expiration_date		[ISODCL (848, 864)]; /* 8.4.26.1 */
	char effective_date		[ISODCL (865, 881)]; /* 8.4.26.1 */
	char file_structure_version	[ISODCL (882, 882)]; /* 711 */
	char unused4			[ISODCL (883, 883)];
	char application_data		[ISODCL (884, 1395)];
	char unused5			[ISODCL (1396, 2048)];
};

struct iso_directory_record {
	char length			[ISODCL (1, 1)]; /* 711 */
	char ext_attr_length		[ISODCL (2, 2)]; /* 711 */
	u_char extent			[ISODCL (3, 10)]; /* 733 */
	u_char size			[ISODCL (11, 18)]; /* 733 */
	char date			[ISODCL (19, 25)]; /* 7 by 711 */
	char flags			[ISODCL (26, 26)];
	char file_unit_size		[ISODCL (27, 27)]; /* 711 */
	char interleave			[ISODCL (28, 28)]; /* 711 */
	char volume_sequence_number	[ISODCL (29, 32)]; /* 723 */
	char name_len			[ISODCL (33, 33)]; /* 711 */
	char name			[1];			/* XXX */
};
/* can't take sizeof(iso_directory_record), because of possible alignment
   of the last entry (34 instead of 33) */
#define ISO_DIRECTORY_RECORD_SIZE	33

struct iso_extended_attributes {
	u_char owner			[ISODCL (1, 4)]; /* 723 */
	u_char group			[ISODCL (5, 8)]; /* 723 */
	u_char perm			[ISODCL (9, 10)]; /* 9.5.3 */
	char ctime			[ISODCL (11, 27)]; /* 8.4.26.1 */
	char mtime			[ISODCL (28, 44)]; /* 8.4.26.1 */
	char xtime			[ISODCL (45, 61)]; /* 8.4.26.1 */
	char ftime			[ISODCL (62, 78)]; /* 8.4.26.1 */
	char recfmt			[ISODCL (79, 79)]; /* 711 */
	char recattr			[ISODCL (80, 80)]; /* 711 */
	u_char reclen			[ISODCL (81, 84)]; /* 723 */
	char system_id			[ISODCL (85, 116)]; /* achars */
	char system_use			[ISODCL (117, 180)];
	char version			[ISODCL (181, 181)]; /* 711 */
	char len_esc			[ISODCL (182, 182)]; /* 711 */
	char reserved			[ISODCL (183, 246)];
	u_char len_au			[ISODCL (247, 250)]; /* 723 */
};

static __inline int isonum_711(u_char *) __attribute__ ((__unused__));
static __inline int isonum_712(char *) __attribute__ ((__unused__));
static __inline int isonum_721(u_char *) __attribute__ ((__unused__));
static __inline int isonum_722(u_char *) __attribute__ ((__unused__));
static __inline int isonum_723(u_char *) __attribute__ ((__unused__));
static __inline int isonum_731(u_char *) __attribute__ ((__unused__));
static __inline int isonum_732(u_char *) __attribute__ ((__unused__));
static __inline int isonum_733(u_char *) __attribute__ ((__unused__));

/* 7.1.1: unsigned char */
static __inline int
isonum_711(u_char *p)
{
	return *p;
}

/* 7.1.2: signed(?) char */
static __inline int
isonum_712(char *p)
{
	return *p;
}

/* 7.2.1: unsigned little-endian 16-bit value.  NOT USED IN KERNEL. */
static __inline int
isonum_721(u_char *p)
{
#if !defined(__STRICT_ALIGNMENT) && (BYTE_ORDER == LITTLE_ENDIAN)
	return *(u_int16_t *)p;
#else
	return *p|((char)p[1] << 8);
#endif
}

/* 7.2.2: unsigned big-endian 16-bit value.  NOT USED IN KERNEL. */
static __inline int     
isonum_722(unsigned char *p)
{
#if !defined(__STRICT_ALIGNMENT) && (BYTE_ORDER == BIG_ENDIAN)
	return *(u_int16_t *)p;
#else
	return ((char)*p << 8)|p[1];
#endif
} 

/* 7.2.3: unsigned both-endian (little, then big) 16-bit value */
static __inline int
isonum_723(u_char *p)
{
#if !defined(__STRICT_ALIGNMENT) && \
    ((BYTE_ORDER == LITTLE_ENDIAN) || (BYTE_ORDER == BIG_ENDIAN))
#if BYTE_ORDER == LITTLE_ENDIAN
	return *(u_int16_t *)p;
#else
	return *(u_int16_t *)(p + 2);
#endif
#else /* __STRICT_ALIGNMENT or weird byte order */
	return *p|(p[1] << 8);
#endif
}

/* 7.3.1: unsigned little-endian 32-bit value.  NOT USED IN KERNEL. */
static __inline int
isonum_731(u_char *p)
{
#if !defined(__STRICT_ALIGNMENT) && (BYTE_ORDER == LITTLE_ENDIAN)
	return *(u_int32_t *)p;
#else
	return *p|(p[1] << 8)|(p[2] << 16)|(p[3] << 24);
#endif
}

/* 7.3.2: unsigned big-endian 32-bit value.  NOT USED IN KERNEL. */
static __inline int
isonum_732(unsigned char *p)
{
#if !defined(__STRICT_ALIGNMENT) && (BYTE_ORDER == BIG_ENDIAN)
	return *(u_int32_t *)p;
#else
	return (*p << 24)|(p[1] << 16)|(p[2] << 8)|p[3];
#endif
}

/* 7.3.3: unsigned both-endian (little, then big) 32-bit value */
static __inline int
isonum_733(u_char *p)
{
#if !defined(__STRICT_ALIGNMENT) && \
    ((BYTE_ORDER == LITTLE_ENDIAN) || (BYTE_ORDER == BIG_ENDIAN))
#if BYTE_ORDER == LITTLE_ENDIAN
	return *(u_int32_t *)p;
#else
	return *(u_int32_t *)(p + 4);
#endif
#else /* __STRICT_ALIGNMENT or weird byte order */
	return *p|(p[1] << 8)|(p[2] << 16)|(p[3] << 24);
#endif
}

/*
 * Associated files have a leading '='.
 */
#define	ASSOCCHAR	'='
