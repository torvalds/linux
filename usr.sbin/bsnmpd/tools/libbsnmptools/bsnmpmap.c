/*-
 * Copyright (c) 2006 The FreeBSD Project
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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <bsnmp/asn1.h>
#include <bsnmp/snmp.h>
#include "bsnmptc.h"
#include "bsnmptools.h"

#define	DEBUG	if (_bsnmptools_debug) fprintf

/* Allocate memory and initialize list. */
struct snmp_mappings *
snmp_mapping_init(void)
{
	struct snmp_mappings *m;

	if ((m = calloc(1, sizeof(struct snmp_mappings))) == NULL) {
		syslog(LOG_ERR, "malloc() failed: %s", strerror(errno));
		return (NULL);
	}

	return (m);
}

#define		snmp_nodelist	mappings->nodelist
#define		snmp_intlist	mappings->intlist
#define		snmp_octlist	mappings->octlist
#define		snmp_oidlist	mappings->oidlist
#define		snmp_iplist	mappings->iplist
#define		snmp_ticklist	mappings->ticklist
#define		snmp_cntlist	mappings->cntlist
#define		snmp_gaugelist	mappings->gaugelist
#define		snmp_cnt64list	mappings->cnt64list
#define		snmp_enumlist	mappings->enumlist
#define		snmp_tablelist	mappings->tablelist
#define		snmp_tclist	mappings->tclist

void
enum_pairs_free(struct enum_pairs *headp)
{
	struct enum_pair *e;

	if (headp == NULL)
		return;

	while ((e = STAILQ_FIRST(headp)) != NULL) {
		STAILQ_REMOVE_HEAD(headp, link);

		if (e->enum_str)
			free(e->enum_str);
		free(e);
	}

	free(headp);
}

void
snmp_mapping_entryfree(struct snmp_oid2str *entry)
{
	if (entry->string)
		free(entry->string);

	if (entry->tc == SNMP_TC_OWN)
		enum_pairs_free(entry->snmp_enum);

	free(entry);
}

static void
snmp_mapping_listfree(struct snmp_mapping *headp)
{
	struct snmp_oid2str *p;

	while ((p = SLIST_FIRST(headp)) != NULL) {
		SLIST_REMOVE_HEAD(headp, link);

		if (p->string)
			free(p->string);

		if (p->tc == SNMP_TC_OWN)
			enum_pairs_free(p->snmp_enum);
		free(p);
	}

	SLIST_INIT(headp);
}

void
snmp_index_listfree(struct snmp_idxlist *headp)
{
	struct index *i;

	while ((i = STAILQ_FIRST(headp)) != NULL) {
		STAILQ_REMOVE_HEAD(headp, link);
		if (i->tc == SNMP_TC_OWN)
			enum_pairs_free(i->snmp_enum);
		free(i);
	}

	STAILQ_INIT(headp);
}

static void
snmp_mapping_table_listfree(struct snmp_table_index *headp)
{
	struct snmp_index_entry *t;

	while ((t = SLIST_FIRST(headp)) != NULL) {
		SLIST_REMOVE_HEAD(headp, link);

		if (t->string)
			free(t->string);

		snmp_index_listfree(&(t->index_list));
		free(t);
	}
}

static void
snmp_enumtc_listfree(struct snmp_enum_tc *headp)
{
	struct enum_type *t;

	while ((t = SLIST_FIRST(headp)) != NULL) {
		SLIST_REMOVE_HEAD(headp, link);

		if (t->name)
			free(t->name);
		enum_pairs_free(t->snmp_enum);
		free(t);
	}
}

