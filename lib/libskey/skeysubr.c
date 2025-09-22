/* OpenBSD S/Key (skeysubr.c)
 *
 * Authors:
 *          Neil M. Haller <nmh@thumper.bellcore.com>
 *          Philip R. Karn <karn@chicago.qualcomm.com>
 *          John S. Walden <jsw@thumper.bellcore.com>
 *          Scott Chasin <chasin@crimelab.com>
 *          Todd C. Miller <millert@openbsd.org>
 *
 * S/Key misc routines.
 *
 * $OpenBSD: skeysubr.c,v 1.36 2023/03/08 04:43:05 guenther Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <md5.h>
#include <sha1.h>
#include <rmd160.h>

#include "skey.h"

/* Default hash function to use (index into skey_algorithm_table array) */
#ifndef SKEY_HASH_DEFAULT
#define SKEY_HASH_DEFAULT	0	/* md5 */
#endif

static void keycrunch_md5(char *, char *, size_t);
static void keycrunch_sha1(char *, char *, size_t);
static void keycrunch_rmd160(char *, char *, size_t);
static void skey_echo(int);
static void trapped(int);

/* Current hash type (index into skey_algorithm_table array) */
static int skey_hash_type = SKEY_HASH_DEFAULT;

/*
 * Hash types we support.
 * Each has an associated keycrunch() and f() function.
 */
struct skey_algorithm_table {
	const char *name;
	void (*keycrunch)(char *, char *, size_t);
};
static struct skey_algorithm_table skey_algorithm_table[] = {
	{ "md5", keycrunch_md5 },
	{ "sha1", keycrunch_sha1 },
	{ "rmd160", keycrunch_rmd160 },
	{ NULL }
};


/*
 * Crunch a key:
 *  Concatenate the seed and the password, run through hash function and
 *  collapse to 64 bits.  This is defined as the user's starting key.
 *  The result pointer must have at least SKEY_BINKEY_SIZE bytes of storage.
 *  The seed and password may be of any length.
 */
int
keycrunch(char *result, char *seed, char *passwd)
{
	char *buf, *p;
	size_t buflen;

	buflen = strlen(seed) + strlen(passwd);
	if ((buf = malloc(buflen + 1)) == NULL)
		return(-1);

	(void)strlcpy(buf, seed, buflen + 1);
	for (p = buf; *p; p++)
		*p = (char)tolower((unsigned char)*p);

	(void)strlcat(buf, passwd, buflen + 1);
	sevenbit(buf);

	skey_algorithm_table[skey_hash_type].keycrunch(result, buf, buflen);

	(void)free(buf);
	return(0);
}

static void
keycrunch_md5(char *result, char *buf, size_t buflen)
{
	MD5_CTX md;
	u_int32_t results[4];

	/* Crunch the key through MD5 */
	MD5Init(&md);
	MD5Update(&md, (unsigned char *)buf, buflen);
	MD5Final((unsigned char *)results, &md);

	/* Fold result from 128 to 64 bits */
	results[0] ^= results[2];
	results[1] ^= results[3];

	(void)memcpy((void *)result, (void *)results, SKEY_BINKEY_SIZE);
}

static void
keycrunch_sha1(char *result, char *buf, size_t buflen)
{
	SHA1_CTX sha;
	int i, j;

	/* Crunch the key through SHA1 */
	SHA1Init(&sha);
	SHA1Update(&sha, (unsigned char *)buf, buflen);
	SHA1Pad(&sha);

	/* Fold 160 to 64 bits */
	sha.state[0] ^= sha.state[2];
	sha.state[1] ^= sha.state[3];
	sha.state[0] ^= sha.state[4];

	/*
	 * SHA1 is a big endian algorithm but RFC2289 mandates that
	 * the result be in little endian form, so we copy to the
	 * result buffer manually.
	 */
	for (i = 0, j = 0; j < 8; i++, j += 4) {
		result[j]   = (u_char)(sha.state[i] & 0xff);
		result[j+1] = (u_char)((sha.state[i] >> 8)  & 0xff);
		result[j+2] = (u_char)((sha.state[i] >> 16) & 0xff);
		result[j+3] = (u_char)((sha.state[i] >> 24) & 0xff);
	}
}

static void
keycrunch_rmd160(char *result, char *buf, size_t buflen)
{
	RMD160_CTX rmd;
	u_int32_t results[5];

	/* Crunch the key through RMD-160 */
	RMD160Init(&rmd);
	RMD160Update(&rmd, (unsigned char *)buf, buflen);
	RMD160Final((unsigned char *)results, &rmd);

	/* Fold 160 to 64 bits */
	results[0] ^= results[2];
	results[1] ^= results[3];
	results[0] ^= results[4];

	(void)memcpy((void *)result, (void *)results, SKEY_BINKEY_SIZE);
}

/*
 * The one-way hash function f().
 * Takes SKEY_BINKEY_SIZE bytes and returns SKEY_BINKEY_SIZE bytes in place.
 */
