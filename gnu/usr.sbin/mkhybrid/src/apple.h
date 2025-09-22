/*
** apple.h:	cut down macfile.h from CAP distribution
*/
#ifndef _APPLE_H

#include <sys/param.h>
#include <mactypes.h>

#ifdef __sgi		/* bit of a hack ... need to investigate further */
#define __svr4__	/* maybe there's a "configure" solution ? */
#endif /* __sgi */

#ifdef __svr4__
#include <sys/statvfs.h>
#else
#if defined(__FreeBSD__) || defined(__bsdi__) || defined(__OpenBSD__)
#include <sys/mount.h>
#else
#if defined(_IBMR2)
#include <sys/statfs.h>
#else
#include <sys/vfs.h>
#endif /* _IBMR2 */
#endif /* __FreeBSD__ || __bsdi__ */
#endif /* __svr4__ */

#ifndef O_BINARY
#define O_BINARY 0
#endif /* O_BINARY */

#ifdef _WIN32_TEST
#undef UNICODE
#include <windows.h>
#endif /* _WIN32 */

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif /* MIN */

#define CT_SIZE		4			/* Size of type/creator */
#define NUMMAP		512			/* initial number of maps */
#define BLANK		"    "			/* blank type/creator */
#define DEFMATCH	"*"			/* default mapping extension */

typedef struct {
	char		*extn;			/* filename extension */
	int		elen;			/* length of extension */
	char		type[CT_SIZE+1];	/* extension type */
	char		creator[CT_SIZE+1];	/* extension creator */
	unsigned short	fdflags;		/* finder flags */
} afpmap;

/* from "data.h" - libhfs routines */
unsigned long d_toutime(unsigned long);
long d_getl(unsigned char *);
short d_getw(unsigned char *);

#include "libfile/proto.h"

/****** TYPE_CAP ******/

/*
 * taken from the CAP distribution:
 * macfile.h - header file with Macintosh file definitions 
 *
 * AppleTalk package for UNIX (4.2 BSD).
 *
 * Copyright (c) 1986, 1987, 1988 by The Trustees of Columbia University in the
 * City of New York.
 *
 * Edit History:
 *
 *  Sept 1987	Created by Charlie
 *
 */


#ifndef USE_MAC_DATES
#define	USE_MAC_DATES
#endif /* USE_MAC_DATES */

typedef unsigned char byte;
typedef char sbyte;
typedef unsigned short word;
typedef short sword;
typedef unsigned int dword;
typedef int sdword;
/*
typedef unsigned long dword;
typedef long sdword;
*/

#define MAXCLEN 199		/* max size of a comment string */
#define FINFOLEN 32		/* Finder info is 32 bytes */
#define MAXMACFLEN 31		/* max Mac file name length */

typedef struct {
  /* base finder information */
  byte fdType[4];		/* File type [4]*/
  byte fdCreator[4];		/* File creator [8]*/
  word fdFlags;			/* Finder flags [10]*/
  word fdLocation[2];		/* File's location [14] */
  word fdFldr;			/* File's window [16] */
  /* extended finder information */
  word fdIconID;		/* Icon ID [18] */
  word fdUnused[4];		/* Unused [26] */
  word fdComment;		/* Comment ID [28] */
  dword fdPutAway;		/* Home directory ID [32] */
  word fi_attr;			/* attributes */
#define FI_MAGIC1 255
  byte fi_magic1;		/* was: length of comment */
#define FI_VERSION 0x10		/* version major 1, minor 0 */
				/* if we have more than 8 versions wer're */
				/* doiong something wrong anyway */
  byte fi_version;		/* version number */
#define FI_MAGIC 0xda
  byte fi_magic;		/* magic word check */
  byte fi_bitmap;		/* bitmap of included info */
#define FI_BM_SHORTFILENAME 0x1	/* is this included? */
#define FI_BM_MACINTOSHFILENAME 0x2 /* is this included? */
  byte fi_shortfilename[12+1];	/* possible short file name */
  byte fi_macfilename[32+1];	/* possible macintosh file name */
  byte fi_comln;		/* comment length */
  byte fi_comnt[MAXCLEN+1];	/* comment string */
#ifdef USE_MAC_DATES
  byte fi_datemagic;		/* sanity check */
#define FI_MDATE 0x01		/* mtime & utime are valid */
#define FI_CDATE 0x02		/* ctime is valid */
  byte fi_datevalid;		/* validity flags */
  byte fi_ctime[4];		/* mac file create time */
  byte fi_mtime[4];		/* mac file modify time */
  byte fi_utime[4];		/* (real) time mtime was set */
#endif /* USE_MAC_DATES */
} FileInfo;

