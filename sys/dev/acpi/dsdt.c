/* $OpenBSD: dsdt.c,v 1.275 2025/06/22 11:19:00 kettenis Exp $ */
/*
 * Copyright (c) 2005 Jordan Hargrave <jordan@openbsd.org>
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
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/time.h>

#include <machine/bus.h>

#ifdef DDB
#include <machine/db_machdep.h>
#endif

#include <dev/acpi/acpidev.h>
#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/i2c/i2cvar.h>

#ifdef SMALL_KERNEL
#undef ACPI_DEBUG
#endif

#define opsize(opcode) (((opcode) & 0xFF00) ? 2 : 1)

#define AML_FIELD_RESERVED	0x00
#define AML_FIELD_ATTRIB	0x01

#define AML_REVISION		0x01
#define AML_INTSTRLEN		16
#define AML_NAMESEG_LEN		4

struct aml_value	*aml_loadtable(struct acpi_softc *, const char *,
			    const char *, const char *, const char *,
			    const char *, struct aml_value *);
struct aml_scope	*aml_load(struct acpi_softc *, struct aml_scope *,
			    struct aml_value *, struct aml_value *);

void			aml_copyvalue(struct aml_value *, struct aml_value *);

void			aml_freevalue(struct aml_value *);
struct aml_value	*aml_allocvalue(int, int64_t, const void *);
struct aml_value	*_aml_setvalue(struct aml_value *, int, int64_t,
			    const void *);

uint64_t		aml_convradix(uint64_t, int, int);
uint64_t		aml_evalexpr(uint64_t, uint64_t, int);
int			aml_lsb(uint64_t);
int			aml_msb(uint64_t);

int			aml_tstbit(const uint8_t *, int);
void			aml_setbit(uint8_t *, int, int);

void			aml_addref(struct aml_value *, const char *);
void			aml_delref(struct aml_value **, const char *);

void			aml_bufcpy(void *, int, const void *, int, int);

int			aml_pc(uint8_t *);

struct aml_opcode	*aml_findopcode(int);

#define acpi_os_malloc(sz) _acpi_os_malloc(sz, __FUNCTION__, __LINE__)
#define acpi_os_free(ptr)  _acpi_os_free(ptr, __FUNCTION__, __LINE__)

void			*_acpi_os_malloc(size_t, const char *, int);
void			_acpi_os_free(void *, const char *, int);
void			acpi_stall(int);

struct aml_value	*aml_callosi(struct aml_scope *, struct aml_value *);

const char		*aml_getname(const char *);
int64_t			aml_hextoint(const char *);
void			aml_dump(int, uint8_t *);
__dead void		_aml_die(const char *fn, int line, const char *fmt, ...);
#define aml_die(x...)	_aml_die(__FUNCTION__, __LINE__, x)

void aml_notify_task(void *, int);
void acpi_poll_notify_task(void *, int);

/*
 * @@@: Global variables
 */
int			aml_intlen = 64;
struct aml_node		aml_root;
struct aml_value	*aml_global_lock;

/* Perfect Hash key */
#define HASH_OFF		6904
#define HASH_SIZE		179
#define HASH_KEY(k)		(((k) ^ HASH_OFF) % HASH_SIZE)

/*
 * XXX this array should be sorted, and then aml_findopcode() should
 * do a binary search
 */
struct aml_opcode **aml_ophash;
struct aml_opcode aml_table[] = {
	/* Simple types */
	{ AMLOP_ZERO,		"Zero",		"c",	},
	{ AMLOP_ONE,		"One",		"c",	},
	{ AMLOP_ONES,		"Ones",		"c",	},
	{ AMLOP_REVISION,	"Revision",	"R",	},
	{ AMLOP_BYTEPREFIX,	".Byte",	"b",	},
	{ AMLOP_WORDPREFIX,	".Word",	"w",	},
	{ AMLOP_DWORDPREFIX,	".DWord",	"d",	},
	{ AMLOP_QWORDPREFIX,	".QWord",	"q",	},
	{ AMLOP_STRINGPREFIX,	".String",	"a",	},
	{ AMLOP_DEBUG,		"DebugOp",	"D",	},
	{ AMLOP_BUFFER,		"Buffer",	"piB",	},
	{ AMLOP_PACKAGE,	"Package",	"pbT",	},
	{ AMLOP_VARPACKAGE,	"VarPackage",	"piT",	},

	/* Simple objects */
	{ AMLOP_LOCAL0,		"Local0",	"L",	},
	{ AMLOP_LOCAL1,		"Local1",	"L",	},
	{ AMLOP_LOCAL2,		"Local2",	"L",	},
	{ AMLOP_LOCAL3,		"Local3",	"L",	},
	{ AMLOP_LOCAL4,		"Local4",	"L",	},
	{ AMLOP_LOCAL5,		"Local5",	"L",	},
	{ AMLOP_LOCAL6,		"Local6",	"L",	},
	{ AMLOP_LOCAL7,		"Local7",	"L",	},
	{ AMLOP_ARG0,		"Arg0",		"A",	},
	{ AMLOP_ARG1,		"Arg1",		"A",	},
	{ AMLOP_ARG2,		"Arg2",		"A",	},
	{ AMLOP_ARG3,		"Arg3",		"A",	},
	{ AMLOP_ARG4,		"Arg4",		"A",	},
	{ AMLOP_ARG5,		"Arg5",		"A",	},
	{ AMLOP_ARG6,		"Arg6",		"A",	},

	/* Control flow */
	{ AMLOP_IF,		"If",		"piI",	},
	{ AMLOP_ELSE,		"Else",		"pT" },
	{ AMLOP_WHILE,		"While",	"piT",	},
	{ AMLOP_BREAK,		"Break",	"" },
	{ AMLOP_CONTINUE,	"Continue",	"" },
	{ AMLOP_RETURN,		"Return",	"t",	},
	{ AMLOP_FATAL,		"Fatal",	"bdi",	},
	{ AMLOP_NOP,		"Nop",		"",	},
	{ AMLOP_BREAKPOINT,	"BreakPoint",	"",     },

	/* Arithmetic operations */
	{ AMLOP_INCREMENT,	"Increment",	"S",	},
	{ AMLOP_DECREMENT,	"Decrement",	"S",	},
	{ AMLOP_ADD,		"Add",		"iir",	},
	{ AMLOP_SUBTRACT,	"Subtract",	"iir",	},
	{ AMLOP_MULTIPLY,	"Multiply",	"iir",	},
	{ AMLOP_DIVIDE,		"Divide",	"iirr",	},
	{ AMLOP_SHL,		"ShiftLeft",	"iir",	},
	{ AMLOP_SHR,		"ShiftRight",	"iir",	},
	{ AMLOP_AND,		"And",		"iir",	},
	{ AMLOP_NAND,		"Nand",		"iir",	},
	{ AMLOP_OR,		"Or",		"iir",	},
	{ AMLOP_NOR,		"Nor",		"iir",	},
	{ AMLOP_XOR,		"Xor",		"iir",	},
	{ AMLOP_NOT,		"Not",		"ir",	},
	{ AMLOP_MOD,		"Mod",		"iir",	},
	{ AMLOP_FINDSETLEFTBIT,	"FindSetLeftBit", "ir",	},
	{ AMLOP_FINDSETRIGHTBIT,"FindSetRightBit", "ir",},

	/* Logical test operations */
	{ AMLOP_LAND,		"LAnd",		"ii",	},
	{ AMLOP_LOR,		"LOr",		"ii",	},
	{ AMLOP_LNOT,		"LNot",		"i",	},
	{ AMLOP_LNOTEQUAL,	"LNotEqual",	"tt",	},
	{ AMLOP_LLESSEQUAL,	"LLessEqual",	"tt",	},
	{ AMLOP_LGREATEREQUAL,	"LGreaterEqual", "tt",	},
	{ AMLOP_LEQUAL,		"LEqual",	"tt",	},
	{ AMLOP_LGREATER,	"LGreater",	"tt",	},
	{ AMLOP_LLESS,		"LLess",	"tt",	},

	/* Named objects */
	{ AMLOP_NAMECHAR,	".NameRef",	"n",	},
	{ AMLOP_ALIAS,		"Alias",	"nN",	},
	{ AMLOP_NAME,		"Name",	"Nt",	},
	{ AMLOP_EVENT,		"Event",	"N",	},
	{ AMLOP_MUTEX,		"Mutex",	"Nb",	},
	{ AMLOP_DATAREGION,	"DataRegion",	"Nttt",	},
	{ AMLOP_OPREGION,	"OpRegion",	"Nbii",	},
	{ AMLOP_SCOPE,		"Scope",	"pnT",	},
	{ AMLOP_DEVICE,		"Device",	"pNT",	},
	{ AMLOP_POWERRSRC,	"Power Resource", "pNbwT",},
	{ AMLOP_THERMALZONE,	"ThermalZone",	"pNT",	},
	{ AMLOP_PROCESSOR,	"Processor",	"pNbdbT", },
	{ AMLOP_METHOD,		"Method",	"pNbM",	},

	/* Field operations */
	{ AMLOP_FIELD,		"Field",	"pnbF",	},
	{ AMLOP_INDEXFIELD,	"IndexField",	"pnnbF",},
	{ AMLOP_BANKFIELD,	"BankField",	"pnnibF",},
	{ AMLOP_CREATEFIELD,	"CreateField",	"tiiN",		},
	{ AMLOP_CREATEQWORDFIELD, "CreateQWordField","tiN",},
	{ AMLOP_CREATEDWORDFIELD, "CreateDWordField","tiN",},
	{ AMLOP_CREATEWORDFIELD, "CreateWordField", "tiN",},
	{ AMLOP_CREATEBYTEFIELD, "CreateByteField", "tiN",},
	{ AMLOP_CREATEBITFIELD,	"CreateBitField", "tiN",	},

	/* Conversion operations */
	{ AMLOP_TOINTEGER,	"ToInteger",	"tr",	},
	{ AMLOP_TOBUFFER,	"ToBuffer",	"tr",	},
	{ AMLOP_TODECSTRING,	"ToDecString",	"tr",	},
	{ AMLOP_TOHEXSTRING,	"ToHexString",	"tr",	},
	{ AMLOP_TOSTRING,	"ToString",	"tir",	},
	{ AMLOP_MID,		"Mid",		"tiir",	},
	{ AMLOP_FROMBCD,	"FromBCD",	"ir",	},
	{ AMLOP_TOBCD,		"ToBCD",	"ir",	},

	/* Mutex/Signal operations */
	{ AMLOP_ACQUIRE,	"Acquire",	"Sw",	},
	{ AMLOP_RELEASE,	"Release",	"S",	},
	{ AMLOP_SIGNAL,		"Signal",	"S",	},
	{ AMLOP_WAIT,		"Wait",		"Si",	},
	{ AMLOP_RESET,		"Reset",	"S",	},

	{ AMLOP_INDEX,		"Index",	"tir",	},
	{ AMLOP_DEREFOF,	"DerefOf",	"t",	},
	{ AMLOP_REFOF,		"RefOf",	"S",	},
	{ AMLOP_CONDREFOF,	"CondRef",	"Sr",	},

	{ AMLOP_LOADTABLE,	"LoadTable",	"tttttt" },
	{ AMLOP_STALL,		"Stall",	"i",	},
	{ AMLOP_SLEEP,		"Sleep",	"i",	},
	{ AMLOP_TIMER,		"Timer",	"",	},
	{ AMLOP_LOAD,		"Load",		"nS",	},
	{ AMLOP_UNLOAD,		"Unload",	"t" },
	{ AMLOP_STORE,		"Store",	"tS",	},
	{ AMLOP_CONCAT,		"Concat",	"ttr",	},
	{ AMLOP_CONCATRES,	"ConcatRes",	"ttt" },
	{ AMLOP_NOTIFY,		"Notify",	"Si",	},
	{ AMLOP_SIZEOF,		"Sizeof",	"S",	},
	{ AMLOP_MATCH,		"Match",	"tbibii", },
	{ AMLOP_OBJECTTYPE,	"ObjectType",	"S",	},
	{ AMLOP_COPYOBJECT,	"CopyObject",	"tS",	},
};

int
aml_pc(uint8_t *src)
{
	return src - aml_root.start;
}

struct aml_scope *aml_lastscope;

void
_aml_die(const char *fn, int line, const char *fmt, ...)
{
#ifndef SMALL_KERNEL
	struct aml_scope *root;
	struct aml_value *sp;
	int idx;
#endif /* SMALL_KERNEL */
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);

#ifndef SMALL_KERNEL
	for (root = aml_lastscope; root && root->pos; root = root->parent) {
		printf("%.4x Called: %s\n", aml_pc(root->pos),
		    aml_nodename(root->node));
		for (idx = 0; idx < AML_MAX_ARG; idx++) {
			sp = aml_getstack(root, AMLOP_ARG0+idx);
			if (sp && sp->type) {
				printf("  arg%d: ", idx);
				aml_showvalue(sp);
			}
		}
		for (idx = 0; idx < AML_MAX_LOCAL; idx++) {
			sp = aml_getstack(root, AMLOP_LOCAL0+idx);
			if (sp && sp->type) {
				printf("  local%d: ", idx);
				aml_showvalue(sp);
			}
		}
	}
#endif /* SMALL_KERNEL */

	/* XXX: don't panic */
	panic("aml_die %s:%d", fn, line);
}

void
aml_hashopcodes(void)
{
	int i;

	/* Dynamically allocate hash table */
	aml_ophash = (struct aml_opcode **)acpi_os_malloc(HASH_SIZE *
	    sizeof(struct aml_opcode *));
	for (i = 0; i < sizeof(aml_table) / sizeof(aml_table[0]); i++)
		aml_ophash[HASH_KEY(aml_table[i].opcode)] = &aml_table[i];
}

struct aml_opcode *
aml_findopcode(int opcode)
{
	struct aml_opcode *hop;

	hop = aml_ophash[HASH_KEY(opcode)];
	if (hop && hop->opcode == opcode)
		return hop;
	return NULL;
}

#if defined(DDB) || !defined(SMALL_KERNEL)
const char *
aml_mnem(int opcode, uint8_t *pos)
{
	struct aml_opcode *tab;
	static char mnemstr[32];

	if ((tab = aml_findopcode(opcode)) != NULL) {
		strlcpy(mnemstr, tab->mnem, sizeof(mnemstr));
		if (pos != NULL) {
			switch (opcode) {
			case AMLOP_STRINGPREFIX:
				snprintf(mnemstr, sizeof(mnemstr), "\"%s\"", pos);
				break;
			case AMLOP_BYTEPREFIX:
				snprintf(mnemstr, sizeof(mnemstr), "0x%.2x",
					 *(uint8_t *)pos);
				break;
			case AMLOP_WORDPREFIX:
				snprintf(mnemstr, sizeof(mnemstr), "0x%.4x",
					 *(uint16_t *)pos);
				break;
			case AMLOP_DWORDPREFIX:
				snprintf(mnemstr, sizeof(mnemstr), "0x%.4x",
					 *(uint16_t *)pos);
				break;
			case AMLOP_NAMECHAR:
				strlcpy(mnemstr, aml_getname(pos), sizeof(mnemstr));
				break;
			}
		}
		return mnemstr;
	}
	return ("xxx");
}
#endif /* defined(DDB) || !defined(SMALL_KERNEL) */

struct aml_notify_data {
	struct aml_node		*node;
	char			pnpid[20];
	void			*cbarg;
	int			(*cbproc)(struct aml_node *, int, void *);
	int			flags;

	SLIST_ENTRY(aml_notify_data) link;
};

SLIST_HEAD(aml_notify_head, aml_notify_data);
struct aml_notify_head aml_notify_list =
    SLIST_HEAD_INITIALIZER(aml_notify_list);

/*
 *  @@@: Memory management functions
 */

long acpi_nalloc;

struct acpi_memblock {
	size_t size;
#ifdef ACPI_MEMDEBUG
	const char *fn;
	int         line;
	int         sig;
	LIST_ENTRY(acpi_memblock) link;
#endif
};

#ifdef ACPI_MEMDEBUG
LIST_HEAD(, acpi_memblock)	acpi_memhead;
int				acpi_memsig;

int
acpi_walkmem(int sig, const char *lbl)
{
	struct acpi_memblock *sptr;

	printf("--- walkmem:%s %x --- %lx bytes alloced\n", lbl, sig,
	    acpi_nalloc);
	LIST_FOREACH(sptr, &acpi_memhead, link) {
		if (sptr->sig < sig)
			break;
		printf("%.4x Alloc %.8lx bytes @ %s:%d\n",
			sptr->sig, sptr->size, sptr->fn, sptr->line);
	}
	return acpi_memsig;
}
#endif /* ACPI_MEMDEBUG */

void *
_acpi_os_malloc(size_t size, const char *fn, int line)
{
	struct acpi_memblock *sptr;

	sptr = malloc(size+sizeof(*sptr), M_ACPI, M_WAITOK | M_ZERO);
	dnprintf(99, "alloc: %p %s:%d\n", sptr, fn, line);
	acpi_nalloc += size;
	sptr->size = size;
#ifdef ACPI_MEMDEBUG
	sptr->line = line;
	sptr->fn = fn;
	sptr->sig = ++acpi_memsig;

	LIST_INSERT_HEAD(&acpi_memhead, sptr, link);
#endif

	return &sptr[1];
}

void
_acpi_os_free(void *ptr, const char *fn, int line)
{
	struct acpi_memblock *sptr;

	if (ptr != NULL) {
		sptr = &(((struct acpi_memblock *)ptr)[-1]);
		acpi_nalloc -= sptr->size;

#ifdef ACPI_MEMDEBUG
		LIST_REMOVE(sptr, link);
#endif

		dnprintf(99, "free: %p %s:%d\n", sptr, fn, line);
		free(sptr, M_ACPI, sizeof(*sptr) + sptr->size);
	}
}

void
acpi_sleep(int ms, char *reason)
{
	static int acpinowait;

	/* XXX ACPI integers are supposed to be unsigned. */
	ms = MAX(1, ms);

	if (cold)
		delay(ms * 1000);
	else
		tsleep_nsec(&acpinowait, PWAIT, reason, MSEC_TO_NSEC(ms));
}

void
acpi_stall(int us)
{
	delay(us);
}

/*
 * @@@: Misc utility functions
 */

#ifdef ACPI_DEBUG
void
aml_dump(int len, uint8_t *buf)
{
	int		idx;

	dnprintf(50, "{ ");
	for (idx = 0; idx < len; idx++) {
		dnprintf(50, "%s0x%.2x", idx ? ", " : "", buf[idx]);
	}
	dnprintf(50, " }\n");
}
#endif

/* Bit mangling code */
int
aml_tstbit(const uint8_t *pb, int bit)
{
	pb += aml_bytepos(bit);

	return (*pb & aml_bitmask(bit));
}

void
aml_setbit(uint8_t *pb, int bit, int val)
{
	pb += aml_bytepos(bit);

	if (val)
		*pb |= aml_bitmask(bit);
	else
		*pb &= ~aml_bitmask(bit);
}

/*
 * @@@: Notify functions
 */
void
acpi_poll(void *arg)
{
	int s;

	s = splbio();
	acpi_addtask(acpi_softc, acpi_poll_notify_task, NULL, 0);
	acpi_softc->sc_threadwaiting = 0;
	wakeup(acpi_softc);
	splx(s);

	timeout_add_sec(&acpi_softc->sc_dev_timeout, 10);
}

void
aml_notify_task(void *node, int notify_value)
{
	struct aml_notify_data	*pdata = NULL;

	dnprintf(10,"run notify: %s %x\n", aml_nodename(node), notify_value);
	SLIST_FOREACH(pdata, &aml_notify_list, link)
		if (pdata->node == node)
			pdata->cbproc(pdata->node, notify_value, pdata->cbarg);
}

void
aml_register_notify(struct aml_node *node, const char *pnpid,
    int (*proc)(struct aml_node *, int, void *), void *arg, int flags)
{
	struct aml_notify_data	*pdata;
	extern int acpi_poll_enabled;

	dnprintf(10, "aml_register_notify: %s %s %p\n",
	    node->name, pnpid ? pnpid : "", proc);

	pdata = acpi_os_malloc(sizeof(struct aml_notify_data));
	pdata->node = node;
	pdata->cbarg = arg;
	pdata->cbproc = proc;
	pdata->flags = flags;

	if (pnpid)
		strlcpy(pdata->pnpid, pnpid, sizeof(pdata->pnpid));

	SLIST_INSERT_HEAD(&aml_notify_list, pdata, link);

	if ((flags & ACPIDEV_POLL) && !acpi_poll_enabled)
		timeout_add_sec(&acpi_softc->sc_dev_timeout, 10);
}