void
f(char *x)
{
	skey_algorithm_table[skey_hash_type].keycrunch(x, x, SKEY_BINKEY_SIZE);
}

/* Strip trailing cr/lf from a line of text */
void
rip(char *buf)
{
	buf += strcspn(buf, "\r\n");

	if (*buf)
		*buf = '\0';
}

/* Read in secret password (turns off echo) */
char *
readpass(char *buf, int n)
{
	void (*old_handler)(int);

	/* Turn off echoing */
	skey_echo(0);

	/* Catch SIGINT and save old signal handler */
	old_handler = signal(SIGINT, trapped);

	if (fgets(buf, n, stdin) == NULL)
		buf[0] = '\0';
	rip(buf);

	(void)putc('\n', stderr);
	(void)fflush(stderr);

	/* Restore signal handler and turn echo back on */
	if (old_handler != SIG_ERR)
		(void)signal(SIGINT, old_handler);
	skey_echo(1);

	sevenbit(buf);

	return(buf);
}

/* Read in an s/key OTP (does not turn off echo) */
char *
readskey(char *buf, int n)
{
	if (fgets(buf, n, stdin) == NULL)
		buf[0] = '\0';
	rip(buf);

	sevenbit(buf);

	return(buf);
}

/* Signal handler for trapping ^C */
static void
trapped(int sig)
{
	write(STDERR_FILENO, "^C\n", 3);

	/* Turn on echo if necessary */
	skey_echo(1);

	_exit(1);
}

/*
 * Convert 16-byte hex-ascii string to 8-byte binary array
 * Returns 0 on success, -1 on error
 */
int
atob8(char *out, char *in)
{
	int i;
	int val;

	if (in == NULL || out == NULL)
		return(-1);

	for (i=0; i < 8; i++) {
		if ((in = skipspace(in)) == NULL)
			return(-1);
		if ((val = htoi(*in++)) == -1)
			return(-1);
		*out = val << 4;

		if ((in = skipspace(in)) == NULL)
			return(-1);
		if ((val = htoi(*in++)) == -1)
			return(-1);
		*out++ |= val;
	}
	return(0);
}

/* Convert 8-byte binary array to 16-byte hex-ascii string */
int
btoa8(char *out, char *in)
{
	if (in == NULL || out == NULL)
		return(-1);

	(void)snprintf(out, 17, "%02x%02x%02x%02x%02x%02x%02x%02x",
	    in[0] & 0xff, in[1] & 0xff, in[2] & 0xff, in[3] & 0xff,
	    in[4] & 0xff, in[5] & 0xff, in[6] & 0xff, in[7] & 0xff);

	return(0);
}

/* Convert hex digit to binary integer */
int
htoi(int c)
{
	if ('0' <= c && c <= '9')
		return(c - '0');
	if ('a' <= c && c <= 'f')
		return(10 + c - 'a');
	if ('A' <= c && c <= 'F')
		return(10 + c - 'A');
	return(-1);
}

/* Skip leading spaces from the string */
char *
skipspace(char *cp)
{
	while (*cp == ' ' || *cp == '\t')
		cp++;

	if (*cp == '\0')
		return(NULL);
	else
		return(cp);
}

/* Remove backspaced over characters from the string */
void
backspace(char *buf)
{
	char bs = 0x8;
	char *cp = buf;
	char *out = buf;

	while (*cp) {
		if (*cp == bs) {
			if (out == buf) {
				cp++;
				continue;
			} else {
				cp++;
				out--;
			}
		} else {
			*out++ = *cp++;
		}

	}
	*out = '\0';
}

/* Make sure line is all seven bits */
void
sevenbit(char *s)
{
	while (*s)
		*s++ &= 0x7f;
}

/* Set hash algorithm type */
char *
skey_set_algorithm(char *new)
{
	int i;

	for (i = 0; skey_algorithm_table[i].name; i++) {
		if (strcmp(new, skey_algorithm_table[i].name) == 0) {
			skey_hash_type = i;
			return(new);
		}
	}

	return(NULL);
}

/* Get current hash type */
const char *
skey_get_algorithm(void)
{
	return(skey_algorithm_table[skey_hash_type].name);
}

/* Turn echo on/off */
static void
skey_echo(int action)
{
	static struct termios term;
	static int echo = 0;

	if (action == 0) {
		/* Turn echo off */
		(void) tcgetattr(fileno(stdin), &term);
		if ((echo = (term.c_lflag & ECHO))) {
			term.c_lflag &= ~ECHO;
			(void) tcsetattr(fileno(stdin), TCSAFLUSH|TCSASOFT, &term);
		}
	} else if (action && echo) {
		/* Turn echo on */
		term.c_lflag |= ECHO;
		(void) tcsetattr(fileno(stdin), TCSAFLUSH|TCSASOFT, &term);
		echo = 0;
	}
}
