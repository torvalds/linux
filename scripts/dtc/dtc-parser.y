/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *                                                                   USA
 */

%{
#include <stdio.h>

#include "dtc.h"
#include "srcpos.h"

YYLTYPE yylloc;

extern int yylex(void);
extern void print_error(char const *fmt, ...);
extern void yyerror(char const *s);

extern struct boot_info *the_boot_info;
extern int treesource_error;

static unsigned long long eval_literal(const char *s, int base, int bits);
static unsigned char eval_char_literal(const char *s);
%}

%union {
	char *propnodename;
	char *literal;
	char *labelref;
	unsigned int cbase;
	uint8_t byte;
	struct data data;

	struct {
		struct data	data;
		int		bits;
	} array;

	struct property *prop;
	struct property *proplist;
	struct node *node;
	struct node *nodelist;
	struct reserve_info *re;
	uint64_t integer;
}

%token DT_V1
%token DT_MEMRESERVE
%token DT_LSHIFT DT_RSHIFT DT_LE DT_GE DT_EQ DT_NE DT_AND DT_OR
%token DT_BITS
%token DT_DEL_PROP
%token DT_DEL_NODE
%token <propnodename> DT_PROPNODENAME
%token <literal> DT_LITERAL
%token <literal> DT_CHAR_LITERAL
%token <cbase> DT_BASE
%token <byte> DT_BYTE
%token <data> DT_STRING
%token <labelref> DT_LABEL
%token <labelref> DT_REF
%token DT_INCBIN

%type <data> propdata
%type <data> propdataprefix
%type <re> memreserve
%type <re> memreserves
%type <array> arrayprefix
%type <data> bytestring
%type <prop> propdef
%type <proplist> proplist

%type <node> devicetree
%type <node> nodedef
%type <node> subnode
%type <nodelist> subnodes

%type <integer> integer_prim
%type <integer> integer_unary
%type <integer> integer_mul
%type <integer> integer_add
%type <integer> integer_shift
%type <integer> integer_rela
%type <integer> integer_eq
%type <integer> integer_bitand
%type <integer> integer_bitxor
%type <integer> integer_bitor
%type <integer> integer_and
%type <integer> integer_or
%type <integer> integer_trinary
%type <integer> integer_expr

%%

sourcefile:
	  DT_V1 ';' memreserves devicetree
		{
			the_boot_info = build_boot_info($3, $4,
							guess_boot_cpuid($4));
		}
	;

memreserves:
	  /* empty */
		{
			$$ = NULL;
		}
	| memreserve memreserves
		{
			$$ = chain_reserve_entry($1, $2);
		}
	;

memreserve:
	  DT_MEMRESERVE integer_prim integer_prim ';'
		{
			$$ = build_reserve_entry($2, $3);
		}
	| DT_LABEL memreserve
		{
			add_label(&$2->labels, $1);
			$$ = $2;
		}
	;

devicetree:
	  '/' nodedef
		{
			$$ = name_node($2, "");
		}
	| devicetree '/' nodedef
		{
			$$ = merge_nodes($1, $3);
		}
	| devicetree DT_REF nodedef
		{
			struct node *target = get_node_by_ref($1, $2);

			if (target)
				merge_nodes(target, $3);
			else
				print_error("label or path, '%s', not found", $2);
			$$ = $1;
		}
	| devicetree DT_DEL_NODE DT_REF ';'
		{
			struct node *target = get_node_by_ref($1, $3);

			if (!target)
				print_error("label or path, '%s', not found", $3);
			else
				delete_node(target);

			$$ = $1;
		}
	;

nodedef:
	  '{' proplist subnodes '}' ';'
		{
			$$ = build_node($2, $3);
		}
	;

proplist:
	  /* empty */
		{
			$$ = NULL;
		}
	| proplist propdef
		{
			$$ = chain_property($2, $1);
		}
	;

propdef:
	  DT_PROPNODENAME '=' propdata ';'
		{
			$$ = build_property($1, $3);
		}
	| DT_PROPNODENAME ';'
		{
			$$ = build_property($1, empty_data);
		}
	| DT_DEL_PROP DT_PROPNODENAME ';'
		{
			$$ = build_property_delete($2);
		}
	| DT_LABEL propdef
		{
			add_label(&$2->labels, $1);
			$$ = $2;
		}
	;

