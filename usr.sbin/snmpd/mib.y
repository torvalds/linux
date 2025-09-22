/*	$OpenBSD: mib.y,v 1.2 2024/02/20 12:41:13 martijn Exp $	*/

/*
 * Copyright (c) 2023 Martijn van Duren <martijn@openbsd.org>
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

%{

#include <sys/tree.h>

#include <assert.h>
#include <ber.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <time.h>

#include "log.h"
#include "mib.h"

/* RFC2578 section 3.1 */
#define DESCRIPTOR_MAX 64

/* Values from real life testing, could be adjusted */
#define ITEM_MAX DESCRIPTOR_MAX
#define MODULENAME_MAX 64
#define SYMBOLS_MAX 256
#define IMPORTS_MAX 16
#define TEXT_MAX 16384

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

struct objidcomponent {
	enum {
		OCT_DESCRIPTOR,
		OCT_NUMBER,
		OCT_NAMEANDNUMBER
	}			 type;
	uint32_t		 number;
	char			 name[DESCRIPTOR_MAX + 1];
};

struct oid_unresolved {
	/* Unusual to have long lists of unresolved components */
	struct objidcomponent	 bo_id[16];
	size_t			 bo_n;
};

struct oid_resolved {
	uint32_t		*bo_id;
	size_t			 bo_n;
};

enum status {
	CURRENT,
	DEPRECATED,
	OBSOLETE
};

enum access {
	NOTACCESSIBLE,
	ACCESSIBLEFORNOTIFY,
	READONLY,
	READWRITE,
	READCREATE
};

struct objectidentity {
	enum status		 status;
	char			*description;
	char			*reference;
};

struct objecttype {
	void			*syntax;
	char			*units;
	enum access		 maxaccess;
	enum status		 status;
	char			*description;
	char			*reference;
	void			*index;
	void			*defval;
};

struct notificationtype {
	void			*objects;
	enum status		 status;
	char			*description;
	char			*reference;
};

struct textualconvention {
	char			*displayhint;
	enum status		 status;
	char			*description;
	char			*reference;
	void			*syntax;
};

struct item {
	char			 name[DESCRIPTOR_MAX + 1];
	enum item_type {
		IT_OID,
		IT_MACRO,
		IT_MODULE_IDENTITY,
		IT_OBJECT_IDENTITY,
		IT_APPLICATIONSYNTAX,
		IT_OBJECT_TYPE,
		IT_NOTIFICATION_TYPE,
		IT_TEXTUAL_CONVENTION,
		IT_OBJECT_GROUP,
		IT_NOTIFICATION_GROUP,
		IT_MODULE_COMPLIANCE,
		IT_AGENT_CAPABITIES
	}			 type;
	int			 resolved;
	struct module		*module;

	union {
		struct oid_unresolved	*oid_unresolved;
		struct oid_resolved	 oid;
	};

	union {
		struct objectidentity	 objectidentity;
		struct objecttype	 objecttype;
		struct notificationtype	 notificationtype;
		struct textualconvention textualconvention;
	};

	/* Global case insensitive */
	RB_ENTRY(item)		 entrygci;
	/* Module case insensitive */
	RB_ENTRY(item)		 entryci;
	/* Module case sensitive */
	RB_ENTRY(item)		 entrycs;
	/* Global oid */
	RB_ENTRY(item)		 entry;
};

struct import_symbol {
	char			 name[DESCRIPTOR_MAX + 1];
	struct item		*item;
};

struct import {
	char			 name[MODULENAME_MAX + 1];
	struct module		*module;
	struct import_symbol	 symbols[SYMBOLS_MAX];
	size_t			 nsymbols;
};

static struct module {
	char			 name[MODULENAME_MAX + 1];
	int8_t			 resolved;

	time_t			 lastupdated;

	struct import		*imports;

	RB_HEAD(itemscs, item) itemscs;
	RB_HEAD(itemsci, item) itemsci;

	RB_ENTRY(module) entryci;
	RB_ENTRY(module) entrycs;
} *module;

int		 yylex(void);
static void	 yyerror(const char *, ...)
    __attribute__((__format__ (printf, 1, 2)));
void		 mib_defaults(void);
void		 mib_modulefree(struct module *);
int		 mib_imports_add(char *, char **);
int		 mib_oid_append(struct oid_unresolved *,
		    const struct objidcomponent *);
int		 mib_macro(const char *);
int		 mib_oid_concat(struct oid_unresolved *,
		    const struct oid_unresolved *);
struct item	*mib_item(const char *, enum item_type);
int		 mib_item_oid(struct item *,
		    const struct oid_unresolved *);
int		 mib_macro(const char *);
int		 mib_applicationsyntax(const char *);
struct item	*mib_oid(const char *, const struct oid_unresolved *);
int		 mib_moduleidentity(const char *, time_t, const char *,
		    const char *, const char *, const struct oid_unresolved *);
int		 mib_objectidentity(const char *, enum status, const char *,
		    const char *, const struct oid_unresolved *);
int		 mib_objecttype(const char *, void  *, const char *,
		    enum access, enum status, const char *, const char *,
		    void *, void *, const struct oid_unresolved *);
int		 mib_notificationtype(const char *, void *, enum status,
		    const char *, const char *, const struct oid_unresolved *);
int		 mib_textualconvetion(const char *, const char *, enum status,
		    const char *, const char *, void *);
int		 mib_objectgroup(const char *, void *, enum status,
		    const char *, const char *, const struct oid_unresolved *);
int		 mib_notificationgroup(const char *, void *, enum status,
		    const char *, const char *, const struct oid_unresolved *);
int		 mib_modulecompliance(const char *, enum status, const char *,
		    const char *, void *, const struct oid_unresolved *);
struct item	*mib_item_find(struct item *, const char *);
struct item	*mib_item_parent(struct ber_oid *);
int		  mib_resolve_oid(struct oid_resolved *,
		    struct oid_unresolved *, struct item *);
int		 mib_resolve_item(struct item *);
int		 mib_resolve_module(struct module *);
int		 module_cmp_cs(struct module *, struct module *);
int		 module_cmp_ci(struct module *, struct module *);
int		 item_cmp_cs(struct item *, struct item *);
int		 item_cmp_ci(struct item *, struct item *);
int		 item_cmp_oid(struct item *, struct item *);

RB_HEAD(modulesci, module) modulesci = RB_INITIALIZER(&modulesci);
RB_HEAD(modulescs, module) modulescs = RB_INITIALIZER(&modulescs);
RB_HEAD(items, item) items = RB_INITIALIZER(&items);
RB_HEAD(itemsgci, item) itemsci = RB_INITIALIZER(&itemsci);
/*
 * Use case sensitive matching internally (for resolving IMPORTS) and
 * case sensitive matching, followed by case insensitive matching
 * for end-user resolving (e.g. mib_string2oid()).
 * It shouldn't happen there's case-based overlap in module/item names,
 * but allow all to be resolved in case there is.
 */
RB_PROTOTYPE_STATIC(modulesci, module, entryci, module_cmp_ci);
RB_PROTOTYPE_STATIC(modulescs, module, entrycs, module_cmp_cs);
/*
 * mib_string2oid() should match case insensitive on:
 * <module>::<descriptor>
 * <descriptor>
 */
RB_PROTOTYPE_STATIC(itemsgci, item, entrygci, item_cmp_ci);
RB_PROTOTYPE_STATIC(itemsci, item, entryci, item_cmp_ci);
RB_PROTOTYPE_STATIC(itemscs, item, entrycs, item_cmp_cs);
RB_PROTOTYPE_STATIC(items, item, entry, item_cmp_oid);

struct file {
	FILE		*stream;
	const char	*name;
	size_t		 lineno;
	enum {
		FILE_UNDEFINED,
		FILE_ASN1,
		FILE_SMI2
	}		 state;
} file;

typedef union {
	char			 string[TEXT_MAX];
	unsigned long long	 number;
	long long		 signednumber;
	char			 symbollist[SYMBOLS_MAX][DESCRIPTOR_MAX + 1];
	struct objidcomponent	 objidcomponent;
	struct oid_unresolved	 oid;
	time_t			 time;
	enum status		 status;
	enum access		 access;
} YYSTYPE;

%}

%token	ERROR
%token	HSTRING BSTRING

