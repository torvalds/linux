/*	$OpenBSD: x509_policy.c,v 1.33 2025/08/10 06:36:45 beck Exp $ */
/*
 * Copyright (c) 2022, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <string.h>

#include <openssl/objects.h>
#include <openssl/stack.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "stack_local.h"
#include "x509_internal.h"
#include "x509_local.h"

/*
 * This file computes the X.509 policy tree, as described in RFC 5280,
 * section 6.1 and RFC 9618. It differs in that:
 *
 *  (1) It does not track "qualifier_set". This is not needed as it is not
 *      output by this implementation.
 *
 *  (2) It builds a directed acyclic graph, rather than a tree. When a given
 *      policy matches multiple parents, RFC 5280 makes a separate node for
 *      each parent. This representation condenses them into one node with
 *      multiple parents. Thus we refer to this structure as a "policy graph",
 *      rather than a "policy tree".
 *
 *  (3) "expected_policy_set" is not tracked explicitly and built temporarily
 *      as part of building the graph.
 *
 *  (4) anyPolicy nodes are not tracked explicitly.
 *
 *  (5) Some pruning steps are deferred to when policies are evaluated, as a
 *      reachability pass.
 */

/*
 * An X509_POLICY_NODE is a node in the policy graph. It corresponds to a node
 * from RFC 5280, section 6.1.2, step (a), but we store some fields differently.
 */
typedef struct x509_policy_node_st {
	/* policy is the "valid_policy" field from RFC 5280. */
	ASN1_OBJECT *policy;

	/*
	 * parent_policies, if non-empty, is the list of "valid_policy" values
	 * for all nodes which are a parent of this node. In this case, no entry
	 * in this list will be anyPolicy. This list is in no particular order
	 * and may contain duplicates if the corresponding certificate had
	 * duplicate mappings.
	 *
	 * If empty, this node has a single parent, anyPolicy. The node is then
	 * a root policies, and is in authorities-constrained-policy-set if it
	 * has a path to a leaf node.
	 *
	 * Note it is not possible for a policy to have both anyPolicy and a
	 * concrete policy as a parent. Section 6.1.3, step (d.1.ii) only runs
	 * if there was no match in step (d.1.i). We do not need to represent a
	 * parent list of, say, {anyPolicy, OID1, OID2}.
	 */
	STACK_OF(ASN1_OBJECT) *parent_policies;

	/*
	 * mapped is one if this node matches a policy mapping in the
	 * certificate and zero otherwise.
	 */
	int mapped;

	/*
	 * reachable is one if this node is reachable from some valid policy in
	 * the end-entity certificate. It is computed during |has_explicit_policy|.
	 */
	int reachable;
} X509_POLICY_NODE;

DECLARE_STACK_OF(X509_POLICY_NODE)

#define sk_X509_POLICY_NODE_new(cmp) SKM_sk_new(X509_POLICY_NODE, (cmp))
#define sk_X509_POLICY_NODE_new_null() SKM_sk_new_null(X509_POLICY_NODE)
#define sk_X509_POLICY_NODE_free(st) SKM_sk_free(X509_POLICY_NODE, (st))
#define sk_X509_POLICY_NODE_num(st) SKM_sk_num(X509_POLICY_NODE, (st))
#define sk_X509_POLICY_NODE_value(st, i) SKM_sk_value(X509_POLICY_NODE, (st), (i))
#define sk_X509_POLICY_NODE_set(st, i, val) SKM_sk_set(X509_POLICY_NODE, (st), (i), (val))
#define sk_X509_POLICY_NODE_zero(st) SKM_sk_zero(X509_POLICY_NODE, (st))
#define sk_X509_POLICY_NODE_push(st, val) SKM_sk_push(X509_POLICY_NODE, (st), (val))
#define sk_X509_POLICY_NODE_unshift(st, val) SKM_sk_unshift(X509_POLICY_NODE, (st), (val))
#define sk_X509_POLICY_NODE_find(st, val) SKM_sk_find(X509_POLICY_NODE, (st), (val))
#define sk_X509_POLICY_NODE_delete(st, i) SKM_sk_delete(X509_POLICY_NODE, (st), (i))
#define sk_X509_POLICY_NODE_delete_ptr(st, ptr) SKM_sk_delete_ptr(X509_POLICY_NODE, (st), (ptr))
#define sk_X509_POLICY_NODE_insert(st, val, i) SKM_sk_insert(X509_POLICY_NODE, (st), (val), (i))
#define sk_X509_POLICY_NODE_set_cmp_func(st, cmp) SKM_sk_set_cmp_func(X509_POLICY_NODE, (st), (cmp))
#define sk_X509_POLICY_NODE_dup(st) SKM_sk_dup(X509_POLICY_NODE, st)
#define sk_X509_POLICY_NODE_pop_free(st, free_func) SKM_sk_pop_free(X509_POLICY_NODE, (st), (free_func))
#define sk_X509_POLICY_NODE_shift(st) SKM_sk_shift(X509_POLICY_NODE, (st))
#define sk_X509_POLICY_NODE_pop(st) SKM_sk_pop(X509_POLICY_NODE, (st))
#define sk_X509_POLICY_NODE_sort(st) SKM_sk_sort(X509_POLICY_NODE, (st))
#define sk_X509_POLICY_NODE_is_sorted(st) SKM_sk_is_sorted(X509_POLICY_NODE, (st))