propdata:
	  propdataprefix DT_STRING
		{
			$$ = data_merge($1, $2);
		}
	| propdataprefix arrayprefix '>'
		{
			$$ = data_merge($1, $2.data);
		}
	| propdataprefix '[' bytestring ']'
		{
			$$ = data_merge($1, $3);
		}
	| propdataprefix DT_REF
		{
			$$ = data_add_marker($1, REF_PATH, $2);
		}
	| propdataprefix DT_INCBIN '(' DT_STRING ',' integer_prim ',' integer_prim ')'
		{
			FILE *f = srcfile_relative_open($4.val, NULL);
			struct data d;

			if ($6 != 0)
				if (fseek(f, $6, SEEK_SET) != 0)
					print_error("Couldn't seek to offset %llu in \"%s\": %s",
						     (unsigned long long)$6,
						     $4.val,
						     strerror(errno));

			d = data_copy_file(f, $8);

			$$ = data_merge($1, d);
			fclose(f);
		}
	| propdataprefix DT_INCBIN '(' DT_STRING ')'
		{
			FILE *f = srcfile_relative_open($4.val, NULL);
			struct data d = empty_data;

			d = data_copy_file(f, -1);

			$$ = data_merge($1, d);
			fclose(f);
		}
	| propdata DT_LABEL
		{
			$$ = data_add_marker($1, LABEL, $2);
		}
	;

propdataprefix:
	  /* empty */
		{
			$$ = empty_data;
		}
	| propdata ','
		{
			$$ = $1;
		}
	| propdataprefix DT_LABEL
		{
			$$ = data_add_marker($1, LABEL, $2);
		}
	;

arrayprefix:
	DT_BITS DT_LITERAL '<'
		{
			$$.data = empty_data;
			$$.bits = eval_literal($2, 0, 7);

			if (($$.bits !=  8) &&
			    ($$.bits != 16) &&
			    ($$.bits != 32) &&
			    ($$.bits != 64))
			{
				print_error("Only 8, 16, 32 and 64-bit elements"
					    " are currently supported");
				$$.bits = 32;
			}
		}
	| '<'
		{
			$$.data = empty_data;
			$$.bits = 32;
		}
	| arrayprefix integer_prim
		{
			if ($1.bits < 64) {
				uint64_t mask = (1ULL << $1.bits) - 1;
				/*
				 * Bits above mask must either be all zero
				 * (positive within range of mask) or all one
				 * (negative and sign-extended). The second
				 * condition is true if when we set all bits
				 * within the mask to one (i.e. | in the
				 * mask), all bits are one.
				 */
				if (($2 > mask) && (($2 | mask) != -1ULL))
					print_error(
						"integer value out of range "
						"%016lx (%d bits)", $1.bits);
			}

			$$.data = data_append_integer($1.data, $2, $1.bits);
		}
	| arrayprefix DT_REF
		{
			uint64_t val = ~0ULL >> (64 - $1.bits);

			if ($1.bits == 32)
				$1.data = data_add_marker($1.data,
							  REF_PHANDLE,
							  $2);
			else
				print_error("References are only allowed in "
					    "arrays with 32-bit elements.");

			$$.data = data_append_integer($1.data, val, $1.bits);
		}
	| arrayprefix DT_LABEL
		{
			$$.data = data_add_marker($1.data, LABEL, $2);
		}
	;

integer_prim:
	  DT_LITERAL
		{
			$$ = eval_literal($1, 0, 64);
		}
	| DT_CHAR_LITERAL
		{
			$$ = eval_char_literal($1);
		}
	| '(' integer_expr ')'
		{
			$$ = $2;
		}
	;

integer_expr:
	integer_trinary
	;

integer_trinary:
	  integer_or
	| integer_or '?' integer_expr ':' integer_trinary { $$ = $1 ? $3 : $5; }
	;

integer_or:
	  integer_and
	| integer_or DT_OR integer_and { $$ = $1 || $3; }
	;

