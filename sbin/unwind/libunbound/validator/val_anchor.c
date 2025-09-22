/*
 * validator/val_anchor.c - validator trust anchor storage.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains storage for the trust anchors for the validator.
 */
#include "config.h"
#include <ctype.h>
#include "validator/val_anchor.h"
#include "validator/val_sigcrypt.h"
#include "validator/autotrust.h"
#include "util/data/packed_rrset.h"
#include "util/data/dname.h"
#include "util/log.h"
#include "util/net_help.h"
#include "util/config_file.h"
#include "util/as112.h"
#include "sldns/sbuffer.h"
#include "sldns/rrdef.h"
#include "sldns/str2wire.h"
#ifdef HAVE_GLOB_H
#include <glob.h>
#endif

int
anchor_cmp(const void* k1, const void* k2)
{
	int m;
	struct trust_anchor* n1 = (struct trust_anchor*)k1;
	struct trust_anchor* n2 = (struct trust_anchor*)k2;
	/* no need to ntohs(class) because sort order is irrelevant */
	if(n1->dclass != n2->dclass) {
		if(n1->dclass < n2->dclass)
			return -1;
		return 1;
	}
	return dname_lab_cmp(n1->name, n1->namelabs, n2->name, n2->namelabs, 
		&m);
}

struct val_anchors* 
anchors_create(void)
{
	struct val_anchors* a = (struct val_anchors*)calloc(1, sizeof(*a));
	if(!a)
		return NULL;
	a->tree = rbtree_create(anchor_cmp);
	if(!a->tree) {
		anchors_delete(a);
		return NULL;
	}
	a->autr = autr_global_create();
	if(!a->autr) {
		anchors_delete(a);
		return NULL;
	}
	lock_basic_init(&a->lock);
	lock_protect(&a->lock, a, sizeof(*a));
	lock_protect(&a->lock, a->autr, sizeof(*a->autr));
	return a;
}

/** delete assembled rrset */
static void
assembled_rrset_delete(struct ub_packed_rrset_key* pkey)
{
	if(!pkey) return;
	if(pkey->entry.data) {
		struct packed_rrset_data* pd = (struct packed_rrset_data*)
			pkey->entry.data;
		free(pd->rr_data);
		free(pd->rr_ttl);
		free(pd->rr_len);
		free(pd);
	}
	free(pkey->rk.dname);
	free(pkey);
}

/** destroy locks in tree and delete autotrust anchors */
static void
anchors_delfunc(rbnode_type* elem, void* ATTR_UNUSED(arg))
{
	struct trust_anchor* ta = (struct trust_anchor*)elem;
	if(!ta) return;
	if(ta->autr) {
		autr_point_delete(ta);
	} else {
		struct ta_key* p, *np;
		lock_basic_destroy(&ta->lock);
		free(ta->name);
		p = ta->keylist;
		while(p) {
			np = p->next;
			free(p->data);
			free(p);
			p = np;
		}
		assembled_rrset_delete(ta->ds_rrset);
		assembled_rrset_delete(ta->dnskey_rrset);
		free(ta);
	}
}

void 
anchors_delete(struct val_anchors* anchors)
{
	if(!anchors)
		return;
	lock_unprotect(&anchors->lock, anchors->autr);
	lock_unprotect(&anchors->lock, anchors);
	lock_basic_destroy(&anchors->lock);
	if(anchors->tree)
		traverse_postorder(anchors->tree, anchors_delfunc, NULL);
	free(anchors->tree);
	autr_global_delete(anchors->autr);
	free(anchors);
}

void
anchors_init_parents_locked(struct val_anchors* anchors)
{
	struct trust_anchor* node, *prev = NULL, *p;
	int m; 
	/* nobody else can grab locks because we hold the main lock.
	 * Thus the previous items, after unlocked, are not deleted */
	RBTREE_FOR(node, struct trust_anchor*, anchors->tree) {
		lock_basic_lock(&node->lock);
		node->parent = NULL;
		if(!prev || prev->dclass != node->dclass) {
			prev = node;
			lock_basic_unlock(&node->lock);
			continue;
		}
		(void)dname_lab_cmp(prev->name, prev->namelabs, node->name, 
			node->namelabs, &m); /* we know prev is smaller */
		/* sort order like: . com. bla.com. zwb.com. net. */
		/* find the previous, or parent-parent-parent */
		for(p = prev; p; p = p->parent)
			/* looking for name with few labels, a parent */
			if(p->namelabs <= m) {
				/* ==: since prev matched m, this is closest*/
				/* <: prev matches more, but is not a parent,
			 	* this one is a (grand)parent */
				node->parent = p;
				break;
			}
		lock_basic_unlock(&node->lock);
		prev = node;
	}
}

/** initialise parent pointers in the tree */
static void
init_parents(struct val_anchors* anchors)
{
	lock_basic_lock(&anchors->lock);
	anchors_init_parents_locked(anchors);
	lock_basic_unlock(&anchors->lock);
}

struct trust_anchor*
anchor_find(struct val_anchors* anchors, uint8_t* name, int namelabs,
	size_t namelen, uint16_t dclass)
{
	struct trust_anchor key;
	rbnode_type* n;
	if(!name) return NULL;
	key.node.key = &key;
	key.name = name;
	key.namelabs = namelabs;
	key.namelen = namelen;
	key.dclass = dclass;
	lock_basic_lock(&anchors->lock);
	n = rbtree_search(anchors->tree, &key);
	if(n) {
		lock_basic_lock(&((struct trust_anchor*)n->key)->lock);
	}
	lock_basic_unlock(&anchors->lock);
	if(!n)
		return NULL;
	return (struct trust_anchor*)n->key;
}