int
snmp_mapping_free(struct snmp_toolinfo *snmptoolctx)
{
	if (snmptoolctx == NULL || snmptoolctx->mappings == NULL)
		return (-1);

	snmp_mapping_listfree(&snmptoolctx->snmp_nodelist);
	snmp_mapping_listfree(&snmptoolctx->snmp_intlist);
	snmp_mapping_listfree(&snmptoolctx->snmp_octlist);
	snmp_mapping_listfree(&snmptoolctx->snmp_oidlist);
	snmp_mapping_listfree(&snmptoolctx->snmp_iplist);
	snmp_mapping_listfree(&snmptoolctx->snmp_ticklist);
	snmp_mapping_listfree(&snmptoolctx->snmp_cntlist);
	snmp_mapping_listfree(&snmptoolctx->snmp_gaugelist);
	snmp_mapping_listfree(&snmptoolctx->snmp_cnt64list);
	snmp_mapping_listfree(&snmptoolctx->snmp_enumlist);
	snmp_mapping_table_listfree(&snmptoolctx->snmp_tablelist);
	snmp_enumtc_listfree(&snmptoolctx->snmp_tclist);
	free(snmptoolctx->mappings);

	return (0);
}

static void
snmp_dump_enumpairs(struct enum_pairs *headp)
{
	struct enum_pair *entry;

	if (headp == NULL)
		return;

	fprintf(stderr,"enums: ");
	STAILQ_FOREACH(entry, headp, link)
		fprintf(stderr,"%d - %s, ", entry->enum_val,
		    (entry->enum_str == NULL)?"NULL":entry->enum_str);

	fprintf(stderr,"; ");
}

void
snmp_dump_oid2str(struct snmp_oid2str *entry)
{
	char buf[ASN_OIDSTRLEN];

	if (entry != NULL) {
		memset(buf, 0, sizeof(buf));
		asn_oid2str_r(&(entry->var), buf);
		DEBUG(stderr, "%s - %s - %d - %d - %d", buf, entry->string,
		    entry->syntax, entry->access, entry->strlen);
		snmp_dump_enumpairs(entry->snmp_enum);
		DEBUG(stderr,"%s \n", (entry->table_idx == NULL)?"No table":
		    entry->table_idx->string);
	}
}

static void
snmp_dump_indexlist(struct snmp_idxlist *headp)
{
	struct index *entry;

	if (headp == NULL)
		return;

	STAILQ_FOREACH(entry, headp, link) {
		fprintf(stderr,"%d, ", entry->syntax);
		snmp_dump_enumpairs(entry->snmp_enum);
	}

	fprintf(stderr,"\n");
}

/* Initialize the enum pairs list of a oid2str entry. */
struct enum_pairs *
enum_pairs_init(void)
{
	struct enum_pairs *snmp_enum;

	if ((snmp_enum = malloc(sizeof(struct enum_pairs))) == NULL) {
		syslog(LOG_ERR, "malloc() failed: %s", strerror(errno));
		return (NULL);
	}

	STAILQ_INIT(snmp_enum);
	return (snmp_enum);
}

/*
 * Given a number and string, allocate memory for a (int, string) pair and add
 * it to the given oid2str mapping entry's enum pairs list.
 */
int32_t
enum_pair_insert(struct enum_pairs *headp, int32_t enum_val, char *enum_str)
{
	struct enum_pair *e_new;

	if ((e_new = calloc(1, sizeof(struct enum_pair))) == NULL) {
		syslog(LOG_ERR, "calloc() failed: %s", strerror(errno));
		return (-1);
	}

	if ((e_new->enum_str = strdup(enum_str)) == NULL) {
		syslog(LOG_ERR, "strdup() failed: %s", strerror(errno));
		free(e_new);
		return (-1);
	}

	e_new->enum_val = enum_val;
	STAILQ_INSERT_TAIL(headp, e_new, link);

	return (1);

}

/*
 * Insert an entry in a list - entries are lexicographicaly order by asn_oid.
 * Returns 1 on success, -1 if list is not initialized, 0 if a matching oid already
 * exists. Error cheking is left to calling function.
 */
