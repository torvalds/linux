/*-
 * Copyright (c) 2005-2006 The FreeBSD Project
 * All rights reserved.
 *
 * Author: Shteryana Shopova <syrinx@FreeBSD.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Helper functions common for all tools.
 *
 * $FreeBSD$
 */

#ifndef	_BSNMP_TOOLS_H_
#define	_BSNMP_TOOLS_H_

/* From asn1.h + 1 byte for trailing zero. */
#define	MAX_OCTSTRING_LEN	ASN_MAXOCTETSTRING + 1
#define	MAX_CMD_SYNTAX_LEN	12

/* Arbitrary upper limit on node names and function names - gensnmptree.c. */
#define	MAXSTR			1000

/* Should be enough to fetch the biggest allowed octet string. */
#define	MAX_BUFF_SIZE		(ASN_MAXOCTETSTRING + 50)

#define	SNMP_DEFS_DIR		"/usr/share/snmp/defs/"
#define	SNMP_DEFAULT_LOCAL	"/var/run/snmpd.sock"

#define	SNMP_MAX_REPETITIONS	10

enum snmp_access {
	SNMP_ACCESS_NONE = 0,
	SNMP_ACCESS_GET,
	SNMP_ACCESS_SET,
	SNMP_ACCESS_GETSET,
};

/* A structure for integer-string enumerations. */
struct enum_pair {
	int32_t	enum_val;
	char	*enum_str;
	STAILQ_ENTRY(enum_pair)	link;
};

STAILQ_HEAD(enum_pairs, enum_pair);

struct enum_type {
	char		*name;
	uint32_t	syntax;
	int32_t		is_enum;
	int32_t		is_bits;
	struct enum_pairs	*snmp_enum;
	SLIST_ENTRY(enum_type)	link;
};

SLIST_HEAD(snmp_enum_tc, enum_type);

struct index {
	enum snmp_tc		tc;
	enum snmp_syntax	syntax;
	struct enum_pairs	*snmp_enum;
	STAILQ_ENTRY(index)	link;
};

STAILQ_HEAD(snmp_idxlist, index);

struct snmp_index_entry {
	char			*string;
	uint32_t		strlen;
	struct asn_oid		var;
	struct snmp_idxlist	index_list;
	SLIST_ENTRY(snmp_index_entry)	link;
};

/* Information needed for oid to string conversion. */
struct snmp_oid2str {
	char			*string;
	uint32_t		strlen;
	enum snmp_tc		tc;
	enum snmp_syntax	syntax;
	enum snmp_access	access;
	struct asn_oid		var;
	/* A pointer to a entry from the table list - OK if NULL. */
	struct snmp_index_entry	*table_idx;
	/*
	 * A singly-linked tail queue of all (int, string) pairs -
	 * for INTEGER syntax only.
	 */
	struct enum_pairs	*snmp_enum;
	SLIST_ENTRY(snmp_oid2str)	link;
};

/* A structure to hold each oid input by user. */
struct snmp_object {
	/* Flag - if set, the variable caused error in a previous request. */
	int32_t			error;
	/*
	 * A pointer in the mapping lists - not used if OIDs are input as
	 * numericals.
	 */
	struct snmp_oid2str	*info;
	/* A snmp value to hold the actual oid, syntax and value. */
	struct snmp_value	val;
	SLIST_ENTRY(snmp_object)	link;
};

struct fname {
	char		*name;
	int32_t		done;
	struct asn_oid	cut;
	SLIST_ENTRY(fname)	link;
};

SLIST_HEAD(snmp_mapping, snmp_oid2str);
SLIST_HEAD(fname_list, fname);
SLIST_HEAD(snmp_table_index, snmp_index_entry);

/*
 * Keep a list for every syntax type.
 */
struct snmp_mappings {
	/* The list containing all non-leaf nodes. */
	struct snmp_mapping		nodelist;
	/* INTEGER/INTEGER32 types. */
	struct snmp_mapping		intlist;
	/* OCTETSTRING types. */
	struct snmp_mapping		octlist;
	/* OID types. */
	struct snmp_mapping		oidlist;
	/* IPADDRESS types. */
	struct snmp_mapping		iplist;
	/* TIMETICKS types. */
	struct snmp_mapping		ticklist;
	/* COUNTER types. */
	struct snmp_mapping		cntlist;
	/* GAUGE types. */
	struct snmp_mapping		gaugelist;
	/* COUNTER64 types. */
	struct snmp_mapping		cnt64list;
	/* ENUM values for oid types. */
	struct snmp_mapping		enumlist;
	/* Description of all table entry types. */
	struct snmp_table_index		tablelist;
	/* Defined enumerated textual conventions. */
	struct snmp_enum_tc		tclist;
};

struct snmp_toolinfo {
	uint32_t					flags;
	/* Number of initially input OIDs. */
	int32_t						objects;
	/* List of all input OIDs. */
	SLIST_HEAD(snmp_objectlist, snmp_object)	snmp_objectlist;
	/* All known OID to string mapping data. */
	struct snmp_mappings				*mappings;
	/* A list of .defs filenames to search oid<->string mapping. */
	struct fname_list				filelist;
	/* SNMPv3 USM user credentials */
	char						*passwd;
};

