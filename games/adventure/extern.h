/*	$OpenBSD: extern.h,v 1.12 2023/01/04 13:00:11 jsg Exp $	*/
/*	$NetBSD: extern.h,v 1.3 1997/10/11 01:55:27 lukem Exp $	*/

/*
 * Copyright (c) 1997 Christos Zoulas.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#include <string.h>

/* crc.c */
void crc_start(void);
unsigned long crc(const char *, int);

/* done.c */
int score(void);
void done(int);
void die(int);

/* init.c */
void init(void);
char   *decr(char, char, char, char, char);
void linkdata(void);
void trapdel(int);
void startup(void);

/* io.c */
void getin(char *, size_t, char *, size_t);
int yes(int, int, int);
int yesm(int, int, int);
int next(void);
void rdata(void);
int rnum(void);
void rdesc(int);
void rtrav(void);
#ifdef DEBUG
void twrite(int);
#endif
void rvoc(void);
void rlocs(void);
void rdflt(void);
void rliq(void);
void rhints(void);
void rspeak(int);
void mspeak(int);
struct text;
void speak(const struct text *);
void pspeak(int, int);

/* save.c */
int save(const char *);
int restore(const char *);

/* subr.c */
int toting(int);
int here(int);
int at(int);
int liq2(int);
int liq(void);
int liqloc(int);
int bitset(int, int);
int forced(int);
int dark(void);
int pct(int);
int fdwarf(void);
int march(void);
int mback(void);
int specials(void);
int trbridge(void);
void badmove(void);
__dead void bug(int);
void checkhints(void);
int trsay(void);
int trtake(void);
int dropper(void);
int trdrop(void);
int tropen(void);
int trkill(void);
int trtoss(void);
int trfeed(void);
int trfill(void);
void closing(void);
void caveclose(void);

/* vocab.c */
void dstroy(int);
void juggle(int);
void move(int, int);
int put(int, int, int);
void carry(int, int);
void drop(int, int);
int vocab(const char *, int, int);
void prht(void);

/* These three used to be functions in vocab.c */
#define weq(str1, str2)		(!strncmp((str1), (str2), 5))
#define length(str)			(strlen((str)) + 1)

/* wizard.c */
void poof(void);
int Start(void);
int wizard(void);
void ciao(void);
int ran(int);