/** create new trust anchor object */
static struct trust_anchor*
anchor_new_ta(struct val_anchors* anchors, uint8_t* name, int namelabs,
	size_t namelen, uint16_t dclass, int lockit)
{
#ifdef UNBOUND_DEBUG
	rbnode_type* r;
#endif
	struct trust_anchor* ta = (struct trust_anchor*)malloc(
		sizeof(struct trust_anchor));
	if(!ta)
		return NULL;
	memset(ta, 0, sizeof(*ta));
	ta->node.key = ta;
	ta->name = memdup(name, namelen);
	if(!ta->name) {
		free(ta);
		return NULL;
	}
	ta->namelabs = namelabs;
	ta->namelen = namelen;
	ta->dclass = dclass;
	lock_basic_init(&ta->lock);
	if(lockit) {
		lock_basic_lock(&anchors->lock);
	}
#ifdef UNBOUND_DEBUG
	r =
#else
	(void)
#endif
	rbtree_insert(anchors->tree, &ta->node);
	if(lockit) {
		lock_basic_unlock(&anchors->lock);
	}
	log_assert(r != NULL);
	return ta;
}

/** find trustanchor key by exact data match */
static struct ta_key*
anchor_find_key(struct trust_anchor* ta, uint8_t* rdata, size_t rdata_len,
	uint16_t type)
{
	struct ta_key* k;
	for(k = ta->keylist; k; k = k->next) {
		if(k->type == type && k->len == rdata_len &&
			memcmp(k->data, rdata, rdata_len) == 0)
			return k;
	}
	return NULL;
}
	
/** create new trustanchor key */
static struct ta_key*
anchor_new_ta_key(uint8_t* rdata, size_t rdata_len, uint16_t type)
{
	struct ta_key* k = (struct ta_key*)malloc(sizeof(*k));
	if(!k)
		return NULL;
	memset(k, 0, sizeof(*k));
	k->data = memdup(rdata, rdata_len);
	if(!k->data) {
		free(k);
		return NULL;
	}
	k->len = rdata_len;
	k->type = type;
	return k;
}

/**
 * This routine adds a new RR to a trust anchor. The trust anchor may not
 * exist yet, and is created if not. The RR can be DS or DNSKEY.
 * This routine will also remove duplicates; storing them only once.
 * @param anchors: anchor storage.
 * @param name: name of trust anchor (wireformat)
 * @param type: type or RR
 * @param dclass: class of RR
 * @param rdata: rdata wireformat, starting with rdlength.
 *	If NULL, nothing is stored, but an entry is created.
 * @param rdata_len: length of rdata including rdlength.
 * @return: NULL on error, else the trust anchor.
 */
static struct trust_anchor*
anchor_store_new_key(struct val_anchors* anchors, uint8_t* name, uint16_t type,
	uint16_t dclass, uint8_t* rdata, size_t rdata_len)
{
	struct ta_key* k;
	struct trust_anchor* ta;
	int namelabs;
	size_t namelen;
	namelabs = dname_count_size_labels(name, &namelen);
	if(type != LDNS_RR_TYPE_DS && type != LDNS_RR_TYPE_DNSKEY) {
		log_err("Bad type for trust anchor");
		return 0;
	}
	/* lookup or create trustanchor */
	ta = anchor_find(anchors, name, namelabs, namelen, dclass);
	if(!ta) {
		ta = anchor_new_ta(anchors, name, namelabs, namelen, dclass, 1);
		if(!ta)
			return NULL;
		lock_basic_lock(&ta->lock);
	}
	if(!rdata) {
		lock_basic_unlock(&ta->lock);
		return ta;
	}
	/* look for duplicates */
	if(anchor_find_key(ta, rdata, rdata_len, type)) {
		lock_basic_unlock(&ta->lock);
		return ta;
	}
	k = anchor_new_ta_key(rdata, rdata_len, type);
	if(!k) {
		lock_basic_unlock(&ta->lock);
		return NULL;
	}
	/* add new key */
	if(type == LDNS_RR_TYPE_DS)
		ta->numDS++;
	else	ta->numDNSKEY++;
	k->next = ta->keylist;
	ta->keylist = k;
	lock_basic_unlock(&ta->lock);
	return ta;
}

/**
 * Add new RR. It converts ldns RR to wire format.
 * @param anchors: anchor storage.
 * @param rr: the wirerr.
 * @param rl: length of rr.
 * @param dl: length of dname.
 * @return NULL on error, else the trust anchor.
 */
static struct trust_anchor*
anchor_store_new_rr(struct val_anchors* anchors, uint8_t* rr, size_t rl,
	size_t dl)
{
	struct trust_anchor* ta;
	if(!(ta=anchor_store_new_key(anchors, rr,
		sldns_wirerr_get_type(rr, rl, dl),
		sldns_wirerr_get_class(rr, rl, dl),
		sldns_wirerr_get_rdatawl(rr, rl, dl),
		sldns_wirerr_get_rdatalen(rr, rl, dl)+2))) {
		return NULL;
	}
	log_nametypeclass(VERB_QUERY, "adding trusted key",
		rr, sldns_wirerr_get_type(rr, rl, dl),
		sldns_wirerr_get_class(rr, rl, dl));
	return ta;
}

/**
 * Insert insecure anchor
 * @param anchors: anchor storage.
 * @param str: the domain name.
 * @return NULL on error, Else last trust anchor point
 */
static struct trust_anchor*
anchor_insert_insecure(struct val_anchors* anchors, const char* str)
{
	struct trust_anchor* ta;
	size_t dname_len = 0;
	uint8_t* nm = sldns_str2wire_dname(str, &dname_len);
	if(!nm) {
		log_err("parse error in domain name '%s'", str);
		return NULL;
	}
	ta = anchor_store_new_key(anchors, nm, LDNS_RR_TYPE_DS,
		LDNS_RR_CLASS_IN, NULL, 0);
	free(nm);
	return ta;
}