/* Atribute flags */
#define FI_ATTR_SETCLEAR 0x8000 /* set-clear attributes */
#define FI_ATTR_READONLY 0x20	/* file is read-only */
#define FI_ATTR_ROPEN 0x10	/* resource fork in use */
#define FI_ATTR_DOPEN 0x80	/* data fork in use */
#define FI_ATTR_MUSER 0x2	/* multi-user */
#define FI_ATTR_INVISIBLE 0x1	/* invisible */

/**** MAC STUFF *****/

/* Flags */
#define FNDR_fOnDesk 0x1
#define FNDR_fHasBundle 0x2000
#define FNDR_fInvisible 0x4000
/* locations */
#define FNDR_fTrash -3	/* File in Trash */
#define FNDR_fDesktop -2	/* File on desktop */
#define FNDR_fDisk 0	/* File in disk window */

/****** TYPE_ESHARE ******/

/*
**	Information supplied by Jens-Uwe Mager (jum@helios.de)
*/

#define ES_VERSION 	0x0102
#define ES_MAGIC 	0x3681093
#define ES_INFOLEN	32
#define ES_INFO_SIZE	512

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;

typedef struct {
	uint32	  	magic;
	uint32	  	serno;			/* written only, never read */
	uint16	  	version;
	uint16	  	attr;			/* invisible... */
	uint16	  	openMax;		/* max number of opens */
	uint16	  	filler0;
	uint32	  	backupCleared;		/* time backup bit cleared */
	uint32          id;			/* dir/file id */
        uint32          createTime;             /* unix format */
        uint32          backupTime;             /* unix format */
/*	uint8		finderInfo[INFOLEN];*/
  	/* base finder information (compatible with CAP) */
	uint8		fdType[4];		/* File type [4]*/
	uint8		fdCreator[4];		/* File creator [8]*/
	uint16		fdFlags;		/* Finder flags [10]*/
	uint16		fdLocation[2];		/* File's location [14] */
	uint16		fdFldr;			/* File's window [16] */
	/* extended finder information */
	uint16		fdIconID;		/* Icon ID [18] */
	uint16		fdUnused[4];		/* Unused [26] */
	uint16		fdComment;		/* Comment ID [28] */
	uint32		fdPutAway;		/* Home directory ID [32] */
} es_FileInfo;

/****** TYPE_USHARE ******/

/* similar to the EtherShare layout, but the finder info stuff is different
   info provided by: Phil Sylvester <psylvstr@interaccess.com> */

typedef struct {
	uint8		fdType[4];		/* File type [4]*/
	uint8		fdCreator[4];		/* File creator [8]*/
	uint16		fdFlags;		/* Finder flags [10]*/
	uint8		unknown1[22];		/* ignore [32] */
	uint32		btime;			/* mac file backup time [36]*/
	uint8		unknown2[4];		/* ignore [40] */
	uint32		ctime;			/* mac file create time [44]*/
	uint8		unknown3[8];		/* ignore [52] */
	uint32		mtime;			/* mac file modify time [56]*/
	uint8		unknown4[456];		/* ignore [512] */
} us_FileInfo;

/****** TYPE_DOUBLE, TYPE_SINGLE ******/

/*
**	Taken from cvt2cap (c) May 1988, Paul Campbell
*/

typedef struct {
	dword id;
	dword offset;
	dword length;
} a_entry;

typedef struct {
	dword   magic;
	dword   version;
	char    home[16];
	word    nentries;
	a_entry	entries[1];
} a_hdr;

#define A_HDR_SIZE	26
#define A_ENTRY_SIZE	sizeof(a_entry)

