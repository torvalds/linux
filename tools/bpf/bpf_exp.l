/*
 * BPF asm code lexer
 *
 * This program is free software; you can distribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Syntax kept close to:
 *
 * Steven McCanne and Van Jacobson. 1993. The BSD packet filter: a new
 * architecture for user-level packet capture. In Proceedings of the
 * USENIX Winter 1993 Conference Proceedings on USENIX Winter 1993
 * Conference Proceedings (USENIX'93). USENIX Association, Berkeley,
 * CA, USA, 2-2.
 *
 * Copyright 2013 Daniel Borkmann <borkmann@redhat.com>
 * Licensed under the GNU General Public License, version 2.0 (GPLv2)
 */

%{

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <linux/filter.h>

#include "bpf_exp.yacc.h"

extern void yyerror(const char *str);

%}

%option align
%option ecs

%option nounput
%option noreject
%option noinput
%option noyywrap

%option 8bit
%option caseless
%option yylineno

%%

"ldb"		{ return OP_LDB; }
"ldh"		{ return OP_LDH; }
"ld"		{ return OP_LD; }
"ldi"		{ return OP_LDI; }
"ldx"		{ return OP_LDX; }
"ldxi"		{ return OP_LDXI; }
"ldxb"		{ return OP_LDXB; }
"st"		{ return OP_ST; }
"stx"		{ return OP_STX; }
"jmp"		{ return OP_JMP; }
"ja"		{ return OP_JMP; }
"jeq"		{ return OP_JEQ; }
"jneq"		{ return OP_JNEQ; }
"jne"		{ return OP_JNEQ; }
"jlt"		{ return OP_JLT; }
"jle"		{ return OP_JLE; }
"jgt"		{ return OP_JGT; }
"jge"		{ return OP_JGE; }
"jset"		{ return OP_JSET; }
"add"		{ return OP_ADD; }
"sub"		{ return OP_SUB; }
"mul"		{ return OP_MUL; }
"div"		{ return OP_DIV; }
"mod"		{ return OP_MOD; }
"neg"		{ return OP_NEG; }
"and"		{ return OP_AND; }
"xor"		{ return OP_XOR; }
"or"		{ return OP_OR; }
"lsh"		{ return OP_LSH; }
"rsh"		{ return OP_RSH; }
"ret"		{ return OP_RET; }
"tax"		{ return OP_TAX; }
"txa"		{ return OP_TXA; }

"#"?("len")	{ return K_PKT_LEN; }

"#"?("proto")	{
		yylval.number = SKF_AD_PROTOCOL;
		return extension;
	}
"#"?("type")	{
		yylval.number = SKF_AD_PKTTYPE;
		return extension;
	}
"#"?("poff")	{
		yylval.number = SKF_AD_PAY_OFFSET;
		return extension;
	}
"#"?("ifidx")	{
		yylval.number = SKF_AD_IFINDEX;
		return extension;
	}
"#"?("nla")	{
		yylval.number = SKF_AD_NLATTR;
		return extension;
	}
"#"?("nlan")	{
		yylval.number = SKF_AD_NLATTR_NEST;
		return extension;
	}
"#"?("mark")	{
		yylval.number = SKF_AD_MARK;
		return extension;
	}
"#"?("queue")	{
		yylval.number = SKF_AD_QUEUE;
		return extension;
	}
"#"?("hatype")	{
		yylval.number = SKF_AD_HATYPE;
		return extension;
	}
"#"?("rxhash")	{
		yylval.number = SKF_AD_RXHASH;
		return extension;
	}
"#"?("cpu")	{
		yylval.number = SKF_AD_CPU;
		return extension;
	}
"#"?("vlan_tci") {
		yylval.number = SKF_AD_VLAN_TAG;
		return extension;
	}
"#"?("vlan_pr")	{
		yylval.number = SKF_AD_VLAN_TAG_PRESENT;
		return extension;
	}
"#"?("vlan_avail") {
		yylval.number = SKF_AD_VLAN_TAG_PRESENT;
		return extension;
	}
"#"?("vlan_tpid") {
		yylval.number = SKF_AD_VLAN_TPID;
		return extension;
	}
"#"?("rand")	{
		yylval.number = SKF_AD_RANDOM;
		return extension;
	}

":"		{ return ':'; }
","		{ return ','; }
"#"		{ return '#'; }
"%"		{ return '%'; }
"["		{ return '['; }
"]"		{ return ']'; }
"("		{ return '('; }
")"		{ return ')'; }
"x"		{ return 'x'; }
"a"		{ return 'a'; }
"+"		{ return '+'; }
"M"		{ return 'M'; }
"*"		{ return '*'; }
"&"		{ return '&'; }

([0][x][a-fA-F0-9]+) {
			yylval.number = strtoul(yytext, NULL, 16);
			return number;
		}
([0][b][0-1]+)	{
			yylval.number = strtol(yytext + 2, NULL, 2);
			return number;
		}
(([0])|([-+]?[1-9][0-9]*)) {
			yylval.number = strtol(yytext, NULL, 10);
			return number;
		}
([0][0-9]+)	{
			yylval.number = strtol(yytext + 1, NULL, 8);
			return number;
		}
[a-zA-Z_][a-zA-Z0-9_]+ {
			yylval.label = strdup(yytext);
			return label;
		}

"/*"([^\*]|\*[^/])*"*/"		{ /* NOP */ }
";"[^\n]*			{ /* NOP */ }
^#.*				{ /* NOP */ }
[ \t]+				{ /* NOP */ }
[ \n]+				{ /* NOP */ }

.		{
			printf("unknown character \'%s\'", yytext);
			yyerror("lex unknown character");
		}

%%