static int
snmp_mapping_insert(struct snmp_mapping *headp, struct snmp_oid2str *entry)
{
	int32_t rc;
	struct snmp_oid2str *temp, *prev;

	if (entry == NULL)
		return(-1);

	if ((prev = SLIST_FIRST(headp)) == NULL ||
	    asn_compare_oid(&(entry->var), &(prev->var)) < 0) {
		SLIST_INSERT_HEAD(headp, entry, link);
		return (1);
	} else
		rc = -1;	/* Make the compiler happy. */

	SLIST_FOREACH(temp, headp, link) {
		if ((rc = asn_compare_oid(&(entry->var), &(temp->var))) <= 0)
			break;
		prev = temp;
		rc = -1;
	}

	switch (rc) {
	    case 0:
		/* Ops, matching OIDs - hope the rest info also matches. */
		if (strncmp(temp->string, entry->string, entry->strlen)) {
			syslog(LOG_INFO, "Matching OIDs with different string "
			    "mappings: old - %s, new - %s", temp->string,
			    entry->string);
			return (-1);
		}
		/*
		 * Ok, we have that already.
		 * As long as the strings match - don't complain.
		 */
		return (0);

	    case 1:
		SLIST_INSERT_AFTER(temp, entry, link);
		break;

	    case -1:
		SLIST_INSERT_AFTER(prev, entry, link);
		break;

	    default:
		/* NOTREACHED */
		return (-1);
	}

	return (1);
}

int32_t
snmp_node_insert(struct snmp_toolinfo *snmptoolctx, struct snmp_oid2str *entry)
{
	if (snmptoolctx != NULL && snmptoolctx->mappings)
		return (snmp_mapping_insert(&snmptoolctx->snmp_nodelist,entry));

	return (-1);
}

static int32_t
snmp_int_insert(struct snmp_toolinfo *snmptoolctx, struct snmp_oid2str *entry)
{
	if (snmptoolctx != NULL && snmptoolctx->mappings)
		return (snmp_mapping_insert(&snmptoolctx->snmp_intlist,entry));

	return (-1);
}

static int32_t
snmp_oct_insert(struct snmp_toolinfo *snmptoolctx, struct snmp_oid2str *entry)
{
	if (snmptoolctx != NULL && snmptoolctx->mappings)
		return (snmp_mapping_insert(&snmptoolctx->snmp_octlist,entry));

	return (-1);
}

static int32_t
snmp_oid_insert(struct snmp_toolinfo *snmptoolctx, struct snmp_oid2str *entry)
{
	if (snmptoolctx != NULL && snmptoolctx->mappings)
		return (snmp_mapping_insert(&snmptoolctx->snmp_oidlist,entry));

	return (-1);
}

static int32_t
snmp_ip_insert(struct snmp_toolinfo *snmptoolctx, struct snmp_oid2str *entry)
{
	if (snmptoolctx != NULL && snmptoolctx->mappings)
		return (snmp_mapping_insert(&snmptoolctx->snmp_iplist,entry));

	return (-1);
}

static int32_t
snmp_tick_insert(struct snmp_toolinfo *snmptoolctx, struct snmp_oid2str *entry)
{
	if (snmptoolctx != NULL && snmptoolctx->mappings)
		return (snmp_mapping_insert(&snmptoolctx->snmp_ticklist,entry));

	return (-1);
}

static int32_t
snmp_cnt_insert(struct snmp_toolinfo *snmptoolctx, struct snmp_oid2str *entry)
{
	if (snmptoolctx != NULL && snmptoolctx->mappings)
		return (snmp_mapping_insert(&snmptoolctx->snmp_cntlist,entry));

	return (-1);
}

static int32_t
snmp_gauge_insert(struct snmp_toolinfo *snmptoolctx, struct snmp_oid2str *entry)
{
	if (snmptoolctx != NULL && snmptoolctx->mappings)
		return (snmp_mapping_insert(&snmptoolctx->snmp_gaugelist,entry));

	return (-1);
}

static int32_t
snmp_cnt64_insert(struct snmp_toolinfo *snmptoolctx, struct snmp_oid2str *entry)
{
	if (snmptoolctx != NULL && snmptoolctx->mappings)
		return (snmp_mapping_insert(&snmptoolctx->snmp_cnt64list,entry));

	return (-1);
}