struct trust_anchor*
anchor_store_str(struct val_anchors* anchors, sldns_buffer* buffer,
	const char* str)
{
	struct trust_anchor* ta;
	uint8_t* rr = sldns_buffer_begin(buffer);
	size_t len = sldns_buffer_capacity(buffer), dname_len = 0;
	int status = sldns_str2wire_rr_buf(str, rr, &len, &dname_len,
		0, NULL, 0, NULL, 0);
	if(status != 0) {
		log_err("error parsing trust anchor %s: at %d: %s", 
			str, LDNS_WIREPARSE_OFFSET(status),
			sldns_get_errorstr_parse(status));
		return NULL;
	}
	if(!(ta=anchor_store_new_rr(anchors, rr, len, dname_len))) {
		log_err("out of memory");
		return NULL;
	}
	return ta;
}

/**
 * Read a file with trust anchors
 * @param anchors: anchor storage.
 * @param buffer: parsing buffer.
 * @param fname: string.
 * @param onlyone: only one trust anchor allowed in file.
 * @return NULL on error. Else last trust-anchor point.
 */
static struct trust_anchor*
anchor_read_file(struct val_anchors* anchors, sldns_buffer* buffer,
	const char* fname, int onlyone)
{
	struct trust_anchor* ta = NULL, *tanew;
	struct sldns_file_parse_state pst;
	int status;
	size_t len, dname_len;
	uint8_t* rr = sldns_buffer_begin(buffer);
	int ok = 1;
	FILE* in = fopen(fname, "r");
	if(!in) {
		log_err("error opening file %s: %s", fname, strerror(errno));
		return 0;
	}
	memset(&pst, 0, sizeof(pst));
	pst.default_ttl = 3600;
	pst.lineno = 1;
	while(!feof(in)) {
		len = sldns_buffer_capacity(buffer);
		dname_len = 0;
		status = sldns_fp2wire_rr_buf(in, rr, &len, &dname_len, &pst);
		if(len == 0) /* empty, $TTL, $ORIGIN */
			continue;
		if(status != 0) {
			log_err("parse error in %s:%d:%d : %s", fname,
				pst.lineno, LDNS_WIREPARSE_OFFSET(status),
				sldns_get_errorstr_parse(status));
			ok = 0;
			break;
		}
		if(sldns_wirerr_get_type(rr, len, dname_len) !=
			LDNS_RR_TYPE_DS && sldns_wirerr_get_type(rr, len,
			dname_len) != LDNS_RR_TYPE_DNSKEY) {
			continue;
		}
		if(!(tanew=anchor_store_new_rr(anchors, rr, len, dname_len))) {
			log_err("mem error at %s line %d", fname, pst.lineno);
			ok = 0;
			break;
		}
		if(onlyone && ta && ta != tanew) {
			log_err("error at %s line %d: no multiple anchor "
				"domains allowed (you can have multiple "
				"keys, but they must have the same name).", 
				fname, pst.lineno);
			ok = 0;
			break;
		}
		ta = tanew;
	}
	fclose(in);
	if(!ok) return NULL;
	/* empty file is OK when multiple anchors are allowed */
	if(!onlyone && !ta) return (struct trust_anchor*)1;
	return ta;
}

/** skip file to end of line */
static void
skip_to_eol(FILE* in, int *c)
{
	while((*c = getc(in)) != EOF ) {
		if(*c == '\n')
			return;
	}
}

/** true for special characters in bind configs */
static int
is_bind_special(int c)
{
	switch(c) {
		case '{':
		case '}':
		case '"':
		case ';':
			return 1;
	}
	return 0;
}

/** 
 * Read a keyword skipping bind comments; spaces, specials, restkeywords. 
 * The file is split into the following tokens:
 *	* special characters, on their own, rdlen=1, { } doublequote ;
 *	* whitespace becomes a single ' ' or tab. Newlines become spaces.
 *	* other words ('keywords')
 *	* comments are skipped if desired
 *		/ / C++ style comment to end of line
 *		# to end of line
 *		/ * C style comment * /
 * @param in: file to read from.
 * @param buf: buffer, what is read is stored after current buffer position.
 *	Space is left in the buffer to write a terminating 0.
 * @param line: line number is increased per line, for error reports.
 * @param comments: if 0, comments are not possible and become text.
 *	if 1, comments are skipped entirely.
 *	In BIND files, this is when reading quoted strings, for example
 *	" base 64 text with / / in there "
 * @return the number of character written to the buffer. 
 *	0 on end of file.
 */
