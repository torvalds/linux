/*	$OpenBSD: common.h,v 1.10 2022/12/28 21:30:17 jmc Exp $	*/
/*	$NetBSD: common.h,v 1.9 2011/08/30 19:49:10 joerg Exp $	*/

/*
 * Copyright (c) 1993-95 Mats O Jansson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _COMMON_H_
#define _COMMON_H_

#define MAXDL		16		/* maximum number concurrent load */
#define IFNAME_SIZE	32		/* maximum size if interface name */
#define BUFSIZE		1600		/* main receive buffer size	*/
#define HDRSIZ		22		/* room for 803.2 header	*/

#ifndef MOP_FILE_PATH
#define MOP_FILE_PATH	"/tftpboot/mop"
#endif

#define DEBUG_ONELINE	1
#define DEBUG_HEADER	2
#define DEBUG_INFO	3

/*
 * structure per interface
 *
 */

struct if_info {
	int	fd;			/* File Descriptor                 */
	int	trans;			/* Transport type Ethernet/802.3   */
	u_char	eaddr[6];		/* Ethernet addr of this interface */
	char	if_name[IFNAME_SIZE];	/* Interface Name		   */
	int	(*iopen)(char *, int, u_short, int);
					/* Interface Open Routine	   */
	int	(*write)(int, u_char *, int, int);
					/* Interface Write Routine	   */
	void	(*read)(void);		/* Interface Read Routine          */
	struct if_info *next;		/* Next Interface		   */
};

#define DL_STATUS_FREE		 0
#define DL_STATUS_READ_IMGHDR	 1
#define DL_STATUS_SENT_MLD	 2
#define DL_STATUS_SENT_PLT	 3

typedef enum {
	IMAGE_TYPE_MOP,			/* MOP image */
	IMAGE_TYPE_AOUT,		/* a.out image */
	IMAGE_TYPE_ELF32,		/* Elf32 image */
	IMAGE_TYPE_ELF64		/* Elf64 image */
} mopd_imagetype;

struct dllist {
	u_char		status;		/* Status byte			*/
	struct if_info *ii;		/* interface pointer		*/
	u_char		eaddr[6];	/* targets ethernet address	*/
	int		ldfd;		/* filedescriptor for loadfile	*/
	u_short		dl_bsz;		/* Data Link Buffer Size	*/
	int		timeout;	/* Timeout counter		*/
	u_char		count;		/* Packet Counter		*/
	u_int32_t	loadaddr;	/* Load Address			*/
	u_int32_t	xferaddr;	/* Transfer Address		*/
	u_int32_t	nloadaddr;	/* Next Load Address		*/
	off_t		lseek;		/* Seek before last read	*/
	mopd_imagetype	image_type;	/* what type of image is it?	*/

	/* For ELF files */
	int		e_machine;	/* Machine ID			*/
	int		e_nsec;		/* number of program sections	*/
#define	SEC_MAX	4
	struct {
		off_t s_foff;		/* file offset of section	*/
		u_int32_t s_vaddr;	/* virtual address of section	*/
		u_int32_t s_fsize;	/* file size of section		*/
		u_int32_t s_msize;	/* memory size of section	*/
		u_int32_t s_pad;	/* padding until next section	*/
		u_int32_t s_loff;	/* logical offset into image	*/
	} e_sections[SEC_MAX];		/* program sections		*/
	u_int32_t	e_curpos;	/* current logical position	*/
	int		e_cursec;	/* current section */

	/* For a.out files */
	int		a_mid;		/* Machine ID			*/
	u_int32_t	a_text;		/* Size of text segment		*/
	u_int32_t	a_text_fill;	/* Size of text segment fill	*/
	u_int32_t	a_data;		/* Size of data segment		*/
	u_int32_t	a_data_fill;	/* Size of data segment fill	*/
	u_int32_t	a_bss;		/* Size of bss segment		*/
	u_int32_t	a_bss_fill;	/* Size of bss segment fill	*/
	off_t		a_lseek;	/* Keep track of pos in newfile */
};

#endif /* _COMMON_H_ */
