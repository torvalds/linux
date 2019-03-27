/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1996,1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef lint
static const char rcsid[] = "$Id: ns_name.c,v 1.11 2009/01/23 19:59:16 each Exp $";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "port_before.h"

#include <sys/types.h>

#include <netinet/in.h>
#include <arpa/nameser.h>

#include <errno.h>
#include <resolv.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>

#include "port_after.h"

#ifdef SPRINTF_CHAR
# define SPRINTF(x) strlen(sprintf/**/x)
#else
# define SPRINTF(x) ((size_t)sprintf x)
#endif

#define NS_TYPE_ELT			0x40 /*%< EDNS0 extended label type */
#define DNS_LABELTYPE_BITSTRING		0x41

/* Data. */

static const char	digits[] = "0123456789";

static const char digitvalue[256] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	/*16*/
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*32*/
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*48*/
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1, /*64*/
	-1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*80*/
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*96*/
	-1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*112*/
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*128*/
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*256*/
};

/* Forward. */

static int		special(int);
static int		printable(int);
static int		dn_find(const u_char *, const u_char *,
				const u_char * const *,
				const u_char * const *);
static int		encode_bitsring(const char **, const char *,
					unsigned char **, unsigned char **,
					unsigned const char *);
static int		labellen(const u_char *);
static int		decode_bitstring(const unsigned char **,
					 char *, const char *);

/* Public. */

/*%
 *	Convert an encoded domain name to printable ascii as per RFC1035.

 * return:
 *\li	Number of bytes written to buffer, or -1 (with errno set)
 *
 * notes:
 *\li	The root is returned as "."
 *\li	All other domains are returned in non absolute form
 */
int
ns_name_ntop(const u_char *src, char *dst, size_t dstsiz)
{
	const u_char *cp;
	char *dn, *eom;
	u_char c;
	u_int n;
	int l;

	cp = src;
	dn = dst;
	eom = dst + dstsiz;

	while ((n = *cp++) != 0) {
		if ((n & NS_CMPRSFLGS) == NS_CMPRSFLGS) {
			/* Some kind of compression pointer. */
			errno = EMSGSIZE;
			return (-1);
		}
		if (dn != dst) {
			if (dn >= eom) {
				errno = EMSGSIZE;
				return (-1);
			}
			*dn++ = '.';
		}
		if ((l = labellen(cp - 1)) < 0) {
			errno = EMSGSIZE; /*%< XXX */
			return (-1);
		}
		if (dn + l >= eom) {
			errno = EMSGSIZE;
			return (-1);
		}
		if ((n & NS_CMPRSFLGS) == NS_TYPE_ELT) {
			int m;

			if (n != DNS_LABELTYPE_BITSTRING) {
				/* XXX: labellen should reject this case */
				errno = EINVAL;
				return (-1);
			}
			if ((m = decode_bitstring(&cp, dn, eom)) < 0)
			{
				errno = EMSGSIZE;
				return (-1);
			}
			dn += m; 
			continue;
		}
		for ((void)NULL; l > 0; l--) {
			c = *cp++;
			if (special(c)) {
				if (dn + 1 >= eom) {
					errno = EMSGSIZE;
					return (-1);
				}
				*dn++ = '\\';
				*dn++ = (char)c;
			} else if (!printable(c)) {
				if (dn + 3 >= eom) {
					errno = EMSGSIZE;
					return (-1);
				}
				*dn++ = '\\';
				*dn++ = digits[c / 100];
				*dn++ = digits[(c % 100) / 10];
				*dn++ = digits[c % 10];
			} else {
				if (dn >= eom) {
					errno = EMSGSIZE;
					return (-1);
				}
				*dn++ = (char)c;
			}
		}
	}
	if (dn == dst) {
		if (dn >= eom) {
			errno = EMSGSIZE;
			return (-1);
		}
		*dn++ = '.';
	}
	if (dn >= eom) {
		errno = EMSGSIZE;
		return (-1);
	}
	*dn++ = '\0';
	return (dn - dst);
}