/* RFC2578 section 3.7 */
%token	ABSENT ACCESS AGENTCAPABILITIES ANY APPLICATION AUGMENTS BEGIN
%token	BIT BITS BOOLEAN BY CHOICE COMPONENT COMPONENTS CONTACTINFO
%token	CREATIONREQUIRES Counter32 Counter64 DEFAULT DEFINED
%token	DEFINITIONS DEFVAL DESCRIPTION DISPLAYHINT END ENUMERATED
%token	ENTERPRISE EXPLICIT EXPORTS EXTERNAL FALSE FROM GROUP Gauge32
%token	IDENTIFIER IMPLICIT IMPLIED IMPORTS INCLUDES INDEX INTEGER
%token	Integer32 IpAddress LASTUPDATED MANDATORYGROUPS MAX MAXACCESS
%token	MIN MINACCESS MINUSINFINITY MODULE MODULECOMPLIANCE MODULEIDENTITY 
%token	NOTIFICATIONGROUP NOTIFICATIONTYPE NOTIFICATIONS ASNNULL
%token	OBJECT OBJECTGROUP OBJECTIDENTITY OBJECTTYPE OBJECTS OCTET OF
%token	OPTIONAL ORGANIZATION Opaque PLUSINFINITY PRESENT PRIVATE
%token	PRODUCTRELEASE REAL REFERENCE REVISION SEQUENCE SET SIZE STATUS
%token	STRING SUPPORTS SYNTAX TAGS TEXTUALCONVENTION TRAPTYPE TRUE
%token	TimeTicks UNITS UNIVERSAL Unsigned32 VARIABLES VARIATION WITH
%token	WRITESYNTAX

/* SMIv2 */
%token	SNMPv2SMI SNMPv2CONF SNMPv2TC

/* X.208 */
%token	PRODUCTION RANGESEPARATOR

%token	<string>	typereference identifier TEXT HSTRING BSTRING
%token	<number>	NUMBER
%token	<signednumber>	SIGNEDNUMBER
%type	<string>	moduleidentifier smiv2moduleidentifier
%type	<import>	symbolsfrom
%type	<symbollist>	symbollist
%type	<string>	descriptor symbol
%type	<objidcomponent>objidcomponentfirst objidcomponent
%type	<oid>		objidcomponentlist objectidentifiervalue
%type	<string>	displaypart referpart unitspart
%type	<time>		lastupdated
%type	<status>	status
%type	<access>	access

%%

grammar			: /* empty */
			| grammar module
			;

module			: moduleidentifier DEFINITIONS PRODUCTION BEGIN {
				file.state = FILE_ASN1;
				module = calloc(1, sizeof(*module));
				if (module == NULL) {
					yyerror("malloc");
					YYERROR;
				}
				RB_INIT(&module->itemscs);
				RB_INIT(&module->itemsci);
				module->resolved = 0;
				if (strlcpy(module->name, $1,
				    sizeof(module->name)) >=
				    sizeof(module->name)) {
					yyerror("module name too long");
					free(module);
					YYERROR;
				}
			} imports moduleidentity modulebody END {
				struct module *mprev;

				if ((mprev = RB_INSERT(modulescs, &modulescs,
				    module)) != NULL) {
					if (module->lastupdated >
					    mprev->lastupdated) {
						mib_modulefree(mprev);
						RB_INSERT(modulescs, &modulescs,
						    module);
						RB_INSERT(modulesci, &modulesci,
						    module);
					} else
						mib_modulefree(module);
				} else 
					RB_INSERT(modulesci, &modulesci, module);
				module = NULL;
			}
			| smiv2moduleidentifier {
				log_debug("%s: SMIv2 definitions: skipping",
				   file.name);
				YYACCEPT;
			}
			;

moduleidentifier	: typereference { strlcpy($$, $1, sizeof($$)); }
			;

smiv2moduleidentifier	: SNMPv2SMI { strlcpy($$, "SNMPv2-SMI", sizeof($$)); }
			| SNMPv2CONF { strlcpy($$, "SNMPv2-CONF", sizeof($$)); }
			| SNMPv2TC { strlcpy($$, "SNMPv2-TC", sizeof($$)); }
			;

modulebody		: assignmentlist
			;

imports			: IMPORTS importlist ';'
			;

importlist		: importlist symbolsfrom
			| /* start */
			;

symbolsfrom		: symbollist FROM moduleidentifier {
				size_t i;
				char *symbols[SYMBOLS_MAX];

				for (i = 0; $1[i][0] != '\0'; i++)
					symbols[i] = $1[i];
				symbols[i] = NULL;
					
				if (mib_imports_add($3, symbols) == -1)
					YYERROR;
			}
			| symbollist FROM smiv2moduleidentifier {
				size_t i;
				char *symbols[SYMBOLS_MAX];

				for (i = 0; $1[i][0] != '\0'; i++)
					symbols[i] = $1[i];
				symbols[i] = NULL;
					
				if (mib_imports_add($3, symbols) == -1)
					YYERROR;

				file.state = FILE_SMI2;
			}
			;

symbollist		: symbollist ',' symbol {
				size_t i;
				for (i = 0; $1[i][0] != '\0'; i++)
					strlcpy($$[i], $1[i], sizeof($$[i]));
				if (i + 1 == nitems($$)) {
					yyerror("too many symbols from module");
					YYERROR;
				}
				if (strlcpy($$[i], $3, sizeof($$[i])) >=
				    sizeof($$[i])) {
					yyerror("symbol too long");
					YYERROR;
				}
				$$[i + 1][0] = '\0';
			}
			| symbol {
				if (strlcpy($$[0], $1, sizeof($$[0])) >=
				    sizeof($$[0])) {
					yyerror("symbol too long");
					YYERROR;
				}
				$$[1][0] = '\0';
			}
			;

symbol			: typereference { strlcpy($$, $1, sizeof($$)); }
			| descriptor { strlcpy($$, $1, sizeof($$)); }
			/* SNMPv2-SMI */
			| MODULEIDENTITY {
				strlcpy($$, "MODULE-IDENTITY", sizeof($$));
			}
			| OBJECTIDENTITY {
				strlcpy($$, "OBJECT-IDENTITY", sizeof($$));
			}
			| Integer32  { strlcpy($$, "Integer32", sizeof($$)); }
			| IpAddress { strlcpy($$, "IpAddress", sizeof($$)); }
			| Counter32 { strlcpy($$, "Counter32", sizeof($$)); }
			| Gauge32 { strlcpy($$, "Gauge32", sizeof($$)); }
			| Unsigned32 { strlcpy($$, "Unsigned32", sizeof($$)); }
			| TimeTicks { strlcpy($$, "TimeTicks", sizeof($$)); }
			| Opaque { strlcpy($$, "Opaque", sizeof($$)); }
			| Counter64 { strlcpy($$, "Counter64", sizeof($$)); }
			| OBJECTTYPE { strlcpy($$, "OBJECT-TYPE", sizeof($$)); }
			| NOTIFICATIONTYPE {
				strlcpy($$, "NOTIFICATION-TYPE", sizeof($$));
			}
			/* SNMPv2-TC */
			| TEXTUALCONVENTION {
				strlcpy($$, "TEXTUAL-CONVENTION", sizeof($$));
			}
			/* SNMPv2-CONF */
			| OBJECTGROUP {
				strlcpy($$, "OBJECT-GROUP", sizeof($$));
			}
			| NOTIFICATIONGROUP {
				strlcpy($$, "NOTIFICATION-GROUP", sizeof($$));
			}
			| MODULECOMPLIANCE {
				strlcpy($$, "MODULE-COMPLIANCE", sizeof($$));
			}
			| AGENTCAPABILITIES {
				strlcpy($$, "AGENT-CAPABILITIES", sizeof($$));
			}
			;

descriptor		: identifier {
				if (strlen($1) > DESCRIPTOR_MAX) {
					yyerror("descriptor too long");
					YYERROR;
				}
				strlcpy($$, $1, sizeof($$));
			}
			;

moduleidentity		: descriptor MODULEIDENTITY lastupdated
			  ORGANIZATION TEXT CONTACTINFO TEXT DESCRIPTION TEXT
			  revisionpart PRODUCTION objectidentifiervalue {
				if (mib_moduleidentity(
				    $1, $3, $5, $7, $9, &$12) == -1)
					YYERROR;
			}
			;

lastupdated		: LASTUPDATED TEXT {
				char timebuf[14] = "";
				struct tm tm = {};
				size_t len;

				if ((len = strlen($2)) == 11)
					snprintf(timebuf, sizeof(timebuf),
					    "19%s", $2);
				else if (len == 13)
					strlcpy(timebuf, $2, sizeof(timebuf));
				else {
					yyerror("Invalid LAST-UPDATED: %s", $2);
					YYERROR;
				}

				if (strptime(timebuf, "%Y%m%d%H%MZ", &tm) == NULL) {
					yyerror("Invalid LAST-UPDATED: %s", $2);
					YYERROR;
				}

				if (($$ = mktime(&tm)) == -1) {
					yyerror("Invalid LAST-UPDATED: %s", $2);
					YYERROR;
				}
			}
			;

