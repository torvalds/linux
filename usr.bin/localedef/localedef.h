/*-
 * Copyright 2018 Nexenta Systems, Inc.
 * Copyright 2015 John Marino <draco@marino.st>
 *
 * This source code is derived from the illumos localedef command, and
 * provided under BSD-style license terms by Nexenta Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * POSIX localedef.
 */

/* Common header files. */
#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

extern int com_char;
extern int esc_char;
extern int mb_cur_max;
extern int mb_cur_min;
extern int last_kw;
extern int verbose;
extern int yydebug;
extern int lineno;
extern int undefok;	/* mostly ignore undefined symbols */
extern int warnok;
extern int warnings;

int yylex(void);
void yyerror(const char *);
_Noreturn void errf(const char *, ...) __printflike(1, 2);
void warn(const char *, ...) __printflike(1, 2);

int putl_category(const char *, FILE *);
int wr_category(void *, size_t, FILE *);
FILE *open_category(void);
void close_category(FILE *);
void copy_category(char *);
const char *category_name(void);

int get_category(void);
int get_symbol(void);
int get_escaped(int);
int get_wide(void);
void reset_scanner(const char *);
void scan_to_eol(void);
void add_wcs(wchar_t);
void add_tok(int);
wchar_t *get_wcs(void);

uint32_t htote(uint32_t);

/* charmap.c - CHARMAP handling */
void init_charmap(void);
void add_charmap(const char *, int);
void add_charmap_undefined(char *);
void add_charmap_posix(void);
void add_charmap_range(char *, char *, int);
void add_charmap_char(const char *name, int val);
int lookup_charmap(const char *, wchar_t *);
int check_charmap_undefined(char *);
int check_charmap(wchar_t);

/* collate.o - LC_COLLATE handling */
typedef struct collelem collelem_t;
typedef struct collsym collsym_t;
void init_collate(void);
void define_collsym(char *);
void define_collelem(char *, wchar_t *);
void add_order_directive(void);
void add_order_bit(int);
void dump_collate(void);
collsym_t *lookup_collsym(char *);
collelem_t *lookup_collelem(char *);
void start_order_collelem(collelem_t *);
void start_order_undefined(void);
void start_order_symbol(char *);
void start_order_char(wchar_t);
void start_order_ellipsis(void);
void end_order_collsym(collsym_t *);
void end_order(void);
void add_weight(int32_t, int);
void add_weights(int32_t *);
void add_weight_num(int);
void add_order_collelem(collelem_t *);
void add_order_collsym(collsym_t *);
void add_order_char(wchar_t);
void add_order_ignore(void);
void add_order_ellipsis(void);
void add_order_symbol(char *);
void add_order_subst(void);
void add_subst_char(wchar_t);
void add_subst_collsym(collsym_t *);
void add_subst_collelem(collelem_t *);
void add_subst_symbol(char *);
int32_t get_weight(int32_t, int);
wchar_t * wsncpy(wchar_t *, const wchar_t *, size_t);


/* ctype.c - LC_CTYPE handling */
void init_ctype(void);
void add_ctype(int);
void add_ctype_range(wchar_t);
void add_width(int, int);
void add_width_range(int, int, int);
void add_caseconv(int, int);
void dump_ctype(void);

/* messages.c - LC_MESSAGES handling */
void init_messages(void);
void add_message(wchar_t *);
void dump_messages(void);

/* monetary.c - LC_MONETARY handling */
void init_monetary(void);
void add_monetary_str(wchar_t *);
void add_monetary_num(int);
void reset_monetary_group(void);
void add_monetary_group(int);
void dump_monetary(void);

/* numeric.c - LC_NUMERIC handling */
void init_numeric(void);
void add_numeric_str(wchar_t *);
void reset_numeric_group(void);
void add_numeric_group(int);
void dump_numeric(void);

/* time.c - LC_TIME handling */
void init_time(void);
void add_time_str(wchar_t *);
void reset_time_list(void);
void add_time_list(wchar_t *);
void check_time_list(void);
void dump_time(void);

/* wide.c -  Wide character handling. */
int to_wide(wchar_t *, const char *);
int to_mbs(char *, wchar_t);
int to_mb(char *, wchar_t);
char *to_mb_string(const wchar_t *);
void set_wide_encoding(const char *);
void werr(const char *, ...);
const char *get_wide_encoding(void);
int max_wide(void);

//#define	_(x)	gettext(x)
#define	INTERR	fprintf(stderr,"internal fault (%s:%d)", __FILE__, __LINE__)