/*%
 *	Convert a ascii string into an encoded domain name as per RFC1035.
 *
 * return:
 *
 *\li	-1 if it fails
 *\li	1 if string was fully qualified
 *\li	0 is string was not fully qualified
 *
 * notes:
 *\li	Enforces label and domain length limits.
 */
int
ns_name_pton(const char *src, u_char *dst, size_t dstsiz) {
	return (ns_name_pton2(src, dst, dstsiz, NULL));
}

/*
 * ns_name_pton2(src, dst, dstsiz, *dstlen)
 *	Convert a ascii string into an encoded domain name as per RFC1035.
 * return:
 *	-1 if it fails
 *	1 if string was fully qualified
 *	0 is string was not fully qualified
 * side effects:
 *	fills in *dstlen (if non-NULL)
 * notes:
 *	Enforces label and domain length limits.
 */
int
ns_name_pton2(const char *src, u_char *dst, size_t dstsiz, size_t *dstlen) {
	u_char *label, *bp, *eom;
	int c, n, escaped, e = 0;
	char *cp;

	escaped = 0;
	bp = dst;
	eom = dst + dstsiz;
	label = bp++;

	while ((c = *src++) != 0) {
		if (escaped) {
			if (c == '[') { /*%< start a bit string label */
				if ((cp = strchr(src, ']')) == NULL) {
					errno = EINVAL; /*%< ??? */
					return (-1);
				}
				if ((e = encode_bitsring(&src, cp + 2,
							 &label, &bp, eom))
				    != 0) {
					errno = e;
					return (-1);
				}
				escaped = 0;
				label = bp++;
				if ((c = *src++) == 0)
					goto done;
				else if (c != '.') {
					errno = EINVAL;
					return	(-1);
				}
				continue;
			}
			else if ((cp = strchr(digits, c)) != NULL) {
				n = (cp - digits) * 100;
				if ((c = *src++) == 0 ||
				    (cp = strchr(digits, c)) == NULL) {
					errno = EMSGSIZE;
					return (-1);
				}
				n += (cp - digits) * 10;
				if ((c = *src++) == 0 ||
				    (cp = strchr(digits, c)) == NULL) {
					errno = EMSGSIZE;
					return (-1);
				}
				n += (cp - digits);
				if (n > 255) {
					errno = EMSGSIZE;
					return (-1);
				}
				c = n;
			}
			escaped = 0;
		} else if (c == '\\') {
			escaped = 1;
			continue;
		} else if (c == '.') {
			c = (bp - label - 1);
			if ((c & NS_CMPRSFLGS) != 0) {	/*%< Label too big. */
				errno = EMSGSIZE;
				return (-1);
			}
			if (label >= eom) {
				errno = EMSGSIZE;
				return (-1);
			}
			*label = c;
			/* Fully qualified ? */
			if (*src == '\0') {
				if (c != 0) {
					if (bp >= eom) {
						errno = EMSGSIZE;
						return (-1);
					}
					*bp++ = '\0';
				}
				if ((bp - dst) > MAXCDNAME) {
					errno = EMSGSIZE;
					return (-1);
				}
				if (dstlen != NULL)
					*dstlen = (bp - dst);
				return (1);
			}
			if (c == 0 || *src == '.') {
				errno = EMSGSIZE;
				return (-1);
			}
			label = bp++;
			continue;
		}
		if (bp >= eom) {
			errno = EMSGSIZE;
			return (-1);
		}
		*bp++ = (u_char)c;
	}
	c = (bp - label - 1);
	if ((c & NS_CMPRSFLGS) != 0) {		/*%< Label too big. */
		errno = EMSGSIZE;
		return (-1);
	}
  done:
	if (label >= eom) {
		errno = EMSGSIZE;
		return (-1);
	}
	*label = c;
	if (c != 0) {
		if (bp >= eom) {
			errno = EMSGSIZE;
			return (-1);
		}
		*bp++ = 0;
	}
	if ((bp - dst) > MAXCDNAME) {	/*%< src too big */
		errno = EMSGSIZE;
		return (-1);
	}
	if (dstlen != NULL)
		*dstlen = (bp - dst);
	return (0);
}