static int
readkeyword_bindfile(FILE* in, sldns_buffer* buf, int* line, int comments)
{
	int c;
	int numdone = 0;
	while((c = getc(in)) != EOF ) {
		if(comments && c == '#') {	/*   # blabla   */
			skip_to_eol(in, &c);
			if(c == EOF) return 0;
			(*line)++;
			continue;
		} else if(comments && c=='/' && numdone>0 && /* /_/ bla*/
			sldns_buffer_read_u8_at(buf, 
			sldns_buffer_position(buf)-1) == '/') {
			sldns_buffer_skip(buf, -1);
			numdone--;
			skip_to_eol(in, &c);
			if(c == EOF) return 0;
			(*line)++;
			continue;
		} else if(comments && c=='*' && numdone>0 && /* /_* bla *_/ */
			sldns_buffer_read_u8_at(buf, 
			sldns_buffer_position(buf)-1) == '/') {
			sldns_buffer_skip(buf, -1);
			numdone--;
			/* skip to end of comment */
			while(c != EOF && (c=getc(in)) != EOF ) {
				if(c == '*') {
					if((c=getc(in)) == '/')
						break;
				}
				if(c == '\n')
					(*line)++;
			}
			if(c == EOF) return 0;
			continue;
		}
		/* not a comment, complete the keyword */
		if(numdone > 0) {
			/* check same type */
			if(isspace((unsigned char)c)) {
				ungetc(c, in);
				return numdone;
			}
			if(is_bind_special(c)) {
				ungetc(c, in);
				return numdone;
			}
		}
		if(c == '\n') {
			c = ' ';
			(*line)++;
		}
		/* space for 1 char + 0 string terminator */
		if(sldns_buffer_remaining(buf) < 2) {
			fatal_exit("trusted-keys, %d, string too long", *line);
		}
		sldns_buffer_write_u8(buf, (uint8_t)c);
		numdone++;
		if(isspace((unsigned char)c)) {
			/* collate whitespace into ' ' */
			while((c = getc(in)) != EOF ) {
				if(c == '\n')
					(*line)++;
				if(!isspace((unsigned char)c)) {
					ungetc(c, in);
					break;
				}
			}
			if(c == EOF) return 0;
			return numdone;
		}
		if(is_bind_special(c))
			return numdone;
	}
	return numdone;
}

/** skip through file to { or ; */
static int 
skip_to_special(FILE* in, sldns_buffer* buf, int* line, int spec) 
{
	int rdlen;
	sldns_buffer_clear(buf);
	while((rdlen=readkeyword_bindfile(in, buf, line, 1))) {
		if(rdlen == 1 && isspace((unsigned char)*sldns_buffer_begin(buf))) {
			sldns_buffer_clear(buf);
			continue;
		}
		if(rdlen != 1 || *sldns_buffer_begin(buf) != (uint8_t)spec) {
			sldns_buffer_write_u8(buf, 0);
			log_err("trusted-keys, line %d, expected %c", 
				*line, spec);
			return 0;
		}
		return 1;
	}
	log_err("trusted-keys, line %d, expected %c got EOF", *line, spec);
	return 0;
}

/** 
 * read contents of trusted-keys{ ... ; clauses and insert keys into storage.
 * @param anchors: where to store keys
 * @param buf: buffer to use
 * @param line: line number in file
 * @param in: file to read from.
 * @return 0 on error.
 */
static int
process_bind_contents(struct val_anchors* anchors, sldns_buffer* buf, 
	int* line, FILE* in)
{
	/* loop over contents, collate strings before ; */
	/* contents is (numbered): 0   1    2  3 4   5  6 7 8    */
	/*                           name. 257 3 5 base64 base64 */
	/* quoted value:           0 "111"  0  0 0   0  0 0 0    */
	/* comments value:         1 "000"  1  1  1 "0  0 0 0"  1 */
	int contnum = 0;
	int quoted = 0;
	int comments = 1;
	int rdlen;
	char* str = 0;
	sldns_buffer_clear(buf);
	while((rdlen=readkeyword_bindfile(in, buf, line, comments))) {
		if(rdlen == 1 && sldns_buffer_position(buf) == 1
			&& isspace((unsigned char)*sldns_buffer_begin(buf))) {
			/* starting whitespace is removed */
			sldns_buffer_clear(buf);
			continue;
		} else if(rdlen == 1 && sldns_buffer_current(buf)[-1] == '"') {
			/* remove " from the string */
			if(contnum == 0) {
				quoted = 1;
				comments = 0;
			}
			sldns_buffer_skip(buf, -1);
			if(contnum > 0 && quoted) {
				if(sldns_buffer_remaining(buf) < 8+1) {
					log_err("line %d, too long", *line);
					return 0;
				}
				sldns_buffer_write(buf, " DNSKEY ", 8);
				quoted = 0;
				comments = 1;
			} else if(contnum > 0)
				comments = !comments;
			continue;
		} else if(rdlen == 1 && sldns_buffer_current(buf)[-1] == ';') {

			if(contnum < 5) {
				sldns_buffer_write_u8(buf, 0);
				log_err("line %d, bad key", *line);
				return 0;
			}
			sldns_buffer_skip(buf, -1);
			sldns_buffer_write_u8(buf, 0);
			str = strdup((char*)sldns_buffer_begin(buf));
			if(!str) {
				log_err("line %d, allocation failure", *line);
				return 0;
			}
			if(!anchor_store_str(anchors, buf, str)) {
				log_err("line %d, bad key", *line);
				free(str);
				return 0;
			}
			free(str);
			sldns_buffer_clear(buf);
			contnum = 0;
			quoted = 0;
			comments = 1;
			continue;
		} else if(rdlen == 1 && sldns_buffer_current(buf)[-1] == '}') {
			if(contnum > 0) {
				sldns_buffer_write_u8(buf, 0);
				log_err("line %d, bad key before }", *line);
				return 0;
			}
			return 1;
		} else if(rdlen == 1 && 
			isspace((unsigned char)sldns_buffer_current(buf)[-1])) {
			/* leave whitespace here */
		} else {
			/* not space or whatnot, so actual content */
			contnum ++;
			if(contnum == 1 && !quoted) {
				if(sldns_buffer_remaining(buf) < 8+1) {
					log_err("line %d, too long", *line);
					return 0;
				}	
				sldns_buffer_write(buf, " DNSKEY ", 8);
			}
		}
	}

	log_err("line %d, EOF before }", *line);
	return 0;
}

/**
 * Read a BIND9 like file with trust anchors in named.conf format.
 * @param anchors: anchor storage.
 * @param buffer: parsing buffer.
 * @param fname: string.
 * @return false on error.
 */
