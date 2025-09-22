/*	$OpenBSD: som.h,v 1.4 2004/04/07 18:24:20 mickey Exp $	*/

/*
 * Copyright (c) 1998 Michael Shalayeff
 * All rights reserved.
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_MACHINE_SOM_H_
#define	_MACHINE_SOM_H_

/* system_id */
#define	SOM_BSD		800
#define	SOM_PA10	0x20b
#define	SOM_PA11	0x210
#define	SOM_PA12	0x211
#define	SOM_PA20	0x214

/* a_magic */
#define	SOM_MAGIC	0x107
#define	SOM_SHARED	0x108
#define	SOM_DEMAND	0x10B

#define	SOM_BADMAGIC(fh) \
	((fh)->system_id != SOM_PA10 && \
	 (fh)->system_id != SOM_PA11 && \
	 (fh)->system_id != SOM_PA12 && \
	 (fh)->system_id != SOM_PA20)

struct som_filehdr {
	u_short system_id;
	u_short a_magic;
	u_int   version_id;
	u_int	time_secs;		/* sys time (zero if unused) */
	u_int	time_nsecs;
	u_int	ep_space;		/* ep space */
	u_int	ep_subspace;
	u_int	entry;			/* how is it different from a_entry? */
	u_int	aux_loc;		/* aux header location */
	u_int	aux_size;
	u_int	som_length;		/* entire image length */
	u_int	dp;			/* dp presumed at compilation time */
	u_int	space_loc;		/* space dictionary location */
	u_int	space_total;		/* N of entries in the space dict */
	u_int	subspace_loc;		/* subspace dict location */
	u_int	subspace_total;		/* N of entries in the subspace dict */
	u_int	ld_fixup_loc;		/* space ref array (relocs?) */
	u_int	ld_fixup_total;		/* N of space ref records */
	u_int	space_str_loc;		/* {,sub}space string table location */
	u_int	space_str_size;		/* size of the above */
	u_int	init_loc;		/* init ptrs location */
	u_int	init_total;		/* N of entries in the above */
	u_int	dict_loc;		/* module dictionary location */
	u_int	dict_total;		/* number of modules */
	u_int	sym_loc;		/* symbol table location */
	u_int	sym_total;		/* N of symbols */
	u_int	fixup_loc;		/* fixpup reqs location */
	u_int	fixup_total;		/* N of the fixup reqs */
	u_int	strings_loc;		/* string table location */
	u_int	strings_size;		/* size of the strings table */
	u_int	unloadable_loc;		/* unloadable spaces location */
	u_int	unloadable_size;	/* size of the unloadable spaces */
	u_int	checksum;		/* header checksum? */
};

struct som_exec_aux {
	u_int	mandatory : 1;
	u_int	copy : 1;
	u_int	append : 1;
	u_int	ignore : 1;
	u_int	reserved : 12;
	u_int	type : 16;
	u_int	length;
	long	a_tsize;
	long	a_tmem;
	long	a_tfile;
	long	a_dsize;
	long	a_dmem;
	long	a_dfile;
	long	a_bsize;
	long	a_entry;
	long	a_flags;
	long	a_bfill;
};

struct som_sym {
	u_int	sym_type : 8;
	u_int	sym_scope : 4;
	u_int	sym_chklevel : 3;
	u_int	sym_qualify : 1;
	u_int	sym_ifrozen : 1;
	u_int	sym_resident : 1;
	u_int	sym_is_common : 1;
	u_int	sym_dup_common : 1;
	u_int	sym_xleast : 2;
	u_int	sym_arg_reloc : 10;
	union {
		char *n_name;
		u_int n_strx;
	} sym_name, sym_qualifier_name;
	u_int	sym_info;
	u_int	sym_value;

};

/* sym_type */
#define	SOM_ST_NULL		0
#define	SOM_ST_ABS		1
#define	SOM_ST_DATA		2
#define	SOM_ST_CODE		3
#define	SOM_ST_PRI_PROG		4
#define	SOM_ST_SEC_PROG		5
#define	SOM_ST_ENTRY		6
#define	SOM_ST_STORAGE		7
#define	SOM_ST_STUB		8
#define	SOM_ST_MODULE		9
#define	SOM_ST_SYM_EXT		10
#define	SOM_ST_ARG_EXT		11
#define	SOM_ST_MILLICODE	12
#define	SOM_ST_PLABEL		13

/* sym_scope */
#define	SOM_SS_UNSAT		0
#define	SOM_SS_EXTERNAL		1
#define	SOM_SS_GLOBAL		2
#define	SOM_SS_UNIVERSAL	3

#endif /* _MACHINE_SOM_H_ */
