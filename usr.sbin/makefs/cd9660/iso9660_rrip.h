/*	$NetBSD: iso9660_rrip.h,v 1.5 2009/01/10 22:06:29 bjh21 Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2005 Daniel Watt, Walter Deignan, Ryan Gabrys, Alan
 * Perez-Rathke and Ram Vedam.  All rights reserved.
 *
 * This code was written by Daniel Watt, Walter Deignan, Ryan Gabrys,
 * Alan Perez-Rathke and Ram Vedam.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY DANIEL WATT, WALTER DEIGNAN, RYAN
 * GABRYS, ALAN PEREZ-RATHKE AND RAM VEDAM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL DANIEL WATT, WALTER DEIGNAN, RYAN
 * GABRYS, ALAN PEREZ-RATHKE AND RAM VEDAM BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE,DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef __ISO9660_RRIP_H__
#define __ISO9660_RRIP_H__

/*
 * This will hold all the functions needed to
 * write an ISO 9660 image with Rock Ridge Extensions
 */

/* For writing must use ISO_RRIP_EXTREF structure */

#include "makefs.h"
#include <cd9660_rrip.h>
#include "cd9660.h"
#include <sys/queue.h>

#define	 PX_LENGTH	   0x2C
#define	 PN_LENGTH	   0x14
#define	 TF_CREATION	   0x00
#define	 TF_MODIFY	   0x01
#define	 TF_ACCESS	   0x02
#define	 TF_ATTRIBUTES	   0x04
#define	 TF_BACKUP	   0x08
#define	 TF_EXPIRATION	   0x10
#define	 TF_EFFECTIVE	   0x20
#define	 TF_LONGFORM	   0x40
#define  NM_CONTINUE	   0x80
#define	 NM_CURRENT	   0x100
#define	 NM_PARENT	   0x200


#define	 SUSP_LOC_ENTRY	   0x01
#define	 SUSP_LOC_DOT	   0x02
#define	 SUSP_LOC_DOTDOT   0x04

#define SUSP_TYPE_SUSP		1
#define SUSP_TYPE_RRIP		2

#define SUSP_ENTRY_SUSP_CE	1
#define SUSP_ENTRY_SUSP_PD	2
#define SUSP_ENTRY_SUSP_SP	3
#define SUSP_ENTRY_SUSP_ST	4
#define SUSP_ENTRY_SUSP_ER	5
#define SUSP_ENTRY_SUSP_ES	6

#define SUSP_ENTRY_RRIP_PX	1
#define SUSP_ENTRY_RRIP_PN	2
#define SUSP_ENTRY_RRIP_SL	3
#define SUSP_ENTRY_RRIP_NM	4
#define SUSP_ENTRY_RRIP_CL	5
#define SUSP_ENTRY_RRIP_PL	6
#define SUSP_ENTRY_RRIP_RE	7
#define SUSP_ENTRY_RRIP_TF	8
#define SUSP_ENTRY_RRIP_SF	9

#define SUSP_RRIP_ER_EXT_ID "IEEE_P1282"
#define SUSP_RRIP_ER_EXT_DES "THE IEEE P1282 PROTOCOL PROVIDES SUPPORT FOR POSIX FILE SYSTEM SEMANTICS."
#define SUSP_RRIP_ER_EXT_SRC "PLEASE CONTACT THE IEEE STANDARDS DEPARTMENT, PISCATAWAY, NJ, USA FOR THE P1282 SPECIFICATION."

#define SL_FLAGS_NONE	        0
#define SL_FLAGS_CONTINUE       1
#define SL_FLAGS_CURRENT        2
#define SL_FLAGS_PARENT	        4
#define SL_FLAGS_ROOT	        8

typedef struct {
	ISO_SUSP_HEADER		 h;
	u_char mode		[ISODCL(5,12)];
	u_char links		[ISODCL(13,20)];
	u_char uid		[ISODCL(21,28)];
	u_char gid		[ISODCL(29,36)];
	u_char serial		[ISODCL(37,44)];
} ISO_RRIP_PX;

typedef struct {
	ISO_SUSP_HEADER		 h;
	u_char high		[ISODCL(5,12)];
	u_char low		[ISODCL(13,20)];
} ISO_RRIP_PN;

typedef struct {
	ISO_SUSP_HEADER		 h;
	u_char flags		 [ISODCL ( 4, 4)];
	u_char component	 [ISODCL ( 4, 256)];
	u_int  nBytes;
} ISO_RRIP_SL;

typedef struct {
	ISO_SUSP_HEADER		 h;
	u_char flags		 [ISODCL ( 4, 4)];
	u_char timestamp	 [ISODCL ( 5, 256)];
} ISO_RRIP_TF;

#define RRIP_NM_FLAGS_NONE 0x00
#define RRIP_NM_FLAGS_CONTINUE 0x01
#define RRIP_NM_FLAGS_CURRENT 0x02
#define RRIP_NM_FLAGS_PARENT 0x04

typedef struct {
	ISO_SUSP_HEADER		 h;
	u_char flags		 [ISODCL ( 4, 4)];
	u_char altname		 [ISODCL ( 4, 256)];
} ISO_RRIP_NM;

/* Note that this is the same structure as cd9660_rrip.h : ISO_RRIP_CONT */
typedef struct {
	ISO_SUSP_HEADER		 h;
	u_char ca_sector	 [ISODCL ( 5, 12)];
	u_char offset		 [ISODCL ( 13, 20)];
	u_char length		 [ISODCL ( 21, 28)];
} ISO_SUSP_CE;

typedef struct {
	ISO_SUSP_HEADER		 h;
	u_char padding_area	 [ISODCL ( 4, 256)];
} ISO_SUSP_PD;