/*%
 *	Convert a network strings labels into all lowercase.
 *
 * return:
 *\li	Number of bytes written to buffer, or -1 (with errno set)
 *
 * notes:
 *\li	Enforces label and domain length limits.
 */

int
ns_name_ntol(const u_char *src, u_char *dst, size_t dstsiz)
{
	const u_char *cp;
	u_char *dn, *eom;
	u_char c;
	u_int n;
	int l;

	cp = src;
	dn = dst;
	eom = dst + dstsiz;

	if (dn >= eom) {
		errno = EMSGSIZE;
		return (-1);
	}
	while ((n = *cp++) != 0) {
		if ((n & NS_CMPRSFLGS) == NS_CMPRSFLGS) {
			/* Some kind of compression pointer. */
			errno = EMSGSIZE;
			return (-1);
		}
		*dn++ = n;
		if ((l = labellen(cp - 1)) < 0) {
			errno = EMSGSIZE;
			return (-1);
		}
		if (dn + l >= eom) {
			errno = EMSGSIZE;
			return (-1);
		}
		for ((void)NULL; l > 0; l--) {
			c = *cp++;
			if (isascii(c) && isupper(c))
				*dn++ = tolower(c);
			else
				*dn++ = c;
		}
	}
	*dn++ = '\0';
	return (dn - dst);
}

/*%
 *	Unpack a domain name from a message, source may be compressed.
 *
 * return:
 *\li	-1 if it fails, or consumed octets if it succeeds.
 */
int
ns_name_unpack(const u_char *msg, const u_char *eom, const u_char *src,
	       u_char *dst, size_t dstsiz)
{
	return (ns_name_unpack2(msg, eom, src, dst, dstsiz, NULL));
}

/*
 * ns_name_unpack2(msg, eom, src, dst, dstsiz, *dstlen)
 *	Unpack a domain name from a message, source may be compressed.
 * return:
 *	-1 if it fails, or consumed octets if it succeeds.
 * side effect:
 *	fills in *dstlen (if non-NULL).
 */
int
ns_name_unpack2(const u_char *msg, const u_char *eom, const u_char *src,
		u_char *dst, size_t dstsiz, size_t *dstlen)
{
	const u_char *srcp, *dstlim;
	u_char *dstp;
	int n, len, checked, l;

	len = -1;
	checked = 0;
	dstp = dst;
	srcp = src;
	dstlim = dst + dstsiz;
	if (srcp < msg || srcp >= eom) {
		errno = EMSGSIZE;
		return (-1);
	}
	/* Fetch next label in domain name. */
	while ((n = *srcp++) != 0) {
		/* Check for indirection. */
		switch (n & NS_CMPRSFLGS) {
		case 0:
		case NS_TYPE_ELT:
			/* Limit checks. */
			if ((l = labellen(srcp - 1)) < 0) {
				errno = EMSGSIZE;
				return (-1);
			}
			if (dstp + l + 1 >= dstlim || srcp + l >= eom) {
				errno = EMSGSIZE;
				return (-1);
			}
			checked += l + 1;
			*dstp++ = n;
			memcpy(dstp, srcp, l);
			dstp += l;
			srcp += l;
			break;

		case NS_CMPRSFLGS:
			if (srcp >= eom) {
				errno = EMSGSIZE;
				return (-1);
			}
			if (len < 0)
				len = srcp - src + 1;
			l = ((n & 0x3f) << 8) | (*srcp & 0xff);
			if (l >= eom - msg) {  /*%< Out of range. */
				errno = EMSGSIZE;
				return (-1);
			}
			srcp = msg + l;
			checked += 2;
			/*
			 * Check for loops in the compressed name;
			 * if we've looked at the whole message,
			 * there must be a loop.
			 */
			if (checked >= eom - msg) {
				errno = EMSGSIZE;
				return (-1);
			}
			break;

		default:
			errno = EMSGSIZE;
			return (-1);			/*%< flag error */
		}
	}
	*dstp++ = 0;
	if (dstlen != NULL)
		*dstlen = dstp - dst;
	if (len < 0)
		len = srcp - src;
	return (len);
}