/*
 * An X509_POLICY_LEVEL is the collection of nodes at the same depth in the
 * policy graph. This structure can also be used to represent a level's
 * "expected_policy_set" values. See |process_policy_mappings|.
 */
typedef struct x509_policy_level_st {
	/*
	 * nodes is the list of nodes at this depth, except for the anyPolicy
	 * node, if any. This list is sorted by policy OID for efficient lookup.
	 */
	STACK_OF(X509_POLICY_NODE) *nodes;

	/*
	 * has_any_policy is one if there is an anyPolicy node at this depth,
	 * and zero otherwise.
	 */
	int has_any_policy;
} X509_POLICY_LEVEL;

DECLARE_STACK_OF(X509_POLICY_LEVEL)

#define sk_X509_POLICY_LEVEL_new(cmp) SKM_sk_new(X509_POLICY_LEVEL, (cmp))
#define sk_X509_POLICY_LEVEL_new_null() SKM_sk_new_null(X509_POLICY_LEVEL)
#define sk_X509_POLICY_LEVEL_free(st) SKM_sk_free(X509_POLICY_LEVEL, (st))
#define sk_X509_POLICY_LEVEL_num(st) SKM_sk_num(X509_POLICY_LEVEL, (st))
#define sk_X509_POLICY_LEVEL_value(st, i) SKM_sk_value(X509_POLICY_LEVEL, (st), (i))
#define sk_X509_POLICY_LEVEL_set(st, i, val) SKM_sk_set(X509_POLICY_LEVEL, (st), (i), (val))
#define sk_X509_POLICY_LEVEL_zero(st) SKM_sk_zero(X509_POLICY_LEVEL, (st))
#define sk_X509_POLICY_LEVEL_push(st, val) SKM_sk_push(X509_POLICY_LEVEL, (st), (val))
#define sk_X509_POLICY_LEVEL_unshift(st, val) SKM_sk_unshift(X509_POLICY_LEVEL, (st), (val))
#define sk_X509_POLICY_LEVEL_find(st, val) SKM_sk_find(X509_POLICY_LEVEL, (st), (val))
#define sk_X509_POLICY_LEVEL_delete(st, i) SKM_sk_delete(X509_POLICY_LEVEL, (st), (i))
#define sk_X509_POLICY_LEVEL_delete_ptr(st, ptr) SKM_sk_delete_ptr(X509_POLICY_LEVEL, (st), (ptr))
#define sk_X509_POLICY_LEVEL_insert(st, val, i) SKM_sk_insert(X509_POLICY_LEVEL, (st), (val), (i))
#define sk_X509_POLICY_LEVEL_set_cmp_func(st, cmp) SKM_sk_set_cmp_func(X509_POLICY_LEVEL, (st), (cmp))
#define sk_X509_POLICY_LEVEL_dup(st) SKM_sk_dup(X509_POLICY_LEVEL, st)
#define sk_X509_POLICY_LEVEL_pop_free(st, free_func) SKM_sk_pop_free(X509_POLICY_LEVEL, (st), (free_func))
#define sk_X509_POLICY_LEVEL_shift(st) SKM_sk_shift(X509_POLICY_LEVEL, (st))
#define sk_X509_POLICY_LEVEL_pop(st) SKM_sk_pop(X509_POLICY_LEVEL, (st))
#define sk_X509_POLICY_LEVEL_sort(st) SKM_sk_sort(X509_POLICY_LEVEL, (st))
#define sk_X509_POLICY_LEVEL_is_sorted(st) SKM_sk_is_sorted(X509_POLICY_LEVEL, (st))

/*
 * Don't look Ethel, but you would really not want to look if we did
 * this the OpenSSL way either, and we are not using this boringsslism
 * anywhere else. Callers should ensure that the stack in data is sorted.
 */
void
sk_X509_POLICY_NODE_delete_if(STACK_OF(X509_POLICY_NODE) *nodes,
    int (*delete_if)(X509_POLICY_NODE *, void *), void *data)
{
	_STACK *sk = (_STACK *)nodes;
	X509_POLICY_NODE *node;
	int new_num = 0;
	int i;

	for (i = 0; i < sk_X509_POLICY_NODE_num(nodes); i++) {
		node = sk_X509_POLICY_NODE_value(nodes, i);
		if (!delete_if(node, data))
			sk->data[new_num++] = (char *)node;
	}
	sk->num = new_num;
}

static int
is_any_policy(const ASN1_OBJECT *obj)
{
	return OBJ_obj2nid(obj) == NID_any_policy;
}

