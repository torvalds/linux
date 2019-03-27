/*	$FreeBSD$						*/
/*	$OpenBSD: extern.h,v 1.4 2014/12/01 13:13:00 deraadt Exp $	*/

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

#include <stdbool.h>
#include "bcode.h"


/* inout.c */
void		 src_setstream(struct source *, FILE *);
void		 src_setstring(struct source *, char *);
struct number	*readnumber(struct source *, u_int, u_int);
void		 printnumber(FILE *, const struct number *, u_int);
char		*read_string(struct source *);
void		 print_value(FILE *, const struct value *, const char *, u_int);
void		 print_ascii(FILE *, const struct number *);

/* mem.c */
struct number	*new_number(void);
void		 free_number(struct number *);
struct number	*div_number(struct number *, struct number *, u_int scale);
struct number	*dup_number(const struct number *);
void		*bmalloc(size_t);
void		*breallocarray(void *, size_t, size_t);
char		*bstrdup(const char *p);
void		 bn_check(int);
void		 bn_checkp(const void *);

/* stack.c */
void		 stack_init(struct stack *);
void		 stack_free_value(struct value *);
struct value	*stack_dup_value(const struct value *, struct value *);
void		 stack_swap(struct stack *);
size_t		 stack_size(const struct stack *);
void		 stack_dup(struct stack *);
void		 stack_pushnumber(struct stack *, struct number *);
void		 stack_pushstring(struct stack *stack, char *);
void	 	 stack_push(struct stack *, struct value *);
void		 stack_set_tos(struct stack *, struct value *);
struct value	*stack_tos(const struct stack *);
struct value	*stack_pop(struct stack *);
struct number	*stack_popnumber(struct stack *);
char		*stack_popstring(struct stack *);
void		 stack_clear(struct stack *);
void		 stack_print(FILE *, const struct stack *, const char *,
		    u_int base);
void		 frame_assign(struct stack *, size_t, const struct value *);
struct value	*frame_retrieve(const struct stack *, size_t);
/* void		 frame_free(struct stack *); */
