/*	$OpenBSD: ber.h,v 1.2 2008/12/29 15:48:13 aschrijver Exp $ */
/*	$FreeBSD$ */

/*
 * Copyright (c) 2007 Reyk Floeter <reyk@vantronix.net>
 * Copyright (c) 2006, 2007 Claudio Jeker <claudio@openbsd.org>
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

struct ber_element {
	struct ber_element	*be_next;
	unsigned long		 be_type;
	unsigned long		 be_encoding;
	size_t			 be_len;
	int			 be_free;
	u_int8_t		 be_class;
	union {
		struct ber_element	*bv_sub;
		void			*bv_val;
		long long		 bv_numeric;
	} be_union;
#define be_sub		be_union.bv_sub
#define be_val		be_union.bv_val
#define be_numeric	be_union.bv_numeric
};

struct ber {
	int	 fd;
	u_char	*br_wbuf;
	u_char	*br_wptr;
	u_char	*br_wend;
	u_char	*br_rbuf;
	u_char	*br_rptr;
	u_char	*br_rend;

	unsigned long	(*br_application)(struct ber_element *);
};

/* well-known ber_element types */
#define BER_TYPE_DEFAULT	((unsigned long)-1)
#define BER_TYPE_EOC		0
#define BER_TYPE_BOOLEAN	1
#define BER_TYPE_INTEGER	2
#define BER_TYPE_BITSTRING	3
#define BER_TYPE_OCTETSTRING	4
#define BER_TYPE_NULL		5
#define BER_TYPE_OBJECT		6
#define BER_TYPE_ENUMERATED	10
#define BER_TYPE_SEQUENCE	16
#define BER_TYPE_SET		17

/* ber classes */
#define BER_CLASS_UNIVERSAL	0x0
#define BER_CLASS_UNIV		BER_CLASS_UNIVERSAL
#define BER_CLASS_APPLICATION	0x1
#define BER_CLASS_APP		BER_CLASS_APPLICATION
#define BER_CLASS_CONTEXT	0x2
#define BER_CLASS_PRIVATE	0x3
#define BER_CLASS_MASK		0x3

/* common definitions */
#define BER_MIN_OID_LEN		2	/* OBJECT */
#define BER_MAX_OID_LEN		32	/* OBJECT */

struct ber_oid {
	u_int32_t	bo_id[BER_MAX_OID_LEN + 1];
	size_t		bo_n;
};

__BEGIN_DECLS
struct ber_element	*ber_get_element(unsigned long);
void			 ber_set_header(struct ber_element *, int,
			    unsigned long);
void			 ber_link_elements(struct ber_element *,
			    struct ber_element *);
struct ber_element	*ber_unlink_elements(struct ber_element *);
void			 ber_replace_elements(struct ber_element *,
			    struct ber_element *);
struct ber_element	*ber_add_sequence(struct ber_element *);
struct ber_element	*ber_add_set(struct ber_element *);
struct ber_element	*ber_add_integer(struct ber_element *, long long);
int			 ber_get_integer(struct ber_element *, long long *);
struct ber_element	*ber_add_enumerated(struct ber_element *, long long);
int			 ber_get_enumerated(struct ber_element *, long long *);
struct ber_element	*ber_add_boolean(struct ber_element *, int);
int			 ber_get_boolean(struct ber_element *, int *);
struct ber_element	*ber_add_string(struct ber_element *, const char *);
struct ber_element	*ber_add_nstring(struct ber_element *, const char *,
			    size_t);
int			 ber_get_string(struct ber_element *, char **);
int			 ber_get_nstring(struct ber_element *, void **,
			    size_t *);
struct ber_element	*ber_add_bitstring(struct ber_element *, const void *,
			    size_t);
int			 ber_get_bitstring(struct ber_element *, void **,
			    size_t *);
struct ber_element	*ber_add_null(struct ber_element *);
int			 ber_get_null(struct ber_element *);
struct ber_element	*ber_add_eoc(struct ber_element *);
int			 ber_get_eoc(struct ber_element *);
struct ber_element	*ber_add_oid(struct ber_element *, struct ber_oid *);
struct ber_element	*ber_add_noid(struct ber_element *, struct ber_oid *, int);
struct ber_element	*ber_add_oidstring(struct ber_element *, const char *);
int			 ber_get_oid(struct ber_element *, struct ber_oid *);
size_t			 ber_oid2ber(struct ber_oid *, u_int8_t *, size_t);
int			 ber_string2oid(const char *, struct ber_oid *);
struct ber_element	*ber_printf_elements(struct ber_element *, char *, ...);
int			 ber_scanf_elements(struct ber_element *, char *, ...);
ssize_t			 ber_get_writebuf(struct ber *, void **);
int			 ber_write_elements(struct ber *, struct ber_element *);
void			 ber_set_readbuf(struct ber *, void *, size_t);
struct ber_element	*ber_read_elements(struct ber *, struct ber_element *);
void			 ber_free_elements(struct ber_element *);
size_t			 ber_calc_len(struct ber_element *);
void			 ber_set_application(struct ber *,
			    unsigned long (*)(struct ber_element *));
void			 ber_free(struct ber *);
__END_DECLS