static void
x509_policy_node_free(X509_POLICY_NODE *node)
{
	if (node == NULL)
		return;

	ASN1_OBJECT_free(node->policy);
	sk_ASN1_OBJECT_pop_free(node->parent_policies, ASN1_OBJECT_free);
	free(node);
}

static X509_POLICY_NODE *
x509_policy_node_new(const ASN1_OBJECT *policy)
{
	X509_POLICY_NODE *node = NULL;

	if (is_any_policy(policy))
		goto err;
	if ((node = calloc(1, sizeof(*node))) == NULL)
		goto err;
	if ((node->policy = OBJ_dup(policy)) == NULL)
		goto err;
	if ((node->parent_policies = sk_ASN1_OBJECT_new_null()) == NULL)
		goto err;

	return node;

 err:
	x509_policy_node_free(node);
	return NULL;
}

static int
x509_policy_node_cmp(const X509_POLICY_NODE *const *a,
    const X509_POLICY_NODE *const *b)
{
	return OBJ_cmp((*a)->policy, (*b)->policy);
}

static void
x509_policy_level_free(X509_POLICY_LEVEL *level)
{
	if (level == NULL)
		return;

	sk_X509_POLICY_NODE_pop_free(level->nodes, x509_policy_node_free);
	free(level);
}

static X509_POLICY_LEVEL *
x509_policy_level_new(void)
{
	X509_POLICY_LEVEL *level;

	if ((level = calloc(1, sizeof(*level))) == NULL)
		goto err;
	level->nodes = sk_X509_POLICY_NODE_new(x509_policy_node_cmp);
	if (level->nodes == NULL)
		goto err;

	return level;

 err:
	x509_policy_level_free(level);
	return NULL;
}

static int
x509_policy_level_is_empty(const X509_POLICY_LEVEL *level)
{
	if (level->has_any_policy)
		return 0;

	return sk_X509_POLICY_NODE_num(level->nodes) == 0;
}

static void
x509_policy_level_clear(X509_POLICY_LEVEL *level)
{
	X509_POLICY_NODE *node;
	int i;

	level->has_any_policy = 0;
	for (i = 0; i < sk_X509_POLICY_NODE_num(level->nodes); i++) {
		node = sk_X509_POLICY_NODE_value(level->nodes, i);
		x509_policy_node_free(node);
	}
	sk_X509_POLICY_NODE_zero(level->nodes);
}

/*
 * x509_policy_level_find returns the node in |level| corresponding to |policy|,
 * or NULL if none exists. Callers should ensure that level->nodes is sorted
 * to avoid the cost of sorting it in sk_find().
 */
static X509_POLICY_NODE *
x509_policy_level_find(X509_POLICY_LEVEL *level, const ASN1_OBJECT *policy)
{
	X509_POLICY_NODE node;
	node.policy = (ASN1_OBJECT *)policy;
	int idx;

	if ((idx = sk_X509_POLICY_NODE_find(level->nodes, &node)) < 0)
		return NULL;
	return sk_X509_POLICY_NODE_value(level->nodes, idx);
}

/*
 * x509_policy_level_add_nodes adds the nodes in |nodes| to |level|. It returns
 * one on success and zero on error. No policy in |nodes| may already be present
 * in |level|. This function modifies |nodes| to avoid making a copy, but the
 * caller is still responsible for releasing |nodes| itself.
 *
 * This function is used to add nodes to |level| in bulk, and avoid resorting
 * |level| after each addition.
 */
static int
x509_policy_level_add_nodes(X509_POLICY_LEVEL *level,
    STACK_OF(X509_POLICY_NODE) *nodes)
{
	int i;

	for (i = 0; i < sk_X509_POLICY_NODE_num(nodes); i++) {
		X509_POLICY_NODE *node = sk_X509_POLICY_NODE_value(nodes, i);
		if (!sk_X509_POLICY_NODE_push(level->nodes, node))
			return 0;
		sk_X509_POLICY_NODE_set(nodes, i, NULL);
	}
	sk_X509_POLICY_NODE_sort(level->nodes);

	return 1;
}

static int
policyinfo_cmp(const POLICYINFO *const *a,
    const POLICYINFO *const *b)
{
	return OBJ_cmp((*a)->policyid, (*b)->policyid);
}

static int
delete_if_not_in_policies(X509_POLICY_NODE *node, void *data)
{
	const CERTIFICATEPOLICIES *policies = data;
	POLICYINFO info;
	info.policyid = node->policy;

	if (sk_POLICYINFO_find(policies, &info) >= 0)
		return 0;
	x509_policy_node_free(node);
	return 1;
}

/*
 * process_certificate_policies updates |level| to incorporate |x509|'s
 * certificate policies extension. This implements steps (d) and (e) of RFC
 * 5280, section 6.1.3. |level| must contain the previous level's
 * "expected_policy_set" information. For all but the top-most level, this is
 * the output of |process_policy_mappings|. |any_policy_allowed| specifies
 * whether anyPolicy is allowed or inhibited, taking into account the exception
 * for self-issued certificates.
 */
