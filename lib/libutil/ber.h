/*	$OpenBSD: ber.h,v 1.5 2021/10/31 16:42:08 tb Exp $ */

/*
 * Copyright (c) 2007, 2012 Reyk Floeter <reyk@openbsd.org>
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

#ifndef _BER_H
#define _BER_H

struct ber_octetstring {
	size_t			 ostr_len;
	const void		*ostr_val;
};

struct ber_element {
	struct ber_element	*be_next;
	unsigned int		 be_type;
	unsigned int		 be_encoding;
	size_t			 be_len;
	off_t			 be_offs;
	int			 be_free;
	u_int8_t		 be_class;
	void			(*be_cb)(void *, size_t);
	void			*be_cbarg;
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
	off_t	 br_offs;
	u_char	*br_wbuf;
	u_char	*br_wptr;
	u_char	*br_wend;
	u_char	*br_rbuf;
	u_char	*br_rptr;
	u_char	*br_rend;

	unsigned int	(*br_application)(struct ber_element *);
};

/* well-known ber_element types */
#define BER_TYPE_DEFAULT	((unsigned int)-1)
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
#define BER_MIN_OID_LEN		2		/* X.690 section 8.19.5 */
#define BER_MAX_OID_LEN		128		/* RFC 2578 section 7.1.3 */
#define BER_MAX_SEQ_ELEMENTS	USHRT_MAX	/* 65535 */

struct ber_oid {
	u_int32_t	bo_id[BER_MAX_OID_LEN + 1];
	size_t		bo_n;
};

__BEGIN_DECLS
struct ber_element	*ober_get_element(unsigned int);
void			 ober_set_header(struct ber_element *, int,
			    unsigned int);
void			 ober_link_elements(struct ber_element *,
			    struct ber_element *);
struct ber_element	*ober_unlink_elements(struct ber_element *);
void			 ober_replace_elements(struct ber_element *,
			    struct ber_element *);
struct ber_element	*ober_add_sequence(struct ber_element *);
struct ber_element	*ober_add_set(struct ber_element *);
struct ber_element	*ober_add_integer(struct ber_element *, long long);
int			 ober_get_integer(struct ber_element *, long long *);
struct ber_element	*ober_add_enumerated(struct ber_element *, long long);
int			 ober_get_enumerated(struct ber_element *, long long *);
struct ber_element	*ober_add_boolean(struct ber_element *, int);
int			 ober_get_boolean(struct ber_element *, int *);
struct ber_element	*ober_add_string(struct ber_element *, const char *);
struct ber_element	*ober_add_nstring(struct ber_element *, const char *,
			    size_t);
struct ber_element	*ober_add_ostring(struct ber_element *,
			    struct ber_octetstring *);
int			 ober_get_string(struct ber_element *, char **);
int			 ober_get_nstring(struct ber_element *, void **,
			    size_t *);
int			 ober_get_ostring(struct ber_element *,
			    struct ber_octetstring *);
struct ber_element	*ober_add_bitstring(struct ber_element *, const void *,
			    size_t);
int			 ober_get_bitstring(struct ber_element *, void **,
			    size_t *);
struct ber_element	*ober_add_null(struct ber_element *);
int			 ober_get_null(struct ber_element *);
struct ber_element	*ober_add_eoc(struct ber_element *);
int			 ober_get_eoc(struct ber_element *);
struct ber_element	*ober_add_oid(struct ber_element *, struct ber_oid *);
struct ber_element	*ober_add_noid(struct ber_element *, struct ber_oid *, int);
struct ber_element	*ober_add_oidstring(struct ber_element *, const char *);
int			 ober_get_oid(struct ber_element *, struct ber_oid *);
size_t			 ober_oid2ber(struct ber_oid *, u_int8_t *, size_t);
int			 ober_string2oid(const char *, struct ber_oid *);
struct ber_element	*ober_printf_elements(struct ber_element *, char *, ...);
int			 ober_scanf_elements(struct ber_element *, char *, ...);
ssize_t			 ober_get_writebuf(struct ber *, void **);
ssize_t			 ober_write_elements(struct ber *, struct ber_element *);
void			 ober_set_readbuf(struct ber *, void *, size_t);
struct ber_element	*ober_read_elements(struct ber *, struct ber_element *);
off_t			 ober_getpos(struct ber_element *);
struct ber_element	*ober_dup(struct ber_element *);
void			 ober_free_element(struct ber_element *);
void			 ober_free_elements(struct ber_element *);
size_t			 ober_calc_len(struct ber_element *);
void			 ober_set_application(struct ber *,
			    unsigned int (*)(struct ber_element *));
void			 ober_set_writecallback(struct ber_element *,
			    void (*)(void *, size_t), void *);
void			 ober_free(struct ber *);
int			 ober_oid_cmp(struct ber_oid *, struct ber_oid *);

__END_DECLS

#endif /* _BER_H */