static int
anchor_read_bind_file(struct val_anchors* anchors, sldns_buffer* buffer,
	const char* fname)
{
	int line_nr = 1;
	FILE* in = fopen(fname, "r");
	int rdlen = 0;
	if(!in) {
		log_err("error opening file %s: %s", fname, strerror(errno));
		return 0;
	}
	verbose(VERB_QUERY, "reading in bind-compat-mode: '%s'", fname);
	/* scan for  trusted-keys  keyword, ignore everything else */
	sldns_buffer_clear(buffer);
	while((rdlen=readkeyword_bindfile(in, buffer, &line_nr, 1)) != 0) {
		if(rdlen != 12 || strncmp((char*)sldns_buffer_begin(buffer),
			"trusted-keys", 12) != 0) {
			sldns_buffer_clear(buffer);
			/* ignore everything but trusted-keys */
			continue;
		}
		if(!skip_to_special(in, buffer, &line_nr, '{')) {
			log_err("error in trusted key: \"%s\"", fname);
			fclose(in);
			return 0;
		}
		/* process contents */
		if(!process_bind_contents(anchors, buffer, &line_nr, in)) {
			log_err("error in trusted key: \"%s\"", fname);
			fclose(in);
			return 0;
		}
		if(!skip_to_special(in, buffer, &line_nr, ';')) {
			log_err("error in trusted key: \"%s\"", fname);
			fclose(in);
			return 0;
		}
		sldns_buffer_clear(buffer);
	}
	fclose(in);
	return 1;
}

/**
 * Read a BIND9 like files with trust anchors in named.conf format.
 * Performs wildcard processing of name.
 * @param anchors: anchor storage.
 * @param buffer: parsing buffer.
 * @param pat: pattern string. (can be wildcarded)
 * @return false on error.
 */
static int
anchor_read_bind_file_wild(struct val_anchors* anchors, sldns_buffer* buffer,
	const char* pat)
{
#ifdef HAVE_GLOB
	glob_t g;
	size_t i;
	int r, flags;
	if(!strchr(pat, '*') && !strchr(pat, '?') && !strchr(pat, '[') && 
		!strchr(pat, '{') && !strchr(pat, '~')) {
		return anchor_read_bind_file(anchors, buffer, pat);
	}
	verbose(VERB_QUERY, "wildcard found, processing %s", pat);
	flags = 0 
#ifdef GLOB_ERR
		| GLOB_ERR
#endif
#ifdef GLOB_NOSORT
		| GLOB_NOSORT
#endif
#ifdef GLOB_BRACE
		| GLOB_BRACE
#endif
#ifdef GLOB_TILDE
		| GLOB_TILDE
#endif
	;
	memset(&g, 0, sizeof(g));
	r = glob(pat, flags, NULL, &g);
	if(r) {
		/* some error */
		if(r == GLOB_NOMATCH) {
			verbose(VERB_QUERY, "trusted-keys-file: "
				"no matches for %s", pat);
			return 1;
		} else if(r == GLOB_NOSPACE) {
			log_err("wildcard trusted-keys-file %s: "
				"pattern out of memory", pat);
		} else if(r == GLOB_ABORTED) {
			log_err("wildcard trusted-keys-file %s: expansion "
				"aborted (%s)", pat, strerror(errno));
		} else {
			log_err("wildcard trusted-keys-file %s: expansion "
				"failed (%s)", pat, strerror(errno));
		}
		/* ignore globs that yield no files */
		return 1; 
	}
	/* process files found, if any */
	for(i=0; i<(size_t)g.gl_pathc; i++) {
		if(!anchor_read_bind_file(anchors, buffer, g.gl_pathv[i])) {
			log_err("error reading wildcard "
				"trusted-keys-file: %s", g.gl_pathv[i]);
			globfree(&g);
			return 0;
		}
	}
	globfree(&g);
	return 1;
#else /* not HAVE_GLOB */
	return anchor_read_bind_file(anchors, buffer, pat);
#endif /* HAVE_GLOB */
}

/** 
 * Assemble an rrset structure for the type 
 * @param ta: trust anchor.
 * @param num: number of items to fetch from list.
 * @param type: fetch only items of this type.
 * @return rrset or NULL on error.
 */
static struct ub_packed_rrset_key*
assemble_it(struct trust_anchor* ta, size_t num, uint16_t type)
{
	struct ub_packed_rrset_key* pkey = (struct ub_packed_rrset_key*)
		malloc(sizeof(*pkey));
	struct packed_rrset_data* pd;
	struct ta_key* tk;
	size_t i;
	if(!pkey)
		return NULL;
	memset(pkey, 0, sizeof(*pkey));
	pkey->rk.dname = memdup(ta->name, ta->namelen);
	if(!pkey->rk.dname) {
		free(pkey);
		return NULL;
	}

	pkey->rk.dname_len = ta->namelen;
	pkey->rk.type = htons(type);
	pkey->rk.rrset_class = htons(ta->dclass);
	/* The rrset is build in an uncompressed way. This means it
	 * cannot be copied in the normal way. */
	pd = (struct packed_rrset_data*)malloc(sizeof(*pd));
	if(!pd) {
		free(pkey->rk.dname);
		free(pkey);
		return NULL;
	}
	memset(pd, 0, sizeof(*pd));
	pd->count = num;
	pd->trust = rrset_trust_ultimate;
	pd->rr_len = (size_t*)reallocarray(NULL, num, sizeof(size_t));
	if(!pd->rr_len) {
		free(pd);
		free(pkey->rk.dname);
		free(pkey);
		return NULL;
	}
	pd->rr_ttl = (time_t*)reallocarray(NULL, num, sizeof(time_t));
	if(!pd->rr_ttl) {
		free(pd->rr_len);
		free(pd);
		free(pkey->rk.dname);
		free(pkey);
		return NULL;
	}
	pd->rr_data = (uint8_t**)reallocarray(NULL, num, sizeof(uint8_t*));
	if(!pd->rr_data) {
		free(pd->rr_ttl);
		free(pd->rr_len);
		free(pd);
		free(pkey->rk.dname);
		free(pkey);
		return NULL;
	}
	/* fill in rrs */
	i=0;
	for(tk = ta->keylist; tk; tk = tk->next) {
		if(tk->type != type)
			continue;
		pd->rr_len[i] = tk->len;
		/* reuse data ptr to allocation in talist */
		pd->rr_data[i] = tk->data;
		pd->rr_ttl[i] = 0;
		i++;
	}
	pkey->entry.data = (void*)pd;
	return pkey;
}

