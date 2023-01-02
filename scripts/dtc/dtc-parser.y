// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 */
%locations

%{
#include <stdio.h>
#include <inttypes.h>

#include "dtc.h"
#include "srcpos.h"

extern int yylex(void);
extern void yyerror(char const *s);
#define ERROR(loc, ...) \
	do { \
		srcpos_error((loc), "Error", __VA_ARGS__); \
		treesource_error = true; \
	} while (0)

#define YYERROR_CALL(msg) yyerror(msg)

extern struct dt_info *parser_output;
extern bool treesource_error;

static bool is_ref_relative(const char *ref)
{
	return ref[0] != '/' && strchr(&ref[1], '/');
}

%}

%union {
	char *propnodename;
	char *labelref;
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
	unsigned int flags;
}

%token DT_V1
%token DT_PLUGIN
%token DT_MEMRESERVE
%token DT_LSHIFT DT_RSHIFT DT_LE DT_GE DT_EQ DT_NE DT_AND DT_OR
%token DT_BITS
%token DT_DEL_PROP
%token DT_DEL_NODE
%token DT_OMIT_NO_REF
%token <propnodename> DT_PROPNODENAME
%token <integer> DT_LITERAL
%token <integer> DT_CHAR_LITERAL
%token <byte> DT_BYTE
%token <data> DT_STRING
%token <labelref> DT_LABEL
%token <labelref> DT_LABEL_REF
%token <labelref> DT_PATH_REF
%token DT_INCBIN

%type <data> propdata
%type <data> propdataprefix
%type <flags> header
%type <flags> headers
%type <re> memreserve
%type <re> memreserves
%type <array> arrayprefix
%type <data> bytestring
%type <prop> propdef
%type <proplist> proplist
%type <labelref> dt_ref

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
	  headers memreserves devicetree
		{
			parser_output = build_dt_info($1, $2, $3,
			                              guess_boot_cpuid($3));
		}
	;

header:
	  DT_V1 ';'
		{
			$$ = DTSF_V1;
		}
	| DT_V1 ';' DT_PLUGIN ';'
		{
			$$ = DTSF_V1 | DTSF_PLUGIN;
		}
	;

