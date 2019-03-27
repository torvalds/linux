/*
 * Copyright (c) 1996, 1998 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef DOSIO_H
#define DOSIO_H

/*
 * DOS file attributes
 */

#define FA_RDONLY  001          /* read-only */
#define FA_HIDDEN  002          /* hidden file */
#define FA_SYSTEM  004          /* system file */
#define FA_LABEL   010          /* volume label */
#define FA_DIR     020          /* directory */
#define FA_ARCH    040          /* archive (file modified) */
#define FA_XDE     017          /* extended directory entry */
#define FA_MASK    077          /* all attributes */

/*
 * Macros to convert DOS-format 16-bit and 32-bit quantities
 */

#define cv2(p)  ((uint16_t)(p)[0] |         \
                ((uint16_t)(p)[1] << 010))
#define cv4(p)  ((uint32_t)(p)[0] |          \
                ((uint32_t)(p)[1] << 010) |  \
                ((uint32_t)(p)[2] << 020) |  \
                ((uint32_t)(p)[3] << 030))

/*
 * Directory, filesystem, and file structures.
 */

typedef struct {
    u_char x_case;              /* case */
    u_char c_hsec;              /* created: secs/100 */
    u_char c_time[2];           /* created: time */
    u_char c_date[2];           /* created: date */
    u_char a_date[2];           /* accessed: date */
    u_char h_clus[2];           /* clus[hi] */
} DOS_DEX;

typedef struct {
    u_char name[8];             /* name */
    u_char ext[3];              /* extension */
    u_char attr;                /* attributes */
    DOS_DEX dex;                /* VFAT/FAT32 only */
    u_char time[2];             /* modified: time */
    u_char date[2];             /* modified: date */
    u_char clus[2];             /* starting cluster */
    u_char size[4];             /* size */
} DOS_DE;

typedef struct {
    u_char seq;                 /* flags */
    u_char name1[5][2];         /* 1st name area */
    u_char attr;                /* (see fat_de) */
    u_char res;                 /* reserved */
    u_char chk;                 /* checksum */
    u_char name2[6][2];         /* 2nd name area */
    u_char clus[2];             /* (see fat_de) */
    u_char name3[2][2];         /* 3rd name area */
} DOS_XDE;

typedef union {
    DOS_DE de;                  /* standard directory entry */
    DOS_XDE xde;                /* extended directory entry */
} DOS_DIR;

typedef struct {
    struct open_file *fd;       /* file descriptor */
    u_char *fatbuf;             /* FAT cache buffer */
    u_int fatbuf_blknum;        /* number of 128K block in FAT cache buffer */
    u_int links;                /* active links to structure */
    u_int spc;                  /* sectors per cluster */
    u_int bsize;                /* cluster size in bytes */
    u_int bshift;               /* cluster conversion shift */
    u_int dirents;              /* root directory entries */
    u_int spf;                  /* sectors per fat */
    u_int rdcl;                 /* root directory start cluster */
    u_int lsnfat;               /* start of fat */
    u_int lsndir;               /* start of root dir */
    u_int lsndta;               /* start of data area */
    u_int fatsz;                /* FAT entry size */
    u_int xclus;                /* maximum cluster number */
    DOS_DE root;
} DOS_FS;

typedef struct {
    DOS_FS *fs;                 /* associated filesystem */
    DOS_DE de;                  /* directory entry */
    u_int offset;               /* current offset */
    u_int c;                    /* last cluster read */
} DOS_FILE;

#endif  /* !DOSIO_H */
