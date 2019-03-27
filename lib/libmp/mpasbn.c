/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Dima Dorfman.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This is the traditional Berkeley MP library implemented in terms of
 * the OpenSSL BIGNUM library.  It was written to replace libgmp, and
 * is meant to be as compatible with the latter as feasible.
 *
 * There seems to be a lack of documentation for the Berkeley MP
 * interface.  All I could find was libgmp documentation (which didn't
 * talk about the semantics of the functions) and an old SunOS 4.1
 * manual page from 1989.  The latter wasn't very detailed, either,
 * but at least described what the function's arguments were.  In
 * general the interface seems to be archaic, somewhat poorly
 * designed, and poorly, if at all, documented.  It is considered
 * harmful.
 *
 * Miscellaneous notes on this implementation:
 *
 *  - The SunOS manual page mentioned above indicates that if an error
 *  occurs, the library should "produce messages and core images."
 *  Given that most of the functions don't have return values (and
 *  thus no sane way of alerting the caller to an error), this seems
 *  reasonable.  The MPERR and MPERRX macros call warn and warnx,
 *  respectively, then abort().
 *
 *  - All the functions which take an argument to be "filled in"
 *  assume that the argument has been initialized by one of the *tom()
 *  routines before being passed to it.  I never saw this documented
 *  anywhere, but this seems to be consistent with the way this
 *  library is used.
 *
 *  - msqrt() is the only routine which had to be implemented which
 *  doesn't have a close counterpart in the OpenSSL BIGNUM library.
 *  It was implemented by hand using Newton's recursive formula.
 *  Doing it this way, although more error-prone, has the positive
 *  sideaffect of testing a lot of other functions; if msqrt()
 *  produces the correct results, most of the other routines will as
 *  well.
 *
 *  - Internal-use-only routines (i.e., those defined here statically
 *  and not in mp.h) have an underscore prepended to their name (this
 *  is more for aesthetical reasons than technical).  All such
 *  routines take an extra argument, 'msg', that denotes what they
 *  should call themselves in an error message.  This is so a user
 *  doesn't get an error message from a function they didn't call.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/crypto.h>
#include <openssl/err.h>

#include "mp.h"

#define MPERR(s)	do { warn s; abort(); } while (0)
#define MPERRX(s)	do { warnx s; abort(); } while (0)
#define BN_ERRCHECK(msg, expr) do {		\
	if (!(expr)) _bnerr(msg);		\
} while (0)

static void _bnerr(const char *);
static MINT *_dtom(const char *, const char *);
static MINT *_itom(const char *, short);
static void _madd(const char *, const MINT *, const MINT *, MINT *);
static int _mcmpa(const char *, const MINT *, const MINT *);
static void _mdiv(const char *, const MINT *, const MINT *, MINT *, MINT *,
		BN_CTX *);
static void _mfree(const char *, MINT *);
static void _moveb(const char *, const BIGNUM *, MINT *);
static void _movem(const char *, const MINT *, MINT *);
static void _msub(const char *, const MINT *, const MINT *, MINT *);
static char *_mtod(const char *, const MINT *);
static char *_mtox(const char *, const MINT *);
static void _mult(const char *, const MINT *, const MINT *, MINT *, BN_CTX *);
static void _sdiv(const char *, const MINT *, short, MINT *, short *, BN_CTX *);
static MINT *_xtom(const char *, const char *);

/*
 * Report an error from one of the BN_* functions using MPERRX.
 */
static void
_bnerr(const char *msg)
{

	ERR_load_crypto_strings();
	MPERRX(("%s: %s", msg, ERR_reason_error_string(ERR_get_error())));
}

/*
 * Convert a decimal string to an MINT.
 */
static MINT *
_dtom(const char *msg, const char *s)
{
	MINT *mp;

	mp = malloc(sizeof(*mp));
	if (mp == NULL)
		MPERR(("%s", msg));
	mp->bn = BN_new();
	if (mp->bn == NULL)
		_bnerr(msg);
	BN_ERRCHECK(msg, BN_dec2bn(&mp->bn, s));
	return (mp);
}

/*
 * Compute the greatest common divisor of mp1 and mp2; result goes in rmp.
 */