headers:
	  header
	| header headers
		{
			if ($2 != $1)
				ERROR(&@2, "Header flags don't match earlier ones");
			$$ = $1;
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

dt_ref: DT_LABEL_REF | DT_PATH_REF;

devicetree:
	  '/' nodedef
		{
			$$ = name_node($2, "");
		}
	| devicetree '/' nodedef
		{
			$$ = merge_nodes($1, $3);
		}
	| dt_ref nodedef
		{
			/*
			 * We rely on the rule being always:
			 *   versioninfo plugindecl memreserves devicetree
			 * so $-1 is what we want (plugindecl)
			 */
			if (!($<flags>-1 & DTSF_PLUGIN))
				ERROR(&@2, "Label or path %s not found", $1);
			else if (is_ref_relative($1))
				ERROR(&@2, "Label-relative reference %s not supported in plugin", $1);
			$$ = add_orphan_node(
					name_node(build_node(NULL, NULL, NULL),
						  ""),
					$2, $1);
		}
	| devicetree DT_LABEL dt_ref nodedef
		{
			struct node *target = get_node_by_ref($1, $3);

			if (($<flags>-1 & DTSF_PLUGIN) && is_ref_relative($3))
				ERROR(&@2, "Label-relative reference %s not supported in plugin", $3);

			if (target) {
				add_label(&target->labels, $2);
				merge_nodes(target, $4);
			} else
				ERROR(&@3, "Label or path %s not found", $3);
			$$ = $1;
		}
	| devicetree DT_PATH_REF nodedef
		{
			/*
			 * We rely on the rule being always:
			 *   versioninfo plugindecl memreserves devicetree
			 * so $-1 is what we want (plugindecl)
			 */
			if ($<flags>-1 & DTSF_PLUGIN) {
				if (is_ref_relative($2))
					ERROR(&@2, "Label-relative reference %s not supported in plugin", $2);
				add_orphan_node($1, $3, $2);
			} else {
				struct node *target = get_node_by_ref($1, $2);

				if (target)
					merge_nodes(target, $3);
				else
					ERROR(&@2, "Label or path %s not found", $2);
			}
			$$ = $1;
		}
	| devicetree DT_LABEL_REF nodedef
		{
			struct node *target = get_node_by_ref($1, $2);

			if (target) {
				merge_nodes(target, $3);
			} else {
				/*
				 * We rely on the rule being always:
				 *   versioninfo plugindecl memreserves devicetree
				 * so $-1 is what we want (plugindecl)
				 */
				if ($<flags>-1 & DTSF_PLUGIN)
					add_orphan_node($1, $3, $2);
				else
					ERROR(&@2, "Label or path %s not found", $2);
			}
			$$ = $1;
		}
	| devicetree DT_DEL_NODE dt_ref ';'
		{
			struct node *target = get_node_by_ref($1, $3);

			if (target)
				delete_node(target);
			else
				ERROR(&@3, "Label or path %s not found", $3);


			$$ = $1;
		}
	| devicetree DT_OMIT_NO_REF dt_ref ';'
		{
			struct node *target = get_node_by_ref($1, $3);

			if (target)
				omit_node_if_unused(target);
			else
				ERROR(&@3, "Label or path %s not found", $3);


			$$ = $1;
		}
	;

nodedef:
	  '{' proplist subnodes '}' ';'
		{
			$$ = build_node($2, $3, &@$);
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
			$$ = build_property($1, $3, &@$);
		}
	| DT_PROPNODENAME ';'
		{
			$$ = build_property($1, empty_data, &@$);
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
	| propdataprefix dt_ref
		{
			$1 = data_add_marker($1, TYPE_STRING, $2);
			$$ = data_add_marker($1, REF_PATH, $2);
		}
	| propdataprefix DT_INCBIN '(' DT_STRING ',' integer_prim ',' integer_prim ')'
		{
			FILE *f = srcfile_relative_open($4.val, NULL);
			struct data d;

			if ($6 != 0)
				if (fseek(f, $6, SEEK_SET) != 0)
					die("Couldn't seek to offset %llu in \"%s\": %s",
					    (unsigned long long)$6, $4.val,
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
			unsigned long long bits;
			enum markertype type = TYPE_UINT32;

			bits = $2;

			switch (bits) {
			case 8: type = TYPE_UINT8; break;
			case 16: type = TYPE_UINT16; break;
			case 32: type = TYPE_UINT32; break;
			case 64: type = TYPE_UINT64; break;
			default:
				ERROR(&@2, "Array elements must be"
				      " 8, 16, 32 or 64-bits");
				bits = 32;
			}

			$$.data = data_add_marker(empty_data, type, NULL);
			$$.bits = bits;
		}
	| '<'
		{
			$$.data = data_add_marker(empty_data, TYPE_UINT32, NULL);
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
					ERROR(&@2, "Value out of range for"
					      " %d-bit array element", $1.bits);
			}

			$$.data = data_append_integer($1.data, $2, $1.bits);
		}
	| arrayprefix dt_ref
		{
			uint64_t val = ~0ULL >> (64 - $1.bits);

			if ($1.bits == 32)
				$1.data = data_add_marker($1.data,
							  REF_PHANDLE,
							  $2);
			else
				ERROR(&@2, "References are only allowed in "
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
	| DT_CHAR_LITERAL
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
	  integer_shift DT_LSHIFT integer_add { $$ = ($3 < 64) ? ($1 << $3) : 0; }
	| integer_shift DT_RSHIFT integer_add { $$ = ($3 < 64) ? ($1 >> $3) : 0; }
	| integer_add
	;

integer_add:
	  integer_add '+' integer_mul { $$ = $1 + $3; }
	| integer_add '-' integer_mul { $$ = $1 - $3; }
	| integer_mul
	;

integer_mul:
	  integer_mul '*' integer_unary { $$ = $1 * $3; }
	| integer_mul '/' integer_unary
		{
			if ($3 != 0) {
				$$ = $1 / $3;
			} else {
				ERROR(&@$, "Division by zero");
				$$ = 0;
			}
		}
	| integer_mul '%' integer_unary
		{
			if ($3 != 0) {
				$$ = $1 % $3;
			} else {
				ERROR(&@$, "Division by zero");
				$$ = 0;
			}
		}
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
			$$ = data_add_marker(empty_data, TYPE_UINT8, NULL);
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
			ERROR(&@2, "Properties must precede subnodes");
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
			$$ = name_node(build_node_delete(&@$), $2);
		}
	| DT_OMIT_NO_REF subnode
		{
			$$ = omit_node_if_unused($2);
		}
	| DT_LABEL subnode
		{
			add_label(&$2->labels, $1);
			$$ = $2;
		}
	;

%%

void yyerror(char const *s)
{
	ERROR(&yylloc, "%s", s);
}
