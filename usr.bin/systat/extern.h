/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *      @(#)extern.h	8.1 (Berkeley) 6/6/93
 * $FreeBSD$
 */

#include <sys/cdefs.h>
#include <fcntl.h>
#include <kvm.h>

extern struct	cmdtab *curcmd;
extern struct	cmdtab cmdtab[];
extern struct	text *xtext;
extern WINDOW	*wnd;
extern char	**dr_name;
extern char	c, *namp, hostname[];
extern double	avenrun[3];
extern float	*dk_mspw;
extern kvm_t	*kd;
extern long	ntext, textp;
extern int	*dk_select;
extern int	CMDLINE;
extern int	dk_ndrive;
extern int	hz, stathz;
extern double	hertz;		/* sampling frequency for cp_time and dk_time */
extern int	col;
extern int	nhosts;
extern int	nports;
extern int	protos;
extern int	verbose;
extern unsigned int	delay;

struct in_conninfo;

extern struct device_selection *dev_select;
extern long			generation;
extern int			num_devices;
extern int			num_selected;
extern int			num_selections;
extern long			select_generation;

extern struct nlist		namelist[];

int	 checkhost(struct in_conninfo *);
int	 checkport(struct in_conninfo *);
void	 closeicmp(WINDOW *);
void	 closeicmp6(WINDOW *);
void	 closeifstat(WINDOW *);
void	 closeiostat(WINDOW *);
void	 closeip(WINDOW *);
void	 closeip6(WINDOW *);
void	 closekre(WINDOW *);
void	 closenetstat(WINDOW *);
void	 closepigs(WINDOW *);
void	 closeswap(WINDOW *);
void	 closetcp(WINDOW *);
int	 cmdifstat(const char *, const char *);
int	 cmdiostat(const char *, const char *);
int	 cmdkre(const char *, const char *);
int	 cmdnetstat(const char *, const char *);
struct	 cmdtab *lookup(const char *);
void	 command(const char *);
void	 die(int);
void	 display(void);
int	 dkinit(void);
int	 dkcmd(char *, char *);
void	 error(const char *fmt, ...) __printflike(1, 2);
void	 fetchicmp(void);
void	 fetchicmp6(void);
void	 fetchifstat(void);
void	 fetchip(void);
void	 fetchip6(void);
void	 fetchiostat(void);
void	 fetchkre(void);
void	 fetchnetstat(void);
void	 fetchpigs(void);
void	 fetchswap(void);
void	 fetchtcp(void);
void	 getsysctl(const char *, void *, size_t);
int	 ifcmd(const char *cmd, const char *args);
int	 initicmp(void);
int	 initicmp6(void);
int	 initifstat(void);
int	 initip(void);
int	 initip6(void);
int	 initiostat(void);
int	 initkre(void);
int	 initnetstat(void);
int	 initpigs(void);
int	 initswap(void);
int	 inittcp(void);
int	 keyboard(void);
int	 kvm_ckread(void *, void *, int);
void	 labelicmp(void);
void	 labelicmp6(void);
void	 labelifstat(void);
void	 labelip(void);
void	 labelip6(void);
void	 labeliostat(void);
void	 labelkre(void);
void	 labelnetstat(void);
void	 labelpigs(void);
void	 labels(void);
void	 labelswap(void);
void	 labeltcp(void);
void	 load(void);
int	 netcmd(const char *, const char *);
void	 nlisterr(struct nlist []);
WINDOW	*openicmp(void);
WINDOW	*openicmp6(void);
WINDOW	*openifstat(void);
WINDOW	*openip(void);
WINDOW	*openip6(void);
WINDOW	*openiostat(void);
WINDOW	*openkre(void);
WINDOW	*opennetstat(void);
WINDOW	*openpigs(void);
WINDOW	*openswap(void);
WINDOW	*opentcp(void);
int	 prefix(const char *, const char *);
void	 reseticmp(void);
void	 reseticmp6(void);
void	 resetip(void);
void	 resetip6(void);
void	 resettcp(void);
void	 showicmp(void);
void	 showicmp6(void);
void	 showifstat(void);
void	 showip(void);
void	 showip6(void);
void	 showiostat(void);
void	 showkre(void);
void	 shownetstat(void);
void	 showpigs(void);
void	 showswap(void);
void	 showtcp(void);
void	 status(void);
void	 suspend(int);
char	*sysctl_dynread(const char *, size_t *);

#define SYSTAT_CMD(name)	\
	void	 close ## name(WINDOW *); \
	void	 fetch ## name(void); \
	int	 init ## name(void); \
	void	 label ## name(void); \
	WINDOW	*open ## name(void); \
	void	 reset ## name(void); \
	void	 show ## name(void)

SYSTAT_CMD( zarc );
SYSTAT_CMD ( sctp );
