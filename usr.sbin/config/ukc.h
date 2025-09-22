/*	$OpenBSD: ukc.h,v 1.17 2019/09/06 21:30:31 cheloha Exp $ */

/*
 * Copyright (c) 1999-2001 Mats O Jansson.  All rights reserved.
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
 */

#ifndef _UKC_H
#define _UKC_H

#define P_LOCNAMES	0
#define S_LOCNAMP	1
#define SA_CFROOTS	2
#define I_CFROOTS_SIZE	3
#define I_PV_SIZE	4
#define SA_PV		5
#define P_CFDATA	6
#define P_KERNEL_TEXT	7
#define P_VERSION	8
#define IA_EXTRALOC	9
#define I_REXTRALOC	10
#define I_TEXTRALOC	11
#define	I_HISTLEN	12
#define	CA_HISTORY	13
#define P_PDEVNAMES	14
#define I_PDEVSIZE	15
#define S_PDEVINIT	16
#define I_NKMEMPG	17
#define NLENTRIES	18

#ifdef UKC_MAIN
struct nlist nl[] = {
	{ "_locnames" },
	{ "_locnamp" },
	{ "_cfroots" },
	{ "_cfroots_size" },
	{ "_pv_size" },
	{ "_pv" },
	{ "_cfdata" },
	{ "_kernel_text" },
	{ "_version" },
	{ "_extraloc" },
	{ "_rextraloc" },
	{ "_textraloc" },
	{ "_userconf_histlen" },
	{ "_userconf_history" },
	{ "_pdevnames" },
	{ "_pdevnames_size" },
	{ "_pdevinit" },
	{ "_nkmempages" },
	{ NULL },
};
int	maxdev = 0;
int	totdev = 0;
int	maxpseudo = 0;
int	maxlocnames = 0;
int	base = 16;
int	cnt = -1;
int	lines = 18;
int	oldkernel = 0;
int	nopdev = 0;
#else
extern struct nlist nl[];
extern int maxdev;
extern int totdev;
extern int maxpseudo;
extern int maxlocnames;
extern int base;
extern int cnt;
extern int lines;
extern int oldkernel;
extern int nopdev;
#endif

struct cfdata   *get_cfdata(int);
short	        *get_locnamp(int);

int	more(void);
void	pdev(short);
int	number(const char *, int *);
int	device(char *, int *, short *, short *);
int	attr(char *, int *);
void	modify(char *, int *);
void	change(int);
void	disable(int);
void	enable(int);
void	show(void);
void	common_attr_val(short, int *, char);
void	show_attr(char *);
void	common_dev(char *, int, short, short, char);
void	common_attr(char *, int, char);
void	add_read(char *, char, char *, int, int *);
void	add(char *, int, short, short);

int	config(void);
void	process_history(int, char *);

#define UC_CHANGE 'c'
#define UC_DISABLE 'd'
#define UC_ENABLE 'e'
#define UC_FIND 'f'
#define UC_SHOW 's'

#endif /* _UTIL_H */