void
mp_gcd(const MINT *mp1, const MINT *mp2, MINT *rmp)
{
	BIGNUM *b;
	BN_CTX *c;

	b = NULL;
	c = BN_CTX_new();
	if (c != NULL)
		b = BN_new();
	if (c == NULL || b == NULL)
		_bnerr("gcd");
	BN_ERRCHECK("gcd", BN_gcd(b, mp1->bn, mp2->bn, c));
	_moveb("gcd", b, rmp);
	BN_free(b);
	BN_CTX_free(c);
}

/*
 * Make an MINT out of a short integer.  Return value must be mfree()'d.
 */
static MINT *
_itom(const char *msg, short n)
{
	MINT *mp;
	char *s;

	asprintf(&s, "%x", n);
	if (s == NULL)
		MPERR(("%s", msg));
	mp = _xtom(msg, s);
	free(s);
	return (mp);
}

MINT *
mp_itom(short n)
{

	return (_itom("itom", n));
}

/*
 * Compute rmp=mp1+mp2.
 */
static void
_madd(const char *msg, const MINT *mp1, const MINT *mp2, MINT *rmp)
{
	BIGNUM *b;

	b = BN_new();
	if (b == NULL)
		_bnerr(msg);
	BN_ERRCHECK(msg, BN_add(b, mp1->bn, mp2->bn));
	_moveb(msg, b, rmp);
	BN_free(b);
}

void
mp_madd(const MINT *mp1, const MINT *mp2, MINT *rmp)
{

	_madd("madd", mp1, mp2, rmp);
}

/*
 * Return -1, 0, or 1 if mp1<mp2, mp1==mp2, or mp1>mp2, respectivley.
 */
int
mp_mcmp(const MINT *mp1, const MINT *mp2)
{

	return (BN_cmp(mp1->bn, mp2->bn));
}

/*
 * Same as mcmp but compares absolute values.
 */
static int
_mcmpa(const char *msg __unused, const MINT *mp1, const MINT *mp2)
{

	return (BN_ucmp(mp1->bn, mp2->bn));
}

/*
 * Compute qmp=nmp/dmp and rmp=nmp%dmp.
 */
static void
_mdiv(const char *msg, const MINT *nmp, const MINT *dmp, MINT *qmp, MINT *rmp,
    BN_CTX *c)
{
	BIGNUM *q, *r;

	q = NULL;
	r = BN_new();
	if (r != NULL)
		q = BN_new();
	if (r == NULL || q == NULL)
		_bnerr(msg);
	BN_ERRCHECK(msg, BN_div(q, r, nmp->bn, dmp->bn, c));
	_moveb(msg, q, qmp);
	_moveb(msg, r, rmp);
	BN_free(q);
	BN_free(r);
}

void
mp_mdiv(const MINT *nmp, const MINT *dmp, MINT *qmp, MINT *rmp)
{
	BN_CTX *c;

	c = BN_CTX_new();
	if (c == NULL)
		_bnerr("mdiv");
	_mdiv("mdiv", nmp, dmp, qmp, rmp, c);
	BN_CTX_free(c);
}

/*
 * Free memory associated with an MINT.
 */
static void
_mfree(const char *msg __unused, MINT *mp)
{

	BN_clear(mp->bn);
	BN_free(mp->bn);
	free(mp);
}

void
mp_mfree(MINT *mp)
{

	_mfree("mfree", mp);
}

/*
 * Read an integer from standard input and stick the result in mp.
 * The input is treated to be in base 10.  This must be the silliest
 * API in existence; why can't the program read in a string and call
 * xtom()?  (Or if base 10 is desires, perhaps dtom() could be
 * exported.)
 */
void
mp_min(MINT *mp)
{
	MINT *rmp;
	char *line, *nline;
	size_t linelen;

	line = fgetln(stdin, &linelen);
	if (line == NULL)
		MPERR(("min"));
	nline = malloc(linelen + 1);
	if (nline == NULL)
		MPERR(("min"));
	memcpy(nline, line, linelen);
	nline[linelen] = '\0';
	rmp = _dtom("min", nline);
	_movem("min", rmp, mp);
	_mfree("min", rmp);
	free(nline);
}

/*
 * Print the value of mp to standard output in base 10.  See blurb
 * above min() for why this is so useless.
 */
void
mp_mout(const MINT *mp)
{
	char *s;

	s = _mtod("mout", mp);
	printf("%s", s);
	free(s);
}

/*
 * Set the value of tmp to the value of smp (i.e., tmp=smp).
 */