/* XXX we might want to get away with this and will need to touch
 * XXX the MACROS then too */
extern struct snmp_toolinfo snmptool;

/* Definitions for some flags' bits. */
#define	OUTPUT_BITS	0x00000003	/* bits 0-1 for output type */
#define	NUMERIC_BIT	0x00000004	/* bit 2 for numeric oids */
#define	RETRY_BIT	0x00000008 	/* bit 3 for retry on error response */
#define	ERRIGNORE_BIT	0x00000010	/* bit 4 for skip sanity checking */
#define	ERRIGNORE_BIT	0x00000010	/* bit 4 for skip sanity checking */
#define	EDISCOVER_BIT	0x00000020	/* bit 5 for SNMP Engine Discovery */
#define	LOCALKEY_BIT	0x00000040	/* bit 6 for using localized key */
		/*	0x00000080 */	/* bit 7 reserved */
#define	PDUTYPE_BITS	0x00000f00	/* bits 8-11 for pdu type */
		/*	0x0000f000 */	/* bit 12-15 reserved */
#define	MAXREP_BITS	0x00ff0000	/* bits 16-23 for max-repetit. value */
#define	NONREP_BITS	0xff000000	/* bits 24-31 for non-repeaters value */

#define	OUTPUT_SHORT		0x0
#define	OUTPUT_VERBOSE		0x1
#define	OUTPUT_TABULAR		0x2
#define	OUTPUT_QUIET		0x3

/* Macros for playing with flags' bits. */
#define	SET_OUTPUT(ctx, type)	((ctx)->flags |= ((type) & OUTPUT_BITS))
#define	GET_OUTPUT(ctx)		((ctx)->flags & OUTPUT_BITS)

#define	SET_NUMERIC(ctx)	((ctx)->flags |= NUMERIC_BIT)
#define	ISSET_NUMERIC(ctx)	((ctx)->flags & NUMERIC_BIT)

#define	SET_RETRY(ctx)		((ctx)->flags |= RETRY_BIT)
#define	ISSET_RETRY(ctx)	((ctx)->flags & RETRY_BIT)

#define	SET_ERRIGNORE(ctx)	((ctx)->flags |= ERRIGNORE_BIT)
#define	ISSET_ERRIGNORE(ctx)	((ctx)->flags & ERRIGNORE_BIT)

#define	SET_EDISCOVER(ctx)	((ctx)->flags |= EDISCOVER_BIT)
#define	ISSET_EDISCOVER(ctx)	((ctx)->flags & EDISCOVER_BIT)

#define	SET_LOCALKEY(ctx)	((ctx)->flags |= LOCALKEY_BIT)
#define	ISSET_LOCALKEY(ctx)	((ctx)->flags & LOCALKEY_BIT)

#define	SET_PDUTYPE(ctx, type)	(((ctx)->flags |= (((type) & 0xf) << 8)))
#define	GET_PDUTYPE(ctx)	(((ctx)->flags & PDUTYPE_BITS) >> 8)

#define	SET_MAXREP(ctx, i)	(((ctx)->flags |= (((i) & 0xff) << 16)))
#define	GET_MAXREP(ctx)		(((ctx)->flags & MAXREP_BITS) >> 16)

#define	SET_NONREP(ctx, i)	(((ctx)->flags |= (((i) & 0xff) << 24)))
#define	GET_NONREP(ctx)		(((ctx)->flags & NONREP_BITS) >> 24)

extern int _bsnmptools_debug;
extern const struct asn_oid IsoOrgDod_OID;

int snmptool_init(struct snmp_toolinfo *);
int32_t snmp_import_file(struct snmp_toolinfo *, struct fname *);
int32_t snmp_import_all(struct snmp_toolinfo *);
int32_t add_filename(struct snmp_toolinfo *, const char *,
    const struct asn_oid *, int32_t);
void free_filelist(struct snmp_toolinfo *);
void snmp_tool_freeall(struct snmp_toolinfo *);
void snmp_import_dump(int);

/* bsnmpmap.c */
struct snmp_mappings *snmp_mapping_init(void);
int32_t snmp_mapping_free(struct snmp_toolinfo *);
void snmp_index_listfree(struct snmp_idxlist *);
void snmp_dump_oid2str(struct snmp_oid2str *);
int32_t snmp_node_insert(struct snmp_toolinfo *, struct snmp_oid2str *);
int32_t snmp_leaf_insert(struct snmp_toolinfo *, struct snmp_oid2str *);
int32_t snmp_enum_insert(struct snmp_toolinfo *, struct snmp_oid2str *);
struct enum_pairs *enum_pairs_init(void);
void enum_pairs_free(struct enum_pairs *);
void snmp_mapping_entryfree(struct snmp_oid2str *);
int32_t enum_pair_insert(struct enum_pairs *, int32_t, char *);
char *enum_string_lookup(struct enum_pairs *, int32_t);
int32_t enum_number_lookup(struct enum_pairs *, char *);
int32_t snmp_syntax_insert(struct snmp_idxlist *, struct enum_pairs *,
    enum snmp_syntax, enum snmp_tc);