void
aml_notify(struct aml_node *node, int notify_value)
{
	struct aml_notify_data *pdata;

	if (node == NULL)
		return;

	SLIST_FOREACH(pdata, &aml_notify_list, link) {
		if (pdata->node == node && (pdata->flags & ACPIDEV_WAKEUP))
			acpi_softc->sc_wakeup = 1;
	}

	dnprintf(10,"queue notify: %s %x\n", aml_nodename(node), notify_value);
	acpi_addtask(acpi_softc, aml_notify_task, node, notify_value);
}

void
aml_notify_dev(const char *pnpid, int notify_value)
{
	struct aml_notify_data	*pdata = NULL;

	if (pnpid == NULL)
		return;

	SLIST_FOREACH(pdata, &aml_notify_list, link)
		if (strcmp(pdata->pnpid, pnpid) == 0)
			pdata->cbproc(pdata->node, notify_value, pdata->cbarg);
}

void
acpi_poll_notify_task(void *arg0, int arg1)
{
	struct aml_notify_data	*pdata = NULL;

	SLIST_FOREACH(pdata, &aml_notify_list, link)
		if (pdata->cbproc && (pdata->flags & ACPIDEV_POLL))
			pdata->cbproc(pdata->node, 0, pdata->cbarg);
}

/*
 * @@@: Namespace functions
 */

struct aml_node *__aml_search(struct aml_node *, uint8_t *, int);
struct aml_node *__aml_searchname(struct aml_node *, const void *, int);
void aml_delchildren(struct aml_node *);


/* Search for a name in children nodes */
struct aml_node *
__aml_search(struct aml_node *root, uint8_t *nameseg, int create)
{
	struct aml_node *node;

	/* XXX: Replace with SLIST/SIMPLEQ routines */
	if (root == NULL)
		return NULL;
	SIMPLEQ_FOREACH(node, &root->son, sib) {
		if (!strncmp(node->name, nameseg, AML_NAMESEG_LEN))
			return node;
	}
	if (create) {
		node = acpi_os_malloc(sizeof(struct aml_node));
		memcpy((void *)node->name, nameseg, AML_NAMESEG_LEN);
		node->value = aml_allocvalue(0,0,NULL);
		node->value->node = node;
		node->parent = root;

		SIMPLEQ_INIT(&node->son);
		SIMPLEQ_INSERT_TAIL(&root->son, node, sib);
		return node;
	}
	return NULL;
}

/* Get absolute pathname of AML node */
const char *
aml_nodename(struct aml_node *node)
{
	static char namebuf[128];

	namebuf[0] = 0;
	if (node) {
		aml_nodename(node->parent);
		if (node->parent != &aml_root)
			strlcat(namebuf, ".", sizeof(namebuf));
		strlcat(namebuf, node->name, sizeof(namebuf));
		return namebuf+1;
	}
	return namebuf;
}

const char *
aml_getname(const char *name)
{
	static char namebuf[128], *p;
	int count;

	p = namebuf;
	while (*name == AMLOP_ROOTCHAR || *name == AMLOP_PARENTPREFIX)
		*(p++) = *(name++);
	switch (*name) {
	case 0x00:
		count = 0;
		break;
	case AMLOP_MULTINAMEPREFIX:
		count = name[1];
		name += 2;
		break;
	case AMLOP_DUALNAMEPREFIX:
		count = 2;
		name += 1;
		break;
	default:
		count = 1;
	}
	while (count--) {
		memcpy(p, name, 4);
		p[4] = '.';
		p += 5;
		name += 4;
		if (*name == '.') name++;
	}
	*(--p) = 0;
	return namebuf;
}

/* Free all children nodes/values */
void
aml_delchildren(struct aml_node *node)
{
	struct aml_node *onode;

	if (node == NULL)
		return;
	while ((onode = SIMPLEQ_FIRST(&node->son)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&node->son, sib);

		aml_delchildren(onode);

		/* Don't delete values that have references */
		if (onode->value && onode->value->refcnt > 1)
			onode->value->node = NULL;

		/* Decrease reference count */
		aml_delref(&onode->value, "");

		/* Delete node */
		acpi_os_free(onode);
	}
}

/*
 * @@@: Value functions
 */

/*
 * Field I/O code
 */
void aml_unlockfield(struct aml_scope *, struct aml_value *);
void aml_lockfield(struct aml_scope *, struct aml_value *);

static long global_lock_count = 0;

void
acpi_glk_enter(void)
{
	int st = 0;

	/* If lock is already ours, just continue. */
	if (global_lock_count++)
		return;

	/* Spin to acquire the lock. */
	while (!st) {
		st = acpi_acquire_glk(&acpi_softc->sc_facs->global_lock);
		/* XXX - yield/delay? */
	}
}

void
acpi_glk_leave(void)
{
	int st, x;

	/* If we are the last one, turn out the lights. */
	if (--global_lock_count)
		return;

	st = acpi_release_glk(&acpi_softc->sc_facs->global_lock);
	if (!st)
		return;

	/*
	 * If pending, notify the BIOS that the lock was released by
	 * OSPM.  No locking is needed because nobody outside the ACPI
	 * thread is supposed to touch this register.
	 */
	x = acpi_read_pmreg(acpi_softc, ACPIREG_PM1_CNT, 0);
	x |= ACPI_PM1_GBL_RLS;
	acpi_write_pmreg(acpi_softc, ACPIREG_PM1_CNT, 0, x);
}

void
aml_lockfield(struct aml_scope *scope, struct aml_value *field)
{
	if (AML_FIELD_LOCK(field->v_field.flags) != AML_FIELD_LOCK_ON)
		return;

	acpi_glk_enter();
}

void
aml_unlockfield(struct aml_scope *scope, struct aml_value *field)
{
	if (AML_FIELD_LOCK(field->v_field.flags) != AML_FIELD_LOCK_ON)
		return;

	acpi_glk_leave();
}

/*
 * @@@: Value set/compare/alloc/free routines
 */

#ifndef SMALL_KERNEL
void
aml_showvalue(struct aml_value *val)
{
	int idx;

	if (val == NULL)
		return;

	if (val->node)
		printf(" [%s]", aml_nodename(val->node));
	printf(" %p cnt:%.2x stk:%.2x", val, val->refcnt, val->stack);
	switch (val->type) {
	case AML_OBJTYPE_INTEGER:
		printf(" integer: %llx\n", val->v_integer);
		break;
	case AML_OBJTYPE_STRING:
		printf(" string: %s\n", val->v_string);
		break;
	case AML_OBJTYPE_METHOD:
		printf(" method: %.2x\n", val->v_method.flags);
		break;
	case AML_OBJTYPE_PACKAGE:
		printf(" package: %.2x\n", val->length);
		for (idx = 0; idx < val->length; idx++)
			aml_showvalue(val->v_package[idx]);
		break;
	case AML_OBJTYPE_BUFFER:
		printf(" buffer: %.2x {", val->length);
		for (idx = 0; idx < val->length; idx++)
			printf("%s%.2x", idx ? ", " : "", val->v_buffer[idx]);
		printf("}\n");
		break;
	case AML_OBJTYPE_FIELDUNIT:
	case AML_OBJTYPE_BUFFERFIELD:
		printf(" field: bitpos=%.4x bitlen=%.4x ref1:%p ref2:%p [%s]\n",
		    val->v_field.bitpos, val->v_field.bitlen,
		    val->v_field.ref1, val->v_field.ref2,
		    aml_mnem(val->v_field.type, NULL));
		if (val->v_field.ref1)
			printf("  ref1: %s\n", aml_nodename(val->v_field.ref1->node));
		if (val->v_field.ref2)
			printf("  ref2: %s\n", aml_nodename(val->v_field.ref2->node));
		break;
	case AML_OBJTYPE_MUTEX:
		printf(" mutex: %s ref: %d\n",
		    val->v_mutex ?  val->v_mutex->amt_name : "",
		    val->v_mutex ?  val->v_mutex->amt_ref_count : 0);
		break;
	case AML_OBJTYPE_EVENT:
		printf(" event:\n");
		break;
	case AML_OBJTYPE_OPREGION:
		printf(" opregion: %.2x,%.8llx,%x\n",
		    val->v_opregion.iospace, val->v_opregion.iobase,
		    val->v_opregion.iolen);
		break;
	case AML_OBJTYPE_NAMEREF:
		printf(" nameref: %s\n", aml_getname(val->v_nameref));
		break;
	case AML_OBJTYPE_DEVICE:
		printf(" device:\n");
		break;
	case AML_OBJTYPE_PROCESSOR:
		printf(" cpu: %.2x,%.4x,%.2x\n",
		    val->v_processor.proc_id, val->v_processor.proc_addr,
		    val->v_processor.proc_len);
		break;
	case AML_OBJTYPE_THERMZONE:
		printf(" thermzone:\n");
		break;
	case AML_OBJTYPE_POWERRSRC:
		printf(" pwrrsrc: %.2x,%.2x\n",
		    val->v_powerrsrc.pwr_level, val->v_powerrsrc.pwr_order);
		break;
	case AML_OBJTYPE_OBJREF:
		printf(" objref: %p index:%x opcode:%s\n", val->v_objref.ref,
		    val->v_objref.index, aml_mnem(val->v_objref.type, 0));
		aml_showvalue(val->v_objref.ref);
		break;
	default:
		printf(" !!type: %x\n", val->type);
	}
}
#endif /* SMALL_KERNEL */

int64_t
aml_val2int(struct aml_value *rval)
{
	int64_t ival = 0;

	if (rval == NULL) {
		dnprintf(50, "null val2int\n");
		return (0);
	}
	switch (rval->type) {
	case AML_OBJTYPE_INTEGER:
		ival = rval->v_integer;
		break;
	case AML_OBJTYPE_BUFFER:
		aml_bufcpy(&ival, 0, rval->v_buffer, 0,
		    min(aml_intlen, rval->length*8));
		break;
	case AML_OBJTYPE_STRING:
		ival = aml_hextoint(rval->v_string);
		break;
	}
	return (ival);
}

/* Sets value into LHS: lhs must already be cleared */
struct aml_value *
_aml_setvalue(struct aml_value *lhs, int type, int64_t ival, const void *bval)
{
	memset(&lhs->_, 0x0, sizeof(lhs->_));

	lhs->type = type;
	switch (lhs->type) {
	case AML_OBJTYPE_INTEGER:
		lhs->length = aml_intlen>>3;
		lhs->v_integer = ival;
		break;
	case AML_OBJTYPE_METHOD:
		lhs->v_method.flags = ival;
		lhs->v_method.fneval = bval;
		break;
	case AML_OBJTYPE_NAMEREF:
		lhs->v_nameref = (uint8_t *)bval;
		break;
	case AML_OBJTYPE_OBJREF:
		lhs->v_objref.type = ival;
		lhs->v_objref.ref = (struct aml_value *)bval;
		break;
	case AML_OBJTYPE_BUFFER:
		lhs->length = ival;
		lhs->v_buffer = (uint8_t *)acpi_os_malloc(ival);
		if (bval)
			memcpy(lhs->v_buffer, bval, ival);
		break;
	case AML_OBJTYPE_STRING:
		if (ival == -1)
			ival = strlen((const char *)bval);
		lhs->length = ival;
		lhs->v_string = (char *)acpi_os_malloc(ival+1);
		if (bval)
			strncpy(lhs->v_string, (const char *)bval, ival);
		break;
	case AML_OBJTYPE_PACKAGE:
		lhs->length = ival;
		lhs->v_package = (struct aml_value **)acpi_os_malloc(ival *
		    sizeof(struct aml_value *));
		for (ival = 0; ival < lhs->length; ival++)
			lhs->v_package[ival] = aml_allocvalue(
			    AML_OBJTYPE_UNINITIALIZED, 0, NULL);
		break;
	}
	return lhs;
}

/* Copy object to another value: lhs must already be cleared */
void
aml_copyvalue(struct aml_value *lhs, struct aml_value *rhs)
{
	int idx;

	lhs->type = rhs->type;
	switch (lhs->type) {
	case AML_OBJTYPE_UNINITIALIZED:
		break;
	case AML_OBJTYPE_INTEGER:
		lhs->length = aml_intlen>>3;
		lhs->v_integer = rhs->v_integer;
		break;
	case AML_OBJTYPE_MUTEX:
		lhs->v_mutex = rhs->v_mutex;
		break;
	case AML_OBJTYPE_POWERRSRC:
		lhs->node = rhs->node;
		lhs->v_powerrsrc = rhs->v_powerrsrc;
		break;
	case AML_OBJTYPE_METHOD:
		lhs->v_method = rhs->v_method;
		break;
	case AML_OBJTYPE_BUFFER:
		_aml_setvalue(lhs, rhs->type, rhs->length, rhs->v_buffer);
		break;
	case AML_OBJTYPE_STRING:
		_aml_setvalue(lhs, rhs->type, rhs->length, rhs->v_string);
		break;
	case AML_OBJTYPE_OPREGION:
		lhs->v_opregion = rhs->v_opregion;
		break;
	case AML_OBJTYPE_PROCESSOR:
		lhs->node = rhs->node;
		lhs->v_processor = rhs->v_processor;
		break;
	case AML_OBJTYPE_NAMEREF:
		lhs->v_nameref = rhs->v_nameref;
		break;
	case AML_OBJTYPE_PACKAGE:
		_aml_setvalue(lhs, rhs->type, rhs->length, NULL);
		for (idx = 0; idx < rhs->length; idx++)
			aml_copyvalue(lhs->v_package[idx], rhs->v_package[idx]);
		break;
	case AML_OBJTYPE_OBJREF:
		lhs->v_objref = rhs->v_objref;
		aml_addref(lhs->v_objref.ref, "");
		break;
	case AML_OBJTYPE_DEVICE:
	case AML_OBJTYPE_THERMZONE:
		lhs->node = rhs->node;
		break;
	default:
		printf("copyvalue: %x", rhs->type);
		break;
	}
}

/* Allocate dynamic AML value
 *   type : Type of object to allocate (AML_OBJTYPE_XXXX)
 *   ival : Integer value (action depends on type)
 *   bval : Buffer value (action depends on type)
 */
struct aml_value *
aml_allocvalue(int type, int64_t ival, const void *bval)
{
	struct aml_value *rv;

	rv = (struct aml_value *)acpi_os_malloc(sizeof(struct aml_value));
	if (rv != NULL) {
		aml_addref(rv, "");
		return _aml_setvalue(rv, type, ival, bval);
	}
	return NULL;
}

void
aml_freevalue(struct aml_value *val)
{
	int idx;

	if (val == NULL)
		return;
	switch (val->type) {
	case AML_OBJTYPE_STRING:
		acpi_os_free(val->v_string);
		break;
	case AML_OBJTYPE_BUFFER:
		acpi_os_free(val->v_buffer);
		break;
	case AML_OBJTYPE_PACKAGE:
		for (idx = 0; idx < val->length; idx++)
			aml_delref(&val->v_package[idx], "");
		acpi_os_free(val->v_package);
		break;
	case AML_OBJTYPE_OBJREF:
		aml_delref(&val->v_objref.ref, "");
		break;
	case AML_OBJTYPE_BUFFERFIELD:
	case AML_OBJTYPE_FIELDUNIT:
		aml_delref(&val->v_field.ref1, "");
		aml_delref(&val->v_field.ref2, "");
		break;
	}
	val->type = 0;
	memset(&val->_, 0, sizeof(val->_));
}

/*
 * @@@: Math eval routines
 */

/* Convert number from one radix to another
 * Used in BCD conversion routines */
uint64_t
aml_convradix(uint64_t val, int iradix, int oradix)
{
	uint64_t rv = 0, pwr;

	rv = 0;
	pwr = 1;
	while (val) {
		rv += (val % iradix) * pwr;
		val /= iradix;
		pwr *= oradix;
	}
	return rv;
}

/* Calculate LSB */
int
aml_lsb(uint64_t val)
{
	int		lsb;

	if (val == 0)
		return (0);

	for (lsb = 1; !(val & 0x1); lsb++)
		val >>= 1;

	return (lsb);
}

/* Calculate MSB */
int
aml_msb(uint64_t val)
{
	int		msb;

	if (val == 0)
		return (0);

	for (msb = 1; val != 0x1; msb++)
		val >>= 1;

	return (msb);
}

/* Evaluate Math operands */
uint64_t
aml_evalexpr(uint64_t lhs, uint64_t rhs, int opcode)
{
	uint64_t res = 0;

	switch (opcode) {
		/* Math operations */
	case AMLOP_INCREMENT:
	case AMLOP_ADD:
		res = (lhs + rhs);
		break;
	case AMLOP_DECREMENT:
	case AMLOP_SUBTRACT:
		res = (lhs - rhs);
		break;
	case AMLOP_MULTIPLY:
		res = (lhs * rhs);
		break;
	case AMLOP_DIVIDE:
		res = (lhs / rhs);
		break;
	case AMLOP_MOD:
		res = (lhs % rhs);
		break;
	case AMLOP_SHL:
		res = (lhs << rhs);
		break;
	case AMLOP_SHR:
		res = (lhs >> rhs);
		break;
	case AMLOP_AND:
		res = (lhs & rhs);
		break;
	case AMLOP_NAND:
		res = ~(lhs & rhs);
		break;
	case AMLOP_OR:
		res = (lhs | rhs);
		break;
	case AMLOP_NOR:
		res = ~(lhs | rhs);
		break;
	case AMLOP_XOR:
		res = (lhs ^ rhs);
		break;
	case AMLOP_NOT:
		res = ~(lhs);
		break;

		/* Conversion/misc */
	case AMLOP_FINDSETLEFTBIT:
		res = aml_msb(lhs);
		break;
	case AMLOP_FINDSETRIGHTBIT:
		res = aml_lsb(lhs);
		break;
	case AMLOP_TOINTEGER:
		res = (lhs);
		break;
	case AMLOP_FROMBCD:
		res = aml_convradix(lhs, 16, 10);
		break;
	case AMLOP_TOBCD:
		res = aml_convradix(lhs, 10, 16);
		break;

		/* Logical/Comparison */
	case AMLOP_LAND:
		res = -(lhs && rhs);
		break;
	case AMLOP_LOR:
		res = -(lhs || rhs);
		break;
	case AMLOP_LNOT:
		res = -(!lhs);
		break;
	case AMLOP_LNOTEQUAL:
		res = -(lhs != rhs);
		break;
	case AMLOP_LLESSEQUAL:
		res = -(lhs <= rhs);
		break;
	case AMLOP_LGREATEREQUAL:
		res = -(lhs >= rhs);
		break;
	case AMLOP_LEQUAL:
		res = -(lhs == rhs);
		break;
	case AMLOP_LGREATER:
		res = -(lhs > rhs);
		break;
	case AMLOP_LLESS:
		res = -(lhs < rhs);
		break;
	}

	dnprintf(15,"aml_evalexpr: %s %llx %llx = %llx\n",
		 aml_mnem(opcode, NULL), lhs, rhs, res);

	return res;
}

/*
 * aml_bufcpy copies/shifts buffer data, special case for aligned transfers
 * dstPos/srcPos are bit positions within destination/source buffers
 */
void
aml_bufcpy(void *pvDst, int dstPos, const void *pvSrc, int srcPos, int len)
{
	const uint8_t *pSrc = pvSrc;
	uint8_t *pDst = pvDst;
	int		idx;

	if (aml_bytealigned(dstPos|srcPos|len)) {
		/* Aligned transfer: use memcpy */
		memcpy(pDst+aml_bytepos(dstPos), pSrc+aml_bytepos(srcPos),
		    aml_bytelen(len));
		return;
	}

	/* Misaligned transfer: perform bitwise copy (slow) */
	for (idx = 0; idx < len; idx++)
		aml_setbit(pDst, idx + dstPos, aml_tstbit(pSrc, idx + srcPos));
}

/*
 * @@@: External API
 *
 * evaluate an AML node
 * Returns a copy of the value in res  (must be freed by user)
 */

void
aml_walknodes(struct aml_node *node, int mode,
    int (*nodecb)(struct aml_node *, void *), void *arg)
{
	struct aml_node *child;

	if (node == NULL)
		return;
	if (mode == AML_WALK_PRE)
		if (nodecb(node, arg))
			return;
	SIMPLEQ_FOREACH(child, &node->son, sib)
		aml_walknodes(child, mode, nodecb, arg);
	if (mode == AML_WALK_POST)
		nodecb(node, arg);
}

