/*-
 * Copyright (c) 1997
 *	David L Nugent <davidn@blaze.net.au>.
 *	All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. This work was done expressly for inclusion into FreeBSD.  Other use
 *    is permitted provided this notation is included.
 * 4. Absolutely no warranty of function or purpose is made by the authors.
 * 5. Modifications may be freely made to this file providing the above
 *    conditions are met.
 *
 * Modem chat module - send/expect style functions for getty
 * For semi-intelligent modem handling.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>

#include <ctype.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "gettytab.h"
#include "extern.h"

#define	PAUSE_CH		(unsigned char)'\xff'   /* pause kludge */

#define	CHATDEBUG_RECEIVE	0x01
#define	CHATDEBUG_SEND		0x02
#define	CHATDEBUG_EXPECT	0x04
#define	CHATDEBUG_MISC		0x08

#define	CHATDEBUG_DEFAULT	0
#define CHAT_DEFAULT_TIMEOUT	10


static int chat_debug = CHATDEBUG_DEFAULT;
static int chat_alarm = CHAT_DEFAULT_TIMEOUT; /* Default */

static volatile int alarmed = 0;


static void   chat_alrm(int);
static int    chat_unalarm(void);
static int    getdigit(unsigned char **, int, int);
static char   **read_chat(char **);
static char   *cleanchr(char **, unsigned char);
static const char *cleanstr(const unsigned char *, int);
static const char *result(int);
static int    chat_expect(const char *);
static int    chat_send(char const *);


/*
 * alarm signal handler
 * handle timeouts in read/write
 * change stdin to non-blocking mode to prevent
 * possible hang in read().
 */

static void
chat_alrm(int signo __unused)
{
	int on = 1;

	alarm(1);
	alarmed = 1;
	signal(SIGALRM, chat_alrm);
	ioctl(STDIN_FILENO, FIONBIO, &on);
}


/*
 * Turn back on blocking mode reset by chat_alrm()
 */

static int
chat_unalarm(void)
{
	int off = 0;
	return ioctl(STDIN_FILENO, FIONBIO, &off);
}


/*
 * convert a string of a given base (octal/hex) to binary
 */

static int
getdigit(unsigned char **ptr, int base, int max)
{
	int i, val = 0;
	char * q;

	static const char xdigits[] = "0123456789abcdef";

	for (i = 0, q = *ptr; i++ < max; ++q) {
		int sval;
		const char * s = strchr(xdigits, tolower(*q));

		if (s == NULL || (sval = s - xdigits) >= base)
			break;
		val = (val * base) + sval;
	}
	*ptr = q;
	return val;
}


/*
 * read_chat()
 * Convert a whitespace delimtied string into an array
 * of strings, being expect/send pairs
 */

static char **
read_chat(char **chatstr)
{
	char *str = *chatstr;
	char **res = NULL;

	if (str != NULL) {
		char *tmp = NULL;
		int l;

		if ((l=strlen(str)) > 0 && (tmp=malloc(l + 1)) != NULL &&
		    (res=malloc(((l + 1) / 2 + 1) * sizeof(char *))) != NULL) {
			static char ws[] = " \t";
			char * p;

			for (l = 0, p = strtok(strcpy(tmp, str), ws);
			     p != NULL;
			     p = strtok(NULL, ws))
			{
				unsigned char *q, *r;

				/* Read escapes */
				for (q = r = (unsigned char *)p; *r; ++q)
				{
					if (*q == '\\')
					{
						/* handle special escapes */
						switch (*++q)
						{
						case 'a': /* bell */
							*r++ = '\a';
							break;
						case 'r': /* cr */
							*r++ = '\r';
							break;
						case 'n': /* nl */
							*r++ = '\n';
							break;
						case 'f': /* ff */
							*r++ = '\f';
							break;
						case 'b': /* bs */
							*r++ = '\b';
							break;
						case 'e': /* esc */
							*r++ = 27;
							break;
						case 't': /* tab */
							*r++ = '\t';
							break;
						case 'p': /* pause */
							*r++ = PAUSE_CH;
							break;
						case 's':
						case 'S': /* space */
							*r++ = ' ';
							break;
						case 'x': /* hexdigit */
							++q;
							*r++ = getdigit(&q, 16, 2);
							--q;
							break;
						case '0': /* octal */
							++q;
							*r++ = getdigit(&q, 8, 3);
							--q;
							break;
						default: /* literal */
							*r++ = *q;
							break;
						case 0: /* not past eos */
							--q;
							break;
						}
					} else {
						/* copy standard character */
						*r++ = *q;
					}
				}

				/* Remove surrounding quotes, if any
				 */
				if (*p == '"' || *p == '\'') {
					q = strrchr(p+1, *p);
					if (q != NULL && *q == *p && q[1] == '\0') {
						*q = '\0';
						p++;
					}
				}

				res[l++] = p;
			}
			res[l] = NULL;
			*chatstr = tmp;
			return res;
		}
		free(tmp);
	}
	return res;
}


/*
 * clean a character for display (ctrl/meta character)
 */

static char *
cleanchr(char **buf, unsigned char ch)
{
	int l;
	static char tmpbuf[5];
	char * tmp = buf ? *buf : tmpbuf;

	if (ch & 0x80) {
		strcpy(tmp, "M-");
		l = 2;
		ch &= 0x7f;
	} else
	l = 0;

	if (ch < 32) {
		tmp[l++] = '^';
		tmp[l++] = ch + '@';
	} else if (ch == 127) {
		tmp[l++] = '^';
		tmp[l++] = '?';
	} else
		tmp[l++] = ch;
	tmp[l] = '\0';

	if (buf)
		*buf = tmp + l;
	return tmp;
}


