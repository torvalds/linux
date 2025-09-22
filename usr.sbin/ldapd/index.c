/*	$OpenBSD: index.c,v 1.13 2019/10/24 12:39:26 tb Exp $ */

/*
 * Copyright (c) 2009 Martin Hedenfalk <martin@bzero.se>
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

/* Indices are stored as unique keys in a btree. No data is stored.
 * The keys are made up of the attribute being indexed, concatenated
 * with the distinguished name of the entry. The namespace suffix is
 * stripped, however.
 *
 * Below, the namespace suffix dc=example,dc=com is stripped.
 *
 * Index b-tree sorts with plain strcmp:
 * ...
 * cn=Chunky Bacon,cn=chunky bacon,ou=people,
 * cn=Chunky Bacon,uid=cbacon,ou=accounts,
 * cn=Chunky Beans,cn=chunky beans,ou=people,
 * cn=Chunky Beans,uid=cbeans,ou=accounts,
 * cn=Crispy Bacon,cn=crispy bacon,ou=people,
 * ...
 * sn=Bacon,cn=chunky bacon,ou=people,
 * sn=Bacon,cn=crispy bacon,ou=people,
 * sn=Beans,cn=chunky beans,ou=people,
 * ...
 * This index can be used for equality, prefix and range searches.
 *
 * If an indexed attribute sorts numerically (integer), we might be able
 * to just pad it with zeros... otherwise this needs to be refined.
 *
 * Multiple attributes can be indexed in the same database.
 *
 * Presence index can be stored as:
 * !mail,cn=chunky bacon,ou=people,
 * !mail,cn=chunky beans,ou=people,
 * !mail,cn=crispy bacon,ou=people,
 *
 * Substring index could probably be stored like a suffix tree:
 * sn>Bacon,cn=chunky bacon,ou=people,
 * sn>acon,cn=chunky bacon,ou=people,
 * sn>con,cn=chunky bacon,ou=people,
 * sn>on,cn=chunky bacon,ou=people,
 * sn>n,cn=chunky bacon,ou=people,
 *
 * This facilitates both substring and suffix search.
 *
 * Approximate index:
 * sn~[soundex(bacon)],cn=chunky bacon,ou=people,
 *
 * One level searches can be indexed as follows:
 * @<parent-rdn>,<rdn>
 * example:
 * @ou=accounts,uid=cbacon
 * @ou=accounts,uid=cbeans
 * @ou=people,cn=chunky bacon
 * @ou=people,cn=chunky beans
 * @ou=people,cn=crispy bacon
 *
 */

#include <sys/types.h>
#include <sys/queue.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "ldapd.h"
#include "log.h"

static int
index_attribute(struct namespace *ns, char *attr, struct btval *dn,
    struct ber_element *a)
{
	int			 dnsz, rc;
	char			*s, *t;
	struct ber_element	*v;
	struct btval		 key, val;

	assert(ns);
	assert(ns->indx_txn);
	assert(attr);
	assert(dn);
	assert(a);
	assert(a->be_next);
	memset(&val, 0, sizeof(val));

	log_debug("indexing %.*s on %s", (int)dn->size, (char *)dn->data, attr);

	dnsz = dn->size - strlen(ns->suffix);

	for (v = a->be_next->be_sub; v; v = v->be_next) {
		if (ober_get_string(v, &s) != 0)
			continue;
		memset(&key, 0, sizeof(key));
		key.size = asprintf(&t, "%s=%s,%.*s", attr, s, dnsz,
		    (char *)dn->data);
		if (key.size == (size_t)-1)
			return -1;
		key.data = t;
		normalize_dn(key.data);
		rc = btree_txn_put(NULL, ns->indx_txn, &key, &val,
		    BT_NOOVERWRITE);
		free(t);
		if (rc == -1 && errno != EEXIST)
			return -1;
	}

	return 0;
}

static int
index_rdn_key(struct namespace *ns, struct btval *dn, struct btval *key)
{
	int		 dnsz, rdnsz, pdnsz;
	char		*t, *parent_dn;

	memset(key, 0, sizeof(*key));

	dnsz = dn->size - strlen(ns->suffix);
	if (dnsz-- == 0)
		return -1;

	parent_dn = memchr(dn->data, ',', dnsz);
	if (parent_dn == NULL) {
		rdnsz = dnsz;
		pdnsz = 0;
		parent_dn = "";
	} else {
		rdnsz = parent_dn - (char *)dn->data;
		pdnsz = dnsz - rdnsz - 1;
		++parent_dn;
	}

	if (asprintf(&t, "@%.*s,%.*s", pdnsz, parent_dn, rdnsz,
	    (char *)dn->data) == -1)
		return -1;

	normalize_dn(t);
	key->data = t;
	key->size = strlen(t);
	key->free_data = 1;

	return 0;
}

static int
index_rdn(struct namespace *ns, struct btval *dn)
{
	struct btval	 key, val;
	int		 rc;

	memset(&val, 0, sizeof(val));

	assert(ns);
	assert(ns->indx_txn);
	assert(dn);

	if (index_rdn_key(ns, dn, &key) < 0)
		return 0;

	log_debug("indexing rdn on %.*s", (int)key.size, (char *)key.data);
	rc = btree_txn_put(NULL, ns->indx_txn, &key, &val, BT_NOOVERWRITE);
	btval_reset(&key);
	if (rc == -1 && errno != EEXIST)
		return -1;
	return 0;
}