void
aml_find_node(struct aml_node *node, const char *name,
    int (*cbproc)(struct aml_node *, void *arg), void *arg)
{
	struct aml_node *child;
	const char *nn;

	/* match child of this node first before recursing */
	SIMPLEQ_FOREACH(child, &node->son, sib) {
		nn = child->name;
		if (nn != NULL) {
			if (*nn == AMLOP_ROOTCHAR) nn++;
			while (*nn == AMLOP_PARENTPREFIX) nn++;
			if (strcmp(name, nn) == 0) {
				/* Only recurse if cbproc() wants us to */
				if (cbproc(child, arg) != 0)
					return;
			}
		}
	}

	SIMPLEQ_FOREACH(child, &node->son, sib)
		aml_find_node(child, name, cbproc, arg);
}

/*
 * @@@: Parser functions
 */
uint8_t *aml_parsename(struct aml_node *, uint8_t *, struct aml_value **, int);
uint8_t *aml_parseend(struct aml_scope *scope);
int	aml_parselength(struct aml_scope *);
int	aml_parseopcode(struct aml_scope *);

/* Get AML Opcode */
int
aml_parseopcode(struct aml_scope *scope)
{
	int opcode = (scope->pos[0]);
	int twocode = (scope->pos[0]<<8) + scope->pos[1];

	/* Check if this is an embedded name */
	switch (opcode) {
	case AMLOP_ROOTCHAR:
	case AMLOP_PARENTPREFIX:
	case AMLOP_MULTINAMEPREFIX:
	case AMLOP_DUALNAMEPREFIX:
	case AMLOP_NAMECHAR:
		return AMLOP_NAMECHAR;
	}
	if (opcode >= 'A' && opcode <= 'Z')
		return AMLOP_NAMECHAR;
	if (twocode == AMLOP_LNOTEQUAL || twocode == AMLOP_LLESSEQUAL ||
	    twocode == AMLOP_LGREATEREQUAL || opcode == AMLOP_EXTPREFIX) {
		scope->pos += 2;
		return twocode;
	}
	scope->pos += 1;
	return opcode;
}

/* Decode embedded AML Namestring */
uint8_t *
aml_parsename(struct aml_node *inode, uint8_t *pos, struct aml_value **rval, int create)
{
	struct aml_node *relnode, *node = inode;
	uint8_t	*start = pos;
	int i;

	if (*pos == AMLOP_ROOTCHAR) {
		pos++;
		node = &aml_root;
	}
	while (*pos == AMLOP_PARENTPREFIX) {
		pos++;
		if ((node = node->parent) == NULL)
			node = &aml_root;
	}
	switch (*pos) {
	case 0x00:
		pos++;
		break;
	case AMLOP_MULTINAMEPREFIX:
		for (i=0; i<pos[1]; i++)
			node = __aml_search(node, pos+2+i*AML_NAMESEG_LEN,
			    create);
		pos += 2+i*AML_NAMESEG_LEN;
		break;
	case AMLOP_DUALNAMEPREFIX:
		node = __aml_search(node, pos+1, create);
		node = __aml_search(node, pos+1+AML_NAMESEG_LEN, create);
		pos += 1+2*AML_NAMESEG_LEN;
		break;
	default:
		/* If Relative Search (pos == start), recursively go up root */
		relnode = node;
		do {
			node = __aml_search(relnode, pos, create);
			relnode = relnode->parent;
		} while (!node && pos == start && relnode);
		pos += AML_NAMESEG_LEN;
		break;
	}
	if (node) {
		*rval = node->value;

		/* Dereference ALIAS here */
		if ((*rval)->type == AML_OBJTYPE_OBJREF &&
		    (*rval)->v_objref.type == AMLOP_ALIAS) {
			dnprintf(10, "deref alias: %s\n", aml_nodename(node));
			*rval = (*rval)->v_objref.ref;
		}
		aml_addref(*rval, 0);

		dnprintf(10, "parsename: %s %x\n", aml_nodename(node),
		    (*rval)->type);
	} else {
		*rval = aml_allocvalue(AML_OBJTYPE_NAMEREF, 0, start);

		dnprintf(10, "%s:%s not found\n", aml_nodename(inode),
		    aml_getname(start));
	}

	return pos;
}

/* Decode AML Length field
 *  AML Length field is encoded:
 *    byte0    byte1    byte2    byte3
 *    00xxxxxx                             : if upper bits == 00, length = xxxxxx
 *    01--xxxx yyyyyyyy                    : if upper bits == 01, length = yyyyyyyyxxxx
 *    10--xxxx yyyyyyyy zzzzzzzz           : if upper bits == 10, length = zzzzzzzzyyyyyyyyxxxx
 *    11--xxxx yyyyyyyy zzzzzzzz wwwwwwww  : if upper bits == 11, length = wwwwwwwwzzzzzzzzyyyyyyyyxxxx
 */
int
aml_parselength(struct aml_scope *scope)
{
	int len;
	uint8_t lcode;

	lcode = *(scope->pos++);
	if (lcode <= 0x3F)
		return lcode;

	/* lcode >= 0x40, multibyte length, get first byte of extended length */
	len = lcode & 0xF;
	len += *(scope->pos++) << 4L;
	if (lcode >= 0x80)
		len += *(scope->pos++) << 12L;
	if (lcode >= 0xC0)
		len += *(scope->pos++) << 20L;
	return len;
}

/* Get address of end of scope; based on current address */
uint8_t *
aml_parseend(struct aml_scope *scope)
{
	uint8_t *pos = scope->pos;
	int len;

	len = aml_parselength(scope);
	if (pos+len > scope->end) {
		dnprintf(10,
		    "Bad scope... runover pos:%.4x new end:%.4x scope "
		    "end:%.4x\n", aml_pc(pos), aml_pc(pos+len),
		    aml_pc(scope->end));
		return scope->end;
	}
	return pos+len;
}

/*
 * @@@: Opcode utility functions
 */

/*
 * @@@: Opcode functions
 */

int odp;

const char hext[] = "0123456789ABCDEF";

const char *
aml_eisaid(uint32_t pid)
{
	static char id[8];

	id[0] = '@' + ((pid >> 2) & 0x1F);
	id[1] = '@' + ((pid << 3) & 0x18) + ((pid >> 13) & 0x7);
	id[2] = '@' + ((pid >> 8) & 0x1F);
	id[3] = hext[(pid >> 20) & 0xF];
	id[4] = hext[(pid >> 16) & 0xF];
	id[5] = hext[(pid >> 28) & 0xF];
	id[6] = hext[(pid >> 24) & 0xF];
	id[7] = 0;
	return id;
}

/*
 * @@@: Default Object creation
 */
static char osstring[] = "Macrosift Windogs MT";
struct aml_defval {
	const char		*name;
	int			type;
	int64_t			ival;
	const void		*bval;
	struct aml_value	**gval;
} aml_defobj[] = {
	{ "_OS_", AML_OBJTYPE_STRING, -1, osstring },
	{ "_REV", AML_OBJTYPE_INTEGER, 2, NULL },
	{ "_GL", AML_OBJTYPE_MUTEX, 1, NULL, &aml_global_lock },
	{ "_OSI", AML_OBJTYPE_METHOD, 1, aml_callosi },

	/* Create default scopes */
	{ "_GPE", AML_OBJTYPE_DEVICE },
	{ "_PR_", AML_OBJTYPE_DEVICE },
	{ "_SB_", AML_OBJTYPE_DEVICE },
	{ "_TZ_", AML_OBJTYPE_DEVICE },
	{ "_SI_", AML_OBJTYPE_DEVICE },

	{ NULL }
};

/* _OSI Default Method:
 * Returns True if string argument matches list of known OS strings
 * We return True for Windows to fake out nasty bad AML
 */
char *aml_valid_osi[] = {
	AML_VALID_OSI,
	NULL
};

enum acpi_osi acpi_max_osi = OSI_UNKNOWN;

struct aml_value *
aml_callosi(struct aml_scope *scope, struct aml_value *val)
{
	int idx, result=0;
	struct aml_value *fa;

	fa = aml_getstack(scope, AMLOP_ARG0);

	if (hw_vendor != NULL &&
	    (strcmp(hw_vendor, "Apple Inc.") == 0 ||
	    strcmp(hw_vendor, "Apple Computer, Inc.") == 0)) {
		if (strcmp(fa->v_string, "Darwin") == 0) {
			dnprintf(10,"osi: returning 1 for %s on %s hardware\n",
			    fa->v_string, hw_vendor);
			result = 1;
		} else
			dnprintf(10,"osi: on %s hardware, but ignoring %s\n",
			    hw_vendor, fa->v_string);

		return aml_allocvalue(AML_OBJTYPE_INTEGER, result, NULL);
	}

	for (idx=0; !result && aml_valid_osi[idx] != NULL; idx++) {
		dnprintf(10,"osi: %s,%s\n", fa->v_string, aml_valid_osi[idx]);
		result = !strcmp(fa->v_string, aml_valid_osi[idx]);
		if (result) {
			if (idx > acpi_max_osi)
				acpi_max_osi = idx;
			break;
		}
	}
	dnprintf(10,"@@ OSI found: %x\n", result);
	return aml_allocvalue(AML_OBJTYPE_INTEGER, result, NULL);
}

void
aml_create_defaultobjects(void)
{
	struct aml_value *tmp;
	struct aml_defval *def;

#ifdef ACPI_MEMDEBUG
	LIST_INIT(&acpi_memhead);
#endif

	osstring[1] = 'i';
	osstring[6] = 'o';
	osstring[15] = 'w';
	osstring[18] = 'N';

	SIMPLEQ_INIT(&aml_root.son);
	strlcpy(aml_root.name, "\\", sizeof(aml_root.name));
	aml_root.value = aml_allocvalue(0, 0, NULL);
	aml_root.value->node = &aml_root;

	for (def = aml_defobj; def->name; def++) {
		/* Allocate object value + add to namespace */
		aml_parsename(&aml_root, (uint8_t *)def->name, &tmp, 1);
		_aml_setvalue(tmp, def->type, def->ival, def->bval);
		if (def->gval) {
			/* Set root object pointer */
			*def->gval = tmp;
		}
		aml_delref(&tmp, 0);
	}
}

#ifdef ACPI_DEBUG
int
aml_print_resource(union acpi_resource *crs, void *arg)
{
	int typ = AML_CRSTYPE(crs);

	switch (typ) {
	case LR_EXTIRQ:
		printf("extirq\tflags:%.2x len:%.2x irq:%.4x\n",
		    crs->lr_extirq.flags, crs->lr_extirq.irq_count,
		    letoh32(crs->lr_extirq.irq[0]));
		break;
	case SR_IRQ:
		printf("irq\t%.4x %.2x\n", letoh16(crs->sr_irq.irq_mask),
		    crs->sr_irq.irq_flags);
		break;
	case SR_DMA:
		printf("dma\t%.2x %.2x\n", crs->sr_dma.channel,
		    crs->sr_dma.flags);
		break;
	case SR_IOPORT:
		printf("ioport\tflags:%.2x _min:%.4x _max:%.4x _aln:%.2x _len:%.2x\n",
		    crs->sr_ioport.flags, crs->sr_ioport._min,
		    crs->sr_ioport._max, crs->sr_ioport._aln,
		    crs->sr_ioport._len);
		break;
	case SR_STARTDEP:
		printf("startdep\n");
		break;
	case SR_ENDDEP:
		printf("enddep\n");
		break;
	case LR_WORD:
		printf("word\ttype:%.2x flags:%.2x tflag:%.2x gra:%.4x min:%.4x max:%.4x tra:%.4x len:%.4x\n",
			crs->lr_word.type, crs->lr_word.flags, crs->lr_word.tflags,
			crs->lr_word._gra, crs->lr_word._min, crs->lr_word._max,
			crs->lr_word._tra, crs->lr_word._len);
		break;
	case LR_DWORD:
		printf("dword\ttype:%.2x flags:%.2x tflag:%.2x gra:%.8x min:%.8x max:%.8x tra:%.8x len:%.8x\n",
			crs->lr_dword.type, crs->lr_dword.flags, crs->lr_dword.tflags,
			crs->lr_dword._gra, crs->lr_dword._min, crs->lr_dword._max,
			crs->lr_dword._tra, crs->lr_dword._len);
		break;
	case LR_QWORD:
		printf("dword\ttype:%.2x flags:%.2x tflag:%.2x gra:%.16llx min:%.16llx max:%.16llx tra:%.16llx len:%.16llx\n",
			crs->lr_qword.type, crs->lr_qword.flags, crs->lr_qword.tflags,
			crs->lr_qword._gra, crs->lr_qword._min, crs->lr_qword._max,
			crs->lr_qword._tra, crs->lr_qword._len);
		break;
	default:
		printf("unknown type: %x\n", typ);
		break;
	}
	return (0);
}
#endif /* ACPI_DEBUG */

union acpi_resource *aml_mapresource(union acpi_resource *);

union acpi_resource *
aml_mapresource(union acpi_resource *crs)
{
	static union acpi_resource map;
	int rlen;

	rlen = AML_CRSLEN(crs);
	if (rlen >= sizeof(map))
		return crs;

	memset(&map, 0, sizeof(map));
	memcpy(&map, crs, rlen);

	return &map;
}

int
aml_parse_resource(struct aml_value *res,
    int (*crs_enum)(int, union acpi_resource *, void *), void *arg)
{
	int off, rlen, crsidx;
	union acpi_resource *crs;

	if (res->type != AML_OBJTYPE_BUFFER || res->length < 5)
		return (-1);
	for (off = 0, crsidx = 0; off < res->length; off += rlen, crsidx++) {
		crs = (union acpi_resource *)(res->v_buffer+off);

		rlen = AML_CRSLEN(crs);
		if (crs->hdr.typecode == SRT_ENDTAG || !rlen)
			break;

		crs = aml_mapresource(crs);
#ifdef ACPI_DEBUG
		aml_print_resource(crs, NULL);
#endif
		crs_enum(crsidx, crs, arg);
	}

	return (0);
}

void
aml_foreachpkg(struct aml_value *pkg, int start,
    void (*fn)(struct aml_value *, void *), void *arg)
{
	int idx;

	if (pkg->type != AML_OBJTYPE_PACKAGE)
		return;
	for (idx=start; idx<pkg->length; idx++)
		fn(pkg->v_package[idx], arg);
}

/*
 * Walk nodes and perform fixups for nameref
 */
int aml_fixup_node(struct aml_node *, void *);

int
aml_fixup_node(struct aml_node *node, void *arg)
{
	struct aml_value *val = arg;
	int i;

	if (node->value == NULL)
		return (0);
	if (arg == NULL)
		aml_fixup_node(node, node->value);
	else if (val->type == AML_OBJTYPE_NAMEREF) {
		node = aml_searchname(node, aml_getname(val->v_nameref));
		if (node && node->value) {
			_aml_setvalue(val, AML_OBJTYPE_OBJREF, AMLOP_NAMECHAR,
			    node->value);
		}
	} else if (val->type == AML_OBJTYPE_PACKAGE) {
		for (i = 0; i < val->length; i++)
			aml_fixup_node(node, val->v_package[i]);
	}
	return (0);
}

void
aml_postparse(void)
{
	aml_walknodes(&aml_root, AML_WALK_PRE, aml_fixup_node, NULL);
}

#ifndef SMALL_KERNEL
const char *
aml_val_to_string(const struct aml_value *val)
{
	static char buffer[256];

	int len;

	switch (val->type) {
	case AML_OBJTYPE_BUFFER:
		len = val->length;
		if (len >= sizeof(buffer))
			len = sizeof(buffer) - 1;
		memcpy(buffer, val->v_buffer, len);
		buffer[len] = 0;
		break;
	case AML_OBJTYPE_STRING:
		strlcpy(buffer, val->v_string, sizeof(buffer));
		break;
	case AML_OBJTYPE_INTEGER:
		snprintf(buffer, sizeof(buffer), "%llx", val->v_integer);
		break;
	default:
		snprintf(buffer, sizeof(buffer),
		    "Failed to convert type %d to string!", val->type);
	}

	return (buffer);
}
#endif /* SMALL_KERNEL */

int aml_error;

struct aml_value *aml_gettgt(struct aml_value *, int);
struct aml_value *aml_eval(struct aml_scope *, struct aml_value *, int, int,
    struct aml_value *);
struct aml_value *aml_parsesimple(struct aml_scope *, char,
    struct aml_value *);
struct aml_value *aml_parse(struct aml_scope *, int, const char *);
struct aml_value *aml_seterror(struct aml_scope *, const char *, ...);

struct aml_scope *aml_findscope(struct aml_scope *, int, int);
struct aml_scope *aml_pushscope(struct aml_scope *, struct aml_value *,
    struct aml_node *, int);
struct aml_scope *aml_popscope(struct aml_scope *);

void		aml_showstack(struct aml_scope *);
struct aml_value *aml_tryconv(struct aml_value *, int, int);
struct aml_value *aml_convert(struct aml_value *, int, int);

int		aml_matchtest(int64_t, int64_t, int);
int		aml_match(struct aml_value *, int, int, int, int, int);

int		aml_compare(struct aml_value *, struct aml_value *, int);
struct aml_value *aml_concat(struct aml_value *, struct aml_value *);
struct aml_value *aml_concatres(struct aml_value *, struct aml_value *);
struct aml_value *aml_mid(struct aml_value *, int, int);
int		aml_ccrlen(int, union acpi_resource *, void *);

void		aml_store(struct aml_scope *, struct aml_value *, int64_t,
    struct aml_value *);
void		aml_rwfield(struct aml_value *, int, int, struct aml_value *,
    int);

/*
 * Reference Count functions
 */
void
aml_addref(struct aml_value *val, const char *lbl)
{
	if (val == NULL)
		return;
	dnprintf(50, "XAddRef: %p %s:[%s] %d\n",
	    val, lbl,
	    val->node ? aml_nodename(val->node) : "INTERNAL",
	    val->refcnt);
	val->refcnt++;
}

/* Decrease reference counter */
void
aml_delref(struct aml_value **pv, const char *lbl)
{
	struct aml_value *val;

	if (pv == NULL || *pv == NULL)
		return;
	val = *pv;
	val->refcnt--;
	if (val->refcnt == 0) {
		dnprintf(50, "XDelRef: %p %s %2d [%s] %s\n",
		    val, lbl,
		    val->refcnt,
		    val->node ? aml_nodename(val->node) : "INTERNAL",
		    val->refcnt ? "" : "---------------- FREEING");

		aml_freevalue(val);
		acpi_os_free(val);
		*pv = NULL;
	}
}

/* Walk list of parent scopes until we find one of 'type'
 * If endscope is set, mark all intermediate scopes as invalid (used for Method/While) */
struct aml_scope *
aml_findscope(struct aml_scope *scope, int type, int endscope)
{
	while (scope) {
		switch (endscope) {
		case AMLOP_RETURN:
			scope->pos = scope->end;
			if (scope->type == AMLOP_WHILE)
				scope->pos = NULL;
			break;
		case AMLOP_CONTINUE:
			scope->pos = scope->end;
			break;
		case AMLOP_BREAK:
			scope->pos = scope->end;
			if (scope->type == type)
				scope->parent->pos = scope->end;
			break;
		}
		if (scope->type == type)
			break;
		scope = scope->parent;
	}
	return scope;
}

struct aml_value *
aml_getstack(struct aml_scope *scope, int opcode)
{
	struct aml_value *sp;

	sp = NULL;
	scope = aml_findscope(scope, AMLOP_METHOD, 0);
	if (scope == NULL)
		return NULL;
	if (opcode >= AMLOP_LOCAL0 && opcode <= AMLOP_LOCAL7) {
		if (scope->locals == NULL)
			scope->locals = aml_allocvalue(AML_OBJTYPE_PACKAGE, 8, NULL);
		sp = scope->locals->v_package[opcode - AMLOP_LOCAL0];
		sp->stack = opcode;
	} else if (opcode >= AMLOP_ARG0 && opcode <= AMLOP_ARG6) {
		if (scope->args == NULL)
			scope->args = aml_allocvalue(AML_OBJTYPE_PACKAGE, 7, NULL);
		sp = scope->args->v_package[opcode - AMLOP_ARG0];
		if (sp->type == AML_OBJTYPE_OBJREF)
			sp = sp->v_objref.ref;
	}
	return sp;
}