/**
 * Assemble structures for the trust DS and DNSKEY rrsets.
 * @param ta: trust anchor
 * @return: false on error.
 */
static int
anchors_assemble(struct trust_anchor* ta)
{
	if(ta->numDS > 0) {
		ta->ds_rrset = assemble_it(ta, ta->numDS, LDNS_RR_TYPE_DS);
		if(!ta->ds_rrset)
			return 0;
	}
	if(ta->numDNSKEY > 0) {
		ta->dnskey_rrset = assemble_it(ta, ta->numDNSKEY,
			LDNS_RR_TYPE_DNSKEY);
		if(!ta->dnskey_rrset)
			return 0;
	}
	return 1;
}

/**
 * Check DS algos for support, warn if not.
 * @param ta: trust anchor
 * @return number of DS anchors with unsupported algorithms.
 */
static size_t
anchors_ds_unsupported(struct trust_anchor* ta)
{
	size_t i, num = 0;
	for(i=0; i<ta->numDS; i++) {
		if(!ds_digest_algo_is_supported(ta->ds_rrset, i) || 
			!ds_key_algo_is_supported(ta->ds_rrset, i))
			num++;
	}
	return num;
}

/**
 * Check DNSKEY algos for support, warn if not.
 * @param ta: trust anchor
 * @return number of DNSKEY anchors with unsupported algorithms.
 */
static size_t
anchors_dnskey_unsupported(struct trust_anchor* ta)
{
	size_t i, num = 0;
	for(i=0; i<ta->numDNSKEY; i++) {
		if(!dnskey_algo_is_supported(ta->dnskey_rrset, i) ||
			!dnskey_size_is_supported(ta->dnskey_rrset, i))
			num++;
	}
	return num;
}

/**
 * Assemble the rrsets in the anchors, ready for use by validator.
 * @param anchors: trust anchor storage.
 * @return: false on error.
 */
static int
anchors_assemble_rrsets(struct val_anchors* anchors)
{
	struct trust_anchor* ta;
	struct trust_anchor* next;
	size_t nods, nokey;
	lock_basic_lock(&anchors->lock);
	ta=(struct trust_anchor*)rbtree_first(anchors->tree);
	while((rbnode_type*)ta != RBTREE_NULL) {
		next = (struct trust_anchor*)rbtree_next(&ta->node);
		lock_basic_lock(&ta->lock);
		if(ta->autr || (ta->numDS == 0 && ta->numDNSKEY == 0)) {
			lock_basic_unlock(&ta->lock);
			ta = next; /* skip */
			continue;
		}
		if(!anchors_assemble(ta)) {
			log_err("out of memory");
			lock_basic_unlock(&ta->lock);
			lock_basic_unlock(&anchors->lock);
			return 0;
		}
		nods = anchors_ds_unsupported(ta);
		nokey = anchors_dnskey_unsupported(ta);
		if(nods) {
			log_nametypeclass(NO_VERBOSE, "warning: unsupported "
				"algorithm for trust anchor", 
				ta->name, LDNS_RR_TYPE_DS, ta->dclass);
		}
		if(nokey) {
			log_nametypeclass(NO_VERBOSE, "warning: unsupported "
				"algorithm for trust anchor", 
				ta->name, LDNS_RR_TYPE_DNSKEY, ta->dclass);
		}
		if(nods == ta->numDS && nokey == ta->numDNSKEY) {
			char b[LDNS_MAX_DOMAINLEN];
			dname_str(ta->name, b);
			log_warn("trust anchor %s has no supported algorithms,"
				" the anchor is ignored (check if you need to"
				" upgrade unbound and "
#ifdef HAVE_LIBRESSL
				"libressl"
#else
				"openssl"
#endif
				")", b);
			(void)rbtree_delete(anchors->tree, &ta->node);
			lock_basic_unlock(&ta->lock);
			anchors_delfunc(&ta->node, NULL);
			ta = next;
			continue;
		}
		lock_basic_unlock(&ta->lock);
		ta = next;
	}
	lock_basic_unlock(&anchors->lock);
	return 1;
}