static int
process_certificate_policies(const X509 *x509, X509_POLICY_LEVEL *level,
    int any_policy_allowed)
{
	STACK_OF(X509_POLICY_NODE) *new_nodes = NULL;
	CERTIFICATEPOLICIES *policies;
	const POLICYINFO *policy;
	X509_POLICY_NODE *node;
	int cert_has_any_policy, critical, i, previous_level_has_any_policy;
	int ret = 0;

	policies = X509_get_ext_d2i(x509, NID_certificate_policies, &critical,
	    NULL);
	if (policies == NULL) {
		if (critical != -1)
			return 0;  /* Syntax error in the extension. */

		/* RFC 5280, section 6.1.3, step (e). */
		x509_policy_level_clear(level);
		return 1;
	}

	/*
	 * certificatePolicies may not be empty. See RFC 5280, section 4.2.1.4.
	 * TODO(https://crbug.com/boringssl/443): Move this check into the parser.
	 */
	if (sk_POLICYINFO_num(policies) == 0) {
		X509error(X509_R_INVALID_POLICY_EXTENSION);
		goto err;
	}

	(void)sk_POLICYINFO_set_cmp_func(policies, policyinfo_cmp);
	sk_POLICYINFO_sort(policies);
	cert_has_any_policy = 0;
	for (i = 0; i < sk_POLICYINFO_num(policies); i++) {
		policy = sk_POLICYINFO_value(policies, i);
		if (is_any_policy(policy->policyid))
			cert_has_any_policy = 1;
		if (i > 0 &&
		    OBJ_cmp(sk_POLICYINFO_value(policies, i - 1)->policyid,
		    policy->policyid) == 0) {
			/*
			 * Per RFC 5280, section 4.2.1.4, |policies| may not
			 * have duplicates.
			 */
			X509error(X509_R_INVALID_POLICY_EXTENSION);
			goto err;
		}
	}

	/*
	 * This does the same thing as RFC 5280, section 6.1.3, step (d),
	 * though in a slightly different order. |level| currently contains
	 * "expected_policy_set" values of the previous level.
	 * See |process_policy_mappings| for details.
	 */
	previous_level_has_any_policy = level->has_any_policy;

	/*
	 * First, we handle steps (d.1.i) and (d.2). The net effect of these
	 * two steps is to intersect |level| with |policies|, ignoring
	 * anyPolicy if it is inhibited.
	 */
	if (!cert_has_any_policy || !any_policy_allowed) {
		if (!sk_POLICYINFO_is_sorted(policies))
			goto err;
		sk_X509_POLICY_NODE_delete_if(level->nodes,
		    delete_if_not_in_policies, policies);
		level->has_any_policy = 0;
	}

	/*
	 * Step (d.1.ii) may attach new nodes to the previous level's anyPolicy
	 * node.
	 */
	if (previous_level_has_any_policy) {
		new_nodes = sk_X509_POLICY_NODE_new_null();
		if (new_nodes == NULL)
			goto err;
		for (i = 0; i < sk_POLICYINFO_num(policies); i++) {
			policy = sk_POLICYINFO_value(policies, i);
			/*
			 * Though we've reordered the steps slightly, |policy|
			 * is in |level| if and only if it would have been a
			 * match in step (d.1.ii).
			 */
			if (is_any_policy(policy->policyid))
				continue;
			if (!sk_X509_POLICY_NODE_is_sorted(level->nodes))
				goto err;
			if (x509_policy_level_find(level, policy->policyid) != NULL)
				continue;
			node = x509_policy_node_new(policy->policyid);
			if (node == NULL ||
			    !sk_X509_POLICY_NODE_push(new_nodes, node)) {
				x509_policy_node_free(node);
				goto err;
			}
		}
		if (!x509_policy_level_add_nodes(level, new_nodes))
			goto err;
	}

	ret = 1;

err:
	sk_X509_POLICY_NODE_pop_free(new_nodes, x509_policy_node_free);
	CERTIFICATEPOLICIES_free(policies);
	return ret;
}

static int
compare_issuer_policy(const POLICY_MAPPING *const *a,
    const POLICY_MAPPING *const *b)
{
	return OBJ_cmp((*a)->issuerDomainPolicy, (*b)->issuerDomainPolicy);
}

static int
compare_subject_policy(const POLICY_MAPPING *const *a,
    const POLICY_MAPPING *const *b)
{
	return OBJ_cmp((*a)->subjectDomainPolicy, (*b)->subjectDomainPolicy);
}

static int
delete_if_mapped(X509_POLICY_NODE *node, void *data)
{
	const POLICY_MAPPINGS *mappings = data;
	POLICY_MAPPING mapping;
	mapping.issuerDomainPolicy = node->policy;
	if (sk_POLICY_MAPPING_find(mappings, &mapping) < 0)
		return 0;
	x509_policy_node_free(node);
	return 1;
}