#ifdef ACPI_DEBUG
/* Dump AML Stack */
void
aml_showstack(struct aml_scope *scope)
{
	struct aml_value *sp;
	int idx;

	dnprintf(10, "===== Stack %s:%s\n", aml_nodename(scope->node),
	    aml_mnem(scope->type, 0));
	for (idx=0; scope->args && idx<7; idx++) {
		sp = aml_getstack(scope, AMLOP_ARG0+idx);
		if (sp && sp->type) {
			dnprintf(10," Arg%d: ", idx);
			aml_showvalue(sp);
		}
	}
	for (idx=0; scope->locals && idx<8; idx++) {
		sp = aml_getstack(scope, AMLOP_LOCAL0+idx);
		if (sp && sp->type) {
			dnprintf(10," Local%d: ", idx);
			aml_showvalue(sp);
		}
	}
}
#endif

/* Create a new scope object */
struct aml_scope *
aml_pushscope(struct aml_scope *parent, struct aml_value *range,
    struct aml_node *node, int type)
{
	struct aml_scope *scope;
	uint8_t *start, *end;

	if (range->type == AML_OBJTYPE_METHOD) {
		start = range->v_method.start;
		end = range->v_method.end;
	} else {
		start = range->v_buffer;
		end = start + range->length;
		if (start == end)
			return NULL;
	}
	scope = acpi_os_malloc(sizeof(struct aml_scope));
	if (scope == NULL)
		return NULL;

	scope->node = node;
	scope->start = start;
	scope->end = end;
	scope->pos = scope->start;
	scope->parent = parent;
	scope->type = type;
	scope->sc = acpi_softc;

	if (parent)
		scope->depth = parent->depth+1;

	aml_lastscope = scope;

	return scope;
}

/* Free a scope object and any children */
struct aml_scope *
aml_popscope(struct aml_scope *scope)
{
	struct aml_scope *nscope;

	if (scope == NULL)
		return NULL;

	nscope = scope->parent;

	if (scope->type == AMLOP_METHOD)
		aml_delchildren(scope->node);
	if (scope->locals) {
		aml_freevalue(scope->locals);
		acpi_os_free(scope->locals);
		scope->locals = NULL;
	}
	if (scope->args) {
		aml_freevalue(scope->args);
		acpi_os_free(scope->args);
		scope->args = NULL;
	}
	acpi_os_free(scope);
	aml_lastscope = nscope;

	return nscope;
}

/* Test AMLOP_MATCH codes */
int
aml_matchtest(int64_t a, int64_t b, int op)
{
	switch (op) {
	case AML_MATCH_TR:
		return (1);
	case AML_MATCH_EQ:
		return (a == b);
	case AML_MATCH_LT:
		return (a < b);
	case AML_MATCH_LE:
		return (a <= b);
	case AML_MATCH_GE:
		return (a >= b);
	case AML_MATCH_GT:
		return (a > b);
	}
	return (0);
}

/* Search a package for a matching value */
int
aml_match(struct aml_value *pkg, int index,
	   int op1, int v1,
	   int op2, int v2)
{
	struct aml_value *tmp;
	int flag;

	while (index < pkg->length) {
		/* Convert package value to integer */
		tmp = aml_convert(pkg->v_package[index],
		    AML_OBJTYPE_INTEGER, -1);

		/* Perform test */
		flag = aml_matchtest(tmp->v_integer, v1, op1) &&
		    aml_matchtest(tmp->v_integer, v2, op2);
		aml_delref(&tmp, "xmatch");

		if (flag)
			return index;
		index++;
	}
	return -1;
}

/*
 * Conversion routines
 */
int64_t
aml_hextoint(const char *str)
{
	int64_t v = 0;
	char c;

	while (*str) {
		if (*str >= '0' && *str <= '9')
			c = *(str++) - '0';
		else if (*str >= 'a' && *str <= 'f')
			c = *(str++) - 'a' + 10;
		else if (*str >= 'A' && *str <= 'F')
			c = *(str++) - 'A' + 10;
		else
			break;
		v = (v << 4) + c;
	}
	return v;

}

struct aml_value *
aml_tryconv(struct aml_value *a, int ctype, int clen)
{
	struct aml_value *c = NULL;

	/* Object is already this type */
	if (clen == -1)
		clen = a->length;
	if (a->type == ctype) {
		aml_addref(a, "XConvert");
		return a;
	}
	switch (ctype) {
	case AML_OBJTYPE_BUFFER:
		dnprintf(10,"convert to buffer\n");
		switch (a->type) {
		case AML_OBJTYPE_INTEGER:
			c = aml_allocvalue(AML_OBJTYPE_BUFFER, a->length,
			    &a->v_integer);
			break;
		case AML_OBJTYPE_STRING:
			c = aml_allocvalue(AML_OBJTYPE_BUFFER, a->length,
			    a->v_string);
			break;
		case AML_OBJTYPE_BUFFERFIELD:
		case AML_OBJTYPE_FIELDUNIT:
			c = aml_allocvalue(AML_OBJTYPE_BUFFER, 0, NULL);
			aml_rwfield(a, 0, a->v_field.bitlen, c, ACPI_IOREAD);
			break;
		}
		break;
	case AML_OBJTYPE_INTEGER:
		dnprintf(10,"convert to integer : %x\n", a->type);
		switch (a->type) {
		case AML_OBJTYPE_BUFFER:
			c = aml_allocvalue(AML_OBJTYPE_INTEGER, 0, NULL);
			memcpy(&c->v_integer, a->v_buffer,
			    min(a->length, c->length));
			break;
		case AML_OBJTYPE_STRING:
			c = aml_allocvalue(AML_OBJTYPE_INTEGER, 0, NULL);
			c->v_integer = aml_hextoint(a->v_string);
			break;
		case AML_OBJTYPE_UNINITIALIZED:
			c = aml_allocvalue(AML_OBJTYPE_INTEGER, 0, NULL);
			break;
		case AML_OBJTYPE_BUFFERFIELD:
		case AML_OBJTYPE_FIELDUNIT:
			if (a->v_field.bitlen > aml_intlen)
				break;
			c = aml_allocvalue(AML_OBJTYPE_INTEGER, 0, NULL);
			aml_rwfield(a, 0, a->v_field.bitlen, c, ACPI_IOREAD);
			break;
		}
		break;
	case AML_OBJTYPE_STRING:
	case AML_OBJTYPE_HEXSTRING:
	case AML_OBJTYPE_DECSTRING:
		dnprintf(10,"convert to string\n");
		switch (a->type) {
		case AML_OBJTYPE_INTEGER:
			c = aml_allocvalue(AML_OBJTYPE_STRING, 20, NULL);
			snprintf(c->v_string, c->length, (ctype == AML_OBJTYPE_HEXSTRING) ?
			    "0x%llx" : "%lld", a->v_integer);
			break;
		case AML_OBJTYPE_BUFFER:
			c = aml_allocvalue(AML_OBJTYPE_STRING, a->length,
			    a->v_buffer);
			break;
		case AML_OBJTYPE_STRING:
			aml_addref(a, "XConvert");
			return a;
		case AML_OBJTYPE_PACKAGE: /* XXX Deal with broken Lenovo X1 BIOS. */
			c = aml_allocvalue(AML_OBJTYPE_STRING, 0, NULL);
			break;
		}
		break;
	}
	return c;
}

struct aml_value *
aml_convert(struct aml_value *a, int ctype, int clen)
{
	struct aml_value *c;

	c = aml_tryconv(a, ctype, clen);
	if (c == NULL) {
#ifndef SMALL_KERNEL
		aml_showvalue(a);
#endif
		aml_die("Could not convert %x to %x\n", a->type, ctype);
	}
	return c;
}

int
aml_compare(struct aml_value *a1, struct aml_value *a2, int opcode)
{
	struct aml_value *cv;		/* value after conversion */
	int rc = 0;

	/*
	 * Convert A1 to integer, string, or buffer.
	 *
	 * The possible conversions listed in Table 19.6 of the ACPI spec
	 * imply that unless we already got one of the three supported types,
	 * the conversion must be from field unit or buffer field. In both
	 * cases, the rules (Table 19.7) state that we should convert to
	 * integer if possible with buffer as a fallback.
	 */
	if (a1->type != AML_OBJTYPE_INTEGER && a1->type != AML_OBJTYPE_STRING
	    && a1->type != AML_OBJTYPE_BUFFER) {
		cv = aml_tryconv(a1, AML_OBJTYPE_INTEGER, -1);
		if (cv == NULL)
			cv = aml_convert(a1, AML_OBJTYPE_BUFFER, -1);
		a1 = cv;
	}

	/* Convert A2 to type of A1 */
	a2 = aml_convert(a2, a1->type, -1);
	if (a1->type == AML_OBJTYPE_INTEGER)
		rc = aml_evalexpr(a1->v_integer, a2->v_integer, opcode);
	else {
		/* Perform String/Buffer comparison */
		rc = memcmp(a1->v_buffer, a2->v_buffer,
		    min(a1->length, a2->length));
		if (rc == 0) {
			/* If buffers match, which one is longer */
			rc = a1->length - a2->length;
		}
		/* Perform comparison against zero */
		rc = aml_evalexpr(rc, 0, opcode);
	}
	/* Either deletes temp buffer, or decrease refcnt on original A2 */
	aml_delref(&a2, "xcompare");
	return rc;
}

/* Concatenate two objects, returning pointer to new object */
struct aml_value *
aml_concat(struct aml_value *a1, struct aml_value *a2)
{
	struct aml_value *c = NULL;

	/*
	 * Make A1 an integer, string, or buffer. Unless we already got one
	 * of these three types, convert to string.
	 */
	if (a1->type != AML_OBJTYPE_INTEGER && a1->type != AML_OBJTYPE_STRING
	    && a1->type != AML_OBJTYPE_BUFFER)
		a1 = aml_convert(a1, AML_OBJTYPE_STRING, -1);

	/* Convert arg2 to type of arg1 */
	a2 = aml_convert(a2, a1->type, -1);
	switch (a1->type) {
	case AML_OBJTYPE_INTEGER:
		c = aml_allocvalue(AML_OBJTYPE_BUFFER,
		    a1->length + a2->length, NULL);
		memcpy(c->v_buffer, &a1->v_integer, a1->length);
		memcpy(c->v_buffer+a1->length, &a2->v_integer, a2->length);
		break;
	case AML_OBJTYPE_BUFFER:
		c = aml_allocvalue(AML_OBJTYPE_BUFFER,
		    a1->length + a2->length, NULL);
		memcpy(c->v_buffer, a1->v_buffer, a1->length);
		memcpy(c->v_buffer+a1->length, a2->v_buffer, a2->length);
		break;
	case AML_OBJTYPE_STRING:
		c = aml_allocvalue(AML_OBJTYPE_STRING,
		    a1->length + a2->length, NULL);
		memcpy(c->v_string, a1->v_string, a1->length);
		memcpy(c->v_string+a1->length, a2->v_string, a2->length);
		break;
	default:
		aml_die("concat type mismatch %d != %d\n", a1->type, a2->type);
		break;
	}
	/* Either deletes temp buffer, or decrease refcnt on original A2 */
	aml_delref(&a2, "xconcat");
	return c;
}

/* Calculate length of Resource Template */
int
aml_ccrlen(int crsidx, union acpi_resource *rs, void *arg)
{
	int *plen = arg;

	*plen += AML_CRSLEN(rs);
	return (0);
}

/* Concatenate resource templates, returning pointer to new object */
struct aml_value *
aml_concatres(struct aml_value *a1, struct aml_value *a2)
{
	struct aml_value *c;
	int l1 = 0, l2 = 0, l3 = 2;
	uint8_t a3[] = { SRT_ENDTAG, 0x00 };

	if (a1->type != AML_OBJTYPE_BUFFER || a2->type != AML_OBJTYPE_BUFFER)
		aml_die("concatres: not buffers\n");

	/* Walk a1, a2, get length minus end tags, concatenate buffers, add end tag */
	aml_parse_resource(a1, aml_ccrlen, &l1);
	aml_parse_resource(a2, aml_ccrlen, &l2);

	/* Concatenate buffers, add end tag */
	c = aml_allocvalue(AML_OBJTYPE_BUFFER, l1+l2+l3, NULL);
	memcpy(c->v_buffer,    a1->v_buffer, l1);
	memcpy(c->v_buffer+l1, a2->v_buffer, l2);
	memcpy(c->v_buffer+l1+l2, a3,        l3);

	return c;
}

/* Extract substring from string or buffer */
struct aml_value *
aml_mid(struct aml_value *src, int index, int length)
{
	if (index > src->length)
		index = src->length;
	if ((index + length) > src->length)
		length = src->length - index;
	return aml_allocvalue(src->type, length, src->v_buffer + index);
}

/*
 * Field I/O utility functions
 */
void aml_createfield(struct aml_value *, int, struct aml_value *, int, int,
    struct aml_value *, int, int);
void aml_parsefieldlist(struct aml_scope *, int, int,
    struct aml_value *, struct aml_value *, int);

int
aml_evalhid(struct aml_node *node, struct aml_value *val)
{
	if (aml_evalname(acpi_softc, node, "_HID", 0, NULL, val))
		return (-1);

	/* Integer _HID: convert to EISA ID */
	if (val->type == AML_OBJTYPE_INTEGER)
		_aml_setvalue(val, AML_OBJTYPE_STRING, -1, aml_eisaid(val->v_integer));
	return (0);
}

int
aml_opreg_sysmem_handler(void *cookie, int iodir, uint64_t address, int size,
    uint64_t *value)
{
	return acpi_gasio(acpi_softc, iodir, GAS_SYSTEM_MEMORY,
	    address, size, size, value);
}

int
aml_opreg_sysio_handler(void *cookie, int iodir, uint64_t address, int size,
    uint64_t *value)
{
	return acpi_gasio(acpi_softc, iodir, GAS_SYSTEM_IOSPACE,
	    address, size, size, value);
}

int
aml_opreg_pcicfg_handler(void *cookie, int iodir, uint64_t address, int size,
    uint64_t *value)
{
	return acpi_gasio(acpi_softc, iodir, GAS_PCI_CFG_SPACE,
	    address, size, size, value);
}

int
aml_opreg_ec_handler(void *cookie, int iodir, uint64_t address, int size,
    uint64_t *value)
{
	return acpi_gasio(acpi_softc, iodir, GAS_EMBEDDED,
	    address, size, size, value);
}

struct aml_regionspace {
	void *cookie;
	int (*handler)(void *, int, uint64_t, int, uint64_t *);
};

struct aml_regionspace aml_regionspace[256] = {
	[ACPI_OPREG_SYSMEM] = { NULL, aml_opreg_sysmem_handler },
	[ACPI_OPREG_SYSIO] = { NULL, aml_opreg_sysio_handler },
	[ACPI_OPREG_PCICFG] = { NULL, aml_opreg_pcicfg_handler },
	[ACPI_OPREG_EC] = { NULL, aml_opreg_ec_handler },
};

void
aml_register_regionspace(struct aml_node *node, int iospace, void *cookie,
    int (*handler)(void *, int, uint64_t, int, uint64_t *))
{
	struct aml_value arg[2];

	KASSERT(iospace >= 0 && iospace < 256);

	aml_regionspace[iospace].cookie = cookie;
	aml_regionspace[iospace].handler = handler;

	/* Register address space. */
	memset(&arg, 0, sizeof(arg));
	arg[0].type = AML_OBJTYPE_INTEGER;
	arg[0].v_integer = iospace;
	arg[1].type = AML_OBJTYPE_INTEGER;
	arg[1].v_integer = 1;
	node = aml_searchname(node, "_REG");
	if (node)
		aml_evalnode(acpi_softc, node, 2, arg, NULL);
}

void aml_rwgen(struct aml_value *, int, int, struct aml_value *, int, int);
void aml_rwgpio(struct aml_value *, int, int, struct aml_value *, int, int);
void aml_rwgsb(struct aml_value *, int, int, int, struct aml_value *, int, int);
void aml_rwindexfield(struct aml_value *, struct aml_value *val, int);

/* Get PCI address for opregion objects */
int
aml_rdpciaddr(struct aml_node *pcidev, union amlpci_t *addr)
{
	int64_t res;

	addr->bus = 0;
	addr->seg = 0;
	if (aml_evalinteger(acpi_softc, pcidev, "_ADR", 0, NULL, &res) == 0) {
		addr->fun = res & 0xFFFF;
		addr->dev = res >> 16;
	}
	while (pcidev != NULL) {
		/* HID device (PCI or PCIE root): eval _SEG and _BBN */
		if (__aml_search(pcidev, "_HID", 0)) {
			if (aml_evalinteger(acpi_softc, pcidev, "_SEG",
			        0, NULL, &res) == 0) {
				addr->seg = res;
			}
			if (aml_evalinteger(acpi_softc, pcidev, "_BBN",
			        0, NULL, &res) == 0) {
				addr->bus = res;
				break;
			}
		}
		pcidev = pcidev->parent;
	}
	return (0);
}

int
acpi_genio(struct acpi_softc *sc, int iodir, int iospace, uint64_t address,
    int access_size, int len, void *buffer)
{
	struct aml_regionspace *region = &aml_regionspace[iospace];
	uint8_t *pb;
	int reg;

	dnprintf(50, "genio: %.2x 0x%.8llx %s\n",
	    iospace, address, (iodir == ACPI_IOWRITE) ? "write" : "read");

	KASSERT((len % access_size) == 0);

	pb = (uint8_t *)buffer;
	for (reg = 0; reg < len; reg += access_size) {
		uint64_t value;
		int err;

		if (iodir == ACPI_IOREAD) {
			err = region->handler(region->cookie, iodir,
			    address + reg, access_size, &value);
			if (err)
				return err;
			switch (access_size) {
			case 1:
				*(uint8_t *)(pb + reg) = value;
				break;
			case 2:
				*(uint16_t *)(pb + reg) = value;
				break;
			case 4:
				*(uint32_t *)(pb + reg) = value;
				break;
			default:
				printf("%s: invalid access size %d on read\n",
				    __func__, access_size);
				return -1;
			}
		} else {
			switch (access_size) {
			case 1:
				value = *(uint8_t *)(pb + reg);
				break;
			case 2:
				value = *(uint16_t *)(pb + reg);
				break;
			case 4:
				value = *(uint32_t *)(pb + reg);
				break;
			default:
				printf("%s: invalid access size %d on write\n",
				    __func__, access_size);
				return -1;
			}
			err = region->handler(region->cookie, iodir,
			    address + reg, access_size, &value);
			if (err)
				return err;
		}
	}

	return 0;
}