int 
anchors_apply_cfg(struct val_anchors* anchors, struct config_file* cfg)
{
	struct config_strlist* f;
	const char** zstr;
	char* nm;
	sldns_buffer* parsebuf = sldns_buffer_new(65535);
	if(!parsebuf) {
		log_err("malloc error in anchors_apply_cfg.");
		return 0;
	}
	if(cfg->insecure_lan_zones) {
		for(zstr = as112_zones; *zstr; zstr++) {
			if(!anchor_insert_insecure(anchors, *zstr)) {
				log_err("error in insecure-lan-zones: %s", *zstr);
				sldns_buffer_free(parsebuf);
				return 0;
			}
		}
	}
	for(f = cfg->domain_insecure; f; f = f->next) {
		if(!f->str || f->str[0] == 0) /* empty "" */
			continue;
		if(!anchor_insert_insecure(anchors, f->str)) {
			log_err("error in domain-insecure: %s", f->str);
			sldns_buffer_free(parsebuf);
			return 0;
		}
	}
	for(f = cfg->trust_anchor_file_list; f; f = f->next) {
		if(!f->str || f->str[0] == 0) /* empty "" */
			continue;
		nm = f->str;
		if(cfg->chrootdir && cfg->chrootdir[0] && strncmp(nm,
			cfg->chrootdir, strlen(cfg->chrootdir)) == 0)
			nm += strlen(cfg->chrootdir);
		if(!anchor_read_file(anchors, parsebuf, nm, 0)) {
			log_err("error reading trust-anchor-file: %s", f->str);
			sldns_buffer_free(parsebuf);
			return 0;
		}
	}
	for(f = cfg->trusted_keys_file_list; f; f = f->next) {
		if(!f->str || f->str[0] == 0) /* empty "" */
			continue;
		nm = f->str;
		if(cfg->chrootdir && cfg->chrootdir[0] && strncmp(nm,
			cfg->chrootdir, strlen(cfg->chrootdir)) == 0)
			nm += strlen(cfg->chrootdir);
		if(!anchor_read_bind_file_wild(anchors, parsebuf, nm)) {
			log_err("error reading trusted-keys-file: %s", f->str);
			sldns_buffer_free(parsebuf);
			return 0;
		}
	}
	for(f = cfg->trust_anchor_list; f; f = f->next) {
		if(!f->str || f->str[0] == 0) /* empty "" */
			continue;
		if(!anchor_store_str(anchors, parsebuf, f->str)) {
			log_err("error in trust-anchor: \"%s\"", f->str);
			sldns_buffer_free(parsebuf);
			return 0;
		}
	}
	/* do autr last, so that it sees what anchors are filled by other
	 * means can can print errors about double config for the name */
	for(f = cfg->auto_trust_anchor_file_list; f; f = f->next) {
		if(!f->str || f->str[0] == 0) /* empty "" */
			continue;
		nm = f->str;
		if(cfg->chrootdir && cfg->chrootdir[0] && strncmp(nm,
			cfg->chrootdir, strlen(cfg->chrootdir)) == 0)
			nm += strlen(cfg->chrootdir);
		if(!autr_read_file(anchors, nm)) {
			log_err("error reading auto-trust-anchor-file: %s", 
				f->str);
			sldns_buffer_free(parsebuf);
			return 0;
		}
	}
	/* first assemble, since it may delete useless anchors */
	anchors_assemble_rrsets(anchors);
	init_parents(anchors);
	sldns_buffer_free(parsebuf);
	if(verbosity >= VERB_ALGO) autr_debug_print(anchors);
	return 1;
}

struct trust_anchor* 
anchors_lookup(struct val_anchors* anchors,
        uint8_t* qname, size_t qname_len, uint16_t qclass)
{
	struct trust_anchor key;
	struct trust_anchor* result;
	rbnode_type* res = NULL;
	key.node.key = &key;
	key.name = qname;
	key.namelabs = dname_count_labels(qname);
	key.namelen = qname_len;
	key.dclass = qclass;
	lock_basic_lock(&anchors->lock);
	if(rbtree_find_less_equal(anchors->tree, &key, &res)) {
		/* exact */
		result = (struct trust_anchor*)res;
	} else {
		/* smaller element (or no element) */
		int m;
		result = (struct trust_anchor*)res;
		if(!result || result->dclass != qclass) {
			lock_basic_unlock(&anchors->lock);
			return NULL;
		}
		/* count number of labels matched */
		(void)dname_lab_cmp(result->name, result->namelabs, key.name,
			key.namelabs, &m);
		while(result) { /* go up until qname is subdomain of stub */
			if(result->namelabs <= m)
				break;
			result = result->parent;
		}
	}
	if(result) {
		lock_basic_lock(&result->lock);
	}
	lock_basic_unlock(&anchors->lock);
	return result;
}

/** Get memory usage of assembled key rrset */
static size_t
assembled_rrset_get_mem(struct ub_packed_rrset_key* pkey)
{
	size_t s;
	if(!pkey)
		return 0;
	s = sizeof(*pkey) + pkey->rk.dname_len;
	if(pkey->entry.data) {
		struct packed_rrset_data* pd = (struct packed_rrset_data*)
			pkey->entry.data;
		s += sizeof(*pd) + pd->count * (sizeof(size_t)+sizeof(time_t)+
			sizeof(uint8_t*));
	}
	return s;
}

size_t 
anchors_get_mem(struct val_anchors* anchors)
{
	struct trust_anchor *ta;
	struct ta_key *k;
	size_t s;
	if(!anchors) return 0;
	s = sizeof(*anchors);
	lock_basic_lock(&anchors->lock);
	RBTREE_FOR(ta, struct trust_anchor*, anchors->tree) {
		lock_basic_lock(&ta->lock);
		s += sizeof(*ta) + ta->namelen;
		/* keys and so on */
		for(k = ta->keylist; k; k = k->next) {
			s += sizeof(*k) + k->len;
		}
		s += assembled_rrset_get_mem(ta->ds_rrset);
		s += assembled_rrset_get_mem(ta->dnskey_rrset);
		if(ta->autr) {
			struct autr_ta* p;
			s += sizeof(*ta->autr);
			if(ta->autr->file)
				s += strlen(ta->autr->file);
			for(p = ta->autr->keys; p; p=p->next) {
				s += sizeof(*p) + p->rr_len;
			}
		}
		lock_basic_unlock(&ta->lock);
	}
	lock_basic_unlock(&anchors->lock);
	return s;
}

