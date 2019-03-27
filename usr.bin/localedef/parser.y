%{
/*-
 * Copyright 2010 Nexenta Systems, Inc.  All rights reserved.
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
 * POSIX localedef grammar.
 */

#include <wchar.h>
#include <stdio.h>
#include <limits.h>
#include "localedef.h"

%}
%union {
	int		num;
	wchar_t		wc;
	char		*token;
	collsym_t	*collsym;
	collelem_t	*collelem;
}

%token		T_CODE_SET
%token		T_MB_CUR_MAX
%token		T_MB_CUR_MIN
%token		T_COM_CHAR
%token		T_ESC_CHAR
%token		T_LT
%token		T_GT
%token		T_NL
%token		T_SEMI
%token		T_COMMA
%token		T_ELLIPSIS
%token		T_RPAREN
%token		T_LPAREN
%token		T_QUOTE
%token		T_NULL
%token		T_WS
%token		T_END
%token		T_COPY
%token		T_CHARMAP
%token		T_WIDTH
%token		T_CTYPE
%token		T_ISUPPER
%token		T_ISLOWER
%token		T_ISALPHA
%token		T_ISDIGIT
%token		T_ISPUNCT
%token		T_ISXDIGIT
%token		T_ISSPACE
%token		T_ISPRINT
%token		T_ISGRAPH
%token		T_ISBLANK
%token		T_ISCNTRL
%token		T_ISALNUM
%token		T_ISSPECIAL
%token		T_ISPHONOGRAM
%token		T_ISIDEOGRAM
%token		T_ISENGLISH
%token		T_ISNUMBER
%token		T_TOUPPER
%token		T_TOLOWER
%token		T_COLLATE
%token		T_COLLATING_SYMBOL
%token		T_COLLATING_ELEMENT
%token		T_ORDER_START
%token		T_ORDER_END
%token		T_FORWARD
%token		T_BACKWARD
%token		T_POSITION
%token		T_FROM
%token		T_UNDEFINED
%token		T_IGNORE
%token		T_MESSAGES
%token		T_YESSTR
%token		T_NOSTR
%token		T_YESEXPR
%token		T_NOEXPR
%token		T_MONETARY
%token		T_INT_CURR_SYMBOL
%token		T_CURRENCY_SYMBOL
%token		T_MON_DECIMAL_POINT
%token		T_MON_THOUSANDS_SEP
%token		T_POSITIVE_SIGN
%token		T_NEGATIVE_SIGN
%token		T_MON_GROUPING
%token		T_INT_FRAC_DIGITS
%token		T_FRAC_DIGITS
%token		T_P_CS_PRECEDES
%token		T_P_SEP_BY_SPACE
%token		T_N_CS_PRECEDES
%token		T_N_SEP_BY_SPACE
%token		T_P_SIGN_POSN
%token		T_N_SIGN_POSN
%token		T_INT_P_CS_PRECEDES
%token		T_INT_N_CS_PRECEDES
%token		T_INT_P_SEP_BY_SPACE
%token		T_INT_N_SEP_BY_SPACE
%token		T_INT_P_SIGN_POSN
%token		T_INT_N_SIGN_POSN
%token		T_NUMERIC
%token		T_DECIMAL_POINT
%token		T_THOUSANDS_SEP
%token		T_GROUPING
%token		T_TIME
%token		T_ABDAY
%token		T_DAY
%token		T_ABMON
%token		T_MON
%token		T_ERA
%token		T_ERA_D_FMT
%token		T_ERA_T_FMT
%token		T_ERA_D_T_FMT
%token		T_ALT_DIGITS
%token		T_D_T_FMT
%token		T_D_FMT
%token		T_T_FMT
%token		T_AM_PM
%token		T_T_FMT_AMPM
%token		T_DATE_FMT
%token	<wc>		T_CHAR
%token	<token>		T_NAME
%token	<num>		T_NUMBER
%token	<token>		T_SYMBOL
%token	<collsym>	T_COLLSYM
%token	<collelem>	T_COLLELEM

%%

localedef	: setting_list categories
		| categories
		;

string		: T_QUOTE charlist T_QUOTE
		| T_QUOTE T_QUOTE
		;