void
mp_move(const MINT *smp, MINT *tmp)
{

	_movem("move", smp, tmp);
}


/*
 * Internal routine to set the value of tmp to that of sbp.
 */
static void
_moveb(const char *msg, const BIGNUM *sbp, MINT *tmp)
{

	BN_ERRCHECK(msg, BN_copy(tmp->bn, sbp));
}

/*
 * Internal routine to set the value of tmp to that of smp.
 */
static void
_movem(const char *msg, const MINT *smp, MINT *tmp)
{

	BN_ERRCHECK(msg, BN_copy(tmp->bn, smp->bn));
}

/*
 * Compute the square root of nmp and put the result in xmp.  The
 * remainder goes in rmp.  Should satisfy: rmp=nmp-(xmp*xmp).
 *
 * Note that the OpenSSL BIGNUM library does not have a square root
 * function, so this had to be implemented by hand using Newton's
 * recursive formula:
 *
 *		x = (x + (n / x)) / 2
 *
 * where x is the square root of the positive number n.  In the
 * beginning, x should be a reasonable guess, but the value 1,
 * although suboptimal, works, too; this is that is used below.
 */
void
mp_msqrt(const MINT *nmp, MINT *xmp, MINT *rmp)
{
	BN_CTX *c;
	MINT *tolerance;
	MINT *ox, *x;
	MINT *z1, *z2, *z3;
	short i;

	c = BN_CTX_new();
	if (c == NULL)
		_bnerr("msqrt");
	tolerance = _itom("msqrt", 1);
	x = _itom("msqrt", 1);
	ox = _itom("msqrt", 0);
	z1 = _itom("msqrt", 0);
	z2 = _itom("msqrt", 0);
	z3 = _itom("msqrt", 0);
	do {
		_movem("msqrt", x, ox);
		_mdiv("msqrt", nmp, x, z1, z2, c);
		_madd("msqrt", x, z1, z2);
		_sdiv("msqrt", z2, 2, x, &i, c);
		_msub("msqrt", ox, x, z3);
	} while (_mcmpa("msqrt", z3, tolerance) == 1);
	_movem("msqrt", x, xmp);
	_mult("msqrt", x, x, z1, c);
	_msub("msqrt", nmp, z1, z2);
	_movem("msqrt", z2, rmp);
	_mfree("msqrt", tolerance);
	_mfree("msqrt", ox);
	_mfree("msqrt", x);
	_mfree("msqrt", z1);
	_mfree("msqrt", z2);
	_mfree("msqrt", z3);
	BN_CTX_free(c);
}

/*
 * Compute rmp=mp1-mp2.
 */
static void
_msub(const char *msg, const MINT *mp1, const MINT *mp2, MINT *rmp)
{
	BIGNUM *b;

	b = BN_new();
	if (b == NULL)
		_bnerr(msg);
	BN_ERRCHECK(msg, BN_sub(b, mp1->bn, mp2->bn));
	_moveb(msg, b, rmp);
	BN_free(b);
}

void
mp_msub(const MINT *mp1, const MINT *mp2, MINT *rmp)
{

	_msub("msub", mp1, mp2, rmp);
}

/*
 * Return a decimal representation of mp.  Return value must be
 * free()'d.
 */
static char *
_mtod(const char *msg, const MINT *mp)
{
	char *s, *s2;

	s = BN_bn2dec(mp->bn);
	if (s == NULL)
		_bnerr(msg);
	asprintf(&s2, "%s", s);
	if (s2 == NULL)
		MPERR(("%s", msg));
	OPENSSL_free(s);
	return (s2);
}

/*
 * Return a hexadecimal representation of mp.  Return value must be
 * free()'d.
 */
static char *
_mtox(const char *msg, const MINT *mp)
{
	char *p, *s, *s2;
	int len;

	s = BN_bn2hex(mp->bn);
	if (s == NULL)
		_bnerr(msg);
	asprintf(&s2, "%s", s);
	if (s2 == NULL)
		MPERR(("%s", msg));
	OPENSSL_free(s);

	/*
	 * This is a kludge for libgmp compatibility.  The latter's
	 * implementation of this function returns lower-case letters,
	 * but BN_bn2hex returns upper-case.  Some programs (e.g.,
	 * newkey(1)) are sensitive to this.  Although it's probably
	 * their fault, it's nice to be compatible.
	 */
	len = strlen(s2);
	for (p = s2; p < s2 + len; p++)
		*p = tolower(*p);

	return (s2);
}

