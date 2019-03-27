/*
 * Prototypes for the OSTA functions
 *
 * $FreeBSD$
 */

/*-
 **********************************************************************
 * OSTA compliant Unicode compression, uncompression routines.
 * Copyright 1995 Micro Design International, Inc.
 * Written by Jason M. Rinn.
 * Micro Design International gives permission for the free use of the
 * following source code.
 */

/*
 * Various routines from the OSTA 2.01 specs.  Copyrights are included with
 * each code segment.  Slight whitespace modifications have been made for
 * formatting purposes.  Typos/bugs have been fixed.
 */

#ifndef UNIX
#define	UNIX
#endif

#ifndef MAXLEN
#define	MAXLEN	255
#endif

/***********************************************************************
 * The following two typedef's are to remove compiler dependencies.
 * byte needs to be unsigned 8-bit, and unicode_t needs to be
 * unsigned 16-bit.
 */
typedef unsigned short unicode_t;
typedef unsigned char byte;

int udf_UncompressUnicode(int, byte *, unicode_t *);
int udf_UncompressUnicodeByte(int, byte *, byte *);
int udf_CompressUnicode(int, int, unicode_t *, byte *);
unsigned short udf_cksum(unsigned char *, int);
unsigned short udf_unicode_cksum(unsigned short *, int);
int UDFTransName(unicode_t *, unicode_t *, int);