revisionpart		: revisions
			| /* empty */
			;

revisions		: revision
			| revisions revision
			;

revision		: REVISION TEXT DESCRIPTION TEXT
			;

assignmentlist		: assignment assignmentlist
			| /* empty */
			;

assignment		: descriptor OBJECT IDENTIFIER PRODUCTION
			  objectidentifiervalue {
				if (mib_oid($1, &$5) == NULL)
					YYERROR;
			}
			| descriptor OBJECTIDENTITY STATUS status
			  DESCRIPTION TEXT referpart PRODUCTION
			  objectidentifiervalue {
				const char *reference;

				reference = $7[0] == '\0' ? NULL : $7;

				if (mib_objectidentity($1, $4, $6, reference,
				    &$9) == -1)
					YYERROR;
			}
			| descriptor OBJECTTYPE SYNTAX syntax unitspart
			  MAXACCESS access STATUS status DESCRIPTION TEXT
			  referpart indexpart defvalpart PRODUCTION
			  objectidentifiervalue {
				const char *units, *reference;

				units = $5[0] == '\0' ? NULL : $5;
				reference = $12[0] == '\0' ? NULL : $12;

				if (mib_objecttype($1, NULL, units, $7, $9, $11,
				    reference, NULL, NULL, &$16) == -1)
					YYERROR;
			}
			| descriptor NOTIFICATIONTYPE objectspart STATUS status
			  DESCRIPTION TEXT referpart PRODUCTION
			  objectidentifiervalue {
				const char *reference;

				reference = $8[0] == '\0' ? NULL : $8;

				if (mib_notificationtype($1, NULL, $5, $7,
				    reference, &$10) == -1)
					YYERROR;
			}
			| typereference PRODUCTION SEQUENCE '{' entries '}' {
				/* Table entry, ignore for now */
			}
			| typereference PRODUCTION TEXTUALCONVENTION displaypart
			  STATUS status DESCRIPTION TEXT referpart SYNTAX syntax {
				const char *displayhint, *reference;

				displayhint = $4[0] == '\0' ? NULL : $4;
				reference = $9[0] == '\0' ? NULL : $9;

				if (mib_textualconvetion($1, displayhint, $6,
				    $8, reference, NULL) == -1)
					YYERROR;
			}
			| descriptor MODULECOMPLIANCE STATUS status
			  DESCRIPTION TEXT referpart compliancemodulepart
			  PRODUCTION objectidentifiervalue {
				const char *reference;

				reference = $7[0] == '\0' ? NULL : $7;

				if (mib_modulecompliance($1, $4, $6, reference,
				    NULL, &$10) == -1)
					YYERROR;
			}
			| descriptor OBJECTGROUP objectspart STATUS status
			  DESCRIPTION TEXT referpart PRODUCTION
			  objectidentifiervalue {
				const char *reference;

				reference = $8[0] == '\0' ? NULL : $8;

				if (mib_objectgroup($1, NULL, $5, $7, reference,
				    &$10) == -1)
					YYERROR;
			}
			| descriptor NOTIFICATIONGROUP notificationspart
			  STATUS status DESCRIPTION TEXT referpart PRODUCTION
			  objectidentifiervalue {
				const char *reference;

				reference = $8[0] == '\0' ? NULL : $8;

				if (mib_notificationgroup($1, NULL, $5, $7, reference,
				    &$10) == -1)
					YYERROR;
			}
			| typereference PRODUCTION syntax
			;

notificationspart	: NOTIFICATIONS '{' notifications '}'
			;

notifications		: notification
			| notification ',' notifications
			;

notification		: descriptor
			;

objectspart		: OBJECTS '{' objects '}'
			| /* empty */
			;

objects			: object
			| objects ',' object
			;

object			: descriptor
			;

syntax			: type
			| SEQUENCE OF typereference
			| integersubtype
			| octetstringsubtype
			| enumeration
			| bits
			;

unitspart		: UNITS TEXT {
				strlcpy($$, $2, sizeof($$));
			}
			| /* empty */ {
				$$[0] = '\0';
			}
			;

referpart		: REFERENCE TEXT {
				strlcpy($$, $2, sizeof($$));
			}
			| /* empty */ {
				$$[0] = '\0';
			}
			;

indexpart		: INDEX '{' indextypes '}'
			| AUGMENTS '{' descriptor '}'
			| /* empty */
			;

indextypes		: indextype
			| indextypes ',' indextype
			;

indextype		: IMPLIED index
			| index
			;

index			: descriptor
			;

defvalpart		: DEFVAL '{' defvalue '}'
			| /* empty */
			;

defvalue		: descriptor
			| NUMBER
			| SIGNEDNUMBER
			| HSTRING
			| TEXT
			| bitsvalue
			| objectidentifiervalue
			;

/* single-value and empty are covered by objectidentifiervalue */
bitsvalue		: '{' bitscomponentlist '}'
			;

bitscomponentlist	: bitscomponentlist ',' bitscomponent
			| bitscomponent
			| /* empty */
			;

bitscomponent		: identifier
			;

displaypart		: DISPLAYHINT TEXT {
				strlcpy($$, $2, sizeof($$));
			}
			| /* empty */ {
				$$[0] = '\0';
			}
			;

entries			: entry ',' entries
			| entry
			;

entry			: descriptor syntax
			;

compliancemodulepart	: compliancemodules
			;

compliancemodules	: compliancemodule
			| compliancemodules compliancemodule
			;

compliancemodule	: MODULE modulename modulemandatorypart
			  modulecompliancepart
			;

modulename		: moduleidentifier
			| /* empty */
			;

modulemandatorypart	: MANDATORYGROUPS '{' modulegroups '}'
			| /* empty */
			;

modulegroups		: modulegroup
			| modulegroups ',' modulegroup
			;

modulegroup		: objectidentifiervalue
			| descriptor
			;

modulecompliancepart	: modulecompliances
			| /* empty */
			;

modulecompliances	: modulecompliance
			| modulecompliances modulecompliance
			;

modulecompliance	: modulecompliancegroup
			| moduleobject
			;

modulecompliancegroup	: GROUP modulegroupobjectname DESCRIPTION TEXT
			;

moduleobject		: OBJECT moduleobjectname modulesyntaxpart
			  writesyntaxpart moduleaccesspart DESCRIPTION TEXT
			;

moduleobjectname	: objectidentifiervalue
			| descriptor
			;

modulegroupobjectname	: objectidentifiervalue
			| descriptor
			;

modulesyntaxpart	: SYNTAX syntax
			| /* empty */
			;

writesyntaxpart		: WRITESYNTAX syntax
			| /* empty */
			;

moduleaccesspart	: MINACCESS access
			| /* empty */
			;

/*
 * An OBJECT IDENTIFIER needs at least 2 components and are needed to
 * distinguish from BITS, which can have 0 elements and only differentiate by a
 * comma separator.
 */
objectidentifiervalue	: '{' objidcomponentfirst objidcomponent
			  objidcomponentlist '}' {
				$$.bo_n = 0;

				if ($3.type == OCT_DESCRIPTOR) {
					yyerror("only first component of "
					    "OBJECT IDENTIFIER can be a "
					    "descriptor");
					YYERROR;
				}
				if (mib_oid_append(&$$, &$2) == -1)
					YYERROR;
				if (mib_oid_append(&$$, &$3) == -1)
					YYERROR;
				if (mib_oid_concat(&$$, &$4) == -1)
					YYERROR;
			}
			;

objidcomponentlist	: objidcomponent objidcomponentlist {
				if ($1.type == OCT_DESCRIPTOR) {
					yyerror("only first component of "
					    "OBJECT IDENTIFIER can be a "
					    "descriptor");
					YYERROR;
				}

				$$.bo_n = 0;
				if (mib_oid_append(&$$, &$1) == -1)
					YYERROR;
				if (mib_oid_concat(&$$, &$2) == -1)
					YYERROR;
			}
			| /* empty */ {
				$$.bo_n = 0;
			}
			;

objidcomponentfirst	: descriptor {
				$$.type = OCT_DESCRIPTOR;
				strlcpy($$.name, $1, sizeof($$.name));
			}
			| objidcomponent {
				$$.type = $1.type;
				$$.number = $1.number;
				strlcpy($$.name, $1.name, sizeof($$.name));
			}
			;