char *
mp_mtox(const MINT *mp)
{

	return (_mtox("mtox", mp));
}

/*
 * Compute rmp=mp1*mp2.
 */
static void
_mult(const char *msg, const MINT *mp1, const MINT *mp2, MINT *rmp, BN_CTX *c)
{
	BIGNUM *b;

	b = BN_new();
	if (b == NULL)
		_bnerr(msg);
	BN_ERRCHECK(msg, BN_mul(b, mp1->bn, mp2->bn, c));
	_moveb(msg, b, rmp);
	BN_free(b);
}

void
mp_mult(const MINT *mp1, const MINT *mp2, MINT *rmp)
{
	BN_CTX *c;

	c = BN_CTX_new();
	if (c == NULL)
		_bnerr("mult");
	_mult("mult", mp1, mp2, rmp, c);
	BN_CTX_free(c);
}

/*
 * Compute rmp=(bmp^emp)mod mmp.  (Note that here and above rpow() '^'
 * means 'raise to power', not 'bitwise XOR'.)
 */
void
mp_pow(const MINT *bmp, const MINT *emp, const MINT *mmp, MINT *rmp)
{
	BIGNUM *b;
	BN_CTX *c;

	b = NULL;
	c = BN_CTX_new();
	if (c != NULL)
		b = BN_new();
	if (c == NULL || b == NULL)
		_bnerr("pow");
	BN_ERRCHECK("pow", BN_mod_exp(b, bmp->bn, emp->bn, mmp->bn, c));
	_moveb("pow", b, rmp);
	BN_free(b);
	BN_CTX_free(c);
}

/*
 * Compute rmp=bmp^e.  (See note above pow().)
 */
void
mp_rpow(const MINT *bmp, short e, MINT *rmp)
{
	MINT *emp;
	BIGNUM *b;
	BN_CTX *c;

	b = NULL;
	c = BN_CTX_new();
	if (c != NULL)
		b = BN_new();
	if (c == NULL || b == NULL)
		_bnerr("rpow");
	emp = _itom("rpow", e);
	BN_ERRCHECK("rpow", BN_exp(b, bmp->bn, emp->bn, c));
	_moveb("rpow", b, rmp);
	_mfree("rpow", emp);
	BN_free(b);
	BN_CTX_free(c);
}

/*
 * Compute qmp=nmp/d and ro=nmp%d.
 */
static void
_sdiv(const char *msg, const MINT *nmp, short d, MINT *qmp, short *ro,
    BN_CTX *c)
{
	MINT *dmp, *rmp;
	BIGNUM *q, *r;
	char *s;

	r = NULL;
	q = BN_new();
	if (q != NULL)
		r = BN_new();
	if (q == NULL || r == NULL)
		_bnerr(msg);
	dmp = _itom(msg, d);
	rmp = _itom(msg, 0);
	BN_ERRCHECK(msg, BN_div(q, r, nmp->bn, dmp->bn, c));
	_moveb(msg, q, qmp);
	_moveb(msg, r, rmp);
	s = _mtox(msg, rmp);
	errno = 0;
	*ro = strtol(s, NULL, 16);
	if (errno != 0)
		MPERR(("%s underflow or overflow", msg));
	free(s);
	_mfree(msg, dmp);
	_mfree(msg, rmp);
	BN_free(r);
	BN_free(q);
}

void
mp_sdiv(const MINT *nmp, short d, MINT *qmp, short *ro)
{
	BN_CTX *c;

	c = BN_CTX_new();
	if (c == NULL)
		_bnerr("sdiv");
	_sdiv("sdiv", nmp, d, qmp, ro, c);
	BN_CTX_free(c);
}

/*
 * Convert a hexadecimal string to an MINT.
 */
static MINT *
_xtom(const char *msg, const char *s)
{
	MINT *mp;

	mp = malloc(sizeof(*mp));
	if (mp == NULL)
		MPERR(("%s", msg));
	mp->bn = BN_new();
	if (mp->bn == NULL)
		_bnerr(msg);
	BN_ERRCHECK(msg, BN_hex2bn(&mp->bn, s));
	return (mp);
}

MINT *
mp_xtom(const char *s)
{

	return (_xtom("xtom", s));
}