/* Read/Write from opregion object */
void
aml_rwgen(struct aml_value *rgn, int bpos, int blen, struct aml_value *val,
    int mode, int flag)
{
	struct aml_value tmp;
	union amlpci_t pi;
	void *tbit, *vbit;
	int tlen, type, sz;

	dnprintf(10," %5s %.2x %.8llx %.4x [%s]\n",
		mode == ACPI_IOREAD ? "read" : "write",
		rgn->v_opregion.iospace,
		rgn->v_opregion.iobase + (bpos >> 3),
		blen, aml_nodename(rgn->node));
	memset(&tmp, 0, sizeof(tmp));

	/* Get field access size */
	switch (AML_FIELD_ACCESS(flag)) {
	case AML_FIELD_WORDACC:
		sz = 2;
		break;
	case AML_FIELD_DWORDACC:
		sz = 4;
		break;
	case AML_FIELD_QWORDACC:
		sz = 8;
		break;
	default:
		sz = 1;
		break;
	}

	pi.addr = (rgn->v_opregion.iobase + (bpos >> 3)) & ~(sz - 1);
	bpos += ((rgn->v_opregion.iobase & (sz - 1)) << 3);
	bpos &= ((sz << 3) - 1);

	if (rgn->v_opregion.iospace == ACPI_OPREG_PCICFG) {
		/* Get PCI Root Address for this opregion */
		aml_rdpciaddr(rgn->node->parent, &pi);
	}

	tbit = &tmp.v_integer;
	vbit = &val->v_integer;
	tlen = roundup(bpos + blen, sz << 3);
	type = rgn->v_opregion.iospace;

	if (aml_regionspace[type].handler == NULL) {
		printf("%s: unregistered RegionSpace 0x%x\n", __func__, type);
		return;
	}

	/* Allocate temporary storage */
	if (tlen > aml_intlen) {
		_aml_setvalue(&tmp, AML_OBJTYPE_BUFFER, tlen >> 3, 0);
		tbit = tmp.v_buffer;
	}

	if (blen > aml_intlen) {
		if (mode == ACPI_IOREAD) {
			/* Read from a large field:  create buffer */
			_aml_setvalue(val, AML_OBJTYPE_BUFFER, (blen + 7) >> 3, 0);
		} else {
			/* Write to a large field.. create or convert buffer */
			val = aml_convert(val, AML_OBJTYPE_BUFFER, -1);

			if (blen > (val->length << 3))
				blen = val->length << 3;
		}
		vbit = val->v_buffer;
	} else {
		if (mode == ACPI_IOREAD) {
			/* Read from a short field.. initialize integer */
			_aml_setvalue(val, AML_OBJTYPE_INTEGER, 0, 0);
		} else {
			/* Write to a short field.. convert to integer */
			val = aml_convert(val, AML_OBJTYPE_INTEGER, -1);
		}
	}

	if (mode == ACPI_IOREAD) {
		/* Read bits from opregion */
		acpi_genio(acpi_softc, ACPI_IOREAD, type, pi.addr,
		    sz, tlen >> 3, tbit);
		aml_bufcpy(vbit, 0, tbit, bpos, blen);
	} else {
		/* Write bits to opregion */
		if (AML_FIELD_UPDATE(flag) == AML_FIELD_PRESERVE &&
		    (bpos != 0 || blen != tlen)) {
			acpi_genio(acpi_softc, ACPI_IOREAD, type, pi.addr,
			    sz, tlen >> 3, tbit);
		} else if (AML_FIELD_UPDATE(flag) == AML_FIELD_WRITEASONES) {
			memset(tbit, 0xff, tmp.length);
		}
		/* Copy target bits, then write to region */
		aml_bufcpy(tbit, bpos, vbit, 0, blen);
		acpi_genio(acpi_softc, ACPI_IOWRITE, type, pi.addr,
		    sz, tlen >> 3, tbit);

		aml_delref(&val, "fld.write");
	}
	aml_freevalue(&tmp);
}

void
aml_rwgpio(struct aml_value *conn, int bpos, int blen, struct aml_value *val,
    int mode, int flag)
{
	union acpi_resource *crs = (union acpi_resource *)conn->v_buffer;
	struct aml_node *node;
	uint16_t pin;
	int v = 0;

	if (conn->type != AML_OBJTYPE_BUFFER || conn->length < 5 ||
	    AML_CRSTYPE(crs) != LR_GPIO || AML_CRSLEN(crs) > conn->length)
		aml_die("Invalid GpioIo");
	if (bpos != 0 || blen != 1)
		aml_die("Invalid GpioIo access");

	node = aml_searchname(conn->node,
	    (char *)&crs->pad[crs->lr_gpio.res_off]);
	pin = *(uint16_t *)&crs->pad[crs->lr_gpio.pin_off];

	if (node == NULL || node->gpio == NULL)
		aml_die("Could not find GpioIo pin");

	if (mode == ACPI_IOWRITE) {
		v = aml_val2int(val);
		node->gpio->write_pin(node->gpio->cookie, pin, v);
	} else {
		v = node->gpio->read_pin(node->gpio->cookie, pin);
		_aml_setvalue(val, AML_OBJTYPE_INTEGER, v, NULL);
	}
}

#ifndef SMALL_KERNEL

void
aml_rwgsb(struct aml_value *conn, int len, int bpos, int blen,
    struct aml_value *val, int mode, int flag)
{
	union acpi_resource *crs = (union acpi_resource *)conn->v_buffer;
	struct aml_node *node;
	i2c_tag_t tag;
	i2c_op_t op;
	i2c_addr_t addr;
	int cmdlen, buflen;
	uint8_t cmd[2];
	uint8_t *buf;
	int err;

	if (conn->type != AML_OBJTYPE_BUFFER || conn->length < 5 ||
	    AML_CRSTYPE(crs) != LR_SERBUS || AML_CRSLEN(crs) > conn->length ||
	    crs->lr_i2cbus.revid != 1 || crs->lr_i2cbus.type != LR_SERBUS_I2C)
		aml_die("Invalid GenericSerialBus");
	if (AML_FIELD_ACCESS(flag) != AML_FIELD_BUFFERACC ||
	    bpos & 0x3 || (blen % 8) != 0 || blen > 16)
		aml_die("Invalid GenericSerialBus access");

	node = aml_searchname(conn->node,
	    (char *)&crs->lr_i2cbus.vdata[crs->lr_i2cbus.tlength - 6]);

	switch (((flag >> 6) & 0x3)) {
	case 0:			/* Normal */
		switch (AML_FIELD_ATTR(flag)) {
		case 0x02:	/* AttribQuick */
			cmdlen = 0;
			buflen = 0;
			break;
		case 0x04:	/* AttribSendReceive */
			cmdlen = 0;
			buflen = 1;
			break;
		case 0x06:	/* AttribByte */
			cmdlen = blen / 8;
			buflen = 1;
			break;
		case 0x08:	/* AttribWord */
			cmdlen = blen / 8;
			buflen = 2;
			break;
		case 0x0b:	/* AttribBytes */
			cmdlen = blen / 8;
			buflen = len;
			break;
		case 0x0e:	/* AttribRawBytes */
			cmdlen = 0;
			buflen = len;
			break;
		case 0x0f:	/* AttribRawProcessBytes */
			/*
			 * XXX Not implemented yet but used by various
			 * WoA laptops.  Force an error status instead
			 * of a panic for now.
			 */
			node = NULL;
			cmdlen = 0;
			buflen = len;
			break;
		default:
			aml_die("unsupported access type 0x%x", flag);
			break;
		}
		break;
	case 1:			/* AttribBytes */
		cmdlen = blen / 8;
		buflen = AML_FIELD_ATTR(flag);
		break;
	case 2:			/* AttribRawBytes */
		cmdlen = 0;
		buflen = AML_FIELD_ATTR(flag);
		break;
	default:
		aml_die("unsupported access type 0x%x", flag);
		break;
	}

	if (mode == ACPI_IOREAD) {
		_aml_setvalue(val, AML_OBJTYPE_BUFFER, buflen + 2, NULL);
		op = I2C_OP_READ_WITH_STOP;
	} else {
		op = I2C_OP_WRITE_WITH_STOP;
	}

	buf = val->v_buffer;

	/*
	 * Return an error if we can't find the I2C controller that
	 * we're supposed to use for this request.
	 */
	if (node == NULL || node->i2c == NULL) {
		buf[0] = EIO;
		return;
	}

	tag = node->i2c;
	addr = crs->lr_i2cbus._adr;
	cmd[0] = bpos >> 3;
	cmd[1] = bpos >> 11;

	iic_acquire_bus(tag, 0);
	err = iic_exec(tag, op, addr, &cmd, cmdlen, &buf[2], buflen, 0);
	iic_release_bus(tag, 0);

	/*
	 * The ACPI specification doesn't tell us what the status
	 * codes mean beyond implying that zero means success.  So use
	 * the error returned from the transfer.  All possible error
	 * numbers should fit in a single byte.
	 */
	buf[0] = err;
}

#else

/*
 * We don't support GenericSerialBus in RAMDISK kernels.  Provide a
 * dummy implementation that returns a non-zero error status.
 */

void
aml_rwgsb(struct aml_value *conn, int len, int bpos, int blen,
    struct aml_value *val, int mode, int flag)
{
	int buflen;
	uint8_t *buf;

	if (AML_FIELD_ACCESS(flag) != AML_FIELD_BUFFERACC ||
	    bpos & 0x3 || (blen % 8) != 0 || blen > 16)
		aml_die("Invalid GenericSerialBus access");

	switch (((flag >> 6) & 0x3)) {
	case 0:			/* Normal */
		switch (AML_FIELD_ATTR(flag)) {
		case 0x02:	/* AttribQuick */
			buflen = 0;
			break;
		case 0x04:	/* AttribSendReceive */
		case 0x06:	/* AttribByte */
			buflen = 1;
			break;
		case 0x08:	/* AttribWord */
			buflen = 2;
			break;
		case 0x0b:	/* AttribBytes */
		case 0x0e:	/* AttribRawBytes */
		case 0x0f:	/* AttribRawProcessBytes */
			buflen = len;
			break;
		default:
			aml_die("unsupported access type 0x%x", flag);
			break;
		}
		break;
	case 1:			/* AttribBytes */
	case 2:			/* AttribRawBytes */
		buflen = AML_FIELD_ATTR(flag);
		break;
	default:
		aml_die("unsupported access type 0x%x", flag);
		break;
	}

	if (mode == ACPI_IOREAD)
		_aml_setvalue(val, AML_OBJTYPE_BUFFER, buflen + 2, NULL);

	buf = val->v_buffer;
	buf[0] = EIO;
}

#endif

void
aml_rwindexfield(struct aml_value *fld, struct aml_value *val, int mode)
{
	struct aml_value tmp, *ref1, *ref2;
	void *tbit, *vbit;
	int vpos, bpos, blen;
	int indexval;
	int sz, len;

	ref2 = fld->v_field.ref2;
	ref1 = fld->v_field.ref1;
	bpos = fld->v_field.bitpos;
	blen = fld->v_field.bitlen;

	memset(&tmp, 0, sizeof(tmp));
	tmp.refcnt = 99;

	/* Get field access size */
	switch (AML_FIELD_ACCESS(fld->v_field.flags)) {
	case AML_FIELD_WORDACC:
		sz = 2;
		break;
	case AML_FIELD_DWORDACC:
		sz = 4;
		break;
	case AML_FIELD_QWORDACC:
		sz = 8;
		break;
	default:
		sz = 1;
		break;
	}

	if (blen > aml_intlen) {
		if (mode == ACPI_IOREAD) {
			/* Read from a large field: create buffer */
			_aml_setvalue(val, AML_OBJTYPE_BUFFER,
			    (blen + 7) >> 3, 0);
		}
		vbit = val->v_buffer;
	} else {
		if (mode == ACPI_IOREAD) {
			/* Read from a short field: initialize integer */
			_aml_setvalue(val, AML_OBJTYPE_INTEGER, 0, 0);
		}
		vbit = &val->v_integer;
	}
	tbit = &tmp.v_integer;
	vpos = 0;

	indexval = (bpos >> 3) & ~(sz - 1);
	bpos = bpos - (indexval << 3);

	while (blen > 0) {
		len = min(blen, (sz << 3) - bpos);

		/* Write index register */
		_aml_setvalue(&tmp, AML_OBJTYPE_INTEGER, indexval, 0);
		aml_rwfield(ref2, 0, aml_intlen, &tmp, ACPI_IOWRITE);
		indexval += sz;

		/* Read/write data register */
		_aml_setvalue(&tmp, AML_OBJTYPE_INTEGER, 0, 0);
		if (mode == ACPI_IOWRITE)
			aml_bufcpy(tbit, 0, vbit, vpos, len);
		aml_rwfield(ref1, bpos, len, &tmp, mode);
		if (mode == ACPI_IOREAD)
			aml_bufcpy(vbit, vpos, tbit, 0, len);
		vpos += len;
		blen -= len;
		bpos = 0;
	}
}

void
aml_rwfield(struct aml_value *fld, int bpos, int blen, struct aml_value *val,
    int mode)
{
	struct aml_value tmp, *ref1, *ref2;

	ref2 = fld->v_field.ref2;
	ref1 = fld->v_field.ref1;
	if (blen > fld->v_field.bitlen)
		blen = fld->v_field.bitlen;

	aml_lockfield(NULL, fld);
	memset(&tmp, 0, sizeof(tmp));
	aml_addref(&tmp, "fld.write");
	if (fld->v_field.type == AMLOP_INDEXFIELD) {
		aml_rwindexfield(fld, val, mode);
	} else if (fld->v_field.type == AMLOP_BANKFIELD) {
		_aml_setvalue(&tmp, AML_OBJTYPE_INTEGER, fld->v_field.ref3, 0);
		aml_rwfield(ref2, 0, aml_intlen, &tmp, ACPI_IOWRITE);
		aml_rwgen(ref1, fld->v_field.bitpos, fld->v_field.bitlen,
		    val, mode, fld->v_field.flags);
	} else if (fld->v_field.type == AMLOP_FIELD) {
		switch (ref1->v_opregion.iospace) {
		case ACPI_OPREG_GPIO:
			aml_rwgpio(ref2, bpos, blen, val, mode,
			    fld->v_field.flags);
			break;
		case ACPI_OPREG_GSB:
			aml_rwgsb(ref2, fld->v_field.ref3,
			    fld->v_field.bitpos + bpos, blen,
			    val, mode, fld->v_field.flags);
			break;
		default:
			aml_rwgen(ref1, fld->v_field.bitpos + bpos, blen,
			    val, mode, fld->v_field.flags);
			break;
		}
	} else if (mode == ACPI_IOREAD) {
		/* bufferfield:read */
		_aml_setvalue(val, AML_OBJTYPE_INTEGER, 0, 0);
		aml_bufcpy(&val->v_integer, 0, ref1->v_buffer,
		    fld->v_field.bitpos, fld->v_field.bitlen);
	} else {
		/* bufferfield:write */
		val = aml_convert(val, AML_OBJTYPE_INTEGER, -1);
		aml_bufcpy(ref1->v_buffer, fld->v_field.bitpos, &val->v_integer,
		    0, fld->v_field.bitlen);
		aml_delref(&val, "wrbuffld");
	}
	aml_unlockfield(NULL, fld);
}

/* Create Field Object          data		index
 *   AMLOP_FIELD		n:OpRegion	NULL
 *   AMLOP_INDEXFIELD		n:Field		n:Field
 *   AMLOP_BANKFIELD		n:OpRegion	n:Field
 *   AMLOP_CREATEFIELD		t:Buffer	NULL
 *   AMLOP_CREATEBITFIELD	t:Buffer	NULL
 *   AMLOP_CREATEBYTEFIELD	t:Buffer	NULL
 *   AMLOP_CREATEWORDFIELD	t:Buffer	NULL
 *   AMLOP_CREATEDWORDFIELD	t:Buffer	NULL
 *   AMLOP_CREATEQWORDFIELD	t:Buffer	NULL
 *   AMLOP_INDEX		t:Buffer	NULL
 */
void
aml_createfield(struct aml_value *field, int opcode,
		struct aml_value *data, int bpos, int blen,
		struct aml_value *index, int indexval, int flags)
{
	dnprintf(10, "## %s(%s): %s %.4x-%.4x\n",
	    aml_mnem(opcode, 0),
	    blen > aml_intlen ? "BUF" : "INT",
	    aml_nodename(field->node), bpos, blen);
	if (index) {
		dnprintf(10, "  index:%s:%.2x\n", aml_nodename(index->node),
		    indexval);
	}
	dnprintf(10, "  data:%s\n", aml_nodename(data->node));
	field->type = (opcode == AMLOP_FIELD ||
	    opcode == AMLOP_INDEXFIELD ||
	    opcode == AMLOP_BANKFIELD) ?
	    AML_OBJTYPE_FIELDUNIT :
	    AML_OBJTYPE_BUFFERFIELD;

	if (field->type == AML_OBJTYPE_BUFFERFIELD &&
	    data->type != AML_OBJTYPE_BUFFER)
		data = aml_convert(data, AML_OBJTYPE_BUFFER, -1);

	field->v_field.type = opcode;
	field->v_field.bitpos = bpos;
	field->v_field.bitlen = blen;
	field->v_field.ref3 = indexval;
	field->v_field.ref2 = index;
	field->v_field.ref1 = data;
	field->v_field.flags = flags;

	/* Increase reference count */
	aml_addref(data, "Field.Data");
	aml_addref(index, "Field.Index");
}

/* Parse Field/IndexField/BankField scope */
void
aml_parsefieldlist(struct aml_scope *mscope, int opcode, int flags,
    struct aml_value *data, struct aml_value *index, int indexval)
{
	struct aml_value *conn = NULL;
	struct aml_value *rv;
	int bpos, blen;

	if (mscope == NULL)
		return;
	bpos = 0;
	while (mscope->pos < mscope->end) {
		switch (*mscope->pos) {
		case 0x00: /* ReservedField */
			mscope->pos++;
			blen = aml_parselength(mscope);
			break;
		case 0x01: /* AccessField */
			mscope->pos++;
			blen = 0;
			flags = aml_get8(mscope->pos++);
			flags |= aml_get8(mscope->pos++) << 8;
			break;
		case 0x02: /* ConnectionField */
			mscope->pos++;
			blen = 0;
			conn = aml_parse(mscope, 'o', "Connection");
			if (conn == NULL)
				aml_die("Could not parse connection");
			conn->node = mscope->node;
			break;
		case 0x03: /* ExtendedAccessField */
			mscope->pos++;
			blen = 0;
			flags = aml_get8(mscope->pos++);
			flags |= aml_get8(mscope->pos++) << 8;
			indexval = aml_get8(mscope->pos++);
			break;
		default: /* NamedField */
			mscope->pos = aml_parsename(mscope->node, mscope->pos,
			    &rv, 1);
			blen = aml_parselength(mscope);
			aml_createfield(rv, opcode, data, bpos, blen,
			    conn ? conn : index, indexval, flags);
			aml_delref(&rv, 0);
			break;
		}
		bpos += blen;
	}
	aml_popscope(mscope);
}

/*
 * Mutex/Event utility functions
 */
int	acpi_mutex_acquire(struct aml_scope *, struct aml_value *, int);
void	acpi_mutex_release(struct aml_scope *, struct aml_value *);
int	acpi_event_wait(struct aml_scope *, struct aml_value *, int);
void	acpi_event_signal(struct aml_scope *, struct aml_value *);
void	acpi_event_reset(struct aml_scope *, struct aml_value *);

int
acpi_mutex_acquire(struct aml_scope *scope, struct aml_value *mtx,
    int timeout)
{
	if (mtx->v_mtx.owner == NULL || scope == mtx->v_mtx.owner) {
		/* We are now the owner */
		mtx->v_mtx.owner = scope;
		if (mtx == aml_global_lock) {
			dnprintf(10,"LOCKING GLOBAL\n");
			acpi_glk_enter();
		}
		dnprintf(5,"%s acquires mutex %s\n", scope->node->name,
		    mtx->node->name);
		return (0);
	} else if (timeout == 0) {
		return (-1);
	}
	/* Wait for mutex */
	return (0);
}

void
acpi_mutex_release(struct aml_scope *scope, struct aml_value *mtx)
{
	if (mtx == aml_global_lock) {
		dnprintf(10,"UNLOCKING GLOBAL\n");
		acpi_glk_leave();
	}
	dnprintf(5, "%s releases mutex %s\n", scope->node->name,
	    mtx->node->name);
	mtx->v_mtx.owner = NULL;
	/* Wakeup waiters */
}

int
acpi_event_wait(struct aml_scope *scope, struct aml_value *evt, int timeout)
{
	/* Wait for event to occur; do work in meantime */
	while (evt->v_evt.state == 0 && timeout >= 0) {
		if (acpi_dotask(acpi_softc))
		    continue;
		if (!cold) {
			if (rwsleep(evt, &acpi_softc->sc_lck, PWAIT,
			    "acpievt", 1) == EWOULDBLOCK) {
				if (timeout < AML_NO_TIMEOUT)
					timeout -= (1000 / hz);
			}
		} else {
			delay(1000);
			if (timeout < AML_NO_TIMEOUT)
				timeout--;
		}
	}
	if (evt->v_evt.state == 0)
		return (-1);
	evt->v_evt.state--;
	return (0);
}

void
acpi_event_signal(struct aml_scope *scope, struct aml_value *evt)
{
	evt->v_evt.state++;
	if (evt->v_evt.state > 0)
		wakeup_one(evt);
}

void
acpi_event_reset(struct aml_scope *scope, struct aml_value *evt)
{
	evt->v_evt.state = 0;
}

/* Store result value into an object */
void
aml_store(struct aml_scope *scope, struct aml_value *lhs , int64_t ival,
    struct aml_value *rhs)
{
	struct aml_value tmp;
	struct aml_node *node;
	int mlen;