/*
 * process_policy_mappings processes the policy mappings extension of |cert|,
 * whose corresponding graph level is |level|. |mapping_allowed| specifies
 * whether policy mapping is inhibited at this point. On success, it returns an
 * |X509_POLICY_LEVEL| containing the "expected_policy_set" for |level|. On
 * error, it returns NULL. This implements steps (a) and (b) of RFC 5280,
 * section 6.1.4.
 *
 * We represent the "expected_policy_set" as an |X509_POLICY_LEVEL|.
 * |has_any_policy| indicates whether there is an anyPolicy node with
 * "expected_policy_set" of {anyPolicy}. If a node with policy oid P1 contains
 * P2 in its "expected_policy_set", the level will contain a node of policy P2
 * with P1 in |parent_policies|.
 *
 * This is equivalent to the |X509_POLICY_LEVEL| that would result if the next
 * certificate contained anyPolicy. |process_certificate_policies| will filter
 * this result down to compute the actual level.
 */
static X509_POLICY_LEVEL *
process_policy_mappings(const X509 *cert,
    X509_POLICY_LEVEL *level,
    int mapping_allowed)
{
	STACK_OF(X509_POLICY_NODE) *new_nodes = NULL;
	POLICY_MAPPINGS *mappings;
	const ASN1_OBJECT *last_policy;
	POLICY_MAPPING *mapping;
	X509_POLICY_LEVEL *next = NULL;
	X509_POLICY_NODE *node;
	int critical, i;
	int ok = 0;

	mappings = X509_get_ext_d2i(cert, NID_policy_mappings, &critical, NULL);
	if (mappings == NULL && critical != -1) {
		/* Syntax error in the policy mappings extension. */
		goto err;
	}

	if (mappings != NULL) {
		/*
		 * PolicyMappings may not be empty. See RFC 5280, section 4.2.1.5.
		 * TODO(https://crbug.com/boringssl/443): Move this check into
		 * the parser.
		 */
		if (sk_POLICY_MAPPING_num(mappings) == 0) {
			X509error(X509_R_INVALID_POLICY_EXTENSION);
			goto err;
		}

		/* RFC 5280, section 6.1.4, step (a). */
		for (i = 0; i < sk_POLICY_MAPPING_num(mappings); i++) {
			mapping = sk_POLICY_MAPPING_value(mappings, i);
			if (is_any_policy(mapping->issuerDomainPolicy) ||
			    is_any_policy(mapping->subjectDomainPolicy))
				goto err;
		}

		/* Sort to group by issuerDomainPolicy. */
		(void)sk_POLICY_MAPPING_set_cmp_func(mappings,
		    compare_issuer_policy);
		sk_POLICY_MAPPING_sort(mappings);

		if (mapping_allowed) {
			/*
			 * Mark nodes as mapped, and add any nodes to |level|
			 * which may be needed as part of RFC 5280,
			 * section 6.1.4, step (b.1).
			 */
			new_nodes = sk_X509_POLICY_NODE_new_null();
			if (new_nodes == NULL)
				goto err;
			last_policy = NULL;
			for (i = 0; i < sk_POLICY_MAPPING_num(mappings); i++) {
				mapping = sk_POLICY_MAPPING_value(mappings, i);
				/*
				 * There may be multiple mappings with the same
				 * |issuerDomainPolicy|.
				 */
				if (last_policy != NULL &&
				    OBJ_cmp(mapping->issuerDomainPolicy,
				    last_policy) == 0)
					continue;
				last_policy = mapping->issuerDomainPolicy;

				if (!sk_X509_POLICY_NODE_is_sorted(level->nodes))
					goto err;
				node = x509_policy_level_find(level,
				    mapping->issuerDomainPolicy);
				if (node == NULL) {
					if (!level->has_any_policy)
						continue;
					node = x509_policy_node_new(
					    mapping->issuerDomainPolicy);
					if (node == NULL ||
					    !sk_X509_POLICY_NODE_push(new_nodes,
					    node)) {
						x509_policy_node_free(node);
						goto err;
					}
				}
				node->mapped = 1;
			}
			if (!x509_policy_level_add_nodes(level, new_nodes))
				goto err;
		} else {
			/*
			 * RFC 5280, section 6.1.4, step (b.2). If mapping is
			 * inhibited, delete all mapped nodes.
			 */
			if (!sk_POLICY_MAPPING_is_sorted(mappings))
				goto err;
			sk_X509_POLICY_NODE_delete_if(level->nodes,
			    delete_if_mapped, mappings);
			sk_POLICY_MAPPING_pop_free(mappings,
			    POLICY_MAPPING_free);
			mappings = NULL;
		}
	}

	/*
	 * If a node was not mapped, it retains the original "explicit_policy_set"
	 * value, itself. Add those to |mappings|.
	 */
	if (mappings == NULL) {
		mappings = sk_POLICY_MAPPING_new_null();
		if (mappings == NULL)
			goto err;
	}
	for (i = 0; i < sk_X509_POLICY_NODE_num(level->nodes); i++) {
		node = sk_X509_POLICY_NODE_value(level->nodes, i);
		if (!node->mapped) {
			mapping = POLICY_MAPPING_new();
			if (mapping == NULL)
				goto err;
			mapping->issuerDomainPolicy = OBJ_dup(node->policy);
			mapping->subjectDomainPolicy = OBJ_dup(node->policy);
			if (mapping->issuerDomainPolicy == NULL ||
			    mapping->subjectDomainPolicy == NULL ||
			    !sk_POLICY_MAPPING_push(mappings, mapping)) {
				POLICY_MAPPING_free(mapping);
				goto err;
			}
		}
	}

	/* Sort to group by subjectDomainPolicy. */
	(void)sk_POLICY_MAPPING_set_cmp_func(mappings, compare_subject_policy);
	sk_POLICY_MAPPING_sort(mappings);

	/* Convert |mappings| to our "expected_policy_set" representation. */
	next = x509_policy_level_new();
	if (next == NULL)
		goto err;
	next->has_any_policy = level->has_any_policy;

	X509_POLICY_NODE *last_node = NULL;
	for (i = 0; i < sk_POLICY_MAPPING_num(mappings); i++) {
		mapping = sk_POLICY_MAPPING_value(mappings, i);
		/*
		 * Skip mappings where |issuerDomainPolicy| does not appear in
		 * the graph.
		 */
		if (!level->has_any_policy) {
			if (!sk_X509_POLICY_NODE_is_sorted(level->nodes))
				goto err;
			if (x509_policy_level_find(level,
			    mapping->issuerDomainPolicy) == NULL)
				continue;
		}

		if (last_node == NULL ||
		    OBJ_cmp(last_node->policy, mapping->subjectDomainPolicy) !=
		    0) {
			last_node = x509_policy_node_new(
			    mapping->subjectDomainPolicy);
			if (last_node == NULL ||
			    !sk_X509_POLICY_NODE_push(next->nodes, last_node)) {
				x509_policy_node_free(last_node);
				goto err;
			}
		}

		if (!sk_ASN1_OBJECT_push(last_node->parent_policies,
		    mapping->issuerDomainPolicy))
			goto err;
		mapping->issuerDomainPolicy = NULL;
	}

	sk_X509_POLICY_NODE_sort(next->nodes);
	ok = 1;

err:
	if (!ok) {
		x509_policy_level_free(next);
		next = NULL;
	}

	sk_POLICY_MAPPING_pop_free(mappings, POLICY_MAPPING_free);
	sk_X509_POLICY_NODE_pop_free(new_nodes, x509_policy_node_free);
	return next;
}

