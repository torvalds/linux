/*	$OpenBSD: wsconsctl.h,v 1.16 2017/07/21 20:38:20 bru Exp $	*/
/*	$NetBSD: wsconsctl.h 1.1 1998/12/28 14:01:17 hannken Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Juergen Hannken-Illjes.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <dev/wscons/wsksymvar.h>

struct field {
	char *name;
	void *valp;
#define FMT_UINT	1		/* unsigned integer */
#define FMT_INT		2		/* signed integer */
#define FMT_BOOL	3		/* boolean on/off */
#define FMT_PC		4		/* percentage fixed point 000.00 */
#define FMT_KBDTYPE	101		/* keyboard type */
#define FMT_MSTYPE	102		/* mouse type */
#define FMT_DPYTYPE	103		/* display type */
#define FMT_KBDENC	104		/* keyboard encoding */
#define FMT_KBMAP	105		/* keyboard map */
#define FMT_SCALE	106		/* wsmouse scale */
#define FMT_EMUL	107		/* wsdisplay emulations */
#define FMT_SCREEN	108		/* wsdisplay screen types */
#define FMT_STRING	109		/* free string */
#define FMT_CFG		201		/* wsmouse parameters */
	int format;
#define FLG_RDONLY	0x0001		/* variable cannot be modified */
#define FLG_WRONLY	0x0002		/* variable cannot be displayed */
#define FLG_NOAUTO	0x0004		/* skip variable on -a flag */
#define FLG_MODIFY	0x0008		/* variable may be modified with += */
#define	FLG_NORDBACK	0x0010		/* do not read back variable after write */
#define FLG_GET		0x0100		/* read this variable from driver */
#define FLG_SET		0x0200		/* write this variable to driver */
#define FLG_INIT	0x0400		/* init (read) before write */
#define FLG_DEAD	0x0800		/* the var isn't there, let it rest */
	int flags;
};

struct field_pc {
	int max, min, cur;
};

struct field *
	field_by_name(struct field *, char *);
struct field *
	field_by_value(struct field *, void *);
void	pr_field(const char *, struct field *, const char *);
void	rd_field(struct field *, char *, int);
int	name2ksym(char *);
char *	ksym2name(int);
void	ksymenc(int);
keysym_t ksym_upcase(keysym_t);
void	keyboard_get_values(int);
int	keyboard_put_values(int);
char *	keyboard_next_device(int);
void	mouse_init(int,int);
void	mouse_get_values(int);
int	mouse_put_values(int);
char *	mouse_next_device(int);
void	display_get_values(int);
int	display_put_values(int);
char *	display_next_device(int);
int	yyparse(void);
void	yyerror(char *);
int	yylex(void);
void	map_scan_setinput(char *);
