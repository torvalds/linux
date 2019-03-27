/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
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
 *	@(#)extern.h	8.2 (Berkeley) 4/4/94
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/socket.h>

void	blkfree(char **);
char  **copyblk(char **);
void	cwd(char *);
void	delete(char *);
void	dologout(int);
void	fatalerror(char *);
void    ftpd_logwtmp(char *, char *, struct sockaddr *addr);
int	ftpd_pclose(FILE *);
FILE   *ftpd_popen(char *, char *);
int	get_line(char *, int, FILE *);
void	lreply(int, const char *, ...) __printflike(2, 3);
void	makedir(char *);
void	nack(char *);
void	pass(char *);
void	passive(void);
void	long_passive(char *, int);
void	perror_reply(int, char *);
void	pwd(void);
void	removedir(char *);
void	renamecmd(char *, char *);
char   *renamefrom(char *);
void	reply(int, const char *, ...) __printflike(2, 3);
void	retrieve(char *, char *);
void	send_file_list(char *);
#ifdef OLD_SETPROCTITLE
void	setproctitle(const char *, ...);
#endif
void	statcmd(void);
void	statfilecmd(char *);
void	store(char *, char *, int);
void	upper(char *);
void	user(char *);
void	yyerror(char *);
int	yyparse(void);
int	ls_main(int, char **);

extern	int assumeutf8;
extern	char cbuf[];
extern	union sockunion data_dest;
extern	int epsvall;
extern	int form;
extern	int ftpdebug;
extern	int guest;
extern	union sockunion his_addr;
extern	char *homedir;
extern	int hostinfo;
extern	char *hostname;
extern	int maxtimeout;
extern	int logged_in;
extern	int logging;
extern	int noepsv;
extern	int noguestretr;
extern	int noretr;
extern	int paranoid;
extern	struct passwd *pw;
extern	int pdata;
extern	char proctitle[];
extern	int readonly;
extern	off_t restart_point;
extern	int timeout;
extern  char tmpline[];
extern	int type;
extern	char *typenames[]; /* defined in <arpa/ftp.h> included from ftpd.c */
extern	int usedefault;

struct sockaddr_in;
struct sockaddr_in6;
union sockunion {
	struct sockinet {
		u_char	si_len;
		u_char	si_family;
		u_short	si_port;
	} su_si;
	struct	sockaddr_in  su_sin;
	struct	sockaddr_in6 su_sin6;
};
#define	su_len		su_si.si_len
#define	su_family	su_si.si_family
#define	su_port		su_si.si_port