objidcomponent		: NUMBER {
				$$.type = OCT_NUMBER;
				if ($1 > UINT32_MAX) {
					yyerror("OBJECT IDENTIFIER number "
					    "too large");
					YYERROR;
				}
				$$.number = $1;
				$$.name[0] = '\0';
			}
			| descriptor '(' NUMBER ')' {
				$$.type = OCT_NAMEANDNUMBER;
				if ($3 > UINT32_MAX) {
					yyerror("OBJECT IDENTIFIER number "
					    "too large");
					YYERROR;
				}
				$$.number = $3;
				strlcpy($$.name, $1, sizeof($$.name));
			}
			;

type			: typereference
			| INTEGER
			| OBJECT IDENTIFIER
			| OCTET STRING
			| Integer32
			| IpAddress
			| Counter32
			| Gauge32
			| Unsigned32
			| TimeTicks
			| Opaque
			| Counter64
			| BITS
			;

integersubtype		: INTEGER '(' ranges ')'
			| Integer32 '(' ranges ')'
			| Unsigned32 '(' ranges ')'
			| Gauge32 '(' ranges ')'
			| typereference '(' ranges ')'
			;

octetstringsubtype	: OCTET STRING '(' SIZE '(' ranges ')' ')'
			| Opaque '(' SIZE '(' ranges ')' ')'
			| typereference '(' SIZE '(' ranges ')' ')'
			;

bits			: BITS '{' namedbits '}'
			;

namedbits		: namedbit
			| namedbits ',' namedbit
			;

namedbit		: identifier '(' NUMBER ')'
			;

enumeration		: INTEGER '{' namednumbers '}'
			| typereference '{' namednumbers '}'
			;

namednumbers		: namednumber
			| namednumbers ',' namednumber
			;

namednumber		: identifier '(' NUMBER ')'
			| identifier '(' SIGNEDNUMBER ')'
			;

ranges			: range '|' ranges
			| range
			;

range			: value
			| value RANGESEPARATOR value
			;

value			: SIGNEDNUMBER
			| NUMBER
			| HSTRING
			| BSTRING
			;

access			: descriptor {
				if (strcmp($1, "not-accessible") == 0)
					$$ = NOTACCESSIBLE;
				else if (
				    strcmp($1, "accessible-for-notify") == 0)
					$$ = ACCESSIBLEFORNOTIFY;
				else if (strcmp($1, "read-only") == 0)
					$$ = READONLY;
				else if (strcmp($1, "read-write") == 0)
					$$ = READWRITE;
				else if (strcmp($1, "read-create") == 0)
					$$ = READCREATE;
				else {
					yyerror("invalid access");
					YYERROR;
				}
			}
			;

status			: descriptor {
				if (strcmp($1, "current") == 0)
					$$ = CURRENT;
				else if (strcmp($1, "deprecated") == 0)
					$$ = DEPRECATED;
				else if (strcmp($1, "obsolete") == 0)
					$$ = OBSOLETE;
				else {
					yyerror("invalid status");
					YYERROR;
				}
			}
			;
%%

void
yyerror(const char *fmt, ...)
{
	va_list		 ap;
	char		 msg[1024] = "";

	if (file.state == FILE_UNDEFINED) {
		log_debug("%s: not an ASN.1 file: skipping", file.name);
		return;
	}
	if (file.state == FILE_ASN1) {
		if (strcmp(fmt, "syntax error") == 0) {
			log_debug("%s: not an SMIv2 file: skipping", file.name);
			return;
		}
	}
	if (fmt != NULL) {
		va_start(ap, fmt);
		vsnprintf(msg, sizeof(msg), fmt, ap);
		va_end(ap);
	}
	log_warnx("%s:%zu: %s", file.name, file.lineno, msg);
}

/*
 * X.208
 */
