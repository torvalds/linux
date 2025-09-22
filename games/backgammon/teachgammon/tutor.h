/*	$OpenBSD: tutor.h,v 1.7 2017/01/21 08:22:57 krw Exp $	*/

/*
 * Copyright (c) 1980, 1993
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
 *	@(#)tutor.h	8.1 (Berkeley) 5/31/93
 */

struct situatn {
	int	brd[26];
	int	roll1;
	int	roll2;
	int	mp[4];
	int	mg[4];
	int	new1;
	int	new2;
	const char *const (*com[8]);
	const char *const (*ans[8]);
};

extern	const char	*const doubl[];
extern	const char	*const endgame[];
extern	const char	*const hello[];
extern	const char	*const hits[];
extern	const char	*const intro1[];
extern	const char	*const intro2[];
extern	const char	*const lastch[];
extern	const char	*const list[];
extern	int		maxmoves;
extern	const char	*const moves[];
extern	const char	*const opts;
extern	const char	*const prog[];
extern	const char	*const prompt;
extern	const char	*const removepiece[];
extern	const char	*const stragy[];
extern	const struct situatn	test[];


int	brdeq(const int *, const int *);
void	clrest(void);
__dead void	leave(void);
__dead void	tutor(void);