int
anchors_add_insecure(struct val_anchors* anchors, uint16_t c, uint8_t* nm)
{
	struct trust_anchor key;
	key.node.key = &key;
	key.name = nm;
	key.namelabs = dname_count_size_labels(nm, &key.namelen);
	key.dclass = c;
	lock_basic_lock(&anchors->lock);
	if(rbtree_search(anchors->tree, &key)) {
		lock_basic_unlock(&anchors->lock);
		/* nothing to do, already an anchor or insecure point */
		return 1;
	}
	if(!anchor_new_ta(anchors, nm, key.namelabs, key.namelen, c, 0)) {
		log_err("out of memory");
		lock_basic_unlock(&anchors->lock);
		return 0;
	}
	/* no other contents in new ta, because it is insecure point */
	anchors_init_parents_locked(anchors);
	lock_basic_unlock(&anchors->lock);
	return 1;
}

void
anchors_delete_insecure(struct val_anchors* anchors, uint16_t c,
        uint8_t* nm)
{
	struct trust_anchor key;
	struct trust_anchor* ta;
	key.node.key = &key;
	key.name = nm;
	key.namelabs = dname_count_size_labels(nm, &key.namelen);
	key.dclass = c;
	lock_basic_lock(&anchors->lock);
	if(!(ta=(struct trust_anchor*)rbtree_search(anchors->tree, &key))) {
		lock_basic_unlock(&anchors->lock);
		/* nothing there */
		return;
	}
	/* lock it to drive away other threads that use it */
	lock_basic_lock(&ta->lock);
	/* see if its really an insecure point */
	if(ta->keylist || ta->autr || ta->numDS || ta->numDNSKEY) {
		lock_basic_unlock(&anchors->lock);
		lock_basic_unlock(&ta->lock);
		/* its not an insecure point, do not remove it */
		return;
	}

	/* remove from tree */
	(void)rbtree_delete(anchors->tree, &ta->node);
	anchors_init_parents_locked(anchors);
	lock_basic_unlock(&anchors->lock);

	/* actual free of data */
	lock_basic_unlock(&ta->lock);
	anchors_delfunc(&ta->node, NULL);
}

/** compare two keytags, return -1, 0 or 1 */
static int
keytag_compare(const void* x, const void* y)
{
	if(*(uint16_t*)x == *(uint16_t*)y)
		return 0;
	if(*(uint16_t*)x > *(uint16_t*)y)
		return 1;
	return -1;
}

size_t
anchor_list_keytags(struct trust_anchor* ta, uint16_t* list, size_t num)
{
	size_t i, ret = 0;
	if(ta->numDS == 0 && ta->numDNSKEY == 0)
		return 0; /* insecure point */
	if(ta->numDS != 0 && ta->ds_rrset) {
		struct packed_rrset_data* d=(struct packed_rrset_data*)
			ta->ds_rrset->entry.data;
		for(i=0; i<d->count; i++) {
			if(ret == num) continue;
			list[ret++] = ds_get_keytag(ta->ds_rrset, i);
		}
	}
	if(ta->numDNSKEY != 0 && ta->dnskey_rrset) {
		struct packed_rrset_data* d=(struct packed_rrset_data*)
			ta->dnskey_rrset->entry.data;
		for(i=0; i<d->count; i++) {
			if(ret == num) continue;
			list[ret++] = dnskey_calc_keytag(ta->dnskey_rrset, i);
		}
	}
	qsort(list, ret, sizeof(*list), keytag_compare);
	return ret;
}

int
anchor_has_keytag(struct val_anchors* anchors, uint8_t* name, int namelabs,
	size_t namelen, uint16_t dclass, uint16_t keytag)
{
	uint16_t* taglist;
	uint16_t* tl;
	size_t numtag, i;
	struct trust_anchor* anchor = anchor_find(anchors,
		name, namelabs, namelen, dclass);
	if(!anchor)
		return 0;
	if(!anchor->numDS && !anchor->numDNSKEY) {
		lock_basic_unlock(&anchor->lock);
		return 0;
	}

	taglist = calloc(anchor->numDS + anchor->numDNSKEY, sizeof(*taglist));
	if(!taglist) {
		lock_basic_unlock(&anchor->lock);
		return 0;
	}

	numtag = anchor_list_keytags(anchor, taglist,
		anchor->numDS+anchor->numDNSKEY);
	lock_basic_unlock(&anchor->lock);
	if(!numtag) {
		free(taglist);
		return 0;
	}
	tl = taglist;
	for(i=0; i<numtag; i++) {
		if(*tl == keytag) {
			free(taglist);
			return 1;
		}
		tl++;
	}
	free(taglist);
	return 0;
}

struct trust_anchor*
anchors_find_any_noninsecure(struct val_anchors* anchors)
{
	struct trust_anchor* ta, *next;
	lock_basic_lock(&anchors->lock);
	ta=(struct trust_anchor*)rbtree_first(anchors->tree);
	while((rbnode_type*)ta != RBTREE_NULL) {
		next = (struct trust_anchor*)rbtree_next(&ta->node);
		lock_basic_lock(&ta->lock);
		if(ta->numDS != 0 || ta->numDNSKEY != 0) {
			/* not an insecurepoint */
			lock_basic_unlock(&anchors->lock);
			return ta;
		}
		lock_basic_unlock(&ta->lock);
		ta = next;
	}
	lock_basic_unlock(&anchors->lock);
	return NULL;
}

void
anchors_swap_tree(struct val_anchors* anchors, struct val_anchors* data)
{
	rbtree_type* oldtree;
	rbtree_type oldprobe;

	if(!anchors || !data)
		return; /* If anchors is NULL, there is no validation. */

	oldtree = anchors->tree;
	oldprobe = anchors->autr->probe;

	anchors->tree = data->tree;
	anchors->autr->probe = data->autr->probe;

	data->tree = oldtree;
	data->autr->probe = oldprobe;
}