int32_t
snmp_enum_insert(struct snmp_toolinfo *snmptoolctx, struct snmp_oid2str *entry)
{
	if (snmptoolctx != NULL && snmptoolctx->mappings)
		return (snmp_mapping_insert(&snmptoolctx->snmp_enumlist,entry));

	return (-1);
}

int32_t
snmp_leaf_insert(struct snmp_toolinfo *snmptoolctx, struct snmp_oid2str *entry)
{
	switch (entry->syntax) {
		case SNMP_SYNTAX_INTEGER:
			return (snmp_int_insert(snmptoolctx, entry));
		case SNMP_SYNTAX_OCTETSTRING:
			return (snmp_oct_insert(snmptoolctx, entry));
		case SNMP_SYNTAX_OID:
			return (snmp_oid_insert(snmptoolctx, entry));
		case SNMP_SYNTAX_IPADDRESS:
			return (snmp_ip_insert(snmptoolctx, entry));
		case SNMP_SYNTAX_COUNTER:
			return (snmp_cnt_insert(snmptoolctx, entry));
		case SNMP_SYNTAX_GAUGE:
			return (snmp_gauge_insert(snmptoolctx, entry));
		case SNMP_SYNTAX_TIMETICKS:
			return (snmp_tick_insert(snmptoolctx, entry));
		case SNMP_SYNTAX_COUNTER64:
			return (snmp_cnt64_insert(snmptoolctx, entry));
		default:
			break;
	}

	return (-1);
}

static int32_t
snmp_index_insert(struct snmp_idxlist *headp, struct index *idx)
{
	if (headp == NULL || idx == NULL)
		return (-1);

	STAILQ_INSERT_TAIL(headp, idx, link);
	return (1);
}

int32_t
snmp_syntax_insert(struct snmp_idxlist *headp, struct enum_pairs *enums,
    enum snmp_syntax syntax, enum snmp_tc tc)
{
	struct index *idx;

	if ((idx = calloc(1, sizeof(struct index))) == NULL) {
		syslog(LOG_ERR, "malloc() failed: %s", strerror(errno));
		return (-1);
	}

	if (snmp_index_insert(headp, idx) < 0) {
		free(idx);
		return (-1);
	}

	idx->syntax = syntax;
	idx->snmp_enum = enums;
	idx->tc = tc;

	return (1);
}

int32_t
snmp_table_insert(struct snmp_toolinfo *snmptoolctx,
    struct snmp_index_entry *entry)
{
	int32_t rc;
	struct snmp_index_entry *temp, *prev;

	if (snmptoolctx == NULL || snmptoolctx->mappings == NULL ||
	    entry == NULL)
		return(-1);

	if ((prev = SLIST_FIRST(&snmptoolctx->snmp_tablelist)) == NULL ||
	    asn_compare_oid(&(entry->var), &(prev->var)) < 0) {
		SLIST_INSERT_HEAD(&snmptoolctx->snmp_tablelist, entry, link);
		return (1);
	} else
		rc = -1;	/* Make the compiler happy. */

	SLIST_FOREACH(temp, &snmptoolctx->snmp_tablelist, link) {
		if ((rc = asn_compare_oid(&(entry->var), &(temp->var))) <= 0)
			break;
		prev = temp;
		rc = -1;
	}

	switch (rc) {
	    case 0:
		/* Ops, matching OIDs - hope the rest info also matches. */
		if (strncmp(temp->string, entry->string, entry->strlen)) {
			syslog(LOG_INFO, "Matching OIDs with different string "
			    "mapping - old - %s, new - %s", temp->string,
			    entry->string);
			return (-1);
		}
		return(0);

	    case 1:
		SLIST_INSERT_AFTER(temp, entry, link);
		break;

	    case -1:
		SLIST_INSERT_AFTER(prev, entry, link);
		break;

	    default:
		/* NOTREACHED */
		return (-1);
	}

	return (1);
}