/*
 * apply_skip_certs, if |skip_certs| is non-NULL, sets |*value| to the minimum
 * of its current value and |skip_certs|. It returns one on success and zero if
 * |skip_certs| is negative.
 */
static int
apply_skip_certs(const ASN1_INTEGER *skip_certs, size_t *value)
{
	if (skip_certs == NULL)
		return 1;

	/* TODO(https://crbug.com/boringssl/443): Move this check into the parser. */
	if (skip_certs->type & V_ASN1_NEG) {
		X509error(X509_R_INVALID_POLICY_EXTENSION);
		return 0;
	}

	/* If |skip_certs| does not fit in |uint64_t|, it must exceed |*value|. */
	uint64_t u64;
	if (ASN1_INTEGER_get_uint64(&u64, skip_certs) && u64 < *value)
		*value = (size_t)u64;
	ERR_clear_error();
	return 1;
}

/*
 * process_policy_constraints updates |*explicit_policy|, |*policy_mapping|, and
 * |*inhibit_any_policy| according to |x509|'s policy constraints and inhibit
 * anyPolicy extensions. It returns one on success and zero on error. This
 * implements steps (i) and (j) of RFC 5280, section 6.1.4.
 */
static int
process_policy_constraints(const X509 *x509, size_t *explicit_policy,
    size_t *policy_mapping,
    size_t *inhibit_any_policy)
{
	ASN1_INTEGER *inhibit_any_policy_ext;
	POLICY_CONSTRAINTS *constraints;
	int critical;
	int ok = 0;

	constraints = X509_get_ext_d2i(x509, NID_policy_constraints, &critical,
	    NULL);
	if (constraints == NULL && critical != -1)
		return 0;
	if (constraints != NULL) {
		if (constraints->requireExplicitPolicy == NULL &&
		    constraints->inhibitPolicyMapping == NULL) {
			/*
			 * Per RFC 5280, section 4.2.1.11, at least one of the
			 * fields must be
			 */
			X509error(X509_R_INVALID_POLICY_EXTENSION);
			POLICY_CONSTRAINTS_free(constraints);
			return 0;
		}
		ok = apply_skip_certs(constraints->requireExplicitPolicy,
		    explicit_policy) &&
		    apply_skip_certs(constraints->inhibitPolicyMapping,
		    policy_mapping);
		POLICY_CONSTRAINTS_free(constraints);
		if (!ok)
			return 0;
	}

	inhibit_any_policy_ext = X509_get_ext_d2i(x509, NID_inhibit_any_policy,
	    &critical, NULL);
	if (inhibit_any_policy_ext == NULL && critical != -1)
		return 0;
	ok = apply_skip_certs(inhibit_any_policy_ext, inhibit_any_policy);
	ASN1_INTEGER_free(inhibit_any_policy_ext);
	return ok;
}