	/* Already set */
	if (lhs == rhs || lhs == NULL || lhs->type == AML_OBJTYPE_NOTARGET) {
		return;
	}
	memset(&tmp, 0, sizeof(tmp));
	tmp.refcnt=99;
	if (rhs == NULL) {
		rhs = _aml_setvalue(&tmp, AML_OBJTYPE_INTEGER, ival, NULL);
	}
	if (rhs->type == AML_OBJTYPE_BUFFERFIELD ||
	    rhs->type == AML_OBJTYPE_FIELDUNIT) {
		aml_rwfield(rhs, 0, rhs->v_field.bitlen, &tmp, ACPI_IOREAD);
		rhs = &tmp;
	}
	/* Store to LocalX: free value */
	if (lhs->stack >= AMLOP_LOCAL0 && lhs->stack <= AMLOP_LOCAL7)
		aml_freevalue(lhs);

	lhs = aml_gettgt(lhs, AMLOP_STORE);

	/* Store to LocalX: free value again */
	if (lhs->stack >= AMLOP_LOCAL0 && lhs->stack <= AMLOP_LOCAL7)
		aml_freevalue(lhs);
	switch (lhs->type) {
	case AML_OBJTYPE_UNINITIALIZED:
		aml_copyvalue(lhs, rhs);
		break;
	case AML_OBJTYPE_BUFFERFIELD:
	case AML_OBJTYPE_FIELDUNIT:
		aml_rwfield(lhs, 0, lhs->v_field.bitlen, rhs, ACPI_IOWRITE);
		break;
	case AML_OBJTYPE_DEBUGOBJ:
		break;
	case AML_OBJTYPE_INTEGER:
		rhs = aml_convert(rhs, lhs->type, -1);
		lhs->v_integer = rhs->v_integer;
		aml_delref(&rhs, "store.int");
		break;
	case AML_OBJTYPE_BUFFER:
	case AML_OBJTYPE_STRING:
		rhs = aml_convert(rhs, lhs->type, -1);
		if (lhs->length < rhs->length) {
			dnprintf(10, "Overrun! %d,%d\n",
			    lhs->length, rhs->length);
			aml_freevalue(lhs);
			_aml_setvalue(lhs, rhs->type, rhs->length, NULL);
		}
		mlen = min(lhs->length, rhs->length);
		memset(lhs->v_buffer, 0x00, lhs->length);
		memcpy(lhs->v_buffer, rhs->v_buffer, mlen);
		aml_delref(&rhs, "store.bufstr");
		break;
	case AML_OBJTYPE_PACKAGE:
		/* Convert to LHS type, copy into LHS */
		if (rhs->type != AML_OBJTYPE_PACKAGE) {
			aml_die("Copy non-package into package?");
		}
		aml_freevalue(lhs);
		aml_copyvalue(lhs, rhs);
		break;
	case AML_OBJTYPE_NAMEREF:
		node = __aml_searchname(scope->node,
		    aml_getname(lhs->v_nameref), 1);
		if (node == NULL) {
			aml_die("Could not create node %s",
			    aml_getname(lhs->v_nameref));
		}
		aml_copyvalue(node->value, rhs);
		break;
	case AML_OBJTYPE_METHOD:
		/* Method override */
		if (rhs->type != AML_OBJTYPE_INTEGER) {
			aml_die("Overriding a method with a non-int?");
		}
		aml_freevalue(lhs);
		aml_copyvalue(lhs, rhs);
		break;
	default:
		aml_die("Store to default type!	 %x\n", lhs->type);
		break;
	}
	aml_freevalue(&tmp);
}

#ifdef DDB
/* Disassembler routines */
void aml_disprintf(void *arg, const char *fmt, ...);

void
aml_disprintf(void *arg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

void
aml_disasm(struct aml_scope *scope, int lvl,
    void (*dbprintf)(void *, const char *, ...)
	    __attribute__((__format__(__kprintf__,2,3))),
    void *arg)
{
	int pc, opcode;
	struct aml_opcode *htab;
	uint64_t ival;
	struct aml_value *rv, tmp;
	uint8_t *end = NULL;
	struct aml_scope ms;
	char *ch;
	char  mch[64];

	if (dbprintf == NULL)
		dbprintf = aml_disprintf;

	pc = aml_pc(scope->pos);
	opcode = aml_parseopcode(scope);
	htab = aml_findopcode(opcode);

	/* Display address + indent */
	if (lvl <= 0x7FFF) {
		dbprintf(arg, "%.4x ", pc);
		for (pc=0; pc<lvl; pc++) {
			dbprintf(arg, "	 ");
		}
	}
	ch = NULL;
	switch (opcode) {
	case AMLOP_NAMECHAR:
		scope->pos = aml_parsename(scope->node, scope->pos, &rv, 0);
		if (rv->type == AML_OBJTYPE_NAMEREF) {
			ch = "@@@";
			aml_delref(&rv, "disasm");
			break;
		}
		/* if this is a method, get arguments */
		strlcpy(mch, aml_nodename(rv->node), sizeof(mch));
		if (rv->type == AML_OBJTYPE_METHOD) {
			strlcat(mch, "(", sizeof(mch));
			for (ival=0;
			    ival < AML_METHOD_ARGCOUNT(rv->v_method.flags);
			    ival++) {
				strlcat(mch, ival ? ", %z" : "%z",
				    sizeof(mch));
			}
			strlcat(mch, ")", sizeof(mch));
		}
		aml_delref(&rv, "");
		ch = mch;
		break;

	case AMLOP_ZERO:
	case AMLOP_ONE:
	case AMLOP_ONES:
	case AMLOP_LOCAL0:
	case AMLOP_LOCAL1:
	case AMLOP_LOCAL2:
	case AMLOP_LOCAL3:
	case AMLOP_LOCAL4:
	case AMLOP_LOCAL5:
	case AMLOP_LOCAL6:
	case AMLOP_LOCAL7:
	case AMLOP_ARG0:
	case AMLOP_ARG1:
	case AMLOP_ARG2:
	case AMLOP_ARG3:
	case AMLOP_ARG4:
	case AMLOP_ARG5:
	case AMLOP_ARG6:
	case AMLOP_NOP:
	case AMLOP_REVISION:
	case AMLOP_DEBUG:
	case AMLOP_CONTINUE:
	case AMLOP_BREAKPOINT:
	case AMLOP_BREAK:
		ch="%m";
		break;
	case AMLOP_BYTEPREFIX:
		ch="%b";
		break;
	case AMLOP_WORDPREFIX:
		ch="%w";
		break;
	case AMLOP_DWORDPREFIX:
		ch="%d";
		break;
	case AMLOP_QWORDPREFIX:
		ch="%q";
		break;
	case AMLOP_STRINGPREFIX:
		ch="%a";
		break;

	case AMLOP_INCREMENT:
	case AMLOP_DECREMENT:
	case AMLOP_LNOT:
	case AMLOP_SIZEOF:
	case AMLOP_DEREFOF:
	case AMLOP_REFOF:
	case AMLOP_OBJECTTYPE:
	case AMLOP_UNLOAD:
	case AMLOP_RELEASE:
	case AMLOP_SIGNAL:
	case AMLOP_RESET:
	case AMLOP_STALL:
	case AMLOP_SLEEP:
	case AMLOP_RETURN:
		ch="%m(%n)";
		break;
	case AMLOP_OR:
	case AMLOP_ADD:
	case AMLOP_AND:
	case AMLOP_NAND:
	case AMLOP_XOR:
	case AMLOP_SHL:
	case AMLOP_SHR:
	case AMLOP_NOR:
	case AMLOP_MOD:
	case AMLOP_SUBTRACT:
	case AMLOP_MULTIPLY:
	case AMLOP_INDEX:
	case AMLOP_CONCAT:
	case AMLOP_CONCATRES:
	case AMLOP_TOSTRING:
		ch="%m(%n, %n, %n)";
		break;
	case AMLOP_CREATEBYTEFIELD:
	case AMLOP_CREATEWORDFIELD:
	case AMLOP_CREATEDWORDFIELD:
	case AMLOP_CREATEQWORDFIELD:
	case AMLOP_CREATEBITFIELD:
		ch="%m(%n, %n, %N)";
		break;
	case AMLOP_CREATEFIELD:
		ch="%m(%n, %n, %n, %N)";
		break;
	case AMLOP_DIVIDE:
	case AMLOP_MID:
		ch="%m(%n, %n, %n, %n)";
		break;
	case AMLOP_LAND:
	case AMLOP_LOR:
	case AMLOP_LNOTEQUAL:
	case AMLOP_LLESSEQUAL:
	case AMLOP_LLESS:
	case AMLOP_LEQUAL:
	case AMLOP_LGREATEREQUAL:
	case AMLOP_LGREATER:
	case AMLOP_NOT:
	case AMLOP_FINDSETLEFTBIT:
	case AMLOP_FINDSETRIGHTBIT:
	case AMLOP_TOINTEGER:
	case AMLOP_TOBUFFER:
	case AMLOP_TOHEXSTRING:
	case AMLOP_TODECSTRING:
	case AMLOP_FROMBCD:
	case AMLOP_TOBCD:
	case AMLOP_WAIT:
	case AMLOP_LOAD:
	case AMLOP_STORE:
	case AMLOP_NOTIFY:
	case AMLOP_COPYOBJECT:
		ch="%m(%n, %n)";
		break;
	case AMLOP_ACQUIRE:
		ch = "%m(%n, %w)";
		break;
	case AMLOP_CONDREFOF:
		ch="%m(%R, %n)";
		break;
	case AMLOP_ALIAS:
		ch="%m(%n, %N)";
		break;
	case AMLOP_NAME:
		ch="%m(%N, %n)";
		break;
	case AMLOP_EVENT:
		ch="%m(%N)";
		break;
	case AMLOP_MUTEX:
		ch = "%m(%N, %b)";
		break;
	case AMLOP_OPREGION:
		ch = "%m(%N, %b, %n, %n)";
		break;
	case AMLOP_DATAREGION:
		ch="%m(%N, %n, %n, %n)";
		break;
	case AMLOP_FATAL:
		ch = "%m(%b, %d, %n)";
		break;
	case AMLOP_IF:
	case AMLOP_WHILE:
	case AMLOP_SCOPE:
	case AMLOP_THERMALZONE:
	case AMLOP_VARPACKAGE:
		end = aml_parseend(scope);
		ch = "%m(%n) {\n%T}";
		break;
	case AMLOP_DEVICE:
		end = aml_parseend(scope);
		ch = "%m(%N) {\n%T}";
		break;
	case AMLOP_POWERRSRC:
		end = aml_parseend(scope);
		ch = "%m(%N, %b, %w) {\n%T}";
		break;
	case AMLOP_PROCESSOR:
		end = aml_parseend(scope);
		ch = "%m(%N, %b, %d, %b) {\n%T}";
		break;
	case AMLOP_METHOD:
		end = aml_parseend(scope);
		ch = "%m(%N, %b) {\n%T}";
		break;
	case AMLOP_PACKAGE:
		end = aml_parseend(scope);
		ch = "%m(%b) {\n%T}";
		break;
	case AMLOP_ELSE:
		end = aml_parseend(scope);
		ch = "%m {\n%T}";
		break;
	case AMLOP_BUFFER:
		end = aml_parseend(scope);
		ch = "%m(%n) { %B }";
		break;
	case AMLOP_INDEXFIELD:
		end = aml_parseend(scope);
		ch = "%m(%n, %n, %b) {\n%F}";
		break;
	case AMLOP_BANKFIELD:
		end = aml_parseend(scope);
		ch = "%m(%n, %n, %n, %b) {\n%F}";
		break;
	case AMLOP_FIELD:
		end = aml_parseend(scope);
		ch = "%m(%n, %b) {\n%F}";
		break;
	case AMLOP_MATCH:
		ch = "%m(%n, %b, %n, %b, %n, %n)";
		break;
	case AMLOP_LOADTABLE:
		ch = "%m(%n, %n, %n, %n, %n, %n)";
		break;
	default:
		aml_die("opcode = %x\n", opcode);
		break;
	}

	/* Parse printable buffer args */
	while (ch && *ch) {
		char c;

		if (*ch != '%') {
			dbprintf(arg,"%c", *(ch++));
			continue;
		}
		c = *(++ch);
		switch (c) {
		case 'b':
		case 'w':
		case 'd':
		case 'q':
			/* Parse simple object: don't allocate */
			aml_parsesimple(scope, c, &tmp);
			dbprintf(arg,"0x%llx", tmp.v_integer);
			break;
		case 'a':
			dbprintf(arg, "\'%s\'", scope->pos);
			scope->pos += strlen(scope->pos)+1;
			break;
		case 'N':
			/* Create Name */
			rv = aml_parsesimple(scope, c, NULL);
			dbprintf(arg, "%s", aml_nodename(rv->node));
			break;
		case 'm':
			/* display mnemonic */
			dbprintf(arg, "%s", htab->mnem);
			break;
		case 'R':
			/* Search name */
			printf("%s", aml_getname(scope->pos));
			scope->pos = aml_parsename(scope->node, scope->pos,
			    &rv, 0);
			aml_delref(&rv, 0);
			break;
		case 'z':
		case 'n':
			/* generic arg: recurse */
			aml_disasm(scope, lvl | 0x8000, dbprintf, arg);
			break;
		case 'B':
			/* Buffer */
			scope->pos = end;
			break;
		case 'F':
			/* Scope: Field List */
			memset(&ms, 0, sizeof(ms));
			ms.node = scope->node;
			ms.start = scope->pos;
			ms.end = end;
			ms.pos = ms.start;
			ms.type = AMLOP_FIELD;

			while (ms.pos < ms.end) {
				if (*ms.pos == 0x00) {
					ms.pos++;
					aml_parselength(&ms);
				} else if (*ms.pos == 0x01) {
					ms.pos+=3;
				} else {
					ms.pos = aml_parsename(ms.node,
					     ms.pos, &rv, 1);
					aml_parselength(&ms);
					dbprintf(arg,"	%s\n",
					    aml_nodename(rv->node));
					aml_delref(&rv, 0);
				}
			}

			/* Display address and closing bracket */
			dbprintf(arg,"%.4x ", aml_pc(scope->pos));
			for (pc=0; pc<(lvl & 0x7FFF); pc++) {
				dbprintf(arg,"  ");
			}
			scope->pos = end;
			break;
		case 'T':
			/* Scope: Termlist */
			memset(&ms, 0, sizeof(ms));
			ms.node = scope->node;
			ms.start = scope->pos;
			ms.end = end;
			ms.pos = ms.start;
			ms.type = AMLOP_SCOPE;

			while (ms.pos < ms.end) {
				aml_disasm(&ms, (lvl + 1) & 0x7FFF,
				    dbprintf, arg);
			}

			/* Display address and closing bracket */
			dbprintf(arg,"%.4x ", aml_pc(scope->pos));
			for (pc=0; pc<(lvl & 0x7FFF); pc++) {
				dbprintf(arg,"  ");
			}
			scope->pos = end;
			break;
		}
		ch++;
	}
	if (lvl <= 0x7FFF) {
		dbprintf(arg,"\n");
	}
}
#endif /* DDB */

int aml_busy;

/* Evaluate method or buffervalue objects */
struct aml_value *
aml_eval(struct aml_scope *scope, struct aml_value *my_ret, int ret_type,
    int argc, struct aml_value *argv)
{
	struct aml_value *tmp = my_ret;
	struct aml_scope *ms;
	int idx;

	switch (tmp->type) {
	case AML_OBJTYPE_NAMEREF:
		my_ret = aml_seterror(scope, "Undefined name: %s",
		    aml_getname(my_ret->v_nameref));
		break;
	case AML_OBJTYPE_METHOD:
		dnprintf(10,"\n--== Eval Method [%s, %d args] to %c ==--\n",
		    aml_nodename(tmp->node),
		    AML_METHOD_ARGCOUNT(tmp->v_method.flags),
		    ret_type);
		ms = aml_pushscope(scope, tmp, tmp->node, AMLOP_METHOD);

		/* Parse method arguments */
		for (idx=0; idx<AML_METHOD_ARGCOUNT(tmp->v_method.flags); idx++) {
			struct aml_value *sp;

			sp = aml_getstack(ms, AMLOP_ARG0+idx);
			if (argv) {
				aml_copyvalue(sp, &argv[idx]);
			} else {
				_aml_setvalue(sp, AML_OBJTYPE_OBJREF, AMLOP_ARG0 + idx, 0);
				sp->v_objref.ref = aml_parse(scope, 't', "ARGX");
			}
		}
#ifdef ACPI_DEBUG
		aml_showstack(ms);
#endif

		/* Evaluate method scope */
		aml_root.start = tmp->v_method.base;
		if (tmp->v_method.fneval != NULL) {
			my_ret = tmp->v_method.fneval(ms, NULL);
		} else {
			aml_parse(ms, 'T', "METHEVAL");
			my_ret = ms->retv;
		}
		dnprintf(10,"\n--==Finished evaluating method: %s %c\n",
		    aml_nodename(tmp->node), ret_type);
#ifdef ACPI_DEBUG
		aml_showvalue(my_ret);
		aml_showstack(ms);
#endif
		aml_popscope(ms);
		break;
	case AML_OBJTYPE_BUFFERFIELD:
	case AML_OBJTYPE_FIELDUNIT:
		my_ret = aml_allocvalue(0,0,NULL);
		dnprintf(20,"quick: Convert Bufferfield to %c %p\n",
		    ret_type, my_ret);
		aml_rwfield(tmp, 0, tmp->v_field.bitlen, my_ret, ACPI_IOREAD);
		break;
	}
	if (ret_type == 'i' && my_ret && my_ret->type != AML_OBJTYPE_INTEGER) {
#ifndef SMALL_KERNEL
		aml_showvalue(my_ret);
#endif
		aml_die("Not Integer");
	}
	return my_ret;
}

/*
 * The following opcodes produce return values
 *   TOSTRING	-> Str
 *   TOHEXSTR	-> Str
 *   TODECSTR	-> Str
 *   STRINGPFX	-> Str
 *   BUFFER	-> Buf
 *   CONCATRES	-> Buf
 *   TOBUFFER	-> Buf
 *   MID	-> Buf|Str
 *   CONCAT	-> Buf|Str
 *   PACKAGE	-> Pkg
 *   VARPACKAGE -> Pkg
 *   LOCALx	-> Obj
 *   ARGx	-> Obj
 *   NAMECHAR	-> Obj
 *   REFOF	-> ObjRef
 *   INDEX	-> ObjRef
 *   DEREFOF	-> DataRefObj
 *   COPYOBJECT -> DataRefObj
 *   STORE	-> DataRefObj

 *   ZERO	-> Int
 *   ONE	-> Int
 *   ONES	-> Int
 *   REVISION	-> Int
 *   B/W/D/Q	-> Int
 *   OR		-> Int
 *   AND	-> Int
 *   ADD	-> Int
 *   NAND	-> Int
 *   XOR	-> Int
 *   SHL	-> Int
 *   SHR	-> Int
 *   NOR	-> Int
 *   MOD	-> Int
 *   SUBTRACT	-> Int
 *   MULTIPLY	-> Int
 *   DIVIDE	-> Int
 *   NOT	-> Int
 *   TOBCD	-> Int
 *   FROMBCD	-> Int
 *   FSLEFTBIT	-> Int
 *   FSRIGHTBIT -> Int
 *   INCREMENT	-> Int
 *   DECREMENT	-> Int
 *   TOINTEGER	-> Int
 *   MATCH	-> Int
 *   SIZEOF	-> Int
 *   OBJECTTYPE -> Int
 *   TIMER	-> Int

 *   CONDREFOF	-> Bool
 *   ACQUIRE	-> Bool
 *   WAIT	-> Bool
 *   LNOT	-> Bool
 *   LAND	-> Bool
 *   LOR	-> Bool
 *   LLESS	-> Bool
 *   LEQUAL	-> Bool
 *   LGREATER	-> Bool
 *   LNOTEQUAL	-> Bool
 *   LLESSEQUAL -> Bool
 *   LGREATEREQ -> Bool

 *   LOADTABLE	-> DDB
 *   DEBUG	-> Debug

 *   The following opcodes do not generate a return value:
 *   NOP
 *   BREAKPOINT
 *   RELEASE
 *   RESET
 *   SIGNAL
 *   NAME
 *   ALIAS
 *   OPREGION
 *   DATAREGION
 *   EVENT
 *   MUTEX
 *   SCOPE
 *   DEVICE
 *   THERMALZONE
 *   POWERRSRC
 *   PROCESSOR
 *   METHOD
 *   CREATEFIELD
 *   CREATEBITFIELD
 *   CREATEBYTEFIELD
 *   CREATEWORDFIELD
 *   CREATEDWORDFIELD
 *   CREATEQWORDFIELD
 *   FIELD
 *   INDEXFIELD
 *   BANKFIELD
 *   STALL
 *   SLEEP
 *   NOTIFY
 *   FATAL
 *   LOAD
 *   UNLOAD
 *   IF
 *   ELSE
 *   WHILE
 *   BREAK
 *   CONTINUE
 */

/* Parse a simple object from AML Bytestream */
struct aml_value *
aml_parsesimple(struct aml_scope *scope, char ch, struct aml_value *rv)
{
	if (rv == NULL)
		rv = aml_allocvalue(0,0,NULL);
	switch (ch) {
	case AML_ARG_REVISION:
		_aml_setvalue(rv, AML_OBJTYPE_INTEGER, AML_REVISION, NULL);
		break;
	case AML_ARG_DEBUG:
		_aml_setvalue(rv, AML_OBJTYPE_DEBUGOBJ, 0, NULL);
		break;
	case AML_ARG_BYTE:
		_aml_setvalue(rv, AML_OBJTYPE_INTEGER,
		    aml_get8(scope->pos), NULL);
		scope->pos += 1;
		break;
	case AML_ARG_WORD:
		_aml_setvalue(rv, AML_OBJTYPE_INTEGER,
		    aml_get16(scope->pos), NULL);
		scope->pos += 2;
		break;
	case AML_ARG_DWORD:
		_aml_setvalue(rv, AML_OBJTYPE_INTEGER,
		    aml_get32(scope->pos), NULL);
		scope->pos += 4;
		break;
	case AML_ARG_QWORD:
		_aml_setvalue(rv, AML_OBJTYPE_INTEGER,
		    aml_get64(scope->pos), NULL);
		scope->pos += 8;
		break;
	case AML_ARG_STRING:
		_aml_setvalue(rv, AML_OBJTYPE_STRING, -1, scope->pos);
		scope->pos += rv->length+1;
		break;
	}
	return rv;
}

/*
 * Main Opcode Parser/Evaluator
 *
 * ret_type is expected type for return value
 *   'o' = Data Object (Int/Str/Buf/Pkg/Name)
 *   'i' = Integer
 *   't' = TermArg     (Int/Str/Buf/Pkg)
 *   'r' = Target      (NamedObj/Local/Arg/Null)
 *   'S' = SuperName   (NamedObj/Local/Arg)
 *   'T' = TermList
 */
#define aml_debugger(x)

int maxdp;

struct aml_value *
aml_gettgt(struct aml_value *val, int opcode)
{
	while (val && val->type == AML_OBJTYPE_OBJREF) {
		val = val->v_objref.ref;
	}
	return val;
}

struct aml_value *
aml_seterror(struct aml_scope *scope, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	printf("### AML PARSE ERROR (0x%x): ", aml_pc(scope->pos));
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);

	while (scope) {
		scope->pos = scope->end;
		scope = scope->parent;
	}
	aml_error++;
	return aml_allocvalue(AML_OBJTYPE_INTEGER, 0, 0);
}