struct enum_type *
snmp_enumtc_init(char *name)
{
	struct enum_type *enum_tc;

	if ((enum_tc = calloc(1, sizeof(struct enum_type))) == NULL) {
		syslog(LOG_ERR, "malloc() failed: %s", strerror(errno));
		return (NULL);
	}

	if ((enum_tc->name = strdup(name)) == NULL) {
		syslog(LOG_ERR, "malloc() failed: %s", strerror(errno));
		free(enum_tc);
		return (NULL);
	}

	return (enum_tc);
}

void
snmp_enumtc_free(struct enum_type *tc)
{
	if (tc->name)
		free(tc->name);
	if (tc->snmp_enum)
		enum_pairs_free(tc->snmp_enum);
	free(tc);
}

void
snmp_enumtc_insert(struct snmp_toolinfo *snmptoolctx, struct enum_type *entry)
{
	if (snmptoolctx == NULL || snmptoolctx->mappings == NULL)
		return;	/* XXX no error handling? */

	SLIST_INSERT_HEAD(&snmptoolctx->snmp_tclist, entry, link);
}

struct enum_type *
snmp_enumtc_lookup(struct snmp_toolinfo *snmptoolctx, char *name)
{
	struct enum_type *temp;

	if (snmptoolctx == NULL || snmptoolctx->mappings == NULL)
		return (NULL);

	SLIST_FOREACH(temp, &snmptoolctx->snmp_tclist, link) {
		if (strcmp(temp->name, name) == 0)
			return (temp);
	}
	return (NULL);
}

static void
snmp_mapping_dumplist(struct snmp_mapping *headp)
{
	char buf[ASN_OIDSTRLEN];
	struct snmp_oid2str *entry;

	if (headp == NULL)
		return;

	SLIST_FOREACH(entry,headp,link) {
		memset(buf, 0, sizeof(buf));
		asn_oid2str_r(&(entry->var), buf);
		fprintf(stderr, "%s - %s - %d - %d - %d", buf, entry->string,
		    entry->syntax, entry->access ,entry->strlen);
		fprintf(stderr," - %s \n", (entry->table_idx == NULL)?
		    "No table":entry->table_idx->string);
	}
}

static void
snmp_mapping_dumptable(struct snmp_table_index *headp)
{
	char buf[ASN_OIDSTRLEN];
	struct snmp_index_entry *entry;

	if (headp == NULL)
		return;

	SLIST_FOREACH(entry, headp, link) {
		memset(buf, 0, sizeof(buf));
		asn_oid2str_r(&(entry->var), buf);
		fprintf(stderr,"%s - %s - %d - ", buf, entry->string,
		    entry->strlen);
		snmp_dump_indexlist(&(entry->index_list));
	}
}

void
snmp_mapping_dump(struct snmp_toolinfo *snmptoolctx /* int bits */)
{
	if (!_bsnmptools_debug)
		return;

	if (snmptoolctx == NULL) {
		fprintf(stderr,"No snmptool context!\n");
		return;
	}

	if (snmptoolctx->mappings == NULL) {
		fprintf(stderr,"No mappings!\n");
		return;
	}

	fprintf(stderr,"snmp_nodelist:\n");
	snmp_mapping_dumplist(&snmptoolctx->snmp_nodelist);

	fprintf(stderr,"snmp_intlist:\n");
	snmp_mapping_dumplist(&snmptoolctx->snmp_intlist);

	fprintf(stderr,"snmp_octlist:\n");
	snmp_mapping_dumplist(&snmptoolctx->snmp_octlist);

	fprintf(stderr,"snmp_oidlist:\n");
	snmp_mapping_dumplist(&snmptoolctx->snmp_oidlist);

	fprintf(stderr,"snmp_iplist:\n");
	snmp_mapping_dumplist(&snmptoolctx->snmp_iplist);

	fprintf(stderr,"snmp_ticklist:\n");
	snmp_mapping_dumplist(&snmptoolctx->snmp_ticklist);

	fprintf(stderr,"snmp_cntlist:\n");
	snmp_mapping_dumplist(&snmptoolctx->snmp_cntlist);

	fprintf(stderr,"snmp_gaugelist:\n");
	snmp_mapping_dumplist(&snmptoolctx->snmp_gaugelist);

	fprintf(stderr,"snmp_cnt64list:\n");
	snmp_mapping_dumplist(&snmptoolctx->snmp_cnt64list);

	fprintf(stderr,"snmp_enumlist:\n");
	snmp_mapping_dumplist(&snmptoolctx->snmp_enumlist);

	fprintf(stderr,"snmp_tablelist:\n");
	snmp_mapping_dumptable(&snmptoolctx->snmp_tablelist);
}