/*%
 *	Pack domain name 'domain' into 'comp_dn'.
 *
 * return:
 *\li	Size of the compressed name, or -1.
 *
 * notes:
 *\li	'dnptrs' is an array of pointers to previous compressed names.
 *\li	dnptrs[0] is a pointer to the beginning of the message. The array
 *	ends with NULL.
 *\li	'lastdnptr' is a pointer to the end of the array pointed to
 *	by 'dnptrs'.
 *
 * Side effects:
 *\li	The list of pointers in dnptrs is updated for labels inserted into
 *	the message as we compress the name.  If 'dnptr' is NULL, we don't
 *	try to compress names. If 'lastdnptr' is NULL, we don't update the
 *	list.
 */
int
ns_name_pack(const u_char *src, u_char *dst, int dstsiz,
	     const u_char **dnptrs, const u_char **lastdnptr)
{
	u_char *dstp;
	const u_char **cpp, **lpp, *eob, *msg;
	const u_char *srcp;
	int n, l, first = 1;

	srcp = src;
	dstp = dst;
	eob = dstp + dstsiz;
	lpp = cpp = NULL;
	if (dnptrs != NULL) {
		if ((msg = *dnptrs++) != NULL) {
			for (cpp = dnptrs; *cpp != NULL; cpp++)
				(void)NULL;
			lpp = cpp;	/*%< end of list to search */
		}
	} else
		msg = NULL;

	/* make sure the domain we are about to add is legal */
	l = 0;
	do {
		int l0;

		n = *srcp;
		if ((n & NS_CMPRSFLGS) == NS_CMPRSFLGS) {
			errno = EMSGSIZE;
			return (-1);
		}
		if ((l0 = labellen(srcp)) < 0) {
			errno = EINVAL;
			return (-1);
		}
		l += l0 + 1;
		if (l > MAXCDNAME) {
			errno = EMSGSIZE;
			return (-1);
		}
		srcp += l0 + 1;
	} while (n != 0);

	/* from here on we need to reset compression pointer array on error */
	srcp = src;
	do {
		/* Look to see if we can use pointers. */
		n = *srcp;
		if (n != 0 && msg != NULL) {
			l = dn_find(srcp, msg, (const u_char * const *)dnptrs,
				    (const u_char * const *)lpp);
			if (l >= 0) {
				if (dstp + 1 >= eob) {
					goto cleanup;
				}
				*dstp++ = (l >> 8) | NS_CMPRSFLGS;
				*dstp++ = l % 256;
				return (dstp - dst);
			}
			/* Not found, save it. */
			if (lastdnptr != NULL && cpp < lastdnptr - 1 &&
			    (dstp - msg) < 0x4000 && first) {
				*cpp++ = dstp;
				*cpp = NULL;
				first = 0;
			}
		}
		/* copy label to buffer */
		if ((n & NS_CMPRSFLGS) == NS_CMPRSFLGS) {
			/* Should not happen. */
			goto cleanup;
		}
		n = labellen(srcp);
		if (dstp + 1 + n >= eob) {
			goto cleanup;
		}
		memcpy(dstp, srcp, n + 1);
		srcp += n + 1;
		dstp += n + 1;
	} while (n != 0);

	if (dstp > eob) {
cleanup:
		if (msg != NULL)
			*lpp = NULL;
		errno = EMSGSIZE;
		return (-1);
	} 
	return (dstp - dst);
}