struct aml_value *
aml_loadtable(struct acpi_softc *sc, const char *signature,
     const char *oemid, const char *oemtableid, const char *rootpath,
     const char *parameterpath, struct aml_value *parameterdata)
{
	struct acpi_table_header *hdr;
	struct acpi_dsdt *p_dsdt;
	struct acpi_q *entry;

	if (strlen(parameterpath) > 0)
		aml_die("LoadTable: ParameterPathString unsupported");

	SIMPLEQ_FOREACH(entry, &sc->sc_tables, q_next) {
		hdr = entry->q_table;
		if (strncmp(hdr->signature, signature,
		    sizeof(hdr->signature)) == 0 &&
		    strncmp(hdr->oemid, oemid, sizeof(hdr->oemid)) == 0 &&
		    strncmp(hdr->oemtableid, oemtableid,
		    sizeof(hdr->oemtableid)) == 0) {
			p_dsdt = entry->q_table;
			acpi_parse_aml(sc, rootpath, p_dsdt->aml,
			    p_dsdt->hdr_length - sizeof(p_dsdt->hdr));
			return aml_allocvalue(AML_OBJTYPE_DDBHANDLE, 0, 0);
		}
	}

	return aml_allocvalue(AML_OBJTYPE_INTEGER, 0, 0);
}

/* Load new SSDT scope from memory address */
struct aml_scope *
aml_load(struct acpi_softc *sc, struct aml_scope *scope,
    struct aml_value *rgn, struct aml_value *ddb)
{
	struct acpi_q *entry;
	struct acpi_dsdt *p_ssdt;
	struct aml_value tmp;

	ddb->type = AML_OBJTYPE_DDBHANDLE;
	ddb->v_integer = 0;

	memset(&tmp, 0, sizeof(tmp));
	if (rgn->type != AML_OBJTYPE_OPREGION ||
	    rgn->v_opregion.iospace != GAS_SYSTEM_MEMORY)
		goto fail;

	/* Load SSDT from memory */
	entry = acpi_maptable(sc, rgn->v_opregion.iobase, "SSDT", NULL, NULL, 1);
	if (entry == NULL)
		goto fail;

	dnprintf(10, "%s: loaded SSDT %s @ %llx\n", sc->sc_dev.dv_xname,
	    aml_nodename(rgn->node), rgn->v_opregion.iobase);
	ddb->v_integer = entry->q_id;

	p_ssdt = entry->q_table;
	tmp.v_buffer = p_ssdt->aml;
	tmp.length   = p_ssdt->hdr_length - sizeof(p_ssdt->hdr);

	return aml_pushscope(scope, &tmp, scope->node,
	    AMLOP_LOAD);
fail:
	printf("%s: unable to load %s\n", sc->sc_dev.dv_xname,
	    aml_nodename(rgn->node));
	return NULL;
}

struct aml_value *
aml_parse(struct aml_scope *scope, int ret_type, const char *stype)
{
	int    opcode, idx, pc;
	struct aml_opcode *htab;
	struct aml_value *opargs[8], *my_ret, *rv;
	struct aml_scope *mscope, *iscope;
	uint8_t *start, *end;
	const char *ch;
	int64_t ival, rem;
	struct timespec ts;

	my_ret = NULL;
	if (scope == NULL || scope->pos >= scope->end) {
		return NULL;
	}
	if (odp++ > 125)
		panic("depth");
	if (odp > maxdp) {
		maxdp = odp;
		dnprintf(10, "max depth: %d\n", maxdp);
	}
	end = NULL;
	iscope = scope;
 start:
	/* --== Stage 0: Get Opcode ==-- */
	start = scope->pos;
	pc = aml_pc(scope->pos);
	aml_debugger(scope);

	opcode = aml_parseopcode(scope);
	htab = aml_findopcode(opcode);
	if (htab == NULL) {
		/* No opcode handler */
		aml_die("Unknown opcode: %.4x @ %.4x", opcode, pc);
	}
	dnprintf(18,"%.4x %s\n", pc, aml_mnem(opcode, scope->pos));

	/* --== Stage 1: Process opcode arguments ==-- */
	memset(opargs, 0, sizeof(opargs));
	idx = 0;
	for (ch = htab->args; *ch; ch++) {
		rv = NULL;
		switch (*ch) {
		case AML_ARG_OBJLEN:
			end = aml_parseend(scope);
			break;
		case AML_ARG_IFELSE:
                        /* Special Case: IF-ELSE:piTbpT or IF:piT */
			ch = (*end == AMLOP_ELSE && end < scope->end) ?
			    "-TbpT" : "-T";
			break;

			/* Complex arguments */
		case 's':
		case 'S':
		case AML_ARG_TARGET:
		case AML_ARG_TERMOBJ:
		case AML_ARG_INTEGER:
			if (*ch == 'r' && *scope->pos == AMLOP_ZERO) {
				/* Special case: NULL Target */
				rv = aml_allocvalue(AML_OBJTYPE_NOTARGET, 0, NULL);
				scope->pos++;
			}
			else {
				rv = aml_parse(scope, *ch, htab->mnem);
				if (rv == NULL || aml_error)
					goto parse_error;
			}
			break;

			/* Simple arguments */
		case AML_ARG_BUFFER:
		case AML_ARG_METHOD:
		case AML_ARG_FIELDLIST:
		case AML_ARG_TERMOBJLIST:
			rv = aml_allocvalue(AML_OBJTYPE_SCOPE, 0, NULL);
			rv->v_buffer = scope->pos;
			rv->length = end - scope->pos;
			scope->pos = end;
			break;
		case AML_ARG_CONST:
			rv = aml_allocvalue(AML_OBJTYPE_INTEGER,
			    (char)opcode, NULL);
			break;
		case AML_ARG_CREATENAME:
			scope->pos = aml_parsename(scope->node, scope->pos,
			    &rv, 1);
			break;
		case AML_ARG_SEARCHNAME:
			scope->pos = aml_parsename(scope->node, scope->pos,
			    &rv, 0);
			break;
		case AML_ARG_BYTE:
		case AML_ARG_WORD:
		case AML_ARG_DWORD:
		case AML_ARG_QWORD:
		case AML_ARG_DEBUG:
		case AML_ARG_STRING:
		case AML_ARG_REVISION:
			rv = aml_parsesimple(scope, *ch, NULL);
			break;
		case AML_ARG_STKLOCAL:
		case AML_ARG_STKARG:
			rv = aml_getstack(scope, opcode);
			break;
		default:
			aml_die("Unknown arg type: %c\n", *ch);
			break;
		}
		if (rv != NULL)
			opargs[idx++] = rv;
		}

	/* --== Stage 2: Process opcode ==-- */
	ival = 0;
	my_ret = NULL;
	mscope = NULL;
	switch (opcode) {
	case AMLOP_NOP:
	case AMLOP_BREAKPOINT:
		break;
	case AMLOP_LOCAL0:
	case AMLOP_LOCAL1:
	case AMLOP_LOCAL2:
	case AMLOP_LOCAL3:
	case AMLOP_LOCAL4:
	case AMLOP_LOCAL5:
	case AMLOP_LOCAL6:
	case AMLOP_LOCAL7:
	case AMLOP_ARG0:
	case AMLOP_ARG1:
	case AMLOP_ARG2:
	case AMLOP_ARG3:
	case AMLOP_ARG4:
	case AMLOP_ARG5:
	case AMLOP_ARG6:
		my_ret = opargs[0];
		aml_addref(my_ret, htab->mnem);
		break;
	case AMLOP_NAMECHAR:
		/* opargs[0] = named object (node != NULL), or nameref */
		my_ret = opargs[0];
		if (scope->type == AMLOP_PACKAGE && my_ret->node) {
			/* Special case for package */
			my_ret = aml_allocvalue(AML_OBJTYPE_OBJREF,
			    AMLOP_NAMECHAR, 0);
			my_ret->v_objref.ref = opargs[0];
			aml_addref(my_ret, "package");
		} else if (my_ret->type == AML_OBJTYPE_OBJREF) {
			my_ret = my_ret->v_objref.ref;
			aml_addref(my_ret, "de-alias");
		}
		if (ret_type == 'i' || ret_type == 't' || ret_type == 'T') {
			/* Return TermArg or Integer: Evaluate object */
			my_ret = aml_eval(scope, my_ret, ret_type, 0, NULL);
		} else if (my_ret->type == AML_OBJTYPE_METHOD) {
			/* This should only happen with CondRef */
			dnprintf(12,"non-termarg method : %s\n", stype);
			aml_addref(my_ret, "zoom");
		}
		break;

	case AMLOP_ZERO:
	case AMLOP_ONE:
	case AMLOP_ONES:
	case AMLOP_DEBUG:
	case AMLOP_REVISION:
	case AMLOP_BYTEPREFIX:
	case AMLOP_WORDPREFIX:
	case AMLOP_DWORDPREFIX:
	case AMLOP_QWORDPREFIX:
	case AMLOP_STRINGPREFIX:
		my_ret = opargs[0];
		break;

	case AMLOP_BUFFER:
		/* Buffer: iB => Buffer */
		my_ret = aml_allocvalue(AML_OBJTYPE_BUFFER,
		    opargs[0]->v_integer, NULL);
		memcpy(my_ret->v_buffer, opargs[1]->v_buffer,
		    opargs[1]->length);
		break;
	case AMLOP_PACKAGE:
	case AMLOP_VARPACKAGE:
		/* Package/VarPackage: bT/iT => Package */
		my_ret = aml_allocvalue(AML_OBJTYPE_PACKAGE,
		    opargs[0]->v_integer, 0);
		mscope = aml_pushscope(scope, opargs[1], scope->node,
		    AMLOP_PACKAGE);

		/* Recursively parse package contents */
		for (idx=0; idx<my_ret->length; idx++) {
			rv = aml_parse(mscope, 'o', "Package");
			if (rv != NULL) {
				aml_delref(&my_ret->v_package[idx], "pkginit");
				my_ret->v_package[idx] = rv;
			}
		}
		aml_popscope(mscope);
		mscope = NULL;
		break;

		/* Math/Logical operations */
	case AMLOP_OR:
	case AMLOP_ADD:
	case AMLOP_AND:
	case AMLOP_NAND:
	case AMLOP_XOR:
	case AMLOP_SHL:
	case AMLOP_SHR:
	case AMLOP_NOR:
	case AMLOP_MOD:
	case AMLOP_SUBTRACT:
	case AMLOP_MULTIPLY:
		/* XXX: iir => I */
		ival = aml_evalexpr(opargs[0]->v_integer,
		    opargs[1]->v_integer, opcode);
		aml_store(scope, opargs[2], ival, NULL);
		break;
	case AMLOP_DIVIDE:
		/* Divide: iirr => I */
		if (opargs[1]->v_integer == 0) {
			my_ret = aml_seterror(scope, "Divide by Zero!");
			break;
		}
		rem = aml_evalexpr(opargs[0]->v_integer,
		    opargs[1]->v_integer, AMLOP_MOD);
		ival = aml_evalexpr(opargs[0]->v_integer,
		    opargs[1]->v_integer, AMLOP_DIVIDE);
		aml_store(scope, opargs[2], rem, NULL);
		aml_store(scope, opargs[3], ival, NULL);
		break;
	case AMLOP_NOT:
	case AMLOP_TOBCD:
	case AMLOP_FROMBCD:
	case AMLOP_FINDSETLEFTBIT:
	case AMLOP_FINDSETRIGHTBIT:
		/* XXX: ir => I */
		ival = aml_evalexpr(opargs[0]->v_integer, 0, opcode);
		aml_store(scope, opargs[1], ival, NULL);
		break;
	case AMLOP_INCREMENT:
	case AMLOP_DECREMENT:
		/* Inc/Dec: S => I */
		my_ret = aml_eval(scope, opargs[0], AML_ARG_INTEGER, 0, NULL);
		ival = aml_evalexpr(my_ret->v_integer, 1, opcode);
		aml_store(scope, opargs[0], ival, NULL);
		break;
	case AMLOP_LNOT:
		/* LNot: i => Bool */
		ival = aml_evalexpr(opargs[0]->v_integer, 0, opcode);
		break;
	case AMLOP_LOR:
	case AMLOP_LAND:
		/* XXX: ii => Bool */
		ival = aml_evalexpr(opargs[0]->v_integer,
		    opargs[1]->v_integer, opcode);
		break;
	case AMLOP_LLESS:
	case AMLOP_LEQUAL:
	case AMLOP_LGREATER:
	case AMLOP_LNOTEQUAL:
	case AMLOP_LLESSEQUAL:
	case AMLOP_LGREATEREQUAL:
		/* XXX: tt => Bool */
		ival = aml_compare(opargs[0], opargs[1], opcode);
		break;

		/* Reference/Store operations */
	case AMLOP_CONDREFOF:
		/* CondRef: rr => I */
		ival = 0;
		if (opargs[0]->node != NULL) {
			/* Create Object Reference */
			rv = aml_allocvalue(AML_OBJTYPE_OBJREF, opcode,
				opargs[0]);
			aml_addref(opargs[0], "CondRef");
			aml_store(scope, opargs[1], 0, rv);
			aml_delref(&rv, 0);

			/* Mark that we found it */
			ival = -1;
		}
		break;
	case AMLOP_REFOF:
		/* RefOf: r => ObjRef */
		my_ret = aml_allocvalue(AML_OBJTYPE_OBJREF, opcode, opargs[0]);
		aml_addref(my_ret->v_objref.ref, "RefOf");
		break;
	case AMLOP_INDEX:
		/* Index: tir => ObjRef */
		idx = opargs[1]->v_integer;
		/* Reading past the end of the array? - Ignore */
		if (idx >= opargs[0]->length || idx < 0)
			break;
		switch (opargs[0]->type) {
		case AML_OBJTYPE_PACKAGE:
			/* Don't set opargs[0] to NULL */
			if (ret_type == 't' || ret_type == 'i' || ret_type == 'T') {
				my_ret = opargs[0]->v_package[idx];
				aml_addref(my_ret, "Index.Package");
			} else {
				my_ret = aml_allocvalue(AML_OBJTYPE_OBJREF, AMLOP_PACKAGE,
				    opargs[0]->v_package[idx]);
				aml_addref(my_ret->v_objref.ref,
				    "Index.Package");
			}
			break;
		case AML_OBJTYPE_BUFFER:
		case AML_OBJTYPE_STRING:
		case AML_OBJTYPE_INTEGER:
			rv = aml_convert(opargs[0], AML_OBJTYPE_BUFFER, -1);
			if (ret_type == 't' || ret_type == 'i' || ret_type == 'T') {
				dnprintf(12,"Index.Buf Term: %d = %x\n",
				    idx, rv->v_buffer[idx]);
				ival = rv->v_buffer[idx];
			} else {
				dnprintf(12, "Index.Buf Targ\n");
				my_ret = aml_allocvalue(0,0,NULL);
				aml_createfield(my_ret, AMLOP_INDEX, rv,
				    8 * idx, 8, NULL, 0, AML_FIELD_BYTEACC);
			}
			aml_delref(&rv, "Index.BufStr");
			break;
		default:
			aml_die("Unknown index : %x\n", opargs[0]->type);
			break;
		}
		aml_store(scope, opargs[2], ival, my_ret);
		break;
	case AMLOP_DEREFOF:
		/* DerefOf: t:ObjRef => DataRefObj */
		if (opargs[0]->type == AML_OBJTYPE_OBJREF) {
			my_ret = opargs[0]->v_objref.ref;
			aml_addref(my_ret, "DerefOf");
		} else {
			my_ret = opargs[0];
			//aml_addref(my_ret, "DerefOf");
		}
		break;
	case AMLOP_COPYOBJECT:
		/* CopyObject: t:DataRefObj, s:implename => DataRefObj */
		my_ret = opargs[0];
		aml_freevalue(opargs[1]);
		aml_copyvalue(opargs[1], opargs[0]);
		break;
	case AMLOP_STORE:
		/* Store: t:DataRefObj, S:upername => DataRefObj */
		my_ret = opargs[0];
		aml_store(scope, opargs[1], 0, opargs[0]);
		break;

		/* Conversion */
	case AMLOP_TOINTEGER:
		/* Source:CData, Result => Integer */
		my_ret = aml_convert(opargs[0], AML_OBJTYPE_INTEGER, -1);
		aml_store(scope, opargs[1], 0, my_ret);
		break;
	case AMLOP_TOBUFFER:
		/* Source:CData, Result => Buffer */
		my_ret = aml_convert(opargs[0], AML_OBJTYPE_BUFFER, -1);
		aml_store(scope, opargs[1], 0, my_ret);
		break;
	case AMLOP_TOHEXSTRING:
		/* Source:CData, Result => String */
		my_ret = aml_convert(opargs[0], AML_OBJTYPE_HEXSTRING, -1);
		aml_store(scope, opargs[1], 0, my_ret);
		break;
	case AMLOP_TODECSTRING:
		/* Source:CData, Result => String */
		my_ret = aml_convert(opargs[0], AML_OBJTYPE_DECSTRING, -1);
		aml_store(scope, opargs[1], 0, my_ret);
		break;
	case AMLOP_TOSTRING:
		/* Source:B, Length:I, Result => String */
		my_ret = aml_convert(opargs[0], AML_OBJTYPE_STRING,
		    opargs[1]->v_integer);
		aml_store(scope, opargs[2], 0, my_ret);
		break;
	case AMLOP_CONCAT:
		/* Source1:CData, Source2:CData, Result => CData */
		my_ret = aml_concat(opargs[0], opargs[1]);
		aml_store(scope, opargs[2], 0, my_ret);
		break;
	case AMLOP_CONCATRES:
		/* Concat two resource buffers: buf1, buf2, result => Buffer */
		my_ret = aml_concatres(opargs[0], opargs[1]);
		aml_store(scope, opargs[2], 0, my_ret);
		break;
	case AMLOP_MID:
		/* Source:BS, Index:I, Length:I, Result => BS */
		my_ret = aml_mid(opargs[0], opargs[1]->v_integer,
		    opargs[2]->v_integer);
		aml_store(scope, opargs[3], 0, my_ret);
		break;
	case AMLOP_MATCH:
		/* Match: Pkg, Op1, Val1, Op2, Val2, Index */
		ival = aml_match(opargs[0], opargs[5]->v_integer,
		    opargs[1]->v_integer, opargs[2]->v_integer,
		    opargs[3]->v_integer, opargs[4]->v_integer);
		break;
	case AMLOP_SIZEOF:
		/* Sizeof: S => i */
		rv = aml_gettgt(opargs[0], opcode);
		ival = rv->length;
		break;
	case AMLOP_OBJECTTYPE:
		/* ObjectType: S => i */
		rv = aml_gettgt(opargs[0], opcode);
		ival = rv->type;
		break;

		/* Mutex/Event handlers */
	case AMLOP_ACQUIRE:
		/* Acquire: Sw => Bool */
		rv = aml_gettgt(opargs[0], opcode);
		ival = acpi_mutex_acquire(scope, rv,
		    opargs[1]->v_integer);
		break;
	case AMLOP_RELEASE:
		/* Release: S */
		rv = aml_gettgt(opargs[0], opcode);
		acpi_mutex_release(scope, rv);
		break;
	case AMLOP_WAIT:
		/* Wait: Si => Bool */
		rv = aml_gettgt(opargs[0], opcode);
		ival = acpi_event_wait(scope, rv,
		    opargs[1]->v_integer);
		break;
	case AMLOP_RESET:
		/* Reset: S */
		rv = aml_gettgt(opargs[0], opcode);
		acpi_event_reset(scope, rv);
		break;
	case AMLOP_SIGNAL:
		/* Signal: S */
		rv = aml_gettgt(opargs[0], opcode);
		acpi_event_signal(scope, rv);
		break;

		/* Named objects */
	case AMLOP_NAME:
		/* Name: Nt */
		rv = opargs[0];
		aml_freevalue(rv);
		aml_copyvalue(rv, opargs[1]);
		break;
	case AMLOP_ALIAS:
		/* Alias: nN */
		rv = _aml_setvalue(opargs[1], AML_OBJTYPE_OBJREF, opcode, 0);
		rv->v_objref.ref = aml_gettgt(opargs[0], opcode);
		aml_addref(rv->v_objref.ref, "Alias");
		break;
	case AMLOP_OPREGION:
		/* OpRegion: Nbii */
		rv = _aml_setvalue(opargs[0], AML_OBJTYPE_OPREGION, 0, 0);
		rv->v_opregion.iospace = opargs[1]->v_integer;
		rv->v_opregion.iobase = opargs[2]->v_integer;
		rv->v_opregion.iolen = opargs[3]->v_integer;
		rv->v_opregion.flag = 0;
		break;
	case AMLOP_DATAREGION:
		/* DataTableRegion: N,t:SigStr,t:OemIDStr,t:OemTableIDStr */
		rv = _aml_setvalue(opargs[0], AML_OBJTYPE_OPREGION, 0, 0);
		rv->v_opregion.iospace = GAS_SYSTEM_MEMORY;
		rv->v_opregion.iobase = 0;
		rv->v_opregion.iolen = 0;
		aml_die("AML-DataTableRegion\n");
		break;
	case AMLOP_EVENT:
		/* Event: N */
		rv = _aml_setvalue(opargs[0], AML_OBJTYPE_EVENT, 0, 0);
		rv->v_evt.state = 0;
		break;
	case AMLOP_MUTEX:
		/* Mutex: Nw */
		rv = _aml_setvalue(opargs[0], AML_OBJTYPE_MUTEX, 0, 0);
		rv->v_mtx.synclvl = opargs[1]->v_integer;
		break;
	case AMLOP_SCOPE:
		/* Scope: NT */
		rv = opargs[0];
		if (rv->type == AML_OBJTYPE_NAMEREF) {
			printf("Undefined scope: %s\n", aml_getname(rv->v_nameref));
			break;
		}
		mscope = aml_pushscope(scope, opargs[1], rv->node, opcode);
		break;
	case AMLOP_DEVICE:
		/* Device: NT */
		rv = _aml_setvalue(opargs[0], AML_OBJTYPE_DEVICE, 0, 0);
		mscope = aml_pushscope(scope, opargs[1], rv->node, opcode);
		break;
	case AMLOP_THERMALZONE:
		/* ThermalZone: NT */
		rv = _aml_setvalue(opargs[0], AML_OBJTYPE_THERMZONE, 0, 0);
		mscope = aml_pushscope(scope, opargs[1], rv->node, opcode);
		break;
	case AMLOP_POWERRSRC:
		/* PowerRsrc: NbwT */
		rv = _aml_setvalue(opargs[0], AML_OBJTYPE_POWERRSRC, 0, 0);
		rv->v_powerrsrc.pwr_level = opargs[1]->v_integer;
		rv->v_powerrsrc.pwr_order = opargs[2]->v_integer;
		mscope = aml_pushscope(scope, opargs[3], rv->node, opcode);
		break;
	case AMLOP_PROCESSOR:
		/* Processor: NbdbT */
		rv = _aml_setvalue(opargs[0], AML_OBJTYPE_PROCESSOR, 0, 0);
		rv->v_processor.proc_id = opargs[1]->v_integer;
		rv->v_processor.proc_addr = opargs[2]->v_integer;
		rv->v_processor.proc_len = opargs[3]->v_integer;
		mscope = aml_pushscope(scope, opargs[4], rv->node, opcode);
		break;
	case AMLOP_METHOD:
		/* Method: NbM */
		rv = _aml_setvalue(opargs[0], AML_OBJTYPE_METHOD, 0, 0);
		rv->v_method.flags = opargs[1]->v_integer;
		rv->v_method.start = opargs[2]->v_buffer;
		rv->v_method.end = rv->v_method.start + opargs[2]->length;
		rv->v_method.base = aml_root.start;
		break;

		/* Field objects */
	case AMLOP_CREATEFIELD:
		/* Source:B, BitIndex:I, NumBits:I, FieldName */
		rv = _aml_setvalue(opargs[3], AML_OBJTYPE_BUFFERFIELD, 0, 0);
		aml_createfield(rv, opcode, opargs[0], opargs[1]->v_integer,
		    opargs[2]->v_integer, NULL, 0, 0);
		break;
	case AMLOP_CREATEBITFIELD:
		/* Source:B, BitIndex:I, FieldName */
		rv = _aml_setvalue(opargs[2], AML_OBJTYPE_BUFFERFIELD, 0, 0);
		aml_createfield(rv, opcode, opargs[0], opargs[1]->v_integer,
		    1, NULL, 0, 0);
		break;
	case AMLOP_CREATEBYTEFIELD:
		/* Source:B, ByteIndex:I, FieldName */
		rv = _aml_setvalue(opargs[2], AML_OBJTYPE_BUFFERFIELD, 0, 0);
		aml_createfield(rv, opcode, opargs[0], opargs[1]->v_integer*8,
		    8, NULL, 0, AML_FIELD_BYTEACC);
		break;
	case AMLOP_CREATEWORDFIELD:
		/* Source:B, ByteIndex:I, FieldName */
		rv = _aml_setvalue(opargs[2], AML_OBJTYPE_BUFFERFIELD, 0, 0);
		aml_createfield(rv, opcode, opargs[0], opargs[1]->v_integer*8,
		    16, NULL, 0, AML_FIELD_WORDACC);
		break;
	case AMLOP_CREATEDWORDFIELD:
		/* Source:B, ByteIndex:I, FieldName */
		rv = _aml_setvalue(opargs[2], AML_OBJTYPE_BUFFERFIELD, 0, 0);
		aml_createfield(rv, opcode, opargs[0], opargs[1]->v_integer*8,
		    32, NULL, 0, AML_FIELD_DWORDACC);
		break;
	case AMLOP_CREATEQWORDFIELD:
		/* Source:B, ByteIndex:I, FieldName */
		rv = _aml_setvalue(opargs[2], AML_OBJTYPE_BUFFERFIELD, 0, 0);
		aml_createfield(rv, opcode, opargs[0], opargs[1]->v_integer*8,
		    64, NULL, 0, AML_FIELD_QWORDACC);
		break;
	case AMLOP_FIELD:
		/* Field: n:OpRegion, b:Flags, F:ieldlist */
		mscope = aml_pushscope(scope, opargs[2], scope->node, opcode);
		aml_parsefieldlist(mscope, opcode, opargs[1]->v_integer,
		    opargs[0], NULL, 0);
		mscope = NULL;
		break;
	case AMLOP_INDEXFIELD:
		/* IndexField: n:Index, n:Data, b:Flags, F:ieldlist */
		mscope = aml_pushscope(scope, opargs[3], scope->node, opcode);
		aml_parsefieldlist(mscope, opcode, opargs[2]->v_integer,
		    opargs[1], opargs[0], 0);
		mscope = NULL;
		break;
	case AMLOP_BANKFIELD:
		/* BankField: n:OpRegion, n:Field, i:Bank, b:Flags, F:ieldlist */
		mscope = aml_pushscope(scope, opargs[4], scope->node, opcode);
		aml_parsefieldlist(mscope, opcode, opargs[3]->v_integer,
		    opargs[0], opargs[1], opargs[2]->v_integer);
		mscope = NULL;
		break;

		/* Misc functions */
	case AMLOP_STALL:
		/* Stall: i */
		acpi_stall(opargs[0]->v_integer);
		break;
	case AMLOP_SLEEP:
		/* Sleep: i */
		acpi_sleep(opargs[0]->v_integer, "amlsleep");
		break;
	case AMLOP_NOTIFY:
		/* Notify: Si */
		rv = aml_gettgt(opargs[0], opcode);
		dnprintf(50,"Notifying: %s %llx\n",
		    aml_nodename(rv->node),
		    opargs[1]->v_integer);
		aml_notify(rv->node, opargs[1]->v_integer);
		break;
	case AMLOP_TIMER:
		/* Timer: => i */
		nanouptime(&ts);
		ival = ts.tv_sec * 10000000 + ts.tv_nsec / 100;
		break;
	case AMLOP_FATAL:
		/* Fatal: bdi */
		aml_die("AML FATAL ERROR: %x,%x,%x\n",
		    opargs[0]->v_integer, opargs[1]->v_integer,
		    opargs[2]->v_integer);
		break;
	case AMLOP_LOADTABLE:
		/* LoadTable(Sig:Str, OEMID:Str, OEMTable:Str, [RootPath:Str], [ParmPath:Str],
		   [ParmData:DataRefObj]) => DDBHandle */
		my_ret = aml_loadtable(acpi_softc, opargs[0]->v_string,
		    opargs[1]->v_string, opargs[2]->v_string,
		    opargs[3]->v_string, opargs[4]->v_string, opargs[5]);
		break;
	case AMLOP_LOAD:
		/* Load(Object:NameString, DDBHandle:SuperName) */
		mscope = aml_load(acpi_softc, scope, opargs[0], opargs[1]);
		break;
	case AMLOP_UNLOAD:
		/* DDBHandle */
		aml_die("Unload");
		break;

		/* Control Flow */
	case AMLOP_IF:
		/* Arguments: iT or iTbT */
		if (opargs[0]->v_integer) {
			dnprintf(10,"parse-if @ %.4x\n", pc);
			mscope = aml_pushscope(scope, opargs[1], scope->node,
			    AMLOP_IF);
		} else if (opargs[3] != NULL) {
			dnprintf(10,"parse-else @ %.4x\n", pc);
			mscope = aml_pushscope(scope, opargs[3], scope->node,
			    AMLOP_ELSE);
		}
		break;
	case AMLOP_WHILE:
		if (opargs[0]->v_integer) {
			/* Set parent position to start of WHILE */
			scope->pos = start;
			mscope = aml_pushscope(scope, opargs[1], scope->node,
			    AMLOP_WHILE);
		}
		break;
	case AMLOP_BREAK:
		/* Break: Find While Scope parent, mark type as null */
		aml_findscope(scope, AMLOP_WHILE, AMLOP_BREAK);
		break;
	case AMLOP_CONTINUE:
		/* Find Scope.. mark all objects as invalid on way to root */
		aml_findscope(scope, AMLOP_WHILE, AMLOP_CONTINUE);
		break;
	case AMLOP_RETURN:
		mscope = aml_findscope(scope, AMLOP_METHOD, AMLOP_RETURN);
		if (mscope->retv) {
			aml_die("already allocated\n");
		}
		mscope->retv = aml_allocvalue(0,0,NULL);
		aml_copyvalue(mscope->retv, opargs[0]);
		mscope = NULL;
		break;
	default:
		/* may be set direct result */
		aml_die("Unknown opcode: %x:%s\n", opcode, htab->mnem);
		break;
	}
	if (mscope != NULL) {
		/* Change our scope to new scope */
		scope = mscope;
	}
	if ((ret_type == 'i' || ret_type == 't') && my_ret == NULL) {
		dnprintf(10,"quick: %.4x [%s] alloc return integer = 0x%llx\n",
		    pc, htab->mnem, ival);
		my_ret = aml_allocvalue(AML_OBJTYPE_INTEGER, ival, NULL);
	}
	if (ret_type == 'i' && my_ret && my_ret->type != AML_OBJTYPE_INTEGER) {
		dnprintf(10,"quick: %.4x convert to integer %s -> %s\n",
		    pc, htab->mnem, stype);
		my_ret = aml_convert(my_ret, AML_OBJTYPE_INTEGER, -1);
	}
	if (my_ret != NULL) {
		/* Display result */
		dnprintf(20,"quick: %.4x %18s %c %.4x\n", pc, stype,
		    ret_type, my_ret->stack);
	}

	/* End opcode: display/free arguments */
parse_error:
	for (idx=0; idx<8; idx++) {
		if (opargs[idx] == my_ret)
			opargs[idx] = NULL;
		aml_delref(&opargs[idx], "oparg");
	}

	/* If parsing whole scope and not done, start again */
	if (ret_type == 'T') {
		aml_delref(&my_ret, "scope.loop");
		while (scope->pos >= scope->end && scope != iscope) {
			/* Pop intermediate scope */
			scope = aml_popscope(scope);
		}
		if (scope->pos && scope->pos < scope->end)
			goto start;
	}

	odp--;
	dnprintf(50, ">>return [%s] %s %c %p\n", aml_nodename(scope->node),
	    stype, ret_type, my_ret);

	return my_ret;
}

