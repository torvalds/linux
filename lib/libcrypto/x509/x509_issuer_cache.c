/* $OpenBSD: x509_issuer_cache.c,v 1.7 2023/12/30 18:26:13 tb Exp $ */
/*
 * Copyright (c) 2020 Bob Beck <beck@openbsd.org>
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

/* x509_issuer_cache */

/*
 * The issuer cache is a cache of parent and child x509 certificate
 * hashes with a signature validation result.
 *
 * Entries should only be added to the cache with a validation result
 * from checking the public key math that "parent" signed "child".
 *
 * Finding an entry in the cache gets us the result of a previously
 * performed validation of the signature of "parent" signing for the
 * validity of "child". It allows us to skip doing the public key math
 * when validating a certificate chain. It does not allow us to skip
 * any other steps of validation (times, names, key usage, etc.)
 */

#include <pthread.h>
#include <string.h>

#include "x509_issuer_cache.h"

static int
x509_issuer_cmp(struct x509_issuer *x1, struct x509_issuer *x2)
{
	int pcmp;
	if ((pcmp = memcmp(x1->parent_md, x2->parent_md, EVP_MAX_MD_SIZE)) != 0)
		return pcmp;
	return memcmp(x1->child_md, x2->child_md, EVP_MAX_MD_SIZE);
}

static size_t x509_issuer_cache_count;
static size_t x509_issuer_cache_max = X509_ISSUER_CACHE_MAX;
static RB_HEAD(x509_issuer_tree, x509_issuer) x509_issuer_cache =
    RB_INITIALIZER(&x509_issuer_cache);
static TAILQ_HEAD(lruqueue, x509_issuer) x509_issuer_lru =
    TAILQ_HEAD_INITIALIZER(x509_issuer_lru);
static pthread_mutex_t x509_issuer_tree_mutex = PTHREAD_MUTEX_INITIALIZER;

RB_PROTOTYPE(x509_issuer_tree, x509_issuer, entry, x509_issuer_cmp);
RB_GENERATE(x509_issuer_tree, x509_issuer, entry, x509_issuer_cmp);

/*
 * Set the maximum number of cached entries. On additions to the cache
 * the least recently used entries will be discarded so that the cache
 * stays under the maximum number of entries.  Setting a maximum of 0
 * disables the cache.
 */
int
x509_issuer_cache_set_max(size_t max)
{
	if (pthread_mutex_lock(&x509_issuer_tree_mutex) != 0)
		return 0;
	x509_issuer_cache_max = max;
	(void) pthread_mutex_unlock(&x509_issuer_tree_mutex);

	return 1;
}

/*
 * Free the oldest entry in the issuer cache. Returns 1
 * if an entry was successfully freed, 0 otherwise. Must
 * be called with x509_issuer_tree_mutex held.
 */
static void
x509_issuer_cache_free_oldest(void)
{
	struct x509_issuer *old;

	if (x509_issuer_cache_count == 0)
		return;
	old = TAILQ_LAST(&x509_issuer_lru, lruqueue);
	TAILQ_REMOVE(&x509_issuer_lru, old, queue);
	RB_REMOVE(x509_issuer_tree, &x509_issuer_cache, old);
	free(old->parent_md);
	free(old->child_md);
	free(old);
	x509_issuer_cache_count--;
}

/*
 * Free the entire issuer cache, discarding all entries.
 */
void
x509_issuer_cache_free(void)
{
	if (pthread_mutex_lock(&x509_issuer_tree_mutex) != 0)
		return;
	while (x509_issuer_cache_count > 0)
		x509_issuer_cache_free_oldest();
	(void) pthread_mutex_unlock(&x509_issuer_tree_mutex);
}

/*
 * Find a previous result of checking if parent signed child
 *
 * Returns:
 *	-1 : No entry exists in the cache. signature must be checked.
 *	0 : The signature of parent signing child is invalid.
 *	1 : The signature of parent signing child is valid.
 */
int
x509_issuer_cache_find(unsigned char *parent_md, unsigned char *child_md)
{
	struct x509_issuer candidate, *found;
	int ret = -1;

	memset(&candidate, 0, sizeof(candidate));
	candidate.parent_md = parent_md;
	candidate.child_md = child_md;

	if (x509_issuer_cache_max == 0)
		return -1;

	if (pthread_mutex_lock(&x509_issuer_tree_mutex) != 0)
		return -1;
	if ((found = RB_FIND(x509_issuer_tree, &x509_issuer_cache,
	    &candidate)) != NULL) {
		TAILQ_REMOVE(&x509_issuer_lru, found, queue);
		TAILQ_INSERT_HEAD(&x509_issuer_lru, found, queue);
		ret = found->valid;
	}
	(void) pthread_mutex_unlock(&x509_issuer_tree_mutex);

	return ret;
}

/*
 * Attempt to add a validation result to the cache.
 *
 * valid must be:
 *	0: The signature of parent signing child is invalid.
 *	1: The signature of parent signing child is valid.
 *
 * Previously added entries for the same parent and child are *not* replaced.
 */
void
x509_issuer_cache_add(unsigned char *parent_md, unsigned char *child_md,
    int valid)
{
	struct x509_issuer *new;

	if (x509_issuer_cache_max == 0)
		return;
	if (valid != 0 && valid != 1)
		return;

	if ((new = calloc(1, sizeof(struct x509_issuer))) == NULL)
		return;
	if ((new->parent_md = calloc(1, EVP_MAX_MD_SIZE)) == NULL)
		goto err;
	memcpy(new->parent_md, parent_md, EVP_MAX_MD_SIZE);
	if ((new->child_md = calloc(1, EVP_MAX_MD_SIZE)) == NULL)
		goto err;
	memcpy(new->child_md, child_md, EVP_MAX_MD_SIZE);

	new->valid = valid;

	if (pthread_mutex_lock(&x509_issuer_tree_mutex) != 0)
		goto err;
	while (x509_issuer_cache_count >= x509_issuer_cache_max)
		x509_issuer_cache_free_oldest();
	if (RB_INSERT(x509_issuer_tree, &x509_issuer_cache, new) == NULL) {
		TAILQ_INSERT_HEAD(&x509_issuer_lru, new, queue);
		x509_issuer_cache_count++;
		new = NULL;
	}
	(void) pthread_mutex_unlock(&x509_issuer_tree_mutex);

 err:
	if (new != NULL) {
		free(new->parent_md);
		free(new->child_md);
	}
	free(new);
	return;
}