/*
 * clean a string for display (ctrl/meta characters)
 */

static const char *
cleanstr(const unsigned char *s, int l)
{
	static unsigned char * tmp = NULL;
	static int tmplen = 0;

	if (tmplen < l * 4 + 1)
		tmp = realloc(tmp, tmplen = l * 4 + 1);

	if (tmp == NULL) {
		tmplen = 0;
		return "(mem alloc error)";
	} else {
		int i = 0;
		char * p = tmp;

		while (i < l)
			cleanchr(&p, s[i++]);
		*p = '\0';
	}

	return tmp;
}


/*
 * return result as a pseudo-english word
 */

static const char *
result(int r)
{
	static const char * results[] = {
		"OK", "MEMERROR", "IOERROR", "TIMEOUT"
	};
	return results[r & 3];
}


/*
 * chat_expect()
 * scan input for an expected string
 */

static int
chat_expect(const char *str)
{
	int len, r = 0;

	if (chat_debug & CHATDEBUG_EXPECT)
		syslog(LOG_DEBUG, "chat_expect '%s'", cleanstr(str, strlen(str)));

	if ((len = strlen(str)) > 0) {
		int i = 0;
		char * got;

		if ((got = malloc(len + 1)) == NULL)
			r = 1;
		else {

			memset(got, 0, len+1);
			alarm(chat_alarm);
			alarmed = 0;

			while (r == 0 && i < len) {
				if (alarmed)
					r = 3;
				else {
					unsigned char ch;

					if (read(STDIN_FILENO, &ch, 1) == 1) {

						if (chat_debug & CHATDEBUG_RECEIVE)
							syslog(LOG_DEBUG, "chat_recv '%s' m=%d",
								cleanchr(NULL, ch), i);

						if (ch == str[i])
							got[i++] = ch;
						else if (i > 0) {
							int j = 1;

							/* See if we can resync on a
							 * partial match in our buffer
							 */
							while (j < i && memcmp(got + j, str, i - j) != 0)
								j++;
							if (j < i)
								memcpy(got, got + j, i - j);
							i -= j;
						}
					} else
						r = alarmed ? 3 : 2;
				}
			}
			alarm(0);
        		chat_unalarm();
        		alarmed = 0;
        		free(got);
		}
	}

	if (chat_debug & CHATDEBUG_EXPECT)
		syslog(LOG_DEBUG, "chat_expect %s", result(r));

	return r;
}


/*
 * chat_send()
 * send a chat string
 */

static int
chat_send(char const *str)
{
	int r = 0;

	if (chat_debug & CHATDEBUG_SEND)
		syslog(LOG_DEBUG, "chat_send '%s'", cleanstr(str, strlen(str)));

	if (*str) {
                alarm(chat_alarm);
                alarmed = 0;
                while (r == 0 && *str)
                {
                        unsigned char ch = (unsigned char)*str++;

                        if (alarmed)
        			r = 3;
                        else if (ch == PAUSE_CH)
				usleep(500000); /* 1/2 second */
			else  {
				usleep(10000);	/* be kind to modem */
                                if (write(STDOUT_FILENO, &ch, 1) != 1)
        		  		r = alarmed ? 3 : 2;
                        }
                }
                alarm(0);
                chat_unalarm();
                alarmed = 0;
	}

        if (chat_debug & CHATDEBUG_SEND)
          syslog(LOG_DEBUG, "chat_send %s", result(r));

        return r;
}


/*
 * getty_chat()
 *
 * Termination codes:
 * -1 - no script supplied
 *  0 - script terminated correctly
 *  1 - invalid argument, expect string too large, etc.
 *  2 - error on an I/O operation or fatal error condition
 *  3 - timeout waiting for a simple string
 *
 * Parameters:
 *  char *scrstr     - unparsed chat script
 *  timeout          - seconds timeout
 *  debug            - debug value (bitmask)
 */

int
getty_chat(char *scrstr, int timeout, int debug)
{
        int r = -1;

        chat_alarm = timeout ? timeout : CHAT_DEFAULT_TIMEOUT;
        chat_debug = debug;

        if (scrstr != NULL) {
                char **script;

                if (chat_debug & CHATDEBUG_MISC)
			syslog(LOG_DEBUG, "getty_chat script='%s'", scrstr);

                if ((script = read_chat(&scrstr)) != NULL) {
                        int i = r = 0;
			int off = 0;
                        sig_t old_alarm;

                        /*
			 * We need to be in raw mode for all this
			 * Rely on caller...
                         */

                        old_alarm = signal(SIGALRM, chat_alrm);
                        chat_unalarm(); /* Force blocking mode at start */

			/*
			 * This is the send/expect loop
			 */
                        while (r == 0 && script[i] != NULL)
				if ((r = chat_expect(script[i++])) == 0 && script[i] != NULL)
					r = chat_send(script[i++]);

                        signal(SIGALRM, old_alarm);
                        free(script);
                        free(scrstr);

			/*
			 * Ensure stdin is in blocking mode
			 */
                        ioctl(STDIN_FILENO, FIONBIO, &off);
                }

                if (chat_debug & CHATDEBUG_MISC)
                  syslog(LOG_DEBUG, "getty_chat %s", result(r));

        }
        return r;
}
