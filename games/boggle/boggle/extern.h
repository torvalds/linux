/*	$OpenBSD: extern.h,v 1.9 2015/12/25 20:59:09 mestre Exp $	*/
/*	$NetBSD: extern.h,v 1.3 1995/04/24 12:22:37 cgd Exp $	*/

/*-
 * Copyright (c) 1993
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
 *	@(#)extern.h	8.1 (Berkeley) 6/11/93
 */

void	 addword(char *);
void	 badword(void);
char	*batchword(FILE *);
void	 checkdict(void);
int	 checkword(char *, int, int *);
void	 cleanup(void);
void	 delay(int);
long	 dictseek(FILE *, long, int);
void	 findword(void);
void	 flushin(FILE *);
char	*get_line(char *);
void	 getword(char *);
int	 help(void);
int	 inputch(void);
int	 loaddict(FILE *);
int	 loadindex(char *);
void	 newgame(char *);
char	*nextword(FILE *);
FILE	*opendict(char *);
void	 playgame(void);
void	 prompt(char *);
void	 prtable(char *[],
	    int, int, int, void (*)(char *[], int), int (*)(char *[], int));
void	 putstr(char *);
void	 redraw(void);
void	 results(void);
int	 setup(void);
void	 showboard(char *);
void	 showstr(char *, int);
void	 showword(int);
void	 starttime(void);
void	 startwords(void);
void	 stoptime(void);
int	 timerch(void);
__dead void	 usage(void);
int	 validword(char *);
