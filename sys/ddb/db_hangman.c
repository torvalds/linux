/*	$OpenBSD: db_hangman.c,v 1.39 2024/11/07 16:02:29 miod Exp $	*/

/*
 * Copyright (c) 1996 Theo de Raadt, Michael Shalayeff
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
 *
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>

#include <ddb/db_sym.h>
#include <ddb/db_output.h>

#include <dev/cons.h>

#define ABC_ISCLR(c)	sabc->abc[(c)-'a']==0
#define ABC_ISWRONG(c)	sabc->abc[(c)-'a']=='_'
#define ABC_SETWRONG(c)		(sabc->abc[(c)-'a']='_')
#define ABC_SETRIGHT(c)		(sabc->abc[(c)-'a']='+')
#define ABC_CLR()	memset(sabc->abc,0,sizeof sabc->abc)
struct _abc {
	char	abc[26+2];	/* for int32 alignment */
};

#define	TOLOWER(c)	((c)|0x20)
#define	ISLOWALPHA(c)	('a'<=(c) && (c)<='z')
#define	ISALPHA(c)	ISLOWALPHA(TOLOWER(c))

void	 db_hang(int, const char *, struct _abc *);

u_long		db_plays, db_guesses;

static const char hangpic[]=
	"\n88888\r\n"
	"9 7 6\r\n"
	"97  5\r\n"
	"9  423\r\n"
	"9   2\r\n"
	"9  1 0\r\n"
	"9\r\n"
	"9  ";
static const char substchar[]="\\/|\\/O|/-|";

struct db_hang_forall_arg {
	int cnt;
	Elf_Sym *sym;
};

/*
 * Horrible abuse of the forall function, but we're not in a hurry.
 */
static void db_hang_forall(Elf_Sym *, const char *, const char *, void *);

static void
db_hang_forall(Elf_Sym *sym, const char *name, const char *suff, void *varg)
{
	struct db_hang_forall_arg *arg = varg;

	if (arg->cnt-- == 0)
		arg->sym = sym;
}

static __inline const char *
db_randomsym(size_t *lenp)
{
	int nsyms;
	const char	*p, *q;
	struct db_hang_forall_arg dfa;

	dfa.cnt = 0;
	db_elf_sym_forall(db_hang_forall, &dfa);
	nsyms = -dfa.cnt;

	if (nsyms == 0)
		return (NULL);

	dfa.cnt = arc4random_uniform(nsyms);
	db_elf_sym_forall(db_hang_forall, &dfa);

	db_symbol_values(dfa.sym, &q, 0);

	/* strlen(q) && ignoring underscores and colons */
	for ((*lenp) = 0, p = q; *p; p++)
		if (ISALPHA(*p))
			(*lenp)++;

	return (q);
}

void
db_hang(int tries, const char *word, struct _abc *sabc)
{
	const char	*p;
	int i;
	int c;
#ifdef ABC_BITMASK
	int m;
#endif

	for (p = hangpic; *p; p++)
		cnputc((*p >= '0' && *p <= '9') ? ((tries <= (*p) - '0') ?
		    substchar[(*p) - '0'] : ' ') : *p);

	for (p = word; *p; p++) {
		c = TOLOWER(*p);
		cnputc(ISLOWALPHA(c) && ABC_ISCLR(c) ? '-' : *p);
	}

#ifdef ABC_WRONGSTR
	db_printf(" (%s)\r", ABC_WRONGSTR);
#else
	db_printf(" (");

#ifdef ABC_BITMASK
	m = sabc->wrong;
	for (i = 'a'; i <= 'z'; ++i, m >>= 1)
		if (m&1)
			cnputc(i);
#else
	for (i = 'a'; i <= 'z'; ++i)
		if (ABC_ISWRONG(i))
			cnputc(i);
#endif

	db_printf(")\r");
#endif
}

void
db_hangman(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	const char *word;
	size_t	tries;
	size_t	len;
	struct _abc sabc[1];
	int	skill, c;

	if (modif[0] != 's' || (skill = modif[1] - '0') > 9U)
		skill = 3;
	word = NULL;
	tries = 0;
	for (;;) {

		if (word == NULL) {
			ABC_CLR();

			tries = skill + 1;
			word = db_randomsym(&len);
			if (word == NULL)
				break;

			db_plays++;
		}
		db_hang(tries, word, sabc);
		c = cngetc();
		c = TOLOWER(c);

		if (ISLOWALPHA(c) && ABC_ISCLR(c)) {
			const char *p;
			size_t	n;

			/* strchr(word,c) */
			for (n = 0, p = word; *p ; p++)
				if (TOLOWER(*p) == c)
					n++;

			if (n) {
				ABC_SETRIGHT(c);
				len -= n;
			} else {
				ABC_SETWRONG(c);
				tries--;
			}
		}

		if (tries && len)
			continue;

		if (!tries && skill > 2) {
			const char *p = word;
			for (; *p; p++)
				if (ISALPHA(*p))
					ABC_SETRIGHT(TOLOWER(*p));
		}
		if (tries)
			db_guesses++;
		db_hang(tries, word, sabc);
		db_printf("\nScore: %lu/%lu\n", db_plays, db_guesses);
		word = NULL;
		if (tries)
			break;
	}
}