int
yylex(void)
{
	const struct {
		const char *name;
		int token;
	} keywords[] = {
		/* RFC2578 section 3.7 */
		{ "ABSENT",		ABSENT },
		{ "ACCESS",		ACCESS },
		{ "AGENT-CAPABILITIES",	AGENTCAPABILITIES },
		{ "ANY",		ANY },
		{ "APPLICATION",	APPLICATION },
		{ "AUGMENTS",		AUGMENTS },
		{ "BEGIN",		BEGIN },
		{ "BIT",		BIT },
		{ "BITS",		BITS },
		{ "BOOLEAN",		BOOLEAN },
		{ "BY",			BY },
		{ "CHOICE",		CHOICE },
		{ "COMPONENT",		COMPONENT },
		{ "COMPONENTS",		COMPONENTS },
		{ "CONTACT-INFO",	CONTACTINFO },
		{ "CREATION-REQUIRES",	CREATIONREQUIRES },
		{ "Counter32",		Counter32 },
		{ "Counter64",		Counter64 },
		{ "DEFAULT",		DEFAULT },
		{ "DEFINED",		DEFINED },
		{ "DEFINITIONS",	DEFINITIONS },
		{ "DEFVAL",		DEFVAL },
		{ "DESCRIPTION",	DESCRIPTION },
		{ "DISPLAY-HINT",	DISPLAYHINT },
		{ "END",		END },
		{ "ENUMERATED",		ENUMERATED },
		{ "ENTERPRISE",		ENTERPRISE },
		{ "EXPLICIT",		EXPLICIT },
		{ "EXPORTS",		EXPORTS },
		{ "EXTERNAL",		EXTERNAL },
		{ "FALSE",		FALSE },
		{ "FROM",		FROM },
		{ "GROUP",		GROUP },
		{ "Gauge32",		Gauge32 },
		{ "IDENTIFIER",		IDENTIFIER },
		{ "IMPLICIT",		IMPLICIT },
		{ "IMPLIED",		IMPLIED },
		{ "IMPORTS",		IMPORTS },
		{ "INCLUDES",		INCLUDES },
		{ "INDEX",		INDEX },
		{ "INTEGER",		INTEGER },
		{ "Integer32",		Integer32 },
		{ "IpAddress",		IpAddress },
		{ "LAST-UPDATED",	LASTUPDATED },
		{ "MANDATORY-GROUPS",	MANDATORYGROUPS },
		{ "MAX",		MAX },
		{ "MAX-ACCESS",		MAXACCESS },
		{ "MIN",		MIN },
		{ "MIN-ACCESS",		MINACCESS },
		{ "MINUS-INFINITY",	MINUSINFINITY },
		{ "MODULE",		MODULE },
		{ "MODULE-COMPLIANCE",	MODULECOMPLIANCE },
		{ "MODULE-IDENTITY",	MODULEIDENTITY },
		{ "NOTIFICATION-GROUP",	NOTIFICATIONGROUP },
		{ "NOTIFICATION-TYPE",	NOTIFICATIONTYPE },
		{ "NOTIFICATIONS",	NOTIFICATIONS },
		{ "NULL",		ASNNULL },
		{ "OBJECT",		OBJECT },
		{ "OBJECT-GROUP",	OBJECTGROUP },
		{ "OBJECT-IDENTITY",	OBJECTIDENTITY },
		{ "OBJECT-TYPE",	OBJECTTYPE },
		{ "OBJECTS",		OBJECTS },
		{ "OCTET",		OCTET },
		{ "OF",			OF },
		{ "OPTIONAL",		OPTIONAL },
		{ "ORGANIZATION",	ORGANIZATION },
		{ "Opaque",		Opaque },
		{ "PLUS-INFINITY",	PLUSINFINITY },
		{ "PRESENT",		PRESENT },
		{ "PRIVATE",		PRIVATE },
		{ "PRODUCT-RELEASE",	PRODUCTRELEASE },
		{ "REAL",		REAL },
		{ "REFERENCE",		REFERENCE },
		{ "REVISION",		REVISION },
		{ "SEQUENCE",		SEQUENCE },
		{ "SET",		SET },
		{ "SIZE",		SIZE },
		{ "STATUS",		STATUS },
		{ "STRING",		STRING },
		{ "SUPPORTS",		SUPPORTS },
		{ "SYNTAX",		SYNTAX },
		{ "TAGS",		TAGS },
		{ "TEXTUAL-CONVENTION",	TEXTUALCONVENTION },
		{ "TRAP-TYPE",		TRAPTYPE },
		{ "TRUE",		TRUE },
		{ "TimeTicks",		TimeTicks },
		{ "UNITS",		UNITS },
		{ "UNIVERSAL",		UNIVERSAL },
		{ "Unsigned32",		Unsigned32 },
		{ "VARIABLES",		VARIABLES },
		{ "VARIATION",		VARIATION },
		{ "WITH",		WITH },
		{ "WRITE-SYNTAX",	WRITESYNTAX },

		/* ASN.1 */
		{ "::=",		PRODUCTION },
		{ "..",			RANGESEPARATOR },

		/* SMIv2 */
		{ "SNMPv2-SMI",		SNMPv2SMI },
		{ "SNMPv2-CONF",	SNMPv2CONF },
		{ "SNMPv2-TC",		SNMPv2TC },

		{}
	};
	char buf[TEXT_MAX];
	size_t i = 0, j;
	int c, comment = 0;
	const char *errstr;
	char *endptr;

	while ((c = fgetc(file.stream)) != EOF) {
		if (i == sizeof(buf)) {
			yyerror("token too large");
			return ERROR;
		}
		if (i > 0 && buf[0] == '"') {
			if (c == '"') {
				buf[i] = '\0';
				(void)strlcpy(yylval.string, buf + 1,
				    sizeof(yylval.string));
				return TEXT;
			}
			if (c == '\n')
				file.lineno++;
			buf[i++] = c;
			continue;
		}
		if (comment) {
			if (c == '-') {
				if (++comment == 3)
					comment = 0;
			} else if (c == '\n') {
				file.lineno++;
				comment = 0;
			} else
				comment = 1;
			continue;
		}
		if (c == '\n') {
			if (i != 0) {
				if (buf[i - 1] == '\r') {
					i--;
					if (i == 0) {
						file.lineno++;
						continue;
					}
				}
				ungetc(c, file.stream);
				goto token;
			}
			file.lineno++;
			continue;
		}
		if (c == ' ' || c == '\t') {
			if (i == 0)
				continue;
			goto token;
		}
		if (c == '.' || c == ':') {
			if (i > 0 && buf[0] != '.' && buf[0] != ':') {
				ungetc(c, file.stream);
				goto token;
			}
		}
		if (i > 0 && (buf[0] == '.' || buf[0] == ':')) {
			if (c != '.' && c != ':' && c != '=') {
				ungetc(c, file.stream);
				goto token;
			}
		}
		if (c == ',' || c == ';' || c == '{' || c == '}' ||
		    c == '(' || c == ')' || c == '[' || c == ']' || c == '|') {
			if (i == 0)
				return c;
			ungetc(c, file.stream);
			goto token;
		}
		buf[i++] = c;
		if (i >= 2) {
			if (buf[i - 2] == '-' && buf[i - 1] == '-') {
				if (i > 2) {
					ungetc('-', file.stream);
					ungetc('-', file.stream);
					i -= 2;
					goto token;
				}
				comment = 1;
				i = 0;
				continue;
			}
		}
	}

	if (ferror(file.stream)) {
		yyerror(NULL);
		return ERROR;
	}
	if (i == 0)
		return 0;

 token:
	buf[i] = '\0';

	for (i = 0; keywords[i].name != NULL; i++) {
		if (strcmp(keywords[i].name, buf) == 0)
			return keywords[i].token;
	}

	if (isupper(buf[0])) {
		for (i = 1; buf[i] != '\0'; i++) {
			if (!isalnum(buf[i]) && buf[i] != '-')
				break;
		}
		if (buf[i] == '\0' && buf[i - 1] != '-') {
			strlcpy(yylval.string, buf, sizeof(yylval.string));
			return typereference;
		}
	}
	if (islower(buf[0])) {
		for (i = 1; buf[i] != '\0'; i++) {
			if (!isalnum(buf[i]) && buf[i] != '-')
				break;
		}
		if (buf[i] == '\0'&& buf[i - 1] != '-') {
			strlcpy(yylval.string, buf, sizeof(yylval.string));
			return identifier;
		}
	}

	if (buf[0] == '\'') {
		for (i = 1; buf[i] != '\0'; i++)
			continue;
		if (i < 3 || buf[i - 2] != '\'') {
			yyerror("incomplete binary or hexadecimal string");
			return ERROR;
		}
		if (tolower(buf[i - 1]) == 'b') {
			for (j = 1; j < i - 2; j++) {
				if (buf[j] != '0' && buf[j] != '1') {
					yyerror("invalid character in bstring");
					return ERROR;
				}
			}
			strlcpy(yylval.string, buf + 1, sizeof(yylval.string));
			yylval.string[i - 2] = '\0';
			return BSTRING;
		} else if (tolower(buf[i - 1]) == 'h') {
			for (j = 1; j < i - 2; j++) {
				if (!isxdigit(buf[j])) {
					yyerror("invalid character in hstring");
					return ERROR;
				}
			}
			strlcpy(yylval.string, buf + 1, sizeof(yylval.string));
			yylval.string[i - 2] = '\0';
			return HSTRING;
		}
		yyerror("no valid binary or hexadecimal string");
		return ERROR;
	}
	for (i = 0; buf[i] != '\0'; i++) {
		if (i == 0 && buf[i] == '-')
			continue;
		if (!isdigit(buf[i]))
			break;
	}
	if ((i == 1 && isdigit(buf[0])) || i > 1) {
		yylval.signednumber =
		    strtonum(buf, LLONG_MIN, LLONG_MAX, &errstr);
		if (errstr != NULL) {
			if (errno == ERANGE && isdigit(buf[0])) {
				errno = 0;
				yylval.number = strtoull(buf, &endptr, 10);
				if (errno == 0)
					return NUMBER;
			}
			yyerror("invalid number: %s: %s", buf, errstr);
			return ERROR;
		}
		if (buf[0] == '-')
			return SIGNEDNUMBER;
		yylval.number = yylval.signednumber;
		return NUMBER;
	}

	yyerror("unknown token: %s", buf);
	return ERROR;
}

void
mib_clear(void)
{
	struct module *m;
	struct item *iso;

	while ((m = RB_ROOT(&modulesci)) != NULL)
		mib_modulefree(m);

	/* iso */
	iso = RB_ROOT(&items);
	assert(strcmp(iso->name, "iso") == 0);
	RB_REMOVE(itemsgci, &itemsci, iso);
	RB_REMOVE(items, &items, iso);
	free(iso->oid.bo_id);
	free(iso);
	assert(RB_EMPTY(&modulesci));
	assert(RB_EMPTY(&modulescs));
	assert(RB_EMPTY(&items));
	assert(RB_EMPTY(&itemsci));
}

void
mib_modulefree(struct module *m)
{
	struct item *item;

	if (m == NULL)
		return;

	if (RB_FIND(modulesci, &modulesci, m) == m)
		RB_REMOVE(modulesci, &modulesci, m);
	if (RB_FIND(modulescs, &modulescs, m) == m)
		RB_REMOVE(modulescs, &modulescs, m);
	free(m->imports);

	while ((item = RB_ROOT(&m->itemscs)) != NULL) {
		RB_REMOVE(itemscs, &m->itemscs, item);
		if (RB_FIND(itemsci, &m->itemsci, item) == item)
			RB_REMOVE(itemsci, &m->itemsci, item);
		if (RB_FIND(items, &items, item) == item)
			RB_REMOVE(items, &items, item);
		if (RB_FIND(itemsgci, &itemsci, item) == item)
			RB_REMOVE(itemsgci, &itemsci, item);
		if (!item->resolved)
			free(item->oid_unresolved);
		else
			free(item->oid.bo_id);
		free(item);
	}

	free(m);
}

int
mib_imports_add(char *name, char **symbols)
{
	size_t im, ism, isi;
	struct import *import;

	for (im = 0; module->imports != NULL &&
	    module->imports[im].name[0] != '\0'; im++) {
		if (strcmp(module->imports[im].name, name) == 0)
			break;
	}
	if (module->imports == NULL || module->imports[im].name[0] == '\0') {
		if ((import = reallocarray(module->imports, im + 2,
		    sizeof(*module->imports))) == NULL) {
			yyerror("malloc");
			return -1;
		}
		module->imports = import;
		strlcpy(module->imports[im].name, name,
		    sizeof(module->imports[im].name));
		module->imports[im].nsymbols = 0;

		module->imports[im + 1].name[0] = '\0';
	}

	import = &module->imports[im];
	for (isi = 0; symbols[isi] != NULL; isi++) {
		for (ism = 0; ism < import->nsymbols; ism++) {
			if (strcmp(symbols[isi],
			    import->symbols[ism].name) == 0) {
				yyerror("symbol %s already imported",
				    symbols[isi]);
				break;
			}
		}
		if (ism != import->nsymbols)
			continue;

		if (import->nsymbols == nitems(import->symbols)) {
			yyerror("Too many symbols imported");
			return -1;
		}
		strlcpy(import->symbols[ism].name, symbols[isi],
		    sizeof(import->symbols[ism].name));
		import->symbols[ism].item = NULL;
		import->nsymbols++;
	}
	return 0;
}