int
acpi_parse_aml(struct acpi_softc *sc, const char *rootpath,
    uint8_t *start, uint32_t length)
{
	struct aml_node *root = &aml_root;
	struct aml_scope *scope;
	struct aml_value res;

	if (rootpath) {
		root = aml_searchname(&aml_root, rootpath);
		if (root == NULL)
			aml_die("Invalid RootPathName %s\n", rootpath);
	}

	aml_root.start = start;
	memset(&res, 0, sizeof(res));
	res.type = AML_OBJTYPE_SCOPE;
	res.length = length;
	res.v_buffer = start;

	/* Push toplevel scope, parse AML */
	aml_error = 0;
	scope = aml_pushscope(NULL, &res, &aml_root, AMLOP_SCOPE);
	aml_busy++;
	aml_parse(scope, 'T', "TopLevel");
	aml_busy--;
	aml_popscope(scope);

	if (aml_error) {
		printf("error in acpi_parse_aml\n");
		return -1;
	}
	return (0);
}

/*
 * @@@: External API
 *
 * evaluate an AML node
 * Returns a copy of the value in res  (must be freed by user)
 */
int
aml_evalnode(struct acpi_softc *sc, struct aml_node *node,
    int argc, struct aml_value *argv, struct aml_value *res)
{
	struct aml_value *xres;

	if (res)
		memset(res, 0, sizeof(*res));
	if (node == NULL || node->value == NULL)
		return (ACPI_E_BADVALUE);
	dnprintf(12,"EVALNODE: %s %lx\n", aml_nodename(node), acpi_nalloc);

	aml_error = 0;
	xres = aml_eval(NULL, node->value, 't', argc, argv);
	if (xres) {
		if (res)
			aml_copyvalue(res, xres);
		if (xres != node->value)
			aml_delref(&xres, "evalnode");
	}
	if (aml_error) {
		printf("error evaluating: %s\n", aml_nodename(node));
		return (-1);
	}
	return (0);
}

int
aml_node_setval(struct acpi_softc *sc, struct aml_node *node, int64_t val)
{
	struct aml_value env;

	if (!node)
		return (0);

	memset(&env, 0, sizeof(env));
	env.type = AML_OBJTYPE_INTEGER;
	env.v_integer = val;

	return aml_evalnode(sc, node, 1, &env, NULL);
}

/*
 * evaluate an AML name
 * Returns a copy of the value in res  (must be freed by user)
 */
int
aml_evalname(struct acpi_softc *sc, struct aml_node *parent, const char *name,
    int argc, struct aml_value *argv, struct aml_value *res)
{
	parent = aml_searchname(parent, name);
	return aml_evalnode(sc, parent, argc, argv, res);
}

/*
 * evaluate an AML integer object
 */
int
aml_evalinteger(struct acpi_softc *sc, struct aml_node *parent,
    const char *name, int argc, struct aml_value *argv, int64_t *ival)
{
	struct aml_value res;
	int rc;

	parent = aml_searchname(parent, name);
	rc = aml_evalnode(sc, parent, argc, argv, &res);
	if (rc == 0) {
		*ival = aml_val2int(&res);
		aml_freevalue(&res);
	}
	return rc;
}

/*
 * Search for an AML name in namespace.. root only
 */
struct aml_node *
__aml_searchname(struct aml_node *root, const void *vname, int create)
{
	char *name = (char *)vname;
	char  nseg[AML_NAMESEG_LEN + 1];
	int   i;

	dnprintf(25,"Searchname: %s:%s = ", aml_nodename(root), name);
	while (*name == AMLOP_ROOTCHAR) {
		root = &aml_root;
		name++;
	}
	while (*name != 0) {
		/* Ugh.. we can have short names here: append '_' */
		strlcpy(nseg, "____", sizeof(nseg));
		for (i=0; i < AML_NAMESEG_LEN && *name && *name != '.'; i++)
			nseg[i] = *name++;
		if (*name == '.')
			name++;
		root = __aml_search(root, nseg, create);
	}
	dnprintf(25,"%p %s\n", root, aml_nodename(root));
	return root;
}

struct aml_node *
aml_searchname(struct aml_node *root, const void *vname)
{
	return __aml_searchname(root, vname, 0);
}

/*
 * Search for relative name
 */
struct aml_node *
aml_searchrel(struct aml_node *root, const void *vname)
{
	struct aml_node *res;

	while (root) {
		res = aml_searchname(root, vname);
		if (res != NULL)
			return res;
		root = root->parent;
	}
	return NULL;
}

#ifndef SMALL_KERNEL

void
acpi_getdevlist(struct acpi_devlist_head *list, struct aml_node *root,
    struct aml_value *pkg, int off)
{
	struct acpi_devlist *dl;
	struct aml_value *val;
	struct aml_node *node;
	int idx;

	for (idx = off; idx < pkg->length; idx++) {
		val = pkg->v_package[idx];
		if (val->type == AML_OBJTYPE_NAMEREF) {
			node = aml_searchrel(root, aml_getname(val->v_nameref));
			if (node == NULL) {
				printf("%s: device %s not found\n", __func__,
				    aml_getname(val->v_nameref));
				continue;
			}
			val = node->value;
		}
		if (val->type == AML_OBJTYPE_OBJREF)
			val = val->v_objref.ref;
		if (val->node) {
			dl = acpi_os_malloc(sizeof(*dl));
			if (dl) {
				dl->dev_node = val->node;
				TAILQ_INSERT_TAIL(list, dl, dev_link);
			}
		}
	}
}

void
acpi_freedevlist(struct acpi_devlist_head *list)
{
	struct acpi_devlist *dl;

	while ((dl = TAILQ_FIRST(list)) != NULL) {
		TAILQ_REMOVE(list, dl, dev_link);
		acpi_os_free(dl);
	}
}

#endif /* SMALL_KERNEL */