#define A_VERSION	0x00010000
#define APPLE_SINGLE	0x00051600
#define APPLE_DOUBLE	0x00051607
#define ID_DATA		1
#define ID_RESOURCE	2
#define ID_NAME		3
#define ID_FINDER	9

/****** TYPE_MACBIN ******/
/*
**	taken from capit.c by Nigel Perry, np@doc.ic.ac.uk which is adapted
**	from unmacbin by John M. Sellens, jmsellens@watdragon.uwaterloo.ca
*/


#define MB_NAMELEN 63              /* maximum legal Mac file name length */
#define MB_SIZE 128

/* Format of a bin file:
A bin file is composed of 128 byte blocks.  The first block is the
info_header (see below).  Then comes the data fork, null padded to fill the
last block.  Then comes the resource fork, padded to fill the last block.  A
proposal to follow with the text of the Get Info box has not been implemented,
to the best of my knowledge.  Version, zero1 and zero2 are what the receiving
program looks at to determine if a MacBinary transfer is being initiated.
*/ 
typedef struct {     /* info file header (128 bytes). Unfortunately, these
                        longs don't align to word boundaries */
            byte version;           /* there is only a version 0 at this time */
            byte nlen;              /* Length of filename. */
            byte name[MB_NAMELEN];  /* Filename */
            byte type[4];           /* File type. */
            byte auth[4];           /* File creator. */
            byte flags;             /* file flags: LkIvBnSyBzByChIt */
            byte zero1;             /* Locked, Invisible,Bundle, System */
                                    /* Bozo, Busy, Changed, Init */
            byte icon_vert[2];      /* Vertical icon position within window */
            byte icon_horiz[2];     /* Horizontal icon postion in window */
            byte window_id[2];      /* Window or folder ID. */
            byte protect;           /* = 1 for protected file, 0 otherwise */
            byte zero2;
            byte dflen[4];          /* Data Fork length (bytes) - most sig.  */
            byte rflen[4];          /* Resource Fork length       byte first */
            byte cdate[4];          /* File's creation date. */
            byte mdate[4];          /* File's "last modified" date. */
            byte ilen[2];           /* GetInfo message length */
	    byte flags2;            /* Finder flags, bits 0-7 */
	    byte unused[14];       
	    byte packlen[4];        /* length of total files when unpacked */
	    byte headlen[2];        /* length of secondary header */
	    byte uploadvers;        /* Version of MacBinary II that the uploading program is written for */
	    byte readvers;          /* Minimum MacBinary II version needed to read this file */
            byte crc[2];            /* CRC of the previous 124 bytes */
	    byte padding[2];        /* two trailing unused bytes */
} mb_info;

/*
**	An array useful for CRC calculations that use 0x1021 as the "seed"
**	taken from mcvert.c modified by Jim Van Verth.
*/

