/*	$OpenBSD: res_comp.c,v 1.23 2023/03/15 22:12:00 millert Exp $	*/

/*
 * ++Copyright++ 1985, 1993
 * -
 * Copyright (c) 1985, 1993
 *    The Regents of the University of California.  All rights reserved.
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
 * -
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Digital Equipment Corporation not be used in advertising or
 * publicity pertaining to distribution of the document or software without
 * specific, written prior permission.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 * -
 * --Copyright--
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>

#include <stdio.h>
#include <resolv.h>
#include <ctype.h>

#include <unistd.h>
#include <limits.h>
#include <string.h>

static int dn_find(u_char *, u_char *, u_char **, u_char **);

/*
 * Expand compressed domain name 'comp_dn' to full domain name.
 * 'msg' is a pointer to the beginning of the message,
 * 'eomorig' points to the first location after the message,
 * 'exp_dn' is a pointer to a buffer of size 'length' for the result.
 * Return size of compressed name or -1 if there was an error.
 */
int
dn_expand(const u_char *msg, const u_char *eomorig, const u_char *comp_dn,
    char *exp_dn, int length)
{
	const u_char *cp;
	char *dn;
	int n, c;
	char *eom;
	int len = -1, checked = 0;

	if (comp_dn < msg || comp_dn >= eomorig)
		return (-1);

	dn = exp_dn;
	cp = comp_dn;
	if (length > HOST_NAME_MAX)
		length = HOST_NAME_MAX;
	eom = exp_dn + length;
	/*
	 * fetch next label in domain name
	 */
	while ((n = *cp++)) {
		if (cp >= eomorig)	/* out of range */
			return (-1);

		/*
		 * Check for indirection
		 */
		switch (n & INDIR_MASK) {
		case 0:
			if (dn != exp_dn) {
				if (dn >= eom)
					return (-1);
				*dn++ = '.';
			}
			if (dn+n >= eom)
				return (-1);
			checked += n + 1;
			while (--n >= 0) {
				if (((c = *cp++) == '.') || (c == '\\')) {
					if (dn + n + 2 >= eom)
						return (-1);
					*dn++ = '\\';
				}
				*dn++ = c;
				if (cp >= eomorig)	/* out of range */
					return (-1);
			}
			break;

		case INDIR_MASK:
			if (len < 0)
				len = cp - comp_dn + 1;
			cp = msg + (((n & 0x3f) << 8) | (*cp & 0xff));
			if (cp < msg || cp >= eomorig)	/* out of range */
				return (-1);
			checked += 2;
			/*
			 * Check for loops in the compressed name;
			 * if we've looked at the whole message,
			 * there must be a loop.
			 */
			if (checked >= eomorig - msg)
				return (-1);
			break;

		default:
			return (-1);			/* flag error */
		}
	}
	*dn = '\0';
	if (len < 0)
		len = cp - comp_dn;
	return (len);
}
DEF_WEAK(dn_expand);

/*
 * Compress domain name 'exp_dn' into 'comp_dn'.
 * Return the size of the compressed name or -1.
 * 'length' is the size of the array pointed to by 'comp_dn'.
 * 'dnptrs' is a list of pointers to previous compressed names. dnptrs[0]
 * is a pointer to the beginning of the message. The list ends with NULL.
 * 'lastdnptr' is a pointer to the end of the array pointed to
 * by 'dnptrs'. Side effect is to update the list of pointers for
 * labels inserted into the message as we compress the name.
 * If 'dnptr' is NULL, we don't try to compress names. If 'lastdnptr'
 * is NULL, we don't update the list.
 */
int
dn_comp(const char *exp_dn, u_char *comp_dn, int length, u_char **dnptrs,
    u_char **lastdnptr)
{
	u_char *cp, *dn;
	int c, l;
	u_char **cpp, **lpp, *sp, *eob;
	u_char *msg;

	dn = (u_char *)exp_dn;
	cp = comp_dn;
	eob = cp + length;
	lpp = cpp = NULL;
	if (dnptrs != NULL) {
		if ((msg = *dnptrs++) != NULL) {
			for (cpp = dnptrs; *cpp != NULL; cpp++)
				;
			lpp = cpp;	/* end of list to search */
		}
	} else
		msg = NULL;
	for (c = *dn++; c != '\0'; ) {
		/* look to see if we can use pointers */
		if (msg != NULL) {
			if ((l = dn_find(dn-1, msg, dnptrs, lpp)) >= 0) {
				if (cp+1 >= eob)
					return (-1);
				*cp++ = (l >> 8) | INDIR_MASK;
				*cp++ = l % 256;
				return (cp - comp_dn);
			}
			/* not found, save it */
			if (lastdnptr != NULL && cpp < lastdnptr-1) {
				*cpp++ = cp;
				*cpp = NULL;
			}
		}
		sp = cp++;	/* save ptr to length byte */
		do {
			if (c == '.') {
				c = *dn++;
				break;
			}
			if (c == '\\') {
				if ((c = *dn++) == '\0')
					break;
			}
			if (cp >= eob) {
				if (msg != NULL)
					*lpp = NULL;
				return (-1);
			}
			*cp++ = c;
		} while ((c = *dn++) != '\0');
		/* catch trailing '.'s but not '..' */
		if ((l = cp - sp - 1) == 0 && c == '\0') {
			cp--;
			break;
		}
		if (l <= 0 || l > MAXLABEL) {
			if (msg != NULL)
				*lpp = NULL;
			return (-1);
		}
		*sp = l;
	}
	if (cp >= eob) {
		if (msg != NULL)
			*lpp = NULL;
		return (-1);
	}
	*cp++ = '\0';
	return (cp - comp_dn);
}