integer_and:
	  integer_bitor
	| integer_and DT_AND integer_bitor { $$ = $1 && $3; }
	;

integer_bitor:
	  integer_bitxor
	| integer_bitor '|' integer_bitxor { $$ = $1 | $3; }
	;

integer_bitxor:
	  integer_bitand
	| integer_bitxor '^' integer_bitand { $$ = $1 ^ $3; }
	;

integer_bitand:
	  integer_eq
	| integer_bitand '&' integer_eq { $$ = $1 & $3; }
	;

integer_eq:
	  integer_rela
	| integer_eq DT_EQ integer_rela { $$ = $1 == $3; }
	| integer_eq DT_NE integer_rela { $$ = $1 != $3; }
	;

integer_rela:
	  integer_shift
	| integer_rela '<' integer_shift { $$ = $1 < $3; }
	| integer_rela '>' integer_shift { $$ = $1 > $3; }
	| integer_rela DT_LE integer_shift { $$ = $1 <= $3; }
	| integer_rela DT_GE integer_shift { $$ = $1 >= $3; }
	;

integer_shift:
	  integer_shift DT_LSHIFT integer_add { $$ = $1 << $3; }
	| integer_shift DT_RSHIFT integer_add { $$ = $1 >> $3; }
	| integer_add
	;

integer_add:
	  integer_add '+' integer_mul { $$ = $1 + $3; }
	| integer_add '-' integer_mul { $$ = $1 - $3; }
	| integer_mul
	;

integer_mul:
	  integer_mul '*' integer_unary { $$ = $1 * $3; }
	| integer_mul '/' integer_unary { $$ = $1 / $3; }
	| integer_mul '%' integer_unary { $$ = $1 % $3; }
	| integer_unary
	;

integer_unary:
	  integer_prim
	| '-' integer_unary { $$ = -$2; }
	| '~' integer_unary { $$ = ~$2; }
	| '!' integer_unary { $$ = !$2; }
	;

bytestring:
	  /* empty */
		{
			$$ = empty_data;
		}
	| bytestring DT_BYTE
		{
			$$ = data_append_byte($1, $2);
		}
	| bytestring DT_LABEL
		{
			$$ = data_add_marker($1, LABEL, $2);
		}
	;

subnodes:
	  /* empty */
		{
			$$ = NULL;
		}
	| subnode subnodes
		{
			$$ = chain_node($1, $2);
		}
	| subnode propdef
		{
			print_error("syntax error: properties must precede subnodes");
			YYERROR;
		}
	;

subnode:
	  DT_PROPNODENAME nodedef
		{
			$$ = name_node($2, $1);
		}
	| DT_DEL_NODE DT_PROPNODENAME ';'
		{
			$$ = name_node(build_node_delete(), $2);
		}
	| DT_LABEL subnode
		{
			add_label(&$2->labels, $1);
			$$ = $2;
		}
	;

%%

void print_error(char const *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	srcpos_verror(&yylloc, fmt, va);
	va_end(va);

	treesource_error = 1;
}

void yyerror(char const *s) {
	print_error("%s", s);
}

static unsigned long long eval_literal(const char *s, int base, int bits)
{
	unsigned long long val;
	char *e;

	errno = 0;
	val = strtoull(s, &e, base);
	if (*e) {
		size_t uls = strspn(e, "UL");
		if (e[uls])
			print_error("bad characters in literal");
	}
	if ((errno == ERANGE)
		 || ((bits < 64) && (val >= (1ULL << bits))))
		print_error("literal out of range");
	else if (errno != 0)
		print_error("bad literal");
	return val;
}

static unsigned char eval_char_literal(const char *s)
{
	int i = 1;
	char c = s[0];

	if (c == '\0')
	{
		print_error("empty character literal");
		return 0;
	}

	/*
	 * If the first character in the character literal is a \ then process
	 * the remaining characters as an escape encoding. If the first
	 * character is neither an escape or a terminator it should be the only
	 * character in the literal and will be returned.
	 */
	if (c == '\\')
		c = get_escape_char(s, &i);

	if (s[i] != '\0')
		print_error("malformed character literal");

	return c;
}
