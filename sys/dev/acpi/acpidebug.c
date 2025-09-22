/* $OpenBSD: acpidebug.c,v 1.35 2024/06/26 01:40:49 jsg Exp $ */
/*
 * Copyright (c) 2006 Marco Peereboom <marco@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <machine/bus.h>
#include <machine/db_machdep.h>
#include <ddb/db_output.h>
#include <ddb/db_extern.h>
#include <ddb/db_lex.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/acpidebug.h>
#include <dev/acpi/dsdt.h>

#ifdef DDB

extern int aml_pc(uint8_t *);

extern const char *aml_mnem(int opcode, uint8_t *);
extern const char *aml_nodename(struct aml_node *);
extern void aml_disasm(struct aml_scope *scope, int lvl,
    void (*dbprintf)(void *, const char *, ...),
    void *arg);

const char		*db_aml_objtype(struct aml_value *);
const char		*db_opregion(int);
int			db_parse_name(void);
void			db_aml_dump(int, uint8_t *);
void			db_aml_showvalue(struct aml_value *);
void			db_aml_walktree(struct aml_node *);
void			db_disprint(void *, const char *, ...);

/* name of scope for lexer */
char			scope[80];

const char *
db_opregion(int id)
{
	switch (id) {
	case 0:
		return "SystemMemory";
	case 1:
		return "SystemIO";
	case 2:
		return "PCIConfig";
	case 3:
		return "Embedded";
	case 4:
		return "SMBus";
	case 5:
		return "CMOS";
	case 6:
		return "PCIBAR";
	}
	return "";
}
void
db_aml_dump(int len, uint8_t *buf)
{
	int		idx;

	db_printf("{ ");
	for (idx = 0; idx < len; idx++)
		db_printf("%s0x%.2x", idx ? ", " : "", buf[idx]);
	db_printf(" }\n");
}

void
db_aml_showvalue(struct aml_value *value)
{
	int		idx;

	if (value == NULL)
		return;

	if (value->node)
		db_printf("[%s] ", aml_nodename(value->node));

	switch (value->type) {
	case AML_OBJTYPE_OBJREF:
		db_printf("refof: %x {\n", value->v_objref.index);
		db_aml_showvalue(value->v_objref.ref);
		db_printf("}\n");
		break;
	case AML_OBJTYPE_NAMEREF:
		db_printf("nameref: %s\n", value->v_nameref);
		break;
	case AML_OBJTYPE_INTEGER:
		db_printf("integer: %llx\n", value->v_integer);
		break;
	case AML_OBJTYPE_STRING:
		db_printf("string: %s\n", value->v_string);
		break;
	case AML_OBJTYPE_PACKAGE:
		db_printf("package: %d {\n", value->length);
		for (idx = 0; idx < value->length; idx++)
			db_aml_showvalue(value->v_package[idx]);
		db_printf("}\n");
		break;
	case AML_OBJTYPE_BUFFER:
		db_printf("buffer: %d ", value->length);
		db_aml_dump(value->length, value->v_buffer);
		break;
	case AML_OBJTYPE_DEBUGOBJ:
		db_printf("debug");
		break;
	case AML_OBJTYPE_MUTEX:
		db_printf("mutex : %llx\n", value->v_integer);
		break;
	case AML_OBJTYPE_DEVICE:
		db_printf("device\n");
		break;
	case AML_OBJTYPE_EVENT:
		db_printf("event\n");
		break;
	case AML_OBJTYPE_PROCESSOR:
		db_printf("cpu: %x,%x,%x\n",
		    value->v_processor.proc_id,
		    value->v_processor.proc_addr,
		    value->v_processor.proc_len);
		break;
	case AML_OBJTYPE_METHOD:
		db_printf("method: args=%d, serialized=%d, synclevel=%d\n",
		    AML_METHOD_ARGCOUNT(value->v_method.flags),
		    AML_METHOD_SERIALIZED(value->v_method.flags),
		    AML_METHOD_SYNCLEVEL(value->v_method.flags));
		break;
	case AML_OBJTYPE_FIELDUNIT:
		db_printf("%s: access=%x,lock=%x,update=%x pos=%.4x "
		    "len=%.4x\n",
		    aml_mnem(value->v_field.type, NULL),
		    AML_FIELD_ACCESS(value->v_field.flags),
		    AML_FIELD_LOCK(value->v_field.flags),
		    AML_FIELD_UPDATE(value->v_field.flags),
		    value->v_field.bitpos,
		    value->v_field.bitlen);
		if (value->v_field.ref2)
			db_printf("  index: %.3x %s\n",
			    value->v_field.ref3,
			    aml_nodename(value->v_field.ref2->node));
		if (value->v_field.ref1)
			db_printf("  data: %s\n",
			    aml_nodename(value->v_field.ref1->node));
		break;
	case AML_OBJTYPE_BUFFERFIELD:
		db_printf("%s: pos=%.4x len=%.4x\n",
		    aml_mnem(value->v_field.type, NULL),
		    value->v_field.bitpos,
		    value->v_field.bitlen);
		db_printf("  buffer: %s\n",
		    aml_nodename(value->v_field.ref1->node));
		break;
	case AML_OBJTYPE_OPREGION:
		db_printf("opregion: %s,0x%llx,0x%x\n",
		    db_opregion(value->v_opregion.iospace),
		    value->v_opregion.iobase,
		    value->v_opregion.iolen);
		break;
	default:
		db_printf("unknown: %d\n", value->type);
		break;
	}
}

