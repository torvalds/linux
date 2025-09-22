/*	$OpenBSD: map_parse.y,v 1.7 2012/07/14 08:27:05 shadchin Exp $	*/
/*	$NetBSD: map_parse.y,v 1.2 1999/02/08 11:08:23 hannken Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Juergen Hannken-Illjes.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Parse a keyboard map. Statements are one of
 *
 * keysym sym1 = sym2		Assign the key containing `sym2' to
 *				the key containing `sym1'. This is a copy
 *				from the old to the new map. Therefore it
 *				is possible to exchange keys.
 *
 * keycode pos = sym ...	assign the symbols to key `pos'.
 *				The first symbol may be a command.
 *				The following symbols are assigned
 *				to the normal and altgr groups.
 *				Missing symbols are generated automatically
 *				as either the upper case variant or the
 *				normal group.
 */

%{

#include <sys/time.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsconsio.h>
#include <err.h>
#include "wsconsctl.h"

extern struct wskbd_map_data kbmap;	/* from keyboard.c */

static struct wscons_keymap mapdata[KS_NUMKEYCODES];
struct wskbd_map_data newkbmap;		/* used in util.c */
static struct wscons_keymap *cur_mp;

static int ksym_lookup(keysym_t);

static int
ksym_lookup(keysym_t ksym)
{
	int i;
	struct wscons_keymap *mp;

	for (i = 0; i < kbmap.maplen; i++) {
		mp = kbmap.map + i;
		if (mp->command == ksym ||
		    mp->group1[0] == ksym || mp->group1[1] == ksym ||
		    mp->group2[0] == ksym || mp->group2[1] == ksym)
			return(i);
	}

	errx(1, "keysym %s not found", ksym2name(ksym));
}

%}

%union {
		keysym_t kval;
		int ival;
	}

%token T_KEYSYM T_KEYCODE
%token <kval> T_KEYSYM_VAR T_KEYSYM_CMD_VAR
%token <ival> T_NUMBER

%type <kval> keysym_var

%%

program		: = {
			int i;
			struct wscons_keymap *mp;

			for (i = 0; i < KS_NUMKEYCODES; i++) {
				mp = mapdata + i;
				mp->command = KS_voidSymbol;
				mp->group1[0] = KS_voidSymbol;
				mp->group1[1] = KS_voidSymbol;
				mp->group2[0] = KS_voidSymbol;
				mp->group2[1] = KS_voidSymbol;
			}

			newkbmap.maplen = 0;
			newkbmap.map = mapdata;
		} expr_list
		;

expr_list	: expr
		| expr_list expr
		;

expr		: keysym_expr
		| keycode_expr
		;

keysym_expr	: T_KEYSYM keysym_var "=" keysym_var = {
			int src, dst;

			dst = ksym_lookup($2);
			src = ksym_lookup($4);
			newkbmap.map[dst] = kbmap.map[src];
			if (dst >= newkbmap.maplen)
				newkbmap.maplen = dst + 1;
		}
		;

keycode_expr	: T_KEYCODE T_NUMBER "=" = {
			if ($2 >= KS_NUMKEYCODES)
				errx(1, "%d: keycode too large", $2);
			if ($2 >= newkbmap.maplen)
				newkbmap.maplen = $2 + 1;
			cur_mp = mapdata + $2;
		} keysym_cmd keysym_list
		;

keysym_cmd	: /* empty */
		| T_KEYSYM_CMD_VAR = {
			cur_mp->command = $1;
		}
		;

keysym_list	: keysym_var = {
			cur_mp->group1[0] = $1;
			cur_mp->group1[1] = ksym_upcase(cur_mp->group1[0]);
			cur_mp->group2[0] = cur_mp->group1[0];
			cur_mp->group2[1] = cur_mp->group1[1];
		}
		| keysym_var keysym_var = {
			cur_mp->group1[0] = $1;
			cur_mp->group1[1] = $2;
			cur_mp->group2[0] = cur_mp->group1[0];
			cur_mp->group2[1] = cur_mp->group1[1];
		}
		| keysym_var keysym_var keysym_var = {
			cur_mp->group1[0] = $1;
			cur_mp->group1[1] = $2;
			cur_mp->group2[0] = $3;
			cur_mp->group2[1] = ksym_upcase(cur_mp->group2[0]);
		}
		| keysym_var keysym_var keysym_var keysym_var = {
			cur_mp->group1[0] = $1;
			cur_mp->group1[1] = $2;
			cur_mp->group2[0] = $3;
			cur_mp->group2[1] = $4;
		}
		;

keysym_var	: T_KEYSYM_VAR = {
			$$ = $1;
		}
		| T_NUMBER = {
			char name[2];
			int res;

			if ($1 < 0 || $1 > 9)
				yyerror("keysym expected");
			name[0] = $1 + '0';
			name[1] = '\0';
			res = name2ksym(name);
			if (res < 0)
				yyerror("keysym expected");
			$$ = res;
		}
		;
%%

void
yyerror(char *msg)
{
	errx(1, "parse: %s", msg);
}