/*
 * has_explicit_policy returns one if the set of authority-space policy OIDs
 * |levels| has some non-empty intersection with |user_policies|, and zero
 * otherwise. This mirrors the logic in RFC 5280, section 6.1.5, step (g). This
 * function modifies |levels| and should only be called at the end of policy
 * evaluation.
 */
static int
has_explicit_policy(STACK_OF(X509_POLICY_LEVEL) *levels,
    const STACK_OF(ASN1_OBJECT) *user_policies)
{
	X509_POLICY_LEVEL *level, *prev;
	X509_POLICY_NODE *node, *parent;
	int num_levels, user_has_any_policy;
	int i, j, k;

	if (!sk_ASN1_OBJECT_is_sorted(user_policies))
		return 0;

	/* Step (g.i). If the policy graph is empty, the intersection is empty. */
	num_levels = sk_X509_POLICY_LEVEL_num(levels);
	level = sk_X509_POLICY_LEVEL_value(levels, num_levels - 1);
	if (x509_policy_level_is_empty(level))
		return 0;

	/*
	 * If |user_policies| is empty, we interpret it as having a single
	 * anyPolicy value. The caller may also have supplied anyPolicy
	 * explicitly.
	 */
	user_has_any_policy = sk_ASN1_OBJECT_num(user_policies) <= 0;
	for (i = 0; i < sk_ASN1_OBJECT_num(user_policies); i++) {
		if (is_any_policy(sk_ASN1_OBJECT_value(user_policies, i))) {
			user_has_any_policy = 1;
			break;
		}
	}

	/*
	 * Step (g.ii). If the policy graph is not empty and the user set
	 * contains anyPolicy, the intersection is the entire (non-empty) graph.
	 */
	if (user_has_any_policy)
		return 1;

	/*
	 * Step (g.iii) does not delete anyPolicy nodes, so if the graph has
	 * anyPolicy, some explicit policy will survive. The actual intersection
	 * may synthesize some nodes in step (g.iii.3), but we do not return the
	 * policy list itself, so we skip actually computing this.
	 */
	if (level->has_any_policy)
		return 1;

	/*
	 * We defer pruning the tree, so as we look for nodes with parent
	 * anyPolicy, step (g.iii.1), we must limit to nodes reachable from the
	 * bottommost level. Start by marking each of those nodes as reachable.
	 */
	for (i = 0; i < sk_X509_POLICY_NODE_num(level->nodes); i++)
		sk_X509_POLICY_NODE_value(level->nodes, i)->reachable = 1;

	for (i = num_levels - 1; i >= 0; i--) {
		level = sk_X509_POLICY_LEVEL_value(levels, i);
		for (j = 0; j < sk_X509_POLICY_NODE_num(level->nodes); j++) {
			node = sk_X509_POLICY_NODE_value(level->nodes, j);
			if (!node->reachable)
				continue;
			if (sk_ASN1_OBJECT_num(node->parent_policies) == 0) {
				/*
				 * |node|'s parent is anyPolicy and is part of
				 * "valid_policy_node_set". If it exists in
				 * |user_policies|, the intersection is
				 * non-empty and we * can return immediately.
				 */
				if (sk_ASN1_OBJECT_find(user_policies,
				    node->policy) >= 0)
					return 1;
			} else if (i > 0) {
				int num_parent_policies =
				    sk_ASN1_OBJECT_num(node->parent_policies);
				/*
				 * |node|'s parents are concrete policies. Mark
				 * the parents reachable, to be inspected by the
				 * next loop iteration.
				 */
				prev = sk_X509_POLICY_LEVEL_value(levels, i - 1);
				for (k = 0; k < num_parent_policies; k++) {
					if (!sk_X509_POLICY_NODE_is_sorted(prev->nodes))
						return 0;
					parent = x509_policy_level_find(prev,
					    sk_ASN1_OBJECT_value(node->parent_policies,
					    k));
					if (parent != NULL)
						parent->reachable = 1;
				}
			}
		}
	}

	return 0;
}

static int
asn1_object_cmp(const ASN1_OBJECT *const *a, const ASN1_OBJECT *const *b)
{
	return OBJ_cmp(*a, *b);
}

