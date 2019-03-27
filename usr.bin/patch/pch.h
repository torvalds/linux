/*-
 * Copyright 1986, Larry Wall
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following condition is met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this condition and the following disclaimer.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * patch - a program to apply diffs to original files
 *
 * -C option added in 1998, original code by Marc Espie, based on FreeBSD
 * behaviour
 *
 * $OpenBSD: pch.h,v 1.9 2003/10/31 20:20:45 millert Exp $
 * $FreeBSD$
 */

#define	OLD_FILE	0
#define	NEW_FILE	1
#define	INDEX_FILE	2
#define	MAX_FILE	3

struct file_name {
	char *path;
	bool exists;
};

void		re_patch(void);
void		open_patch_file(const char *);
void		set_hunkmax(void);
bool		there_is_another_patch(void);
bool		another_hunk(void);
bool		pch_swap(void);
char		*pfetch(LINENUM);
unsigned short	pch_line_len(LINENUM);
LINENUM		pch_first(void);
LINENUM		pch_ptrn_lines(void);
LINENUM		pch_newfirst(void);
LINENUM		pch_repl_lines(void);
LINENUM		pch_end(void);
LINENUM		pch_context(void);
LINENUM		pch_hunk_beg(void);
char		pch_char(LINENUM);
void		do_ed_script(void);