char *
enum_string_lookup(struct enum_pairs *headp, int32_t enum_val)
{
	struct enum_pair *temp;

	if (headp == NULL)
		return (NULL);

	STAILQ_FOREACH(temp, headp, link) {
		if (temp->enum_val == enum_val)
			return (temp->enum_str);
	}

	return (NULL);
}

int32_t
enum_number_lookup(struct enum_pairs *headp, char *e_str)
{
	struct enum_pair *tmp;

	if (headp == NULL)
		return (-1);

	STAILQ_FOREACH(tmp, headp, link)
		if (strncmp(tmp->enum_str, e_str, strlen(tmp->enum_str)) == 0)
			return (tmp->enum_val);

	return (-1);
}

static int32_t
snmp_lookuplist_string(struct snmp_mapping *headp, struct snmp_object *s)
{
	struct snmp_oid2str *temp;

	if (headp == NULL)
		return (-1);

	SLIST_FOREACH(temp, headp, link)
		if (asn_compare_oid(&(temp->var), &(s->val.var)) == 0)
			break;

	if ((s->info = temp) == NULL)
		return (-1);

	return (1);
}

/* provided an asn_oid find the corresponding string for it */
static int32_t
snmp_lookup_leaf(struct snmp_mapping *headp, struct snmp_object *s)
{
	struct snmp_oid2str *temp;

	if (headp == NULL)
		return (-1);

	SLIST_FOREACH(temp,headp,link) {
		if ((asn_compare_oid(&(temp->var), &(s->val.var)) == 0) ||
		    (asn_is_suboid(&(temp->var), &(s->val.var)))) {
			s->info = temp;
			return (1);
		}
	}

	return (-1);
}

int32_t
snmp_lookup_leafstring(struct snmp_toolinfo *snmptoolctx, struct snmp_object *s)
{
	if (snmptoolctx == NULL || snmptoolctx->mappings == NULL || s == NULL)
		return (-1);

	switch (s->val.syntax) {
		case SNMP_SYNTAX_INTEGER:
			return (snmp_lookup_leaf(&snmptoolctx->snmp_intlist, s));
		case SNMP_SYNTAX_OCTETSTRING:
			return (snmp_lookup_leaf(&snmptoolctx->snmp_octlist, s));
		case SNMP_SYNTAX_OID:
			return (snmp_lookup_leaf(&snmptoolctx->snmp_oidlist, s));
		case SNMP_SYNTAX_IPADDRESS:
			return (snmp_lookup_leaf(&snmptoolctx->snmp_iplist, s));
		case SNMP_SYNTAX_COUNTER:
			return (snmp_lookup_leaf(&snmptoolctx->snmp_cntlist, s));
		case SNMP_SYNTAX_GAUGE:
			return (snmp_lookup_leaf(
			    &snmptoolctx->snmp_gaugelist, s));
		case SNMP_SYNTAX_TIMETICKS:
			return (snmp_lookup_leaf(
			    &snmptoolctx->snmp_ticklist, s));
		case SNMP_SYNTAX_COUNTER64:
			return (snmp_lookup_leaf(
			    &snmptoolctx->snmp_cnt64list, s));
		case SNMP_SYNTAX_NOSUCHOBJECT:
			/* FALLTHROUGH */
		case SNMP_SYNTAX_NOSUCHINSTANCE:
			/* FALLTHROUGH */
		case SNMP_SYNTAX_ENDOFMIBVIEW:
			return (snmp_lookup_allstring(snmptoolctx, s));
		default:
			warnx("Unknown syntax - %d", s->val.syntax);
			break;
	}

	return (-1);
}