charlist	: charlist T_CHAR
		{
			add_wcs($2);
		}
		| T_CHAR
		{
			add_wcs($1);
		}
		;

setting_list	: setting_list setting
		| setting
		;


setting		: T_COM_CHAR T_CHAR T_NL
		{
			com_char = $2;
		}
		| T_ESC_CHAR T_CHAR T_NL
		{
			esc_char = $2;
		}
		| T_MB_CUR_MAX T_NUMBER T_NL
		{
			mb_cur_max = $2;
		}
		| T_MB_CUR_MIN T_NUMBER T_NL
		{
			mb_cur_min = $2;
		}
		| T_CODE_SET string T_NL
		{
			wchar_t *w = get_wcs();
			set_wide_encoding(to_mb_string(w));
			free(w);
		}
		| T_CODE_SET T_NAME T_NL
		{
			set_wide_encoding($2);
		}
		;

copycat		: T_COPY T_NAME T_NL
		{
			copy_category($2);
		}
		| T_COPY string T_NL
		{
			wchar_t *w = get_wcs();
			copy_category(to_mb_string(w));
			free(w);
		}
		;

categories	: categories category
		| category
		;


category	: charmap
		| messages
		| monetary
		| ctype
		| collate
		| numeric
		| time
		;


charmap		: T_CHARMAP T_NL charmap_list T_END T_CHARMAP T_NL
		| T_WIDTH T_NL width_list T_END T_WIDTH T_NL
		;


charmap_list	: charmap_list charmap_entry
		| charmap_entry
		;


charmap_entry	: T_SYMBOL T_CHAR
		{
			add_charmap($1, $2);
			scan_to_eol();
		}
		| T_SYMBOL T_ELLIPSIS T_SYMBOL T_CHAR
		{
			add_charmap_range($1, $3, $4);
			scan_to_eol();
		}
		| T_NL
		;

width_list	: width_list width_entry
		| width_entry
		;

width_entry	: T_CHAR T_NUMBER T_NL
		{
			add_width($1, $2);
		}
		| T_SYMBOL T_NUMBER T_NL
		{
			add_charmap_undefined($1);
		}
		| T_CHAR T_ELLIPSIS T_CHAR T_NUMBER T_NL
		{
			add_width_range($1, $3, $4);
		}
		| T_SYMBOL T_ELLIPSIS T_SYMBOL T_NUMBER T_NL
		{
			add_charmap_undefined($1);
			add_charmap_undefined($3);
		}
		| T_CHAR T_ELLIPSIS T_SYMBOL T_NUMBER T_NL
		{
			add_width($1, $4);
			add_charmap_undefined($3);
		}
		| T_SYMBOL T_ELLIPSIS T_CHAR T_NUMBER T_NL
		{
			add_width($3, $4);
			add_charmap_undefined($1);
		}
		| T_NL
		;

ctype		: T_CTYPE T_NL ctype_list T_END T_CTYPE T_NL
		{
			dump_ctype();
		}
		| T_CTYPE T_NL copycat  T_END T_CTYPE T_NL
		;

ctype_list	: ctype_list ctype_kw
		| ctype_kw
		;

ctype_kw	: T_ISUPPER cc_list T_NL
		| T_ISLOWER cc_list T_NL
		| T_ISALPHA cc_list T_NL
		| T_ISDIGIT cc_list T_NL
		| T_ISPUNCT cc_list T_NL
		| T_ISXDIGIT cc_list T_NL
		| T_ISSPACE cc_list T_NL
		| T_ISPRINT cc_list T_NL
		| T_ISGRAPH cc_list T_NL
		| T_ISBLANK cc_list T_NL
		| T_ISCNTRL cc_list T_NL
		| T_ISALNUM cc_list T_NL
		| T_ISSPECIAL cc_list T_NL
		| T_ISENGLISH cc_list T_NL
		| T_ISNUMBER cc_list T_NL
		| T_ISIDEOGRAM cc_list T_NL
		| T_ISPHONOGRAM cc_list T_NL
		| T_TOUPPER conv_list T_NL
		| T_TOLOWER conv_list T_NL
		;

cc_list		: cc_list T_SEMI cc_range_end
		| cc_list T_SEMI cc_char
		| cc_char
		;