typedef struct {
	ISO_SUSP_HEADER		 h;
	u_char check		 [ISODCL ( 4, 5)];
	u_char len_skp		 [ISODCL ( 6, 6)];
} ISO_SUSP_SP;

typedef struct {
	ISO_SUSP_HEADER		 h;
} ISO_SUSP_ST;

typedef struct {
	ISO_SUSP_HEADER		 h;
	u_char len_id		 [ISODCL ( 4, 4)];
	u_char len_des		 [ISODCL ( 5, 5)];
	u_char len_src		 [ISODCL ( 6, 6)];
	u_char ext_ver		 [ISODCL ( 7, 7)];
	u_char ext_data		 [ISODCL (8,256)];
/*	u_char ext_id		 [ISODCL ( 8, 256)];
	u_char ext_des		 [ISODCL ( 257, 513)];
	u_char ext_src		 [ISODCL ( 514, 770)];*/
} ISO_SUSP_ER;

typedef struct {
	ISO_SUSP_HEADER		 h;
	u_char ext_seq		 [ISODCL ( 4, 4)];
} ISO_SUSP_ES;

typedef union {
	ISO_RRIP_PX			PX;
	ISO_RRIP_PN			PN;
	ISO_RRIP_SL			SL;
	ISO_RRIP_NM			NM;
	ISO_RRIP_CLINK			CL;
	ISO_RRIP_PLINK			PL;
	ISO_RRIP_RELDIR			RE;
	ISO_RRIP_TF			TF;
} rrip_entry;

typedef union {
	ISO_SUSP_CE			CE;
	ISO_SUSP_PD			PD;
	ISO_SUSP_SP			SP;
	ISO_SUSP_ST			ST;
	ISO_SUSP_ER			ER;
	ISO_SUSP_ES			ES;
} susp_entry;

typedef union {
	susp_entry		  su_entry;
	rrip_entry		  rr_entry;
} SUSP_ENTRIES;

struct ISO_SUSP_ATTRIBUTES {
	SUSP_ENTRIES attr;
	int type;
	char type_of[2];
	char last_in_suf;	/* last entry in the System Use Field? */
	/* Dan's addons - will merge later. This allows use of a switch */
	char susp_type; 	/* SUSP or RRIP */
	char entry_type;	/* Record type */
	char write_location;
	TAILQ_ENTRY(ISO_SUSP_ATTRIBUTES) rr_ll;
};

#define CD9660_SUSP_ENTRY_SIZE(entry)\
	((int) ((entry)->attr.su_entry.SP.h.length[0]))

/* Recursive function - move later to func pointer code*/
int cd9660_susp_finalize(iso9660_disk *, cd9660node *);

/* These two operate on single nodes */
int cd9660_susp_finalize_node(iso9660_disk *, cd9660node *);
int cd9660_rrip_finalize_node(cd9660node *);

/* POSIX File attribute */
int cd9660node_rrip_px(struct ISO_SUSP_ATTRIBUTES *, fsnode *);

/* Device number */
int cd9660node_rrip_pn(struct ISO_SUSP_ATTRIBUTES *, fsnode *);

/* Symbolic link */
int cd9660node_rrip_SL(struct ISO_SUSP_ATTRIBUTES *, fsnode *);

/* Alternate Name function */
void cd9660_rrip_NM(cd9660node *);
void cd9660_rrip_add_NM(cd9660node *,const char *);

/* Parent and child link function */
int cd9660_rrip_PL(struct ISO_SUSP_ATTRIBUTES *, cd9660node *);
int cd9660_rrip_CL(struct ISO_SUSP_ATTRIBUTES *, cd9660node *);
int cd9660_rrip_RE(struct ISO_SUSP_ATTRIBUTES *, cd9660node *);

int cd9660node_rrip_tf(struct ISO_SUSP_ATTRIBUTES *, fsnode *);



/*
 * Relocation directory function. I'm not quite sure what
 * sort of parameters are needed, but personally I don't think
 * any parameters are needed except for the memory address where
 * the information needs to be put in
 */
int cd9660node_rrip_re(void *, fsnode *);

/*
 * Don't know if this function is needed because it apparently is an
 * optional feature that does not really need to be implemented but I
 * thought I should add it anyway.
 */
int cd9660_susp_ce (struct ISO_SUSP_ATTRIBUTES *, cd9660node *);
int cd9660_susp_pd (struct ISO_SUSP_ATTRIBUTES *, int);
int cd9660_susp_sp (struct ISO_SUSP_ATTRIBUTES *, cd9660node *);
int cd9660_susp_st (struct ISO_SUSP_ATTRIBUTES *, cd9660node *);

struct ISO_SUSP_ATTRIBUTES *cd9660_susp_ER(cd9660node *, u_char, const char *,
    const char *, const char *);
struct ISO_SUSP_ATTRIBUTES *cd9660_susp_ES(struct ISO_SUSP_ATTRIBUTES*,
    cd9660node *);


/* Helper functions */

/* Common SUSP/RRIP functions */
int cd9660_susp_initialize(iso9660_disk *, cd9660node *, cd9660node *,
    cd9660node *);
int cd9660_susp_initialize_node(iso9660_disk *, cd9660node *);
struct ISO_SUSP_ATTRIBUTES *cd9660node_susp_create_node(int, int, const char *,
    int);
struct ISO_SUSP_ATTRIBUTES *cd9660node_susp_add_entry(cd9660node *,
    struct ISO_SUSP_ATTRIBUTES *, struct ISO_SUSP_ATTRIBUTES *, int);

/* RRIP specific functions */
int cd9660_rrip_initialize_node(iso9660_disk *, cd9660node *, cd9660node *,
    cd9660node *);
void cd9660_createSL(cd9660node *);

/* Functions that probably can be removed */
/* int cd9660node_initialize_node(int, char *); */


#endif
