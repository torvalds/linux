/*	$FreeBSD$						*/
/*	$OpenBSD: bcode.h,v 1.7 2012/11/07 11:06:14 otto Exp $	*/

/*
 * Copyright (c) 2003, Otto Moerbeek <otto@drijf.net>
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

#include <sys/types.h>
#include <openssl/bn.h>

struct number {
	BIGNUM	*number;
	u_int	 scale;
};

enum stacktype {
	BCODE_NONE,
	BCODE_NUMBER,
	BCODE_STRING
};

enum bcode_compare {
	BCODE_EQUAL,
	BCODE_NOT_EQUAL,
	BCODE_LESS,
	BCODE_NOT_LESS,
	BCODE_GREATER,
	BCODE_NOT_GREATER
};

struct array;

struct value {
	union {
		struct number	*num;
		char		*string;
	} u;
	struct array	*array;
	enum stacktype	 type;
};

struct array {
	struct value	*data;
	size_t		 size;
};

struct stack {
	struct value	*stack;
	ssize_t		 size;
	ssize_t		 sp;
};

struct source;

struct vtable {
	int	(*readchar)(struct source *);
	void	(*unreadchar)(struct source *);
	char	*(*readline)(struct source *);
	void	(*free)(struct source *);
};

struct source {
	union {
			struct {
				u_char	*buf;
				size_t	 pos;
			} string;
			FILE	*stream;
	} u;
	struct vtable	*vtable;
	int		 lastchar;
};

void			init_bmachine(bool);
void			reset_bmachine(struct source *);
u_int			bmachine_scale(void);
void			scale_number(BIGNUM *, int);
void			normalize(struct number *, u_int);
void			eval(void);
void			pn(const char *, const struct number *);
void			pbn(const char *, const BIGNUM *);
void			negate(struct number *);
void			split_number(const struct number *, BIGNUM *, BIGNUM *);
void			bmul_number(struct number *, struct number *,
			    struct number *, u_int scale);
	
static __inline u_int
max(u_int a, u_int b)
{

	return (a > b ? a : b);
}
