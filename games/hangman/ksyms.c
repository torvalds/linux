/*	$OpenBSD: ksyms.c,v 1.13 2021/12/24 23:01:56 tb Exp $	*/

/*
 * Copyright (c) 2008 Miodrag Vallat.
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

#include <ctype.h>
#include <curses.h>
#include <elf.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hangman.h"

static int ksyms_elf_parse(void);

void
sym_getword(void)
{
	uint tries;
	off_t pos;
	int buflen;
	char symbuf[1 + BUFSIZ], *sym, *end;
	size_t symlen;

	for (tries = 0; tries < MAXBADWORDS; tries++) {
		pos = arc4random_uniform(symsize);
		if (lseek(symfd, pos + symoffs, SEEK_SET) == -1)
			continue;
		buflen = read(symfd, symbuf, BUFSIZ);
		if (buflen == -1)
			continue;

		/*
		 * The buffer is hopefully large enough to hold at least
		 * a complete symbol, i.e. two occurences of NUL, or
		 * one occurence of NUL and the buffer containing the end
		 * of the string table. We make sure the buffer will be
		 * NUL terminated in all cases.
		 */
		if (buflen + pos >= symsize)
			buflen = symsize - pos;
		*(end = symbuf + buflen) = '\0';

		for (sym = symbuf; *sym != '\0'; sym++)
			;
		if (sym == end)
			continue;

		symlen = strlen(++sym);
		if (symlen < MINLEN || symlen > MAXLEN)
			continue;

		/* ignore symbols containing dots or dollar signs */
		if (strchr(sym, '.') != NULL || strchr(sym, '$') != NULL)
			continue;
		/* ignore all double unberbar symbols */
		if (strncmp(sym, "__", 2) == 0)
			continue;

		break;
	}

	if (tries >= MAXBADWORDS) {
		mvcur(0, COLS - 1, LINES -1, 0);
		endwin();
		errx(1, "can't seem a suitable symbol in %s",
		    Dict_name);
	}

	strlcpy(Word, sym, sizeof Word);
	strlcpy(Known, sym, sizeof Known);
	for (sym = Known; *sym != '\0'; sym++) {
		if (*sym == '-')
			*sym = '_';	/* try not to confuse player */
		if (isalnum((unsigned char)*sym))
			*sym = '-';
	}
}

int
sym_setup(void)
{
	if ((symfd = open(Dict_name, O_RDONLY)) == -1)
		return -1;

	if (ksyms_elf_parse() == 0)
		return 0;

	close(symfd);
	errno = ENOEXEC;
	return -1;
}

int
ksyms_elf_parse(void)
{
	Elf_Ehdr eh;
	Elf_Shdr sh;
	uint s;

	if (lseek(symfd, 0, SEEK_SET) == -1)
		return -1;

	if (read(symfd, &eh, sizeof eh) != sizeof eh)
		return -1;

	if (!IS_ELF(eh))
		return -1;

	if (lseek(symfd, eh.e_shoff, SEEK_SET) == -1)
		return -1;

	symoffs = 0;
	symsize = 0;

	for (s = 0; s < eh.e_shnum; s++) {
		if (read(symfd, &sh, sizeof sh) != sizeof sh)
			return -1;

		/*
		 * There should be two string table sections, one with the
		 * name of the sections themselves, and one with the symbol
		 * names. Just pick the largest one.
		 */
		if (sh.sh_type == SHT_STRTAB) {
			if (symsize > (off_t)sh.sh_size)
				continue;

			symoffs = (off_t)sh.sh_offset;
			symsize = (off_t)sh.sh_size;
		}
	}

	if (symsize == 0)
		return -1;

	return 0;
}