/*%
 *	Expand compressed domain name to presentation format.
 *
 * return:
 *\li	Number of bytes read out of `src', or -1 (with errno set).
 *
 * note:
 *\li	Root domain returns as "." not "".
 */
int
ns_name_uncompress(const u_char *msg, const u_char *eom, const u_char *src,
		   char *dst, size_t dstsiz)
{
	u_char tmp[NS_MAXCDNAME];
	int n;
	
	if ((n = ns_name_unpack(msg, eom, src, tmp, sizeof tmp)) == -1)
		return (-1);
	if (ns_name_ntop(tmp, dst, dstsiz) == -1)
		return (-1);
	return (n);
}

/*%
 *	Compress a domain name into wire format, using compression pointers.
 *
 * return:
 *\li	Number of bytes consumed in `dst' or -1 (with errno set).
 *
 * notes:
 *\li	'dnptrs' is an array of pointers to previous compressed names.
 *\li	dnptrs[0] is a pointer to the beginning of the message.
 *\li	The list ends with NULL.  'lastdnptr' is a pointer to the end of the
 *	array pointed to by 'dnptrs'. Side effect is to update the list of
 *	pointers for labels inserted into the message as we compress the name.
 *\li	If 'dnptr' is NULL, we don't try to compress names. If 'lastdnptr'
 *	is NULL, we don't update the list.
 */
int
ns_name_compress(const char *src, u_char *dst, size_t dstsiz,
		 const u_char **dnptrs, const u_char **lastdnptr)
{
	u_char tmp[NS_MAXCDNAME];

	if (ns_name_pton(src, tmp, sizeof tmp) == -1)
		return (-1);
	return (ns_name_pack(tmp, dst, dstsiz, dnptrs, lastdnptr));
}

/*%
 * Reset dnptrs so that there are no active references to pointers at or
 * after src.
 */
void
ns_name_rollback(const u_char *src, const u_char **dnptrs,
		 const u_char **lastdnptr)
{
	while (dnptrs < lastdnptr && *dnptrs != NULL) {
		if (*dnptrs >= src) {
			*dnptrs = NULL;
			break;
		}
		dnptrs++;
	}
}

/*%
 *	Advance *ptrptr to skip over the compressed name it points at.
 *
 * return:
 *\li	0 on success, -1 (with errno set) on failure.
 */
int
ns_name_skip(const u_char **ptrptr, const u_char *eom)
{
	const u_char *cp;
	u_int n;
	int l;

	cp = *ptrptr;
	while (cp < eom && (n = *cp++) != 0) {
		/* Check for indirection. */
		switch (n & NS_CMPRSFLGS) {
		case 0:			/*%< normal case, n == len */
			cp += n;
			continue;
		case NS_TYPE_ELT: /*%< EDNS0 extended label */
			if ((l = labellen(cp - 1)) < 0) {
				errno = EMSGSIZE; /*%< XXX */
				return (-1);
			}
			cp += l;
			continue;
		case NS_CMPRSFLGS:	/*%< indirection */
			cp++;
			break;
		default:		/*%< illegal type */
			errno = EMSGSIZE;
			return (-1);
		}
		break;
	}
	if (cp > eom) {
		errno = EMSGSIZE;
		return (-1);
	}
	*ptrptr = cp;
	return (0);
}

/* Find the number of octets an nname takes up, including the root label.
 * (This is basically ns_name_skip() without compression-pointer support.)
 * ((NOTE: can only return zero if passed-in namesiz argument is zero.))
 */