int
mib_oid_append(struct oid_unresolved *oid, const struct objidcomponent *subid)
{
	if (oid->bo_n == nitems(oid->bo_id)) {
		yyerror("oid too long");
		return -1;
	}

	switch (oid->bo_id[oid->bo_n].type = subid->type) {
	case OCT_DESCRIPTOR:
		strlcpy(oid->bo_id[oid->bo_n].name, subid->name,
		    sizeof(oid->bo_id[oid->bo_n].name));
		break;
	case OCT_NUMBER:
		oid->bo_id[oid->bo_n].number = subid->number;
		break;
	case OCT_NAMEANDNUMBER:
		oid->bo_id[oid->bo_n].number = subid->number;
		strlcpy(oid->bo_id[oid->bo_n].name, subid->name,
		    sizeof(oid->bo_id[oid->bo_n].name));
	}
	oid->bo_n++;
	return 0;
}

int
mib_oid_concat(struct oid_unresolved *dst, const struct oid_unresolved *src)
{
	size_t i;

	for (i = 0; i < src->bo_n; i++)
		if (mib_oid_append(dst, &src->bo_id[i]) == -1)
			return -1;
	return 0;
}

struct item *
mib_item(const char *name, enum item_type type)
{
	struct item *item;

	if ((item = calloc(1, sizeof(*item))) == NULL) {
		log_warn("malloc");
		return NULL;
	}

	item->type = type;
	item->resolved = 0;
	item->module = module;
	(void)strlcpy(item->name, name, sizeof(item->name));

	if (RB_INSERT(itemscs, &module->itemscs, item) != NULL) {
		yyerror("duplicate item %s", name);
		free(item);
		return NULL;
	}

	return item;
}

int
mib_item_oid(struct item *item, const struct oid_unresolved *oid)
{
	if ((item->oid_unresolved = calloc(1,
	    sizeof(*item->oid_unresolved))) == NULL) {
		yyerror("malloc");
		return -1;
	}

	*item->oid_unresolved = *oid;
	return 0;
}

int
mib_macro(const char *name)
{
	return mib_item(name, IT_MACRO) == NULL ? -1 : 0;
}

struct item *
mib_oid(const char *name, const struct oid_unresolved *oid)
{
	struct item *item;

	if ((item = mib_item(name, IT_OID)) == NULL)
		return NULL;

	if (mib_item_oid(item, oid) == -1)
		return NULL;
	return item;
}

int
mib_applicationsyntax(const char *name)
{
	return mib_item(name, IT_APPLICATIONSYNTAX) == NULL ? -1 : 0;
}

int
mib_moduleidentity(const char *name, time_t lastupdated,
    const char *organization, const char *contactinfo, const char *description,
    const struct oid_unresolved *oid)
{
	struct item *item;

	if ((item = mib_item(name, IT_MODULE_IDENTITY)) == NULL)
		return -1;

	if (mib_item_oid(item, oid) == -1)
		return -1;

	module->lastupdated = lastupdated;
	return 0;
}

int
mib_objectidentity(const char *name, enum status status,
    const char *description, const char *reference,
    const struct oid_unresolved *oid)
{
	struct item *item;

	if ((item = mib_item(name, IT_OBJECT_IDENTITY)) == NULL)
		return -1;

	item->objectidentity.status = status;
	if (mib_item_oid(item, oid) == -1)
		return -1;

	return 0;
}

int
mib_objecttype(const char *name, void *syntax, const char *units,
    enum access maxaccess, enum status status, const char *description,
    const char *reference, void *index, void *defval,
    const struct oid_unresolved *oid)
{
	struct item *item;

	if ((item = mib_item(name, IT_OBJECT_TYPE)) == NULL)
		return -1;

	item->objecttype.maxaccess = maxaccess;
	item->objecttype.status = status;
	if (mib_item_oid(item, oid) == -1)
		return -1;
	return 0;
}

int
mib_notificationtype(const char *name, void *objects, enum status status,
    const char *description, const char *reference,
    const struct oid_unresolved *oid)
{
	struct item *item;

	if ((item = mib_item(name, IT_NOTIFICATION_TYPE)) == NULL)
		return -1;

	item->notificationtype.status = status;
	if (mib_item_oid(item, oid) == -1)
		return -1;
	return 0;
}

int
mib_textualconvetion(const char *name, const char *displayhint,
    enum status status, const char *description, const char *reference,
    void *syntax)
{
	struct item *item;

	if ((item = mib_item(name, IT_TEXTUAL_CONVENTION)) == NULL)
		return -1;
	item->textualconvention.status = status;
	return 0;
}

int
mib_objectgroup(const char *name, void *objects, enum status status,
    const char *description, const char *reference,
    const struct oid_unresolved *oid)
{
	struct item *item;

	if ((item = mib_item(name, IT_OBJECT_GROUP)) == NULL)
		return -1;

	if (mib_item_oid(item, oid) == -1)
		return -1;
	return 0;
}

int
mib_notificationgroup(const char *name, void *notifications, enum status status,
    const char *description, const char *reference,
    const struct oid_unresolved *oid)
{
	struct item *item;

	if ((item = mib_item(name, IT_NOTIFICATION_GROUP)) == NULL)
		return -1;

	if (mib_item_oid(item, oid) == -1)
		return -1;
	return 0;
}

int
mib_modulecompliance(const char *name, enum status status,
    const char *description, const char *reference, void *mods,
    const struct oid_unresolved *oid)
{
	struct item *item;

	if ((item = mib_item(name, IT_MODULE_COMPLIANCE)) == NULL)
		return -1;

	if (mib_item_oid(item, oid) == -1)
		return -1;
	return 0;
}

void
mib_parsefile(const char *path)
{
	mib_defaults();

	log_debug("mib parsing %s", path);
	if ((file.stream = fopen(path, "r")) == NULL) {
		log_warn("fopen");
		return;
	}
	file.lineno = 1;
	file.name = path;
	file.state = FILE_UNDEFINED;

	if (yyparse() != 0) {
		mib_modulefree(module);
		module = NULL;
	}

	fclose(file.stream);
}

void
mib_parsedir(const char *path)
{
	DIR *dir;
	struct dirent *dirent;
	char mibfile[PATH_MAX];

	if ((dir = opendir(path)) == NULL) {
		log_warn("opendir(%s)", path);
		return;
	}

	errno = 0;
	while ((dirent = readdir(dir)) != NULL) {
		if (dirent->d_name[0] == '.')
			continue;
		if (snprintf(mibfile, sizeof(mibfile), "%s/%s",
		    path, dirent->d_name) >= (int)sizeof(mibfile))
			continue;
		if (dirent->d_type == DT_DIR) {
			mib_parsedir(mibfile);
			continue;
		}
		if (dirent->d_type != DT_REG)
			continue;
		mib_parsefile(mibfile);
	}

	closedir(dir);
}

struct item *
mib_item_parent(struct ber_oid *oid)
{
	struct item *item, search;

	search.oid.bo_n = oid->bo_n;
	search.oid.bo_id = oid->bo_id;

	while (search.oid.bo_n > 0) {
		if ((item = RB_FIND(items, &items, &search)) != NULL)
			return item;
		search.oid.bo_n--;
	}
	return NULL;
}

const char *
mib_string2oid(const char *str, struct ber_oid *oid)
{
	char mname[512], *descriptor, *digits;
	struct module *m = NULL, msearch;
	struct item *item = NULL, isearch;
	struct ber_oid oidbuf;
	size_t i;

	oid->bo_n = 0;

	if (isdigit(str[0])) {
		if (ober_string2oid(str, oid) == -1)
			return "invalid OID";
		return NULL;
	}

	if (strlcpy(mname, str, sizeof(mname)) >= sizeof(mname))
		return "OID name too long";

	if ((descriptor = strchr(mname, ':')) != NULL) {
		descriptor[0] = '\0';
		if (descriptor[1] != ':')
			return "module and descriptor must be separated by "
			    "double colon";
		descriptor += 2;
		if (strlcpy(msearch.name, mname, sizeof(msearch.name)) >=
		    sizeof(msearch.name))
			return "module not found";
		if ((m = RB_FIND(modulescs, &modulescs, &msearch)) == NULL) {
			m = RB_FIND(modulesci, &modulesci, &msearch);
			if (m == NULL)
				return "module not found";
		}
	} else
		descriptor = mname;

	if ((digits = strchr(descriptor, '.')) != NULL) {
		digits++[0] = '\0';
		if (ober_string2oid(digits, &oidbuf) == -1)
			return "invalid OID";
	} else
		oidbuf.bo_n = 0;

	if (strlcpy(isearch.name, descriptor, sizeof(isearch.name)) >=
	    sizeof(isearch.name))
		return "descriptor not found";

	if (m != NULL) {
		item = RB_FIND(itemscs, &m->itemscs, &isearch);
		if (item == NULL)
			item = RB_FIND(itemsci, &m->itemsci, &isearch);
	} else
		item = RB_FIND(itemsgci, &itemsci, &isearch);
	if (item == NULL)
		return "descriptor not found";

	if (item->oid.bo_n + oidbuf.bo_n > nitems(oid->bo_id))
		return "OID too long";

	for (i = 0; i < item->oid.bo_n; i++)
		oid->bo_id[oid->bo_n++] = item->oid.bo_id[i];
	for (i = 0; i < oidbuf.bo_n; i++)
		oid->bo_id[oid->bo_n++] = oidbuf.bo_id[i];

	return NULL;
}