cc_range_end	: T_ELLIPSIS T_SEMI T_CHAR
		{
			add_ctype_range($3);
		}
		;

cc_char		: T_CHAR
		{
			add_ctype($1);
		}
		| T_SYMBOL
		{
			add_charmap_undefined($1);
		}
		;

conv_list	: conv_list T_SEMI conv_pair
		| conv_pair
		;


conv_pair	: T_LPAREN T_CHAR T_COMMA T_CHAR T_RPAREN
		{
			add_caseconv($2, $4);
		}
		| T_LPAREN T_SYMBOL T_COMMA T_CHAR T_RPAREN
		{
			add_charmap_undefined($2);
		}
		| T_LPAREN T_SYMBOL T_COMMA T_SYMBOL T_RPAREN
		{
			add_charmap_undefined($2);
			add_charmap_undefined($4);
		}
		| T_LPAREN T_CHAR T_COMMA T_SYMBOL T_RPAREN
		{
			add_charmap_undefined($4);
		}
		;

collate		: T_COLLATE T_NL coll_order T_END T_COLLATE T_NL
		{
			dump_collate();
		}
		| T_COLLATE T_NL coll_optional coll_order T_END T_COLLATE T_NL
		{
			dump_collate();
		}
		| T_COLLATE T_NL copycat T_END T_COLLATE T_NL
		;


coll_optional	: coll_optional coll_symbols
		| coll_optional coll_elements
		| coll_symbols
		| coll_elements
		;


coll_symbols	: T_COLLATING_SYMBOL T_SYMBOL T_NL
		{
			define_collsym($2);
		}
		;


coll_elements	: T_COLLATING_ELEMENT T_SYMBOL T_FROM string T_NL
		{
			define_collelem($2, get_wcs());
		}
		;

coll_order	: T_ORDER_START T_NL order_list T_ORDER_END T_NL
		{
			/* If no order list supplied default to one forward */
			add_order_bit(T_FORWARD);
			add_order_directive();
		}
		| T_ORDER_START order_args T_NL order_list T_ORDER_END T_NL
		;


order_args	: order_args T_SEMI order_arg
		{
			add_order_directive();
		}
		| order_arg
		{
			add_order_directive();
		}
		;

order_arg	: order_arg T_COMMA order_dir
		| order_dir
		;

order_dir	: T_FORWARD
		{
			add_order_bit(T_FORWARD);
		}
		| T_BACKWARD
		{
			add_order_bit(T_BACKWARD);
		}
		| T_POSITION
		{
			add_order_bit(T_POSITION);
		}
		;

order_list	: order_list order_item
		| order_item
		;

order_item	: T_COLLSYM T_NL
		{
			end_order_collsym($1);
		}
		| order_itemkw T_NL
		{
			end_order();
		}
		| order_itemkw order_weights T_NL
		{
			end_order();
		}
		;

order_itemkw	: T_CHAR
		{
			start_order_char($1);
		}
		| T_ELLIPSIS
		{
			start_order_ellipsis();
		}
		| T_COLLELEM
		{
			start_order_collelem($1);
		}
		| T_UNDEFINED
		{
			start_order_undefined();
		}
		| T_SYMBOL
		{
			start_order_symbol($1);
		}
		;

order_weights	: order_weights T_SEMI order_weight
		| order_weights T_SEMI
		| order_weight
		;

order_weight	: T_COLLELEM
		{
			add_order_collelem($1);
		}
		| T_COLLSYM
		{
			add_order_collsym($1);
		}
		| T_CHAR
		{
			add_order_char($1);
		}
		| T_ELLIPSIS
		{
			add_order_ellipsis();
		}
		| T_IGNORE
		{
			add_order_ignore();
		}
		| T_SYMBOL
		{
			add_order_symbol($1);
		}
		| T_QUOTE order_str T_QUOTE
		{
			add_order_subst();
		}
		;

order_str	: order_str order_stritem
		| order_stritem
		;

order_stritem	: T_CHAR
		{
			add_subst_char($1);
		}
		| T_COLLSYM
		{
			add_subst_collsym($1);
		}
		| T_COLLELEM
		{
			add_subst_collelem($1);
		}
		| T_SYMBOL
		{
			add_subst_symbol($1);
		}
		;

