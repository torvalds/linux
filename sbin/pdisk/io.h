/*	$OpenBSD: io.h,v 1.12 2016/01/26 23:41:48 krw Exp $	*/

/*
 * io.h - simple io and input parsing routines
 *
 * Written by Eryk Vershen
 */

/*
 * Copyright 1996,1997,1998 by Apple Computer, Inc.
 *              All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies and
 * that both the copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * APPLE COMPUTER DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL APPLE COMPUTER BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __io__
#define __io__

unsigned long	get_multiplier(long);
void		bad_input(const char *, ...);
void		flush_to_newline(int);
void		my_ungetch(int);
int		get_command(const char *, int, int *);
int		get_number_argument(const char *, long *);
int		get_okay(const char *, int);
int		get_partition_modifier(void);
char	       *get_dpistr_argument(const char *);
int		number_of_digits(unsigned long);

#endif /* __io__ */