char *
mib_oid2string(struct ber_oid *oid, char *buf, size_t buflen,
    enum mib_oidfmt fmt)
{
	struct item *item;
	char digit[11];
	size_t i = 0;

	buf[0] = '\0';
	if (fmt == MIB_OIDSYMBOLIC && (item = mib_item_parent(oid)) != NULL) {
		snprintf(buf, buflen, "%s::%s", item->module->name,
		    item->name);
		i = item->oid.bo_n;
	}

	for (; i < oid->bo_n; i++) {
		if (i != 0)
			strlcat(buf, ".", buflen);
		snprintf(digit, sizeof(digit), "%"PRIu32, oid->bo_id[i]);
		strlcat(buf, digit, buflen);
	}

	return buf;
}

void
mib_defaults(void)
{
	struct oid_unresolved oid;
	struct item *iso;

	if (!RB_EMPTY(&modulesci))
		return;

	/* ASN.1 constant, not part of a module */
	if ((iso = calloc(1, sizeof(*iso))) == NULL)
		fatal("malloc");
	iso->type = IT_OID;
	iso->resolved = 1;
	iso->module = NULL;
	strlcpy(iso->name, "iso", sizeof(iso->name));
	if ((iso->oid.bo_id = calloc(1, sizeof(*iso->oid.bo_id))) == NULL)
		fatal("malloc");
	iso->oid.bo_id[0] = 1;
	iso->oid.bo_n = 1;
	RB_INSERT(items, &items, iso);
	RB_INSERT(itemsgci, &itemsci, iso);

	file.state = FILE_SMI2;
	file.lineno = 0;

	if ((module = calloc(1, sizeof(*module))) == NULL)
		fatal("malloc");
	RB_INIT(&module->itemscs);
	RB_INIT(&module->itemsci);
	module->resolved = 0;

	strlcpy(module->name, "SNMPv2-SMI", sizeof(module->name));
	file.name = module->name;
	oid.bo_id[0].type = oid.bo_id[1].type = OCT_NUMBER;
	oid.bo_n = 2;

	oid.bo_id[0].number = 1;
	oid.bo_id[1].number = 3;
	if (mib_oid("org", &oid) == NULL)
		exit(1);

	oid.bo_id[0].number = oid.bo_id[1].number = 0;
	if (mib_oid("zeroDotZero", &oid) == NULL)
		exit(1);

	oid.bo_id[0].type = OCT_DESCRIPTOR;
	strlcpy(oid.bo_id[0].name, "org", sizeof(oid.bo_id[0].name));
	oid.bo_id[1].number = 6;
	if (mib_oid("dod", &oid) == NULL)
		exit(1);

	strlcpy(oid.bo_id[0].name, "dod", sizeof(oid.bo_id[0].name));
	oid.bo_id[1].number = 1;
	if (mib_oid("internet", &oid) == NULL)
		exit(1);

	strlcpy(oid.bo_id[0].name, "internet", sizeof(oid.bo_id[0].name));
	oid.bo_id[1].number = 1;
	if (mib_oid("directory", &oid) == NULL)
		exit(1);

	oid.bo_id[1].number = 2;
	if (mib_oid("mgmt", &oid) == NULL)
		exit(1);

	strlcpy(oid.bo_id[0].name, "mgmt", sizeof(oid.bo_id[0].name));
	oid.bo_id[1].number = 1;
	if (mib_oid("mib-2", &oid) == NULL)
		exit(1);

	strlcpy(oid.bo_id[0].name, "mib-2", sizeof(oid.bo_id[0].name));
	oid.bo_id[1].number = 10;
	if (mib_oid("transmission", &oid) == NULL)
		exit(1);

	strlcpy(oid.bo_id[0].name, "internet", sizeof(oid.bo_id[0].name));
	oid.bo_id[1].number = 3;
	if (mib_oid("experimental", &oid) == NULL)
		exit(1);

	oid.bo_id[1].number = 4;
	if (mib_oid("private", &oid) == NULL)
		exit(1);

	oid.bo_id[1].number = 5;
	if (mib_oid("security", &oid) == NULL)
		exit(1);

	oid.bo_id[1].number = 6;
	if (mib_oid("snmpV2", &oid) == NULL)
		exit(1);

	strlcpy(oid.bo_id[0].name, "private", sizeof(oid.bo_id[0].name));
	oid.bo_id[1].number = 1;
	if (mib_oid("enterprises", &oid) == NULL)
		exit(1);

	strlcpy(oid.bo_id[0].name, "snmpV2", sizeof(oid.bo_id[0].name));
	oid.bo_id[1].number = 1;
	if (mib_oid("snmpDomains", &oid) == NULL)
		exit(1);

	oid.bo_id[1].number = 2;
	if (mib_oid("snmpProxys", &oid) == NULL)
		exit(1);

	oid.bo_id[1].number = 3;
	if (mib_oid("snmpModules", &oid) == NULL)
		exit(1);

	if (mib_macro("MODULE-IDENTITY") == -1 ||
	    mib_macro("OBJECT-IDENTITY") == -1 ||
	    mib_macro("OBJECT-TYPE") == -1 ||
	    mib_macro("NOTIFICATION-TYPE") == -1)
		exit(1);

	if (mib_applicationsyntax("Integer32") == -1 ||
	    mib_applicationsyntax("IpAddress") == -1 ||
	    mib_applicationsyntax("Counter32") == -1 ||
	    mib_applicationsyntax("Gauge32") == -1 ||
	    mib_applicationsyntax("Unsigned32") == -1 ||
	    mib_applicationsyntax("TimeTicks") == -1 ||
	    mib_applicationsyntax("Opaque") == -1 ||
	    mib_applicationsyntax("Counter64") == -1)
		exit(1);

	RB_INSERT(modulesci, &modulesci, module);
	RB_INSERT(modulescs, &modulescs, module);

	if ((module = calloc(1, sizeof(*module))) == NULL)
		fatal("malloc");
	RB_INIT(&module->itemscs);
	RB_INIT(&module->itemsci);
	module->resolved = 0;

	strlcpy(module->name, "SNMPv2-TC", sizeof(module->name));
	file.name = module->name;

	if (mib_macro("TEXTUAL-CONVENTION") == -1)
		exit(1);

	if (mib_textualconvetion(
	    "DisplayString", "255a", CURRENT, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "PhysAddress", "1x:", CURRENT, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "MacAddress", "1x:", CURRENT, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "TruthValue", NULL, CURRENT, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "TestAndIncr", NULL, CURRENT, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "AutonomousType", NULL, CURRENT, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "InstancePointer", NULL, OBSOLETE, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "VariablePointer", NULL, CURRENT, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "RowPointer", NULL, CURRENT, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "RowStatus", NULL, CURRENT, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "TimeStamp", NULL, CURRENT, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "TimeInterval", NULL, CURRENT, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "DateAndTime", NULL, CURRENT, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "StorageType", NULL, CURRENT, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "TDomain", NULL, CURRENT, "", NULL, NULL) == -1 ||
	    mib_textualconvetion(
	    "TAddress", NULL, CURRENT, "", NULL, NULL) == -1)
		exit(1);

	RB_INSERT(modulesci, &modulesci, module);
	RB_INSERT(modulescs, &modulescs, module);

	if ((module = calloc(1, sizeof(*module))) == NULL)
		fatal("malloc");
	RB_INIT(&module->itemscs);
	RB_INIT(&module->itemsci);
	module->resolved = 0;

	strlcpy(module->name, "SNMPv2-CONF", sizeof(module->name));
	file.name = module->name;

	if (mib_macro("OBJECT-GROUP") == -1 ||
	    mib_macro("NOTIFICATION-GROUP") == -1 ||
	    mib_macro("MODULE-COMPLIANCE") == -1 ||
	    mib_macro("AGENT-CAPABILITIES") == -1)
		exit(1);

	RB_INSERT(modulesci, &modulesci, module);
	RB_INSERT(modulescs, &modulescs, module);

	module = NULL;
}

