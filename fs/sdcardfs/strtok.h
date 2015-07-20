/*
 * fs/sdcardfs/strtok.h
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd
 *   Authors: Daeho Jeong, Woojoong Lee, Seunghwan Hyun,
 *               Sunghwan Yun, Sungjong Seo
 *
 * This program has been developed as a stackable file system based on
 * the WrapFS which written by
 *
 * Copyright (c) 1998-2011 Erez Zadok
 * Copyright (c) 2009     Shrikar Archak
 * Copyright (c) 2003-2011 Stony Brook University
 * Copyright (c) 2003-2011 The Research Foundation of SUNY
 *
 * This file is dual licensed.  It may be redistributed and/or modified
 * under the terms of the Apache 2.0 License OR version 2 of the GNU
 * General Public License.
 */

static char *
strtok_r(char *s, const char *delim, char **last)
{
        char *spanp;
        int c, sc;
        char *tok;


        /* if (s == NULL && (s = *last) == NULL)
                return NULL;     */
        if (s == NULL) {
                s = *last;
                if (s == NULL)
                        return NULL;
        }

        /*
         * Skip (span) leading delimiters (s += strspn(s, delim), sort of).
         */
cont:
        c = *s++;
        for (spanp = (char *)delim; (sc = *spanp++) != 0;) {
                if (c == sc)
                        goto cont;
        }

        if (c == 0) {           /* no non-delimiter characters */
                *last = NULL;
                return NULL;
        }
        tok = s - 1;

        /*
         * Scan token (scan for delimiters: s += strcspn(s, delim), sort of).
         * Note that delim must have one NUL; we stop if we see that, too.
         */
        for (;;) {
                c = *s++;
                spanp = (char *)delim;
                do {
                        sc = *spanp++;
                        if (sc == c) {
                                if (c == 0)
                                        s = NULL;
                                else
                                        s[-1] = 0;
                                *last = s;
                                return tok;
                        }
                } while (sc != 0);
        }

        /* NOTREACHED */
}