ssize_t
ns_name_length(ns_nname_ct nname, size_t namesiz) {
	ns_nname_ct orig = nname;
	u_int n;

	while (namesiz-- > 0 && (n = *nname++) != 0) {
		if ((n & NS_CMPRSFLGS) != 0) {
			errno = EISDIR;
			return (-1);
		}
		if (n > namesiz) {
			errno = EMSGSIZE;
			return (-1);
		}
		nname += n;
		namesiz -= n;
	}
	return (nname - orig);
}

/* Compare two nname's for equality.  Return -1 on error (setting errno).
 */
int
ns_name_eq(ns_nname_ct a, size_t as, ns_nname_ct b, size_t bs) {
	ns_nname_ct ae = a + as, be = b + bs;
	int ac, bc;

	while (ac = *a, bc = *b, ac != 0 && bc != 0) {
		if ((ac & NS_CMPRSFLGS) != 0 || (bc & NS_CMPRSFLGS) != 0) {
			errno = EISDIR;
			return (-1);
		}
		if (a + ac >= ae || b + bc >= be) {
			errno = EMSGSIZE;
			return (-1);
		}
		if (ac != bc || strncasecmp((const char *) ++a,
					    (const char *) ++b, ac) != 0)
			return (0);
		a += ac, b += bc;
	}
	return (ac == 0 && bc == 0);
}

/* Is domain "A" owned by (at or below) domain "B"?
 */
int
ns_name_owned(ns_namemap_ct a, int an, ns_namemap_ct b, int bn) {
	/* If A is shorter, it cannot be owned by B. */
	if (an < bn)
		return (0);

	/* If they are unequal before the length of the shorter, A cannot... */
	while (bn > 0) {
		if (a->len != b->len ||
		    strncasecmp((const char *) a->base,
				(const char *) b->base, a->len) != 0)
			return (0);
		a++, an--;
		b++, bn--;
	}

	/* A might be longer or not, but either way, B owns it. */
	return (1);
}

/* Build an array of <base,len> tuples from an nname, top-down order.
 * Return the number of tuples (labels) thus discovered.
 */
int
ns_name_map(ns_nname_ct nname, size_t namelen, ns_namemap_t map, int mapsize) {
	u_int n;
	int l;

	n = *nname++;
	namelen--;

	/* Root zone? */
	if (n == 0) {
		/* Extra data follows name? */
		if (namelen > 0) {
			errno = EMSGSIZE;
			return (-1);
		}
		return (0);
	}

	/* Compression pointer? */
	if ((n & NS_CMPRSFLGS) != 0) {
		errno = EISDIR;
		return (-1);
	}

	/* Label too long? */
	if (n > namelen) {
		errno = EMSGSIZE;
		return (-1);
	}

	/* Recurse to get rest of name done first. */
	l = ns_name_map(nname + n, namelen - n, map, mapsize);
	if (l < 0)
		return (-1);

	/* Too many labels? */
	if (l >= mapsize) {
		errno = ENAMETOOLONG;
		return (-1);
	}

	/* We're on our way back up-stack, store current map data. */
	map[l].base = nname;
	map[l].len = n;
	return (l + 1);
}

/* Count the labels in a domain name.  Root counts, so COM. has two.  This
 * is to make the result comparable to the result of ns_name_map().
 */
int
ns_name_labels(ns_nname_ct nname, size_t namesiz) {
	int ret = 0;
	u_int n;

	while (namesiz-- > 0 && (n = *nname++) != 0) {
		if ((n & NS_CMPRSFLGS) != 0) {
			errno = EISDIR;
			return (-1);
		}
		if (n > namesiz) {
			errno = EMSGSIZE;
			return (-1);
		}
		nname += n;
		namesiz -= n;
		ret++;
	}
	return (ret + 1);
}

/* Private. */

/*%
 *	Thinking in noninternationalized USASCII (per the DNS spec),
 *	is this characted special ("in need of quoting") ?
 *
 * return:
 *\li	boolean.
 */