int32_t
snmp_lookup_enumstring(struct snmp_toolinfo *snmptoolctx, struct snmp_object *s)
{
	if (snmptoolctx == NULL || snmptoolctx->mappings == NULL || s == NULL)
		return (-1);

	return (snmp_lookuplist_string(&snmptoolctx->snmp_enumlist, s));
}

int32_t
snmp_lookup_oidstring(struct snmp_toolinfo *snmptoolctx, struct snmp_object *s)
{
	if (snmptoolctx == NULL || snmptoolctx->mappings == NULL || s == NULL)
		return (-1);

	return (snmp_lookuplist_string(&snmptoolctx->snmp_oidlist, s));
}

int32_t
snmp_lookup_nodestring(struct snmp_toolinfo *snmptoolctx, struct snmp_object *s)
{
	if (snmptoolctx == NULL || snmptoolctx->mappings == NULL || s == NULL)
		return (-1);

	return (snmp_lookuplist_string(&snmptoolctx->snmp_nodelist, s));
}

int32_t
snmp_lookup_allstring(struct snmp_toolinfo *snmptoolctx, struct snmp_object *s)
{
	if (snmptoolctx == NULL || snmptoolctx->mappings == NULL)
		return (-1);

	if (snmp_lookup_leaf(&snmptoolctx->snmp_intlist, s) > 0)
		return (1);
	if (snmp_lookup_leaf(&snmptoolctx->snmp_octlist, s) > 0)
		return (1);
	if (snmp_lookup_leaf(&snmptoolctx->snmp_oidlist, s) > 0)
		return (1);
	if (snmp_lookup_leaf(&snmptoolctx->snmp_iplist, s) > 0)
		return (1);
	if (snmp_lookup_leaf(&snmptoolctx->snmp_cntlist, s) > 0)
		return (1);
	if (snmp_lookup_leaf(&snmptoolctx->snmp_gaugelist, s) > 0)
		return (1);
	if (snmp_lookup_leaf(&snmptoolctx->snmp_ticklist, s) > 0)
		return (1);
	if (snmp_lookup_leaf(&snmptoolctx->snmp_cnt64list, s) > 0)
		return (1);
	if (snmp_lookuplist_string(&snmptoolctx->snmp_enumlist, s) > 0)
		return (1);
	if (snmp_lookuplist_string(&snmptoolctx->snmp_nodelist, s) > 0)
		return (1);

	return (-1);
}

int32_t
snmp_lookup_nonleaf_string(struct snmp_toolinfo *snmptoolctx,
    struct snmp_object *s)
{
	if (snmptoolctx == NULL)
		return (-1);

	if (snmp_lookuplist_string(&snmptoolctx->snmp_nodelist, s) > 0)
		return (1);
	if (snmp_lookuplist_string(&snmptoolctx->snmp_enumlist, s) > 0)
		return (1);

	return (-1);
}

static int32_t
snmp_lookup_oidlist(struct snmp_mapping *hp, struct snmp_object *s, char *oid)
{
	struct snmp_oid2str *temp;

	if (hp == NULL)
		return (-1);

	SLIST_FOREACH(temp, hp, link) {
		if (temp->strlen != strlen(oid))
			continue;

		if (strncmp(temp->string, oid, temp->strlen))
			continue;

		s->val.syntax = temp->syntax;
		s->info = temp;
		asn_append_oid(&(s->val.var), &(temp->var));
		return (1);
	}

	return (-1);
}

static int32_t
snmp_lookup_tablelist(struct snmp_toolinfo *snmptoolctx,
    struct snmp_table_index *headp, struct snmp_object *s, char *oid)
{
	struct snmp_index_entry *temp;

	if (snmptoolctx == NULL || headp == NULL)
		return (-1);

