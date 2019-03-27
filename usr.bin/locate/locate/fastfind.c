/*
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995 Wolfram Schneider <wosch@FreeBSD.org>. Berlin.
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * James A. Woods.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *
 * $FreeBSD$
 */


#ifndef _LOCATE_STATISTIC_
#define _LOCATE_STATISTIC_

void 
statistic (fp, path_fcodes)
	FILE *fp;               /* open database */
	char *path_fcodes;  	/* for error message */
{
	register int lines, chars, size, big, zwerg;
	register u_char *p, *s;
	register int c;
	int count, umlaut;
	u_char bigram1[NBG], bigram2[NBG], path[MAXPATHLEN];

	for (c = 0, p = bigram1, s = bigram2; c < NBG; c++) {
		p[c] = check_bigram_char(getc(fp));
		s[c] = check_bigram_char(getc(fp));
	}

	lines = chars = big = zwerg = umlaut = 0;
	size = NBG + NBG;

	for (c = getc(fp), count = 0; c != EOF; size++) {
		if (c == SWITCH) {
			count += getwf(fp) - OFFSET;
			size += sizeof(int);
			zwerg++;
		} else
			count += c - OFFSET;
		
		for (p = path + count; (c = getc(fp)) > SWITCH; size++)
			if (c < PARITY) {
				if (c == UMLAUT) {
					c = getc(fp);
					size++;
					umlaut++;
				}
				p++;
			} else {
				/* bigram char */
				big++;
				p += 2;
			}

		p++;
		lines++;
		chars += (p - path);
	}

	(void)printf("\nDatabase: %s\n", path_fcodes);
	(void)printf("Compression: Front: %2.2f%%, ",
		     (size + big - (2 * NBG)) / (chars / (float)100));
	(void)printf("Bigram: %2.2f%%, ", (size - big) / (size / (float)100));
	(void)printf("Total: %2.2f%%\n", 
		     (size - (2 * NBG)) / (chars / (float)100));
	(void)printf("Filenames: %d, ", lines);
	(void)printf("Characters: %d, ", chars);
	(void)printf("Database size: %d\n", size);
	(void)printf("Bigram characters: %d, ", big);
	(void)printf("Integers: %d, ", zwerg);
	(void)printf("8-Bit characters: %d\n", umlaut);

}
#endif /* _LOCATE_STATISTIC_ */

extern	char	separator;

void
#ifdef FF_MMAP


#ifdef FF_ICASE
fastfind_mmap_icase
#else
fastfind_mmap
#endif /* FF_ICASE */
(pathpart, paddr, len, database)
	char *pathpart; 	/* search string */
	caddr_t paddr;  	/* mmap pointer */
	int len;        	/* length of database */
	char *database; 	/* for error message */


#else /* MMAP */


#ifdef FF_ICASE
fastfind_icase
#else
fastfind
#endif /* FF_ICASE */

(fp, pathpart, database)
	FILE *fp;               /* open database */
	char *pathpart;		/* search string */
	char *database;		/* for error message */


#endif /* MMAP */