int32_t snmp_table_insert(struct snmp_toolinfo *, struct snmp_index_entry *);

struct enum_type *snmp_enumtc_init(char *);
void snmp_enumtc_free(struct enum_type *);
void snmp_enumtc_insert(struct snmp_toolinfo *, struct enum_type *);
struct enum_type *snmp_enumtc_lookup(struct snmp_toolinfo *, char *);

void snmp_mapping_dump(struct snmp_toolinfo *);
int32_t snmp_lookup_leafstring(struct snmp_toolinfo *, struct snmp_object *);
int32_t snmp_lookup_enumstring(struct snmp_toolinfo *, struct snmp_object *);
int32_t snmp_lookup_oidstring(struct snmp_toolinfo *, struct snmp_object *);
int32_t snmp_lookup_nonleaf_string(struct snmp_toolinfo *, struct snmp_object *);
int32_t snmp_lookup_allstring(struct snmp_toolinfo *, struct snmp_object *);
int32_t snmp_lookup_nodestring(struct snmp_toolinfo *, struct snmp_object *);
int32_t snmp_lookup_oidall(struct snmp_toolinfo *, struct snmp_object *, char *);
int32_t snmp_lookup_enumoid(struct snmp_toolinfo *, struct snmp_object *, char *);
int32_t snmp_lookup_oid(struct snmp_toolinfo *, struct snmp_object *, char *);

/* Functions parsing common options for all tools. */
int32_t parse_server(char *);
int32_t parse_timeout(char *);
int32_t parse_retry(char *);
int32_t parse_version(char *);
int32_t parse_local_path(char *);
int32_t parse_buflen(char *);
int32_t parse_debug(void);
int32_t parse_discovery(struct snmp_toolinfo *);
int32_t parse_local_key(struct snmp_toolinfo *);
int32_t parse_num_oids(struct snmp_toolinfo *);
int32_t parse_file(struct snmp_toolinfo *, char *);
int32_t parse_include(struct snmp_toolinfo *, char *);
int32_t parse_output(struct snmp_toolinfo *, char *);
int32_t parse_errors(struct snmp_toolinfo *);
int32_t parse_skip_access(struct snmp_toolinfo *);
int32_t parse_authentication(struct snmp_toolinfo *, char *);
int32_t parse_privacy(struct snmp_toolinfo *, char *);
int32_t parse_context(struct snmp_toolinfo *, char *);
int32_t parse_user_security(struct snmp_toolinfo *, char *);

typedef int32_t (*snmp_verify_inoid_f) (struct snmp_toolinfo *,
    struct snmp_object *, char *);
int32_t snmp_object_add(struct snmp_toolinfo *, snmp_verify_inoid_f, char *);
int32_t snmp_object_remove(struct snmp_toolinfo *, struct asn_oid *oid);
int32_t snmp_object_seterror(struct snmp_toolinfo *, struct snmp_value *,
    int32_t);

enum snmp_syntax parse_syntax(char *);
char *snmp_parse_suboid(char *, struct asn_oid *);
char *snmp_parse_index(struct snmp_toolinfo *, char *, struct snmp_object *);
int32_t snmp_parse_numoid(char *, struct asn_oid *);
int32_t snmp_suboid_append(struct asn_oid *, asn_subid_t);
int32_t snmp_suboid_pop(struct asn_oid *);

typedef int32_t (*snmp_verify_vbind_f) (struct snmp_toolinfo *,
    struct snmp_pdu *, struct snmp_object *);
typedef int32_t (*snmp_add_vbind_f) (struct snmp_pdu *, struct snmp_object *);
int32_t snmp_pdu_add_bindings(struct snmp_toolinfo *, snmp_verify_vbind_f,
    snmp_add_vbind_f, struct snmp_pdu *, int32_t);

int32_t snmp_parse_get_resp(struct snmp_pdu *, struct snmp_pdu *);
int32_t snmp_parse_getbulk_resp(struct snmp_pdu *, struct snmp_pdu *);
int32_t snmp_parse_getnext_resp(struct snmp_pdu *, struct snmp_pdu *);
int32_t snmp_parse_resp(struct snmp_pdu *, struct snmp_pdu *);
int32_t snmp_output_numval(struct snmp_toolinfo *, struct snmp_value *,
    struct snmp_oid2str *);
void snmp_output_val(struct snmp_value *);
int32_t snmp_output_resp(struct snmp_toolinfo *, struct snmp_pdu *, struct asn_oid *);
void snmp_output_err_resp(struct snmp_toolinfo *, struct snmp_pdu *);
void snmp_output_engine(void);
void snmp_output_keys(void);

#endif /* _BSNMP_TOOLS_H_ */
