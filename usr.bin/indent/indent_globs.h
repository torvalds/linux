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
 *
 *	@(#)indent_globs.h	8.1 (Berkeley) 6/6/93
 * $FreeBSD$
 */

#define BACKSLASH '\\'
#define bufsize 200		/* size of internal buffers */
#define sc_size 5000		/* size of save_com buffer */
#define label_offset 2		/* number of levels a label is placed to left
				 * of code */


#define false 0
#define true  1


FILE       *input;		/* the fid for the input file */
FILE       *output;		/* the output file */

#define CHECK_SIZE_CODE(desired_size) \
	if (e_code + (desired_size) >= l_code) { \
	    int nsize = l_code-s_code + 400 + desired_size; \
	    int code_len = e_code-s_code; \
	    codebuf = (char *) realloc(codebuf, nsize); \
	    if (codebuf == NULL) \
		err(1, NULL); \
	    e_code = codebuf + code_len + 1; \
	    l_code = codebuf + nsize - 5; \
	    s_code = codebuf + 1; \
	}
#define CHECK_SIZE_COM(desired_size) \
	if (e_com + (desired_size) >= l_com) { \
	    int nsize = l_com-s_com + 400 + desired_size; \
	    int com_len = e_com - s_com; \
	    int blank_pos; \
	    if (last_bl != NULL) \
		blank_pos = last_bl - combuf; \
	    else \
		blank_pos = -1; \
	    combuf = (char *) realloc(combuf, nsize); \
	    if (combuf == NULL) \
		err(1, NULL); \
	    e_com = combuf + com_len + 1; \
	    if (blank_pos > 0) \
		last_bl = combuf + blank_pos; \
	    l_com = combuf + nsize - 5; \
	    s_com = combuf + 1; \
	}
#define CHECK_SIZE_LAB(desired_size) \
	if (e_lab + (desired_size) >= l_lab) { \
	    int nsize = l_lab-s_lab + 400 + desired_size; \
	    int label_len = e_lab - s_lab; \
	    labbuf = (char *) realloc(labbuf, nsize); \
	    if (labbuf == NULL) \
		err(1, NULL); \
	    e_lab = labbuf + label_len + 1; \
	    l_lab = labbuf + nsize - 5; \
	    s_lab = labbuf + 1; \
	}
#define CHECK_SIZE_TOKEN(desired_size) \
	if (e_token + (desired_size) >= l_token) { \
	    int nsize = l_token-s_token + 400 + desired_size; \
	    int token_len = e_token - s_token; \
	    tokenbuf = (char *) realloc(tokenbuf, nsize); \
	    if (tokenbuf == NULL) \
		err(1, NULL); \
	    e_token = tokenbuf + token_len + 1; \
	    l_token = tokenbuf + nsize - 5; \
	    s_token = tokenbuf + 1; \
	}

char       *labbuf;		/* buffer for label */
char       *s_lab;		/* start ... */
char       *e_lab;		/* .. and end of stored label */
char       *l_lab;		/* limit of label buffer */

char       *codebuf;		/* buffer for code section */
char       *s_code;		/* start ... */
char       *e_code;		/* .. and end of stored code */
char       *l_code;		/* limit of code section */

char       *combuf;		/* buffer for comments */
char       *s_com;		/* start ... */
char       *e_com;		/* ... and end of stored comments */
char       *l_com;		/* limit of comment buffer */

#define token s_token
char       *tokenbuf;		/* the last token scanned */
char	   *s_token;
char       *e_token;
char	   *l_token;

char       *in_buffer;		/* input buffer */
char	   *in_buffer_limit;	/* the end of the input buffer */
char       *buf_ptr;		/* ptr to next character to be taken from
				 * in_buffer */
char       *buf_end;		/* ptr to first after last char in in_buffer */

char        sc_buf[sc_size];	/* input text is saved here when looking for
				 * the brace after an if, while, etc */
char       *save_com;		/* start of the comment stored in sc_buf */
char       *sc_end;		/* pointer into save_com buffer */

char       *bp_save;		/* saved value of buf_ptr when taking input
				 * from save_com */
char       *be_save;		/* similarly saved value of buf_end */


struct options {
    int         blanklines_around_conditional_compilation;
    int         blanklines_after_declarations_at_proctop; /* this is vaguely
				 * similar to blanklines_after_decla except
				 * that in only applies to the first set of
				 * declarations in a procedure (just after
				 * the first '{') and it causes a blank line
				 * to be generated even if there are no
				 * declarations */
    int         blanklines_after_declarations;
    int         blanklines_after_procs;
    int         blanklines_before_blockcomments;
    int         leave_comma;	/* if true, never break declarations after
				 * commas */
    int         btype_2;	/* when true, brace should be on same line
				 * as if, while, etc */
    int         Bill_Shannon;	/* true iff a blank should always be
				 * inserted after sizeof */
    int         comment_delimiter_on_blankline;
    int         decl_com_ind;	/* the column in which comments after
				 * declarations should be put */
    int         cuddle_else;	/* true if else should cuddle up to '}' */
    int         continuation_indent; /* set to the indentation between the
				 * edge of code and continuation lines */
    float       case_indent;	/* The distance to indent case labels from the
				 * switch statement */
    int         com_ind;	/* the column in which comments to the right
				 * of code should start */
    int         decl_indent;	/* column to indent declared identifiers to */
    int         ljust_decl;	/* true if declarations should be left
				 * justified */
    int         unindent_displace; /* comments not to the right of code
				 * will be placed this many
				 * indentation levels to the left of
				 * code */
    int         extra_expression_indent; /* true if continuation lines from
				 * the expression part of "if(e)",
				 * "while(e)", "for(e;e;e)" should be
				 * indented an extra tab stop so that they
				 * don't conflict with the code that follows */
    int         else_if;	/* True iff else if pairs should be handled
				 * specially */
    int         function_brace_split; /* split function declaration and
				 * brace onto separate lines */
    int         format_col1_comments; /* If comments which start in column 1
				 * are to be magically reformatted (just
				 * like comments that begin in later columns) */
    int         format_block_comments; /* true if comments beginning with
				 * `/ * \n' are to be reformatted */
    int         indent_parameters;
    int         ind_size;	/* the size of one indentation level */
    int         block_comment_max_col;
    int         local_decl_indent; /* like decl_indent but for locals */
    int         lineup_to_parens_always; /* if true, do not attempt to keep
				 * lined-up code within the margin */
    int         lineup_to_parens; /* if true, continued code within parens
				 * will be lined up to the open paren */
    int         proc_calls_space; /* If true, procedure calls look like:
				 * foo (bar) rather than foo(bar) */
    int         procnames_start_line; /* if true, the names of procedures
				 * being defined get placed in column 1 (ie.
				 * a newline is placed between the type of
				 * the procedure and its name) */
    int         space_after_cast; /* "b = (int) a" vs "b = (int)a" */
    int         star_comment_cont; /* true iff comment continuation lines
				 * should have stars at the beginning of
				 * each line. */
    int         swallow_optional_blanklines;
    int         auto_typedefs;	/* set true to recognize identifiers
				 * ending in "_t" like typedefs */
    int         tabsize;	/* the size of a tab */
    int         max_col;	/* the maximum allowable line length */
    int         use_tabs;	/* set true to use tabs for spacing, false
				 * uses all spaces */
    int         verbose;	/* when true, non-essential error messages
				 * are printed */
} opt;

int         found_err;
int         n_real_blanklines;
int         prefix_blankline_requested;
int         postfix_blankline_requested;
int         break_comma;	/* when true and not in parens, break after a
				 * comma */
float       case_ind;		/* indentation level to be used for a "case
				 * n:" */
int         code_lines;		/* count of lines with code */
int         had_eof;		/* set to true when input is exhausted */
int         line_no;		/* the current line number. */
int         inhibit_formatting;	/* true if INDENT OFF is in effect */
int         suppress_blanklines;/* set iff following blanklines should be
				 * suppressed */

#define	STACKSIZE 256

struct parser_state {
    int         last_token;
    int         p_stack[STACKSIZE];	/* this is the parsers stack */
    int         il[STACKSIZE];	/* this stack stores indentation levels */
    float       cstk[STACKSIZE];/* used to store case stmt indentation levels */
    int         box_com;	/* set to true when we are in a "boxed"
				 * comment. In that case, the first non-blank
				 * char should be lined up with the / in / followed by * */
    int         comment_delta;	/* used to set up indentation for all lines
				 * of a boxed comment after the first one */
    int         n_comment_delta;/* remembers how many columns there were
				 * before the start of a box comment so that
				 * forthcoming lines of the comment are
				 * indented properly */
    int         cast_mask;	/* indicates which close parens potentially
				 * close off casts */
    int         not_cast_mask;	/* indicates which close parens definitely
				 * close off something else than casts */
    int         block_init;	/* true iff inside a block initialization */
    int         block_init_level;	/* The level of brace nesting in an
					 * initialization */
    int         last_nl;	/* this is true if the last thing scanned was
				 * a newline */
    int         in_or_st;	/* Will be true iff there has been a
				 * declarator (e.g. int or char) and no left
				 * paren since the last semicolon. When true,
				 * a '{' is starting a structure definition or
				 * an initialization list */
    int         bl_line;	/* set to 1 by dump_line if the line is blank */
    int         col_1;		/* set to true if the last token started in
				 * column 1 */
    int         com_col;	/* this is the column in which the current
				 * comment should start */
    int         com_lines;	/* the number of lines with comments, set by
				 * dump_line */
    int         dec_nest;	/* current nesting level for structure or init */
    int         decl_on_line;	/* set to true if this line of code has part
				 * of a declaration on it */
    int         i_l_follow;	/* the level to which ind_level should be set
				 * after the current line is printed */
    int         in_decl;	/* set to true when we are in a declaration
				 * stmt.  The processing of braces is then
				 * slightly different */
    int         in_stmt;	/* set to 1 while in a stmt */
    int         ind_level;	/* the current indentation level */
    int         ind_stmt;	/* set to 1 if next line should have an extra
				 * indentation level because we are in the
				 * middle of a stmt */
    int         last_u_d;	/* set to true after scanning a token which
				 * forces a following operator to be unary */
    int         out_coms;	/* the number of comments processed, set by
				 * pr_comment */
    int         out_lines;	/* the number of lines written, set by
				 * dump_line */
    int         p_l_follow;	/* used to remember how to indent following
				 * statement */
    int         paren_level;	/* parenthesization level. used to indent
				 * within statements */
    short       paren_indents[20];	/* column positions of each paren */
    int         pcase;		/* set to 1 if the current line label is a
				 * case.  It is printed differently from a
				 * regular label */
    int         search_brace;	/* set to true by parse when it is necessary
				 * to buffer up all info up to the start of a
				 * stmt after an if, while, etc */
    int         use_ff;		/* set to one if the current line should be
				 * terminated with a form feed */
    int         want_blank;	/* set to true when the following token should
				 * be prefixed by a blank. (Said prefixing is
				 * ignored in some cases.) */
    int         keyword;	/* the type of a keyword or 0 */
    int         dumped_decl_indent;
    int         in_parameter_declaration;
    int         tos;		/* pointer to top of stack */
    char        procname[100];	/* The name of the current procedure */
    int         just_saw_decl;
}           ps;

int         ifdef_level;
struct parser_state state_stack[5];
struct parser_state match_state[5];
