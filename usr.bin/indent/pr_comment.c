/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1985 Sun Microsystems, Inc.
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)pr_comment.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "indent_globs.h"
#include "indent_codes.h"
#include "indent.h"
/*
 * NAME:
 *	pr_comment
 *
 * FUNCTION:
 *	This routine takes care of scanning and printing comments.
 *
 * ALGORITHM:
 *	1) Decide where the comment should be aligned, and if lines should
 *	   be broken.
 *	2) If lines should not be broken and filled, just copy up to end of
 *	   comment.
 *	3) If lines should be filled, then scan thru input_buffer copying
 *	   characters to com_buf.  Remember where the last blank, tab, or
 *	   newline was.  When line is filled, print up to last blank and
 *	   continue copying.
 *
 * HISTORY:
 *	November 1976	D A Willcox of CAC	Initial coding
 *	12/6/76		D A Willcox of CAC	Modification to handle
 *						UNIX-style comments
 *
 */

/*
 * this routine processes comments.  It makes an attempt to keep comments from
 * going over the max line length.  If a line is too long, it moves everything
 * from the last blank to the next comment line.  Blanks and tabs from the
 * beginning of the input line are removed
 */

void
pr_comment(void)
{
    int         now_col;	/* column we are in now */
    int         adj_max_col;	/* Adjusted max_col for when we decide to
				 * spill comments over the right margin */
    char       *last_bl;	/* points to the last blank in the output
				 * buffer */
    char       *t_ptr;		/* used for moving string */
    int         break_delim = opt.comment_delimiter_on_blankline;
    int         l_just_saw_decl = ps.just_saw_decl;

    adj_max_col = opt.max_col;
    ps.just_saw_decl = 0;
    last_bl = NULL;		/* no blanks found so far */
    ps.box_com = false;		/* at first, assume that we are not in
					 * a boxed comment or some other
					 * comment that should not be touched */
    ++ps.out_coms;		/* keep track of number of comments */

    /* Figure where to align and how to treat the comment */

    if (ps.col_1 && !opt.format_col1_comments) {	/* if comment starts in column
						 * 1 it should not be touched */
	ps.box_com = true;
	break_delim = false;
	ps.com_col = 1;
    }
    else {
	if (*buf_ptr == '-' || *buf_ptr == '*' ||
	    (*buf_ptr == '\n' && !opt.format_block_comments)) {
	    ps.box_com = true;	/* A comment with a '-' or '*' immediately
				 * after the /+* is assumed to be a boxed
				 * comment. A comment with a newline
				 * immediately after the /+* is assumed to
				 * be a block comment and is treated as a
				 * box comment unless format_block_comments
				 * is nonzero (the default). */
	    break_delim = false;
	}
	if ( /* ps.bl_line && */ (s_lab == e_lab) && (s_code == e_code)) {
	    /* klg: check only if this line is blank */
	    /*
	     * If this (*and previous lines are*) blank, dont put comment way
	     * out at left
	     */
	    ps.com_col = (ps.ind_level - opt.unindent_displace) * opt.ind_size + 1;
	    adj_max_col = opt.block_comment_max_col;
	    if (ps.com_col <= 1)
		ps.com_col = 1 + !opt.format_col1_comments;
	}
	else {
	    int target_col;
	    break_delim = false;
	    if (s_code != e_code)
		target_col = count_spaces(compute_code_target(), s_code);
	    else {
		target_col = 1;
		if (s_lab != e_lab)
		    target_col = count_spaces(compute_label_target(), s_lab);
	    }
	    ps.com_col = ps.decl_on_line || ps.ind_level == 0 ? opt.decl_com_ind : opt.com_ind;
	    if (ps.com_col <= target_col)
		ps.com_col = opt.tabsize * (1 + (target_col - 1) / opt.tabsize) + 1;
	    if (ps.com_col + 24 > adj_max_col)
		adj_max_col = ps.com_col + 24;
	}
    }
    if (ps.box_com) {
	/*
	 * Find out how much indentation there was originally, because that
	 * much will have to be ignored by pad_output() in dump_line(). This
	 * is a box comment, so nothing changes -- not even indentation.
	 *
	 * The comment we're about to read usually comes from in_buffer,
	 * unless it has been copied into save_com.
	 */
	char *start;

	start = buf_ptr >= save_com && buf_ptr < save_com + sc_size ?
	    sc_buf : in_buffer;
	ps.n_comment_delta = 1 - count_spaces_until(1, start, buf_ptr - 2);
    }
    else {
	ps.n_comment_delta = 0;
	while (*buf_ptr == ' ' || *buf_ptr == '\t')
	    buf_ptr++;
    }
    ps.comment_delta = 0;
    *e_com++ = '/';		/* put '/' followed by '*' into buffer */
    *e_com++ = '*';
    if (*buf_ptr != ' ' && !ps.box_com)
	*e_com++ = ' ';

    /*
     * Don't put a break delimiter if this is a one-liner that won't wrap.
     */
    if (break_delim)
	for (t_ptr = buf_ptr; *t_ptr != '\0' && *t_ptr != '\n'; t_ptr++) {
	    if (t_ptr >= buf_end)
		fill_buffer();
	    if (t_ptr[0] == '*' && t_ptr[1] == '/') {
		if (adj_max_col >= count_spaces_until(ps.com_col, buf_ptr, t_ptr + 2))
		    break_delim = false;
		break;
	    }
	}

    if (break_delim) {
	char       *t = e_com;
	e_com = s_com + 2;
	*e_com = 0;
	if (opt.blanklines_before_blockcomments && ps.last_token != lbrace)
	    prefix_blankline_requested = 1;
	dump_line();
	e_com = s_com = t;
	if (!ps.box_com && opt.star_comment_cont)
	    *e_com++ = ' ', *e_com++ = '*', *e_com++ = ' ';
    }

    /* Start to copy the comment */

    while (1) {			/* this loop will go until the comment is
				 * copied */
	switch (*buf_ptr) {	/* this checks for various spcl cases */
	case 014:		/* check for a form feed */
	    CHECK_SIZE_COM(3);
	    if (!ps.box_com) {	/* in a text comment, break the line here */
		ps.use_ff = true;
		/* fix so dump_line uses a form feed */
		dump_line();
		last_bl = NULL;
		if (!ps.box_com && opt.star_comment_cont)
		    *e_com++ = ' ', *e_com++ = '*', *e_com++ = ' ';
		while (*++buf_ptr == ' ' || *buf_ptr == '\t')
		    ;
	    }
	    else {
		if (++buf_ptr >= buf_end)
		    fill_buffer();
		*e_com++ = 014;
	    }
	    break;

	case '\n':
	    if (had_eof) {	/* check for unexpected eof */
		printf("Unterminated comment\n");
		dump_line();
		return;
	    }
	    last_bl = NULL;
	    CHECK_SIZE_COM(4);
	    if (ps.box_com || ps.last_nl) {	/* if this is a boxed comment,
						 * we dont ignore the newline */
		if (s_com == e_com)
		    *e_com++ = ' ';
		if (!ps.box_com && e_com - s_com > 3) {
		    dump_line();
		    if (opt.star_comment_cont)
			*e_com++ = ' ', *e_com++ = '*', *e_com++ = ' ';
		}
		dump_line();
		if (!ps.box_com && opt.star_comment_cont)
		    *e_com++ = ' ', *e_com++ = '*', *e_com++ = ' ';
	    }
	    else {
		ps.last_nl = 1;
		if (*(e_com - 1) == ' ' || *(e_com - 1) == '\t')
		    last_bl = e_com - 1;
		/*
		 * if there was a space at the end of the last line, remember
		 * where it was
		 */
		else {		/* otherwise, insert one */
		    last_bl = e_com;
		    *e_com++ = ' ';
		}
	    }
	    ++line_no;		/* keep track of input line number */
	    if (!ps.box_com) {
		int         nstar = 1;
		do {		/* flush any blanks and/or tabs at start of
				 * next line */
		    if (++buf_ptr >= buf_end)
			fill_buffer();
		    if (*buf_ptr == '*' && --nstar >= 0) {
			if (++buf_ptr >= buf_end)
			    fill_buffer();
			if (*buf_ptr == '/')
			    goto end_of_comment;
		    }
		} while (*buf_ptr == ' ' || *buf_ptr == '\t');
	    }
	    else if (++buf_ptr >= buf_end)
		fill_buffer();
	    break;		/* end of case for newline */

	case '*':		/* must check for possibility of being at end
				 * of comment */
	    if (++buf_ptr >= buf_end)	/* get to next char after * */
		fill_buffer();
	    CHECK_SIZE_COM(4);
	    if (*buf_ptr == '/') {	/* it is the end!!! */
	end_of_comment:
		if (++buf_ptr >= buf_end)
		    fill_buffer();
		if (break_delim) {
		    if (e_com > s_com + 3) {
			dump_line();
		    }
		    else
			s_com = e_com;
		    *e_com++ = ' ';
		}
		if (e_com[-1] != ' ' && e_com[-1] != '\t' && !ps.box_com)
		    *e_com++ = ' ';	/* ensure blank before end */
		*e_com++ = '*', *e_com++ = '/', *e_com = '\0';
		ps.just_saw_decl = l_just_saw_decl;
		return;
	    }
	    else		/* handle isolated '*' */
		*e_com++ = '*';
	    break;
	default:		/* we have a random char */
	    now_col = count_spaces_until(ps.com_col, s_com, e_com);
	    do {
		CHECK_SIZE_COM(1);
		*e_com = *buf_ptr++;
		if (buf_ptr >= buf_end)
		    fill_buffer();
		if (*e_com == ' ' || *e_com == '\t')
		    last_bl = e_com;	/* remember we saw a blank */
		++e_com;
		now_col++;
	    } while (!memchr("*\n\r\b\t", *buf_ptr, 6) &&
		(now_col <= adj_max_col || !last_bl));
	    ps.last_nl = false;
	    if (now_col > adj_max_col && !ps.box_com && e_com[-1] > ' ') {
		/*
		 * the comment is too long, it must be broken up
		 */
		if (last_bl == NULL) {
		    dump_line();
		    if (!ps.box_com && opt.star_comment_cont)
			*e_com++ = ' ', *e_com++ = '*', *e_com++ = ' ';
		    break;
		}
		*e_com = '\0';
		e_com = last_bl;
		dump_line();
		if (!ps.box_com && opt.star_comment_cont)
		    *e_com++ = ' ', *e_com++ = '*', *e_com++ = ' ';
		for (t_ptr = last_bl + 1; *t_ptr == ' ' || *t_ptr == '\t';
		    t_ptr++)
			;
		last_bl = NULL;
		/*
		 * t_ptr will be somewhere between e_com (dump_line() reset)
		 * and l_com. So it's safe to copy byte by byte from t_ptr
		 * to e_com without any CHECK_SIZE_COM().
		 */
		while (*t_ptr != '\0') {
		    if (*t_ptr == ' ' || *t_ptr == '\t')
			last_bl = e_com;
		    *e_com++ = *t_ptr++;
		}
	    }
	    break;
	}
    }
}