static int
unindex_attribute(struct namespace *ns, char *attr, struct btval *dn,
    struct ber_element *a)
{
	int			 dnsz, rc;
	char			*s, *t;
	struct ber_element	*v;
	struct btval		 key;

	assert(ns);
	assert(ns->indx_txn);
	assert(attr);
	assert(dn);
	assert(a);
	assert(a->be_next);

	log_debug("unindexing %.*s on %s",
	    (int)dn->size, (char *)dn->data, attr);

	dnsz = dn->size - strlen(ns->suffix);

	for (v = a->be_next->be_sub; v; v = v->be_next) {
		if (ober_get_string(v, &s) != 0)
			continue;
		memset(&key, 0, sizeof(key));
		key.size = asprintf(&t, "%s=%s,%.*s", attr, s, dnsz,
		    (char *)dn->data);
		key.data = t;
		normalize_dn(key.data);
		rc = btree_txn_del(NULL, ns->indx_txn, &key, NULL);
		free(t);
		if (rc == BT_FAIL && errno != ENOENT)
			return -1;
	}

	return 0;
}

int
index_entry(struct namespace *ns, struct btval *dn, struct ber_element *elm)
{
	struct ber_element	*a;
	struct attr_index	*ai;

	assert(ns);
	assert(dn);
	assert(elm);
	TAILQ_FOREACH(ai, &ns->indices, next) {
		a = ldap_get_attribute(elm, ai->attr);
		if (a && index_attribute(ns, ai->attr, dn, a) < 0)
			return -1;
	}

	return index_rdn(ns, dn);
}

static int
unindex_rdn(struct namespace *ns, struct btval *dn)
{
	int		 rc;
	struct btval	 key;

	assert(ns);
	assert(ns->indx_txn);
	assert(dn);

	if (index_rdn_key(ns, dn, &key) < 0)
		return 0;

	log_debug("unindexing rdn on %.*s", (int)key.size, (char *)key.data);
	rc = btree_txn_del(NULL, ns->indx_txn, &key, NULL);
	btval_reset(&key);
	if (rc == BT_FAIL && errno != ENOENT)
		return -1;
	return 0;
}

int
unindex_entry(struct namespace *ns, struct btval *dn, struct ber_element *elm)
{
	struct ber_element	*a;
	struct attr_index	*ai;

	assert(ns);
	assert(dn);
	assert(elm);
	TAILQ_FOREACH(ai, &ns->indices, next) {
		a = ldap_get_attribute(elm, ai->attr);
		if (a && unindex_attribute(ns, ai->attr, dn, a) < 0)
			return -1;
	}

	return unindex_rdn(ns, dn);
}

/* Reconstruct the full dn from the index key and the namespace suffix.
 *
 * Examples:
 *
 * index key:
 *   sn=Bacon,cn=chunky bacon,ou=people,
 * full dn:
 *   cn=chunky bacon,ou=people,dc=example,dc=com
 *
 * index key:
 *   @ou=people,cn=chunky bacon
 * full dn:
 *   cn=chunky bacon,ou=people,dc=example,dc=com
 *
 * index key:
 *   @,ou=people
 * full dn:
 *   ou=people,dc=example,dc=com
 *
 * index key:
 *   @ou=staff,ou=people,cn=chunky bacon
 * full dn:
 *   cn=chunky bacon,ou=staff,ou=people,dc=example,dc=com
 *
 * Filled in dn must be freed with btval_reset().
 */
int
index_to_dn(struct namespace *ns, struct btval *indx, struct btval *dn)
{
	char		*rdn, *parent_rdn, indxtype, *dst;
	int		 rdn_sz, prdn_sz;

	/* Skip past first index part to get the RDN.
	 */

	indxtype = ((char *)indx->data)[0];
	if (indxtype == '@') {
		/* One-level index.
		 * Full DN is made up of rdn + parent rdn + namespace suffix.
		 */
		rdn = memrchr(indx->data, ',', indx->size);
		if (rdn++ == NULL)
			return -1;
		rdn_sz = indx->size - (rdn - (char *)indx->data);

		parent_rdn = (char *)indx->data + 1;
		prdn_sz = rdn - parent_rdn - 1;
		dn->size = indx->size + strlen(ns->suffix);
		if (prdn_sz == 0)
			dn->size--;
		if ((dn->data = malloc(dn->size)) == NULL) {
			log_warn("conn_search: malloc");
			return -1;
		}
		dst = dn->data;
		bcopy(rdn, dst, rdn_sz);
		dst += rdn_sz;
		*dst++ = ',';
		bcopy(parent_rdn, dst, prdn_sz);
		dst += prdn_sz;
		if (prdn_sz > 0)
			*dst++ = ',';
		bcopy(ns->suffix, dst, strlen(ns->suffix));
	} else {
		/* Construct the full DN by appending namespace suffix.
		 */
		rdn = memchr(indx->data, ',', indx->size);
		if (rdn++ == NULL)
			return -1;
		rdn_sz = indx->size - (rdn - (char *)indx->data);
		dn->size = rdn_sz + strlen(ns->suffix);
		if ((dn->data = malloc(dn->size)) == NULL) {
			log_warn("index_to_dn: malloc");
			return -1;
		}
		bcopy(rdn, dn->data, rdn_sz);
		bcopy(ns->suffix, (char *)dn->data + rdn_sz,
		    strlen(ns->suffix));
	}

	dn->free_data = 1;

	return 0;
}