messages	: T_MESSAGES T_NL messages_list T_END T_MESSAGES T_NL
		{
			dump_messages();
		}
		| T_MESSAGES T_NL copycat T_END T_MESSAGES T_NL
		;

messages_list	: messages_list messages_item
		| messages_item
		;

messages_kw	: T_YESSTR
		| T_NOSTR
		| T_YESEXPR
		| T_NOEXPR
		;

messages_item	: messages_kw string T_NL
		{
			add_message(get_wcs());
		}
		;

monetary	: T_MONETARY T_NL monetary_list T_END T_MONETARY T_NL
		{
			dump_monetary();
		}
		| T_MONETARY T_NL copycat T_END T_MONETARY T_NL
		;

monetary_list	: monetary_list monetary_kw
		| monetary_kw
		;

monetary_strkw	: T_INT_CURR_SYMBOL
		| T_CURRENCY_SYMBOL
		| T_MON_DECIMAL_POINT
		| T_MON_THOUSANDS_SEP
		| T_POSITIVE_SIGN
		| T_NEGATIVE_SIGN
		;

monetary_numkw	: T_INT_FRAC_DIGITS
		| T_FRAC_DIGITS
		| T_P_CS_PRECEDES
		| T_P_SEP_BY_SPACE
		| T_N_CS_PRECEDES
		| T_N_SEP_BY_SPACE
		| T_P_SIGN_POSN
		| T_N_SIGN_POSN
		| T_INT_P_CS_PRECEDES
		| T_INT_N_CS_PRECEDES
		| T_INT_P_SEP_BY_SPACE
		| T_INT_N_SEP_BY_SPACE
		| T_INT_P_SIGN_POSN
		| T_INT_N_SIGN_POSN
		;

monetary_kw	: monetary_strkw string T_NL
		{
			add_monetary_str(get_wcs());
		}
		| monetary_numkw T_NUMBER T_NL
		{
			add_monetary_num($2);
		}
		| T_MON_GROUPING mon_group_list T_NL
		;

mon_group_list	: T_NUMBER
		{
			reset_monetary_group();
			add_monetary_group($1);
		}
		| mon_group_list T_SEMI T_NUMBER
		{
			add_monetary_group($3);
		}
		;


numeric		: T_NUMERIC T_NL numeric_list T_END T_NUMERIC T_NL
		{
			dump_numeric();
		}
		| T_NUMERIC T_NL copycat T_END T_NUMERIC T_NL
		;


numeric_list	: numeric_list numeric_item
		| numeric_item
		;


numeric_item	: numeric_strkw string T_NL
		{
			add_numeric_str(get_wcs());
		}
		| T_GROUPING group_list T_NL
		;

numeric_strkw	: T_DECIMAL_POINT
		| T_THOUSANDS_SEP
		;


group_list	: T_NUMBER
		{
			reset_numeric_group();
			add_numeric_group($1);
		}
		| group_list T_SEMI T_NUMBER
		{
			add_numeric_group($3);
		}
		;


time		: T_TIME T_NL time_kwlist T_END T_TIME T_NL
		{
			dump_time();
		}
		| T_TIME T_NL copycat T_END T_NUMERIC T_NL
		;

time_kwlist	: time_kwlist time_kw
		| time_kw
		;

time_kw		: time_strkw string T_NL
		{
			add_time_str(get_wcs());
		}
		| time_listkw time_list T_NL
		{
			check_time_list();
		}
		;

time_listkw	: T_ABDAY
		| T_DAY
		| T_ABMON
		| T_MON
		| T_ERA
		| T_ALT_DIGITS
		| T_AM_PM
		;

time_strkw	: T_ERA_D_T_FMT
		| T_ERA_T_FMT
		| T_ERA_D_FMT
		| T_D_T_FMT
		| T_D_FMT
		| T_T_FMT
		| T_T_FMT_AMPM
		| T_DATE_FMT
		;

time_list	: time_list T_SEMI string
		{
			add_time_list(get_wcs());
		}
		| string
		{
			reset_time_list();
			add_time_list(get_wcs());
		}
		;
