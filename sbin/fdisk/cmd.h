/*	$OpenBSD: cmd.h,v 1.28 2024/05/21 05:00:47 jsg Exp $	*/

/*
 * Copyright (c) 1997 Tobias Weingartner
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define CMD_EXIT	0x0000
#define CMD_QUIT	0x0001
#define CMD_CONT	0x0002
#define CMD_CLEAN	0x0003
#define CMD_DIRTY	0x0004

int		Xreinit(const char *, struct mbr *);
int		Xmanual(const char *, struct mbr *);
int		Xedit(const char *, struct mbr *);
int		Xsetpid(const char *, struct mbr *);
int		Xselect(const char *, struct mbr *);
int		Xswap(const char *, struct mbr *);
int		Xprint(const char *, struct mbr *);
int		Xwrite(const char *, struct mbr *);
int		Xexit(const char *, struct mbr *);
int		Xquit(const char *, struct mbr *);
int		Xabort(const char *, struct mbr *);
int		Xhelp(const char *, struct mbr *);
int		Xflag(const char *, struct mbr *);
int		Xupdate(const char *, struct mbr *);