static unsigned short mb_magic[] = {
    0x0000,  0x1021,  0x2042,  0x3063,  0x4084,  0x50a5,  0x60c6,  0x70e7,
    0x8108,  0x9129,  0xa14a,  0xb16b,  0xc18c,  0xd1ad,  0xe1ce,  0xf1ef,
    0x1231,  0x0210,  0x3273,  0x2252,  0x52b5,  0x4294,  0x72f7,  0x62d6,
    0x9339,  0x8318,  0xb37b,  0xa35a,  0xd3bd,  0xc39c,  0xf3ff,  0xe3de,
    0x2462,  0x3443,  0x0420,  0x1401,  0x64e6,  0x74c7,  0x44a4,  0x5485,
    0xa56a,  0xb54b,  0x8528,  0x9509,  0xe5ee,  0xf5cf,  0xc5ac,  0xd58d,
    0x3653,  0x2672,  0x1611,  0x0630,  0x76d7,  0x66f6,  0x5695,  0x46b4,
    0xb75b,  0xa77a,  0x9719,  0x8738,  0xf7df,  0xe7fe,  0xd79d,  0xc7bc,
    0x48c4,  0x58e5,  0x6886,  0x78a7,  0x0840,  0x1861,  0x2802,  0x3823,
    0xc9cc,  0xd9ed,  0xe98e,  0xf9af,  0x8948,  0x9969,  0xa90a,  0xb92b,
    0x5af5,  0x4ad4,  0x7ab7,  0x6a96,  0x1a71,  0x0a50,  0x3a33,  0x2a12,
    0xdbfd,  0xcbdc,  0xfbbf,  0xeb9e,  0x9b79,  0x8b58,  0xbb3b,  0xab1a,
    0x6ca6,  0x7c87,  0x4ce4,  0x5cc5,  0x2c22,  0x3c03,  0x0c60,  0x1c41,
    0xedae,  0xfd8f,  0xcdec,  0xddcd,  0xad2a,  0xbd0b,  0x8d68,  0x9d49,
    0x7e97,  0x6eb6,  0x5ed5,  0x4ef4,  0x3e13,  0x2e32,  0x1e51,  0x0e70,
    0xff9f,  0xefbe,  0xdfdd,  0xcffc,  0xbf1b,  0xaf3a,  0x9f59,  0x8f78,
    0x9188,  0x81a9,  0xb1ca,  0xa1eb,  0xd10c,  0xc12d,  0xf14e,  0xe16f,
    0x1080,  0x00a1,  0x30c2,  0x20e3,  0x5004,  0x4025,  0x7046,  0x6067,
    0x83b9,  0x9398,  0xa3fb,  0xb3da,  0xc33d,  0xd31c,  0xe37f,  0xf35e,
    0x02b1,  0x1290,  0x22f3,  0x32d2,  0x4235,  0x5214,  0x6277,  0x7256,
    0xb5ea,  0xa5cb,  0x95a8,  0x8589,  0xf56e,  0xe54f,  0xd52c,  0xc50d,
    0x34e2,  0x24c3,  0x14a0,  0x0481,  0x7466,  0x6447,  0x5424,  0x4405,
    0xa7db,  0xb7fa,  0x8799,  0x97b8,  0xe75f,  0xf77e,  0xc71d,  0xd73c,
    0x26d3,  0x36f2,  0x0691,  0x16b0,  0x6657,  0x7676,  0x4615,  0x5634,
    0xd94c,  0xc96d,  0xf90e,  0xe92f,  0x99c8,  0x89e9,  0xb98a,  0xa9ab,
    0x5844,  0x4865,  0x7806,  0x6827,  0x18c0,  0x08e1,  0x3882,  0x28a3,
    0xcb7d,  0xdb5c,  0xeb3f,  0xfb1e,  0x8bf9,  0x9bd8,  0xabbb,  0xbb9a,
    0x4a75,  0x5a54,  0x6a37,  0x7a16,  0x0af1,  0x1ad0,  0x2ab3,  0x3a92,
    0xfd2e,  0xed0f,  0xdd6c,  0xcd4d,  0xbdaa,  0xad8b,  0x9de8,  0x8dc9,
    0x7c26,  0x6c07,  0x5c64,  0x4c45,  0x3ca2,  0x2c83,  0x1ce0,  0x0cc1,
    0xef1f,  0xff3e,  0xcf5d,  0xdf7c,  0xaf9b,  0xbfba,  0x8fd9,  0x9ff8,
    0x6e17,  0x7e36,  0x4e55,  0x5e74,  0x2e93,  0x3eb2,  0x0ed1,  0x1ef0
};


/****** TYPE_FE ******/

/* Information provided by Mark Weinstein <mrwesq@earthlink.net> */

typedef struct {
	byte	nlen;
	byte	name[31];
	byte	type[4];
	byte	creator[4];
	byte	flags[2];
	byte	location[4];
	byte	fldr[2];
	byte	xinfo[16];
	byte	cdate[4];
	byte	mdate[4];
	byte	bdate[4];
	byte	fileid[4];
	byte	sname[8];
	byte	ext[3];
	byte	pad;
} fe_info;

#define FE_SIZE 92

/****** TYPE_SGI ******/

typedef struct {
	char    unknown1[8];
	char    type[4];
	char    creator[4];
	char    unknown2[238];
	char    name[32];
	char    unknown3[14];
} sgi_info;

#define SGI_SIZE 300

#define _APPLE_H
#endif /* _APPLE_H */