	SLIST_FOREACH(temp, headp, link) {
		if (temp->strlen != strlen(oid))
			continue;

		if (strncmp(temp->string, oid, temp->strlen))
			continue;

		/*
		 * Another hack here - if we were given a table name
		 * return the corresponding pointer to it's entry.
		 * That should not change the reponce we'll get.
		 */
		s->val.syntax = SNMP_SYNTAX_NULL;
		asn_append_oid(&(s->val.var), &(temp->var));
		if (snmp_lookup_leaf(&snmptoolctx->snmp_nodelist, s) > 0)
			return (1);
		else
			return (-1);
	}

	return (-1);
}

int32_t
snmp_lookup_oidall(struct snmp_toolinfo *snmptoolctx, struct snmp_object *s,
    char *oid)
{
	if (snmptoolctx == NULL || s == NULL || oid == NULL)
		return (-1);

	if (snmp_lookup_oidlist(&snmptoolctx->snmp_intlist, s, oid) > 0)
		return (1);
	if (snmp_lookup_oidlist(&snmptoolctx->snmp_octlist, s, oid) > 0)
		return (1);
	if (snmp_lookup_oidlist(&snmptoolctx->snmp_oidlist, s, oid) > 0)
		return (1);
	if (snmp_lookup_oidlist(&snmptoolctx->snmp_iplist, s, oid) > 0)
		return (1);
	if (snmp_lookup_oidlist(&snmptoolctx->snmp_ticklist, s, oid) > 0)
		return (1);
	if (snmp_lookup_oidlist(&snmptoolctx->snmp_cntlist, s, oid) > 0)
		return (1);
	if (snmp_lookup_oidlist(&snmptoolctx->snmp_gaugelist, s, oid) > 0)
		return (1);
	if (snmp_lookup_oidlist(&snmptoolctx->snmp_cnt64list, s, oid) > 0)
		return (1);
	if (snmp_lookup_oidlist(&snmptoolctx->snmp_nodelist, s, oid) > 0)
		return (1);
	if (snmp_lookup_tablelist(snmptoolctx, &snmptoolctx->snmp_tablelist,
	    s, oid) > 0)
		return (1);

	return (-1);
}

int32_t
snmp_lookup_enumoid(struct snmp_toolinfo *snmptoolctx, struct snmp_object *s,
    char *oid)
{
	if (snmptoolctx == NULL || s == NULL)
		return (-1);

	return (snmp_lookup_oidlist(&snmptoolctx->snmp_enumlist, s, oid));
}

int32_t
snmp_lookup_oid(struct snmp_toolinfo *snmptoolctx, struct snmp_object *s,
    char *oid)
{
	if (snmptoolctx == NULL || s == NULL)
		return (-1);

	switch (s->val.syntax) {
		case SNMP_SYNTAX_INTEGER:
			return (snmp_lookup_oidlist(&snmptoolctx->snmp_intlist,
			    s, oid));
		case SNMP_SYNTAX_OCTETSTRING:
			return (snmp_lookup_oidlist(&snmptoolctx->snmp_octlist,
			    s, oid));
		case SNMP_SYNTAX_OID:
			return (snmp_lookup_oidlist(&snmptoolctx->snmp_oidlist,
			    s, oid));
		case SNMP_SYNTAX_IPADDRESS:
			return (snmp_lookup_oidlist(&snmptoolctx->snmp_iplist,
			    s, oid));
		case SNMP_SYNTAX_COUNTER:
			return (snmp_lookup_oidlist(&snmptoolctx->snmp_cntlist,
			    s, oid));
		case SNMP_SYNTAX_GAUGE:
			return (snmp_lookup_oidlist(&snmptoolctx->snmp_gaugelist,
			    s, oid));
		case SNMP_SYNTAX_TIMETICKS:
			return (snmp_lookup_oidlist(&snmptoolctx->snmp_ticklist,
			    s, oid));
		case SNMP_SYNTAX_COUNTER64:
			return (snmp_lookup_oidlist(&snmptoolctx->snmp_cnt64list,
			    s, oid));
		case SNMP_SYNTAX_NULL:
			return (snmp_lookup_oidlist(&snmptoolctx->snmp_nodelist,
			    s, oid));
		default:
			warnx("Unknown syntax - %d", s->val.syntax);
			break;
	}

	return (-1);
}