static int
special(int ch) {
	switch (ch) {
	case 0x22: /*%< '"' */
	case 0x2E: /*%< '.' */
	case 0x3B: /*%< ';' */
	case 0x5C: /*%< '\\' */
	case 0x28: /*%< '(' */
	case 0x29: /*%< ')' */
	/* Special modifiers in zone files. */
	case 0x40: /*%< '@' */
	case 0x24: /*%< '$' */
		return (1);
	default:
		return (0);
	}
}

/*%
 *	Thinking in noninternationalized USASCII (per the DNS spec),
 *	is this character visible and not a space when printed ?
 *
 * return:
 *\li	boolean.
 */
static int
printable(int ch) {
	return (ch > 0x20 && ch < 0x7f);
}

/*%
 *	Thinking in noninternationalized USASCII (per the DNS spec),
 *	convert this character to lower case if it's upper case.
 */
static int
mklower(int ch) {
	if (ch >= 0x41 && ch <= 0x5A)
		return (ch + 0x20);
	return (ch);
}

/*%
 *	Search for the counted-label name in an array of compressed names.
 *
 * return:
 *\li	offset from msg if found, or -1.
 *
 * notes:
 *\li	dnptrs is the pointer to the first name on the list,
 *\li	not the pointer to the start of the message.
 */
static int
dn_find(const u_char *domain, const u_char *msg,
	const u_char * const *dnptrs,
	const u_char * const *lastdnptr)
{
	const u_char *dn, *cp, *sp;
	const u_char * const *cpp;
	u_int n;

	for (cpp = dnptrs; cpp < lastdnptr; cpp++) {
		sp = *cpp;
		/*
		 * terminate search on:
		 * root label
		 * compression pointer
		 * unusable offset
		 */
		while (*sp != 0 && (*sp & NS_CMPRSFLGS) == 0 &&
		       (sp - msg) < 0x4000) {
			dn = domain;
			cp = sp;
			while ((n = *cp++) != 0) {
				/*
				 * check for indirection
				 */
				switch (n & NS_CMPRSFLGS) {
				case 0:		/*%< normal case, n == len */
					n = labellen(cp - 1); /*%< XXX */
					if (n != *dn++)
						goto next;

					for ((void)NULL; n > 0; n--)
						if (mklower(*dn++) !=
						    mklower(*cp++))
							goto next;
					/* Is next root for both ? */
					if (*dn == '\0' && *cp == '\0')
						return (sp - msg);
					if (*dn)
						continue;
					goto next;
				case NS_CMPRSFLGS:	/*%< indirection */
					cp = msg + (((n & 0x3f) << 8) | *cp);
					break;

				default:	/*%< illegal type */
					errno = EMSGSIZE;
					return (-1);
				}
			}
 next: ;
			sp += *sp + 1;
		}
	}
	errno = ENOENT;
	return (-1);
}

static int
decode_bitstring(const unsigned char **cpp, char *dn, const char *eom)
{
	const unsigned char *cp = *cpp;
	char *beg = dn, tc;
	int b, blen, plen, i;

	if ((blen = (*cp & 0xff)) == 0)
		blen = 256;
	plen = (blen + 3) / 4;
	plen += sizeof("\\[x/]") + (blen > 99 ? 3 : (blen > 9) ? 2 : 1);
	if (dn + plen >= eom)
		return (-1);

	cp++;
	i = SPRINTF((dn, "\\[x"));
	if (i < 0)
		return (-1);
	dn += i;
	for (b = blen; b > 7; b -= 8, cp++) {
		i = SPRINTF((dn, "%02x", *cp & 0xff));
		if (i < 0)
			return (-1);
		dn += i;
	}
	if (b > 4) {
		tc = *cp++;
		i = SPRINTF((dn, "%02x", tc & (0xff << (8 - b))));
		if (i < 0)
			return (-1);
		dn += i;
	} else if (b > 0) {
		tc = *cp++;
		i = SPRINTF((dn, "%1x",
			       ((tc >> 4) & 0x0f) & (0x0f << (4 - b)))); 
		if (i < 0)
			return (-1);
		dn += i;
	}
	i = SPRINTF((dn, "/%d]", blen));
	if (i < 0)
		return (-1);
	dn += i;

	*cpp = cp;
	return (dn - beg);
}