const char *
db_aml_objtype(struct aml_value *val)
{
	if (val == NULL)
		return "nil";

	switch (val->type) {
	case AML_OBJTYPE_INTEGER:
		return "integer";
	case AML_OBJTYPE_STRING:
		return "string";
	case AML_OBJTYPE_BUFFER:
		return "buffer";
	case AML_OBJTYPE_PACKAGE:
		return "package";
	case AML_OBJTYPE_DEVICE:
		return "device";
	case AML_OBJTYPE_EVENT:
		return "event";
	case AML_OBJTYPE_METHOD:
		return "method";
	case AML_OBJTYPE_MUTEX:
		return "mutex";
	case AML_OBJTYPE_OPREGION:
		return "opregion";
	case AML_OBJTYPE_POWERRSRC:
		return "powerrsrc";
	case AML_OBJTYPE_PROCESSOR:
		return "processor";
	case AML_OBJTYPE_THERMZONE:
		return "thermzone";
	case AML_OBJTYPE_DDBHANDLE:
		return "ddbhandle";
	case AML_OBJTYPE_DEBUGOBJ:
		return "debugobj";
	case AML_OBJTYPE_NAMEREF:
		return "nameref";
	case AML_OBJTYPE_OBJREF:
		return "refof";
	case AML_OBJTYPE_FIELDUNIT:
	case AML_OBJTYPE_BUFFERFIELD:
		return aml_mnem(val->v_field.type, NULL);
	}

	return ("");
}

void
db_aml_walktree(struct aml_node *node)
{
	while (node) {
		db_aml_showvalue(node->value);
		db_aml_walktree(SIMPLEQ_FIRST(&node->son));
		node = SIMPLEQ_NEXT(node, sib);
	}
}

int
db_parse_name(void)
{
	int		t, rv = 1;

	memset(scope, 0, sizeof scope);
	do {
		t = db_read_token();
		if (t == tIDENT) {
			if (strlcat(scope, db_tok_string, sizeof scope) >=
			    sizeof scope) {
				printf("Input too long\n");
				goto error;
			}
			t = db_read_token();
			if (t == tDOT)
				if (strlcat(scope, ".", sizeof scope) >=
				    sizeof scope) {
					printf("Input too long 2\n");
					goto error;
				}
		}
	} while (t != tEOL);

	if (!strlen(scope)) {
		db_printf("Invalid input\n");
		goto error;
	}

	rv = 0;
error:
	/* get rid of the rest of input */
	db_flush_lex();
	return (rv);
}

/* ddb interface */
void
db_acpi_showval(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	struct aml_node *node;

	if (db_parse_name())
		return;

	node = aml_searchname(acpi_softc->sc_root, scope);
	if (node)
		db_aml_showvalue(node->value);
	else
		db_printf("Not a valid value\n");
}

void
db_disprint(void *arg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap,fmt);
	db_vprintf(fmt, ap);
	va_end(ap);
}

void
db_acpi_disasm(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	struct aml_node *node;

	if (db_parse_name())
		return;

	node = aml_searchname(acpi_softc->sc_root, scope);
	if (node && node->value && node->value->type == AML_OBJTYPE_METHOD) {
		struct aml_scope ns;

		memset(&ns, 0, sizeof(ns));
		ns.pos   = node->value->v_method.start;
		ns.end   = node->value->v_method.end;
		ns.node  = node;
		while (ns.pos < ns.end)
			aml_disasm(&ns, 0, db_disprint, 0);
	}
	else
		db_printf("Not a valid method\n");
}

void
db_acpi_tree(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	db_aml_walktree(acpi_softc->sc_root);
}

void
db_acpi_trace(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	struct aml_scope *root;
	struct aml_value *sp;
	int idx;
	extern struct aml_scope *aml_lastscope;

	for (root=aml_lastscope; root && root->pos; root=root->parent) {
		db_printf("%.4x Called: %s\n", aml_pc(root->pos),
		    aml_nodename(root->node));
		for (idx = 0; idx< AML_MAX_ARG; idx++) {
			sp = aml_getstack(root, AMLOP_ARG0 + idx);
			if (sp && sp->type) {
				db_printf("  arg%d: ", idx);
				db_aml_showvalue(sp);
		}
			}
		for (idx = 0; idx < AML_MAX_LOCAL; idx++) {
			sp = aml_getstack(root, AMLOP_LOCAL0 + idx);
			if (sp && sp->type) {
				db_printf("  local%d: ", idx);
				db_aml_showvalue(sp);
		}
	}
	}
}

#endif /* DDB */