/*
 * Skip over a compressed domain name. Return the size or -1.
 */
int
__dn_skipname(const u_char *comp_dn, const u_char *eom)
{
	const u_char *cp;
	int n;

	cp = comp_dn;
	while (cp < eom && (n = *cp++)) {
		/*
		 * check for indirection
		 */
		switch (n & INDIR_MASK) {
		case 0:			/* normal case, n == len */
			cp += n;
			continue;
		case INDIR_MASK:	/* indirection */
			cp++;
			break;
		default:		/* illegal type */
			return (-1);
		}
		break;
	}
	if (cp > eom)
		return (-1);
	return (cp - comp_dn);
}

/*
 * Search for expanded name from a list of previously compressed names.
 * Return the offset from msg if found or -1.
 * dnptrs is the pointer to the first name on the list,
 * not the pointer to the start of the message.
 */
static int
dn_find(u_char *exp_dn, u_char *msg, u_char **dnptrs, u_char **lastdnptr)
{
	u_char *dn, *cp, **cpp;
	int n;
	u_char *sp;

	for (cpp = dnptrs; cpp < lastdnptr; cpp++) {
		dn = exp_dn;
		sp = cp = *cpp;
		while ((n = *cp++)) {
			/*
			 * check for indirection
			 */
			switch (n & INDIR_MASK) {
			case 0:		/* normal case, n == len */
				while (--n >= 0) {
					if (*dn == '.')
						goto next;
					if (*dn == '\\')
						dn++;
					if (tolower((unsigned char)*dn++) !=
					    tolower((unsigned char)*cp++))
						goto next;
				}
				if ((n = *dn++) == '\0' && *cp == '\0')
					return (sp - msg);
				if (n == '.')
					continue;
				goto next;

			case INDIR_MASK:	/* indirection */
				cp = msg + (((n & 0x3f) << 8) | *cp);
				break;

			default:	/* illegal type */
				return (-1);
			}
		}
		if (*dn == '\0')
			return (sp - msg);
	next:	;
	}
	return (-1);
}

/*
 * Verify that a domain name uses an acceptable character set.
 */

/*
 * Note the conspicuous absence of ctype macros in these definitions.  On
 * non-ASCII hosts, we can't depend on string literals or ctype macros to
 * tell us anything about network-format data.  The rest of the BIND system
 * is not careful about this, but for some reason, we're doing it right here.
 */
#define PERIOD 0x2e
#define	hyphenchar(c) ((c) == 0x2d)
#define bslashchar(c) ((c) == 0x5c)
#define underscorechar(c) ((c) == 0x5f)
#define periodchar(c) ((c) == PERIOD)
#define asterchar(c) ((c) == 0x2a)
#define alphachar(c) (((c) >= 0x41 && (c) <= 0x5a) \
		   || ((c) >= 0x61 && (c) <= 0x7a))
#define digitchar(c) ((c) >= 0x30 && (c) <= 0x39)

#define borderchar(c) (alphachar(c) || digitchar(c))
#define middlechar(c) (borderchar(c) || hyphenchar(c) || underscorechar(c))
#define	domainchar(c) ((c) > 0x20 && (c) < 0x7f)

int
__res_hnok(const char *dn)
{
	int pch = PERIOD, ch = *dn++;

	while (ch != '\0') {
		int nch = *dn++;

		if (periodchar(ch)) {
			;
		} else if (periodchar(pch)) {
			if (!borderchar(ch))
				return (0);
		} else if (periodchar(nch) || nch == '\0') {
			if (!borderchar(ch))
				return (0);
		} else {
			if (!middlechar(ch))
				return (0);
		}
		pch = ch, ch = nch;
	}
	return (1);
}
DEF_STRONG(__res_hnok);

/*
 * hostname-like (A, MX, WKS) owners can have "*" as their first label
 * but must otherwise be as a host name.
 */
int
res_ownok(const char *dn)
{
	if (asterchar(dn[0])) {
		if (periodchar(dn[1]))
			return (res_hnok(dn+2));
		if (dn[1] == '\0')
			return (1);
	}
	return (res_hnok(dn));
}

/*
 * SOA RNAMEs and RP RNAMEs can have any printable character in their first
 * label, but the rest of the name has to look like a host name.
 */
int
res_mailok(const char *dn)
{
	int ch, escaped = 0;

	/* "." is a valid missing representation */
	if (*dn == '\0')
		return(1);

	/* otherwise <label>.<hostname> */
	while ((ch = *dn++) != '\0') {
		if (!domainchar(ch))
			return (0);
		if (!escaped && periodchar(ch))
			break;
		if (escaped)
			escaped = 0;
		else if (bslashchar(ch))
			escaped = 1;
	}
	if (periodchar(ch))
		return (res_hnok(dn));
	return(0);
}

/*
 * This function is quite liberal, since RFC 1034's character sets are only
 * recommendations.
 */
int
res_dnok(const char *dn)
{
	int ch;

	while ((ch = *dn++) != '\0')
		if (!domainchar(ch))
			return (0);
	return (1);
}

/*
 * Routines to insert/extract short/long's.
 */

u_int16_t
_getshort(const u_char *msgp)
{
	u_int16_t u;

	GETSHORT(u, msgp);
	return (u);
}
DEF_STRONG(_getshort);

u_int32_t
_getlong(const u_char *msgp)
{
	u_int32_t u;

	GETLONG(u, msgp);
	return (u);
}
DEF_STRONG(_getlong);

void
__putshort(u_int16_t s, u_char *msgp)
{
	PUTSHORT(s, msgp);
}

void
__putlong(u_int32_t l, u_char *msgp)
{
	PUTLONG(l, msgp);
}