/*
 * Used only for resolving phase
 */
struct item *
mib_item_find(struct item *orig, const char *name)
{
	struct module *m = orig->module;
	struct item *item, search;
	struct import *import;
	size_t i, j;

	strlcpy(search.name, name, sizeof(search.name));
	if ((item = RB_FIND(itemscs, &m->itemscs, &search)) != NULL) {
		if (mib_resolve_item(item) == -1)
			return NULL;
		return item;
	}

	for (i = 0; m->imports != NULL && m->imports[i].name[0] != '\0'; i++) {
		import = &m->imports[i];
		for (j = 0; j < import->nsymbols; j++) {
			if (strcmp(name, import->symbols[j].name) == 0)
				return import->symbols[j].item;
		}
	}

	log_warnx("%s::%s: item %s not found: disabling",
	    m->name, orig->name, name);

	return NULL;
}

int
mib_resolve_oid(struct oid_resolved *dst, struct oid_unresolved *src,
    struct item *item)
{
	struct module *m = item->module;
	struct item *reference, search;
	struct ber_oid oid;
	size_t i, j, bo_n;

	oid.bo_n = 0;
	for (i = 0; i < src->bo_n; i++) {
		switch (src->bo_id[i].type) {
		case OCT_DESCRIPTOR:
			if ((reference = mib_item_find(item,
			    src->bo_id[i].name)) == NULL)
				return -1;
			for (j = 0; j < reference->oid.bo_n; j++)
				oid.bo_id[oid.bo_n++] =
				    reference->oid.bo_id[j];
			break;
		case OCT_NUMBER:
			if (oid.bo_n == nitems(oid.bo_id)) {
				log_warnx("%s::%s: OID too long: disabling",
				    m->name, item->name);
				return -1;
			}
			oid.bo_id[oid.bo_n++] = src->bo_id[i].number;
			break;
		case OCT_NAMEANDNUMBER:
			if (oid.bo_n == nitems(oid.bo_id)) {
				log_warnx("%s::%s: OID too long: disabling",
				    m->name, item->name);
				return -1;
			}
			oid.bo_id[oid.bo_n++] = src->bo_id[i].number;
			if (i == src->bo_n - 1) {
				if (strcmp(src->bo_id[i].name, item->name) != 0) {
					log_warnx("%s::%s: last OBJECT "
					    "IDENTIFIER component name doesn't "
					    "match item: disabling", m->name,
					    item->name);
					return -1;
				}
				break;
			}
			strlcpy(search.name, src->bo_id[i].name,
			    sizeof(search.name));
			reference = RB_FIND(itemscs, &m->itemscs, &search);
			if (reference != NULL) {
				search.oid.bo_n = oid.bo_n;
				search.oid.bo_id = oid.bo_id;
				if (item_cmp_oid(reference, &search) != 0) {
					log_warnx("%s::%s: two different OIDs "
					    "for same descriptor: disabling",
					    m->name, item->name);
					return -1;
				}
				break;
			}

			bo_n = src->bo_n;
			src->bo_n = i + 1;
			module = m;
			reference = mib_oid(src->bo_id[i].name, src);
			module = NULL;
			if (reference == NULL)
				return -1;
			if (mib_resolve_item(reference) == -1)
				return -1;
			src->bo_n = bo_n;
			break;
		}
	}

	dst->bo_n = oid.bo_n;
	if ((dst->bo_id = calloc(dst->bo_n, sizeof(*dst->bo_id))) == NULL) {
		log_warn("malloc");
		return -1;
	}
	for (i = 0; i < oid.bo_n; i++)
		dst->bo_id[i] = oid.bo_id[i];

	return 0;
}

/*
 * No recursion protection. Assume MIBs do the right thing
 */
int
mib_resolve_item(struct item *item)
{
	struct item *prev;
	struct oid_resolved oid;

	if (item->resolved)
		return 0;

	item->resolved = 1;

	if (item->type == IT_MACRO ||
	    item->type == IT_APPLICATIONSYNTAX ||
	    item->type == IT_TEXTUAL_CONVENTION)
		return 0;

	if (mib_resolve_oid(&oid, item->oid_unresolved, item) == -1)
		return -1;
	free(item->oid_unresolved);
	item->oid = oid;

	if ((prev = RB_INSERT(items, &items, item)) != NULL) {
		/* Prioritize an OID derived from a MACRO over a plain OID */
		if (prev->type == IT_OID && item->type != IT_OID) {
			RB_REMOVE(items, &items, prev);
			RB_INSERT(items, &items, item);
		}
	}
	RB_INSERT(itemsgci, &itemsci, item);
	RB_INSERT(itemsci, &item->module->itemsci, item);

	return 0;
}

int
mib_resolve_module(struct module *m)
{
	struct module msearch;
	struct import *import;
	struct import_symbol *symbol;
	struct item *item, isearch;
	size_t i, j;

	if (m->resolved)
		return 0;

	m->resolved = 1;

	for (i = 0; m->imports != NULL && m->imports[i].name[0] != '\0'; i++) {
		import = &m->imports[i];
		strlcpy(msearch.name, import->name, sizeof(msearch.name));
		import->module = RB_FIND(modulescs, &modulescs, &msearch);
		if (import->module == NULL ||
		    mib_resolve_module(import->module) == -1) {
			log_warnx("%s: import %s not found: disabling",
			    m->name, import->name);
			goto fail;
		}

		for (j = 0; j < import->nsymbols; j++) {
			symbol = &import->symbols[j];
			strlcpy(isearch.name, symbol->name,
			    sizeof(isearch.name));
			symbol->item = RB_FIND(itemscs,
			    &import->module->itemscs, &isearch);
			if (symbol->item == NULL) {
				log_warnx("%s: symbol %s not found in %s: "
				    "disabling", m->name, symbol->name,
				    import->name);
				goto fail;
			}
		}
	}

	RB_FOREACH(item, itemscs, &m->itemscs) {
		if (mib_resolve_item(item) == -1)
			goto fail;
	}

	free(m->imports);
	m->imports = NULL;

	return 0;
 fail:
	mib_modulefree(m);
	return -1;
}

void
mib_resolve(void)
{
	struct module *m;

	mib_defaults();
 next:
	RB_FOREACH(m, modulescs, &modulescs) {
		if (mib_resolve_module(m) == -1) {
			/*
			 * mib_resolve_module can recurse and remove,
			 * for which RB_FOEACH_SAFE doesn't protect.
			 */
			goto next;
		}
	}
}

int
module_cmp_cs(struct module *m1, struct module *m2)
{
	return strcmp(m1->name, m2->name);
}

int
module_cmp_ci(struct module *m1, struct module *m2)
{
	return strcasecmp(m1->name, m2->name);
}

int
item_cmp_cs(struct item *d1, struct item *d2)
{
	return strcmp(d1->name, d2->name);
}

int
item_cmp_ci(struct item *d1, struct item *d2)
{
	return strcasecmp(d1->name, d2->name);
}

int
item_cmp_oid(struct item *i1, struct item *i2)
{
	size_t   i, min;

	min = i1->oid.bo_n < i2->oid.bo_n ? i1->oid.bo_n : i2->oid.bo_n;
	for (i = 0; i < min; i++) {
		if (i1->oid.bo_id[i] < i2->oid.bo_id[i])
			return (-1);
		if (i1->oid.bo_id[i] > i2->oid.bo_id[i])
			return (1);
	}
	/* i1 is parent of i2 */
	if (i1->oid.bo_n < i2->oid.bo_n)
		return (-2);
	/* i1 is child of i2 */
	if (i1->oid.bo_n > i2->oid.bo_n)
		return 2;
	return (0);
}

RB_GENERATE_STATIC(modulesci, module, entryci, module_cmp_ci);
RB_GENERATE_STATIC(modulescs, module, entrycs, module_cmp_cs);
RB_GENERATE_STATIC(itemsgci, item, entrygci, item_cmp_ci);
RB_GENERATE_STATIC(itemsci, item, entryci, item_cmp_ci);
RB_GENERATE_STATIC(itemscs, item, entrycs, item_cmp_cs);
RB_GENERATE_STATIC(items, item, entry, item_cmp_oid);