int
X509_policy_check(const STACK_OF(X509) *certs,
    const STACK_OF(ASN1_OBJECT) *user_policies,
    unsigned long flags, X509 **out_current_cert)
{
	*out_current_cert = NULL;
	int ret = X509_V_ERR_OUT_OF_MEM;
	X509 *cert;
	X509_POLICY_LEVEL *level = NULL;
	X509_POLICY_LEVEL *current_level;
	STACK_OF(X509_POLICY_LEVEL) *levels = NULL;
	STACK_OF(ASN1_OBJECT) *user_policies_sorted = NULL;
	int num_certs = sk_X509_num(certs);
	int is_self_issued, any_policy_allowed;
	int i;

	/* Skip policy checking if the chain is just the trust anchor. */
	if (num_certs <= 1)
		return X509_V_OK;

	/* See RFC 5280, section 6.1.2, steps (d) through (f). */
	size_t explicit_policy =
	    (flags & X509_V_FLAG_EXPLICIT_POLICY) ? 0 : num_certs + 1;
	size_t inhibit_any_policy =
	    (flags & X509_V_FLAG_INHIBIT_ANY) ? 0 : num_certs + 1;
	size_t policy_mapping =
	    (flags & X509_V_FLAG_INHIBIT_MAP) ? 0 : num_certs + 1;

	levels = sk_X509_POLICY_LEVEL_new_null();
	if (levels == NULL)
		goto err;

	for (i = num_certs - 2; i >= 0; i--) {
		cert = sk_X509_value(certs, i);
		if (!x509v3_cache_extensions(cert))
			goto err;
		is_self_issued = (cert->ex_flags & EXFLAG_SI) != 0;

		if (level == NULL) {
			if (i != num_certs - 2)
				goto err;
			level = x509_policy_level_new();
			if (level == NULL)
				goto err;
			level->has_any_policy = 1;
		}

		/*
		 * RFC 5280, section 6.1.3, steps (d) and (e). |any_policy_allowed|
		 * is computed as in step (d.2).
		 */
		any_policy_allowed =
		    inhibit_any_policy > 0 || (i > 0 && is_self_issued);
		if (!process_certificate_policies(cert, level,
		    any_policy_allowed)) {
			ret = X509_V_ERR_INVALID_POLICY_EXTENSION;
			*out_current_cert = cert;
			goto err;
		}

		/* RFC 5280, section 6.1.3, step (f). */
		if (explicit_policy == 0 && x509_policy_level_is_empty(level)) {
			ret = X509_V_ERR_NO_EXPLICIT_POLICY;
			goto err;
		}

		/* Insert into the list. */
		if (!sk_X509_POLICY_LEVEL_push(levels, level))
			goto err;
		current_level = level;
		level = NULL;

		/*
		 * If this is not the leaf certificate, we go to section 6.1.4.
		 * If it is the leaf certificate, we go to section 6.1.5 instead.
		 */
		if (i != 0) {
			/* RFC 5280, section 6.1.4, steps (a) and (b). */
			level = process_policy_mappings(cert, current_level,
			    policy_mapping > 0);
			if (level == NULL) {
				ret = X509_V_ERR_INVALID_POLICY_EXTENSION;
				*out_current_cert = cert;
				goto err;
			}
		}

		/*
		 * RFC 5280, section 6.1.4, step (h-j) for non-leaves, and
		 * section 6.1.5, step (a-b) for leaves. In the leaf case,
		 * RFC 5280 says only to update |explicit_policy|, but
		 * |policy_mapping| and |inhibit_any_policy| are no
		 * longer read at this point, so we use the same process.
		 */
		if (i == 0 || !is_self_issued) {
			if (explicit_policy > 0)
				explicit_policy--;
			if (policy_mapping > 0)
				policy_mapping--;
			if (inhibit_any_policy > 0)
				inhibit_any_policy--;
		}
		if (!process_policy_constraints(cert, &explicit_policy,
		    &policy_mapping, &inhibit_any_policy)) {
			ret = X509_V_ERR_INVALID_POLICY_EXTENSION;
			*out_current_cert = cert;
			goto err;
		}
	}

	/*
	 * RFC 5280, section 6.1.5, step (g). We do not output the policy set,
	 * so it is only necessary to check if the user-constrained-policy-set
	 * is not empty.
	 */
	if (explicit_policy == 0) {
		/*
		 * Build a sorted copy of |user_policies| for more efficient
		 * lookup.
		 */
		if (user_policies != NULL) {
			user_policies_sorted = sk_ASN1_OBJECT_dup(
			    user_policies);
			if (user_policies_sorted == NULL)
				goto err;
			(void)sk_ASN1_OBJECT_set_cmp_func(user_policies_sorted,
			    asn1_object_cmp);
			sk_ASN1_OBJECT_sort(user_policies_sorted);
		}

		if (!has_explicit_policy(levels, user_policies_sorted)) {
			ret = X509_V_ERR_NO_EXPLICIT_POLICY;
			goto err;
		}
	}

	ret = X509_V_OK;

err:
	x509_policy_level_free(level);
	/*
	 * |user_policies_sorted|'s contents are owned by |user_policies|, so
	 * we do not use |sk_ASN1_OBJECT_pop_free|.
	 */
	sk_ASN1_OBJECT_free(user_policies_sorted);
	sk_X509_POLICY_LEVEL_pop_free(levels, x509_policy_level_free);
	return ret;
}