{
	register u_char *p, *s, *patend, *q, *foundchar;
	register int c, cc;
	int count, found, globflag;
	u_char *cutoff;
	u_char bigram1[NBG], bigram2[NBG], path[MAXPATHLEN];

#ifdef FF_ICASE
	/* use a lookup table for case insensitive search */
	u_char table[UCHAR_MAX + 1];

	tolower_word(pathpart);
#endif /* FF_ICASE*/

	/* init bigram table */
#ifdef FF_MMAP
	for (c = 0, p = bigram1, s = bigram2; c < NBG; c++, len-= 2) {
		p[c] = check_bigram_char(*paddr++);
		s[c] = check_bigram_char(*paddr++);
	}
#else
	for (c = 0, p = bigram1, s = bigram2; c < NBG; c++) {
		p[c] = check_bigram_char(getc(fp));
		s[c] = check_bigram_char(getc(fp));
	}
#endif /* FF_MMAP */

	/* find optimal (last) char for searching */
	for (p = pathpart; *p != '\0'; p++)
		if (strchr(LOCATE_REG, *p) != NULL)
			break;

	if (*p == '\0')
		globflag = 0;
	else
		globflag = 1;

	p = pathpart;
	patend = patprep(p);
	cc = *patend;

#ifdef FF_ICASE
	/* set patend char to true */
        for (c = 0; c < UCHAR_MAX + 1; c++)
                table[c] = 0;

	table[TOLOWER(*patend)] = 1;
	table[toupper(*patend)] = 1;
#endif /* FF_ICASE */


	/* main loop */
	found = count = 0;
	foundchar = 0;

#ifdef FF_MMAP
	c = (u_char)*paddr++; len--;
	for (; len > 0; ) {
#else
	c = getc(fp);
	for (; c != EOF; ) {
#endif /* FF_MMAP */

		/* go forward or backward */
		if (c == SWITCH) { /* big step, an integer */
#ifdef FF_MMAP
			count += getwm(paddr) - OFFSET;
			len -= INTSIZE; paddr += INTSIZE;
#else
			count +=  getwf(fp) - OFFSET;
#endif /* FF_MMAP */
		} else {	   /* slow step, =< 14 chars */
			count += c - OFFSET;
		}

		if (count < 0 || count > MAXPATHLEN)
			errx(1, "corrupted database: %s", database);
		/* overlay old path */
		p = path + count;
		foundchar = p - 1;

#ifdef FF_MMAP
		for (; len > 0;) {
			c = (u_char)*paddr++; 
		        len--;
#else
		for (;;) {
			c = getc(fp);
#endif /* FF_MMAP */
			/*
			 * == UMLAUT: 8 bit char followed
			 * <= SWITCH: offset
			 * >= PARITY: bigram
			 * rest:      single ascii char
			 *
			 * offset < SWITCH < UMLAUT < ascii < PARITY < bigram
			 */
			if (c < PARITY) {
				if (c <= UMLAUT) {
					if (c == UMLAUT) {
#ifdef FF_MMAP
						c = (u_char)*paddr++;
						len--;
#else
						c = getc(fp);
#endif /* FF_MMAP */
						
					} else
						break; /* SWITCH */
				}
#ifdef FF_ICASE
				if (table[c])
#else
				if (c == cc)
#endif /* FF_ICASE */
					foundchar = p;
				*p++ = c;
			}
			else {		
				/* bigrams are parity-marked */
				TO7BIT(c);

#ifndef FF_ICASE
				if (bigram1[c] == cc ||
				    bigram2[c] == cc)
#else

					if (table[bigram1[c]] ||
					    table[bigram2[c]])
#endif /* FF_ICASE */
						foundchar = p + 1;

				*p++ = bigram1[c];
				*p++ = bigram2[c];
			}
		}
		
		if (found) {                     /* previous line matched */
			cutoff = path;
			*p-- = '\0';
			foundchar = p;
		} else if (foundchar >= path + count) { /* a char matched */
			*p-- = '\0';
			cutoff = path + count;
		} else                           /* nothing to do */
			continue;

		found = 0;
		for (s = foundchar; s >= cutoff; s--) {
			if (*s == cc
#ifdef FF_ICASE
			    || TOLOWER(*s) == cc
#endif /* FF_ICASE */
			    ) {	/* fast first char check */
				for (p = patend - 1, q = s - 1; *p != '\0';
				     p--, q--)
					if (*q != *p
#ifdef FF_ICASE
					    && TOLOWER(*q) != *p
#endif /* FF_ICASE */
					    )
						break;
				if (*p == '\0') {   /* fast match success */
					found = 1;
					if (!globflag || 
#ifndef FF_ICASE
					    !fnmatch(pathpart, path, 0)) 
#else 
					    !fnmatch(pathpart, path, 
						     FNM_CASEFOLD))
#endif /* !FF_ICASE */						
					{
						if (f_silent)
							counter++;
						else if (f_limit) {
							counter++;
							if (f_limit >= counter)
								(void)printf("%s%c",path,separator);
							else 
								errx(0, "[show only %d lines]", counter - 1);
						} else
							(void)printf("%s%c",path,separator);
					}
					break;
				}
			}
		}
	}
}