static int
encode_bitsring(const char **bp, const char *end, unsigned char **labelp,
		unsigned char ** dst, unsigned const char *eom)
{
	int afterslash = 0;
	const char *cp = *bp;
	unsigned char *tp;
	char c;
	const char *beg_blen;
	char *end_blen = NULL;
	int value = 0, count = 0, tbcount = 0, blen = 0;

	beg_blen = end_blen = NULL;

	/* a bitstring must contain at least 2 characters */
	if (end - cp < 2)
		return (EINVAL);

	/* XXX: currently, only hex strings are supported */
	if (*cp++ != 'x')
		return (EINVAL);
	if (!isxdigit((*cp) & 0xff)) /*%< reject '\[x/BLEN]' */
		return (EINVAL);

	for (tp = *dst + 1; cp < end && tp < eom; cp++) {
		switch((c = *cp)) {
		case ']':	/*%< end of the bitstring */
			if (afterslash) {
				if (beg_blen == NULL)
					return (EINVAL);
				blen = (int)strtol(beg_blen, &end_blen, 10);
				if (*end_blen != ']')
					return (EINVAL);
			}
			if (count)
				*tp++ = ((value << 4) & 0xff);
			cp++;	/*%< skip ']' */
			goto done;
		case '/':
			afterslash = 1;
			break;
		default:
			if (afterslash) {
				if (!isdigit(c&0xff))
					return (EINVAL);
				if (beg_blen == NULL) {
					
					if (c == '0') {
						/* blen never begings with 0 */
						return (EINVAL);
					}
					beg_blen = cp;
				}
			} else {
				if (!isxdigit(c&0xff))
					return (EINVAL);
				value <<= 4;
				value += digitvalue[(int)c];
				count += 4;
				tbcount += 4;
				if (tbcount > 256)
					return (EINVAL);
				if (count == 8) {
					*tp++ = value;
					count = 0;
				}
			}
			break;
		}
	}
  done:
	if (cp >= end || tp >= eom)
		return (EMSGSIZE);

	/*
	 * bit length validation:
	 * If a <length> is present, the number of digits in the <bit-data>
	 * MUST be just sufficient to contain the number of bits specified
	 * by the <length>. If there are insignificant bits in a final
	 * hexadecimal or octal digit, they MUST be zero.
	 * RFC2673, Section 3.2.
	 */
	if (blen > 0) {
		int traillen;

		if (((blen + 3) & ~3) != tbcount)
			return (EINVAL);
		traillen = tbcount - blen; /*%< between 0 and 3 */
		if (((value << (8 - traillen)) & 0xff) != 0)
			return (EINVAL);
	}
	else
		blen = tbcount;
	if (blen == 256)
		blen = 0;

	/* encode the type and the significant bit fields */
	**labelp = DNS_LABELTYPE_BITSTRING;
	**dst = blen;

	*bp = cp;
	*dst = tp;

	return (0);
}

static int
labellen(const u_char *lp)
{
	int bitlen;
	u_char l = *lp;

	if ((l & NS_CMPRSFLGS) == NS_CMPRSFLGS) {
		/* should be avoided by the caller */
		return (-1);
	}

	if ((l & NS_CMPRSFLGS) == NS_TYPE_ELT) {
		if (l == DNS_LABELTYPE_BITSTRING) {
			if ((bitlen = *(lp + 1)) == 0)
				bitlen = 256;
			return ((bitlen + 7 ) / 8 + 1);
		}
		return (-1);	/*%< unknwon ELT */
	}
	return (l);
}

/*! \file */
