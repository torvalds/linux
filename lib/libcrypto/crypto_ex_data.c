/* $OpenBSD: crypto_ex_data.c,v 1.6 2025/06/15 15:58:56 tb Exp $ */
/*
 * Copyright (c) 2023 Joel Sing <jsing@openbsd.org>
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

#include <stdlib.h>

#include <openssl/crypto.h>

#define CRYPTO_EX_DATA_MAX_INDEX 32

struct crypto_ex_data {
	int class_index;
	void **slots;
	size_t slots_len;
};

struct crypto_ex_data_index {
	CRYPTO_EX_new *new_func;
	CRYPTO_EX_dup *dup_func;
	CRYPTO_EX_free *free_func;
	long argl;
	void *argp;
};

struct crypto_ex_data_class {
	struct crypto_ex_data_index **indexes;
	size_t indexes_len;
	size_t next_index;
};

static struct crypto_ex_data_class **classes;

static int
crypto_ex_data_classes_init(void)
{
	struct crypto_ex_data_class **classes_new = NULL;

	if (classes != NULL)
		return 1;

	if ((classes_new = calloc(CRYPTO_EX_INDEX__COUNT,
	    sizeof(*classes_new))) == NULL)
		return 0;

	CRYPTO_w_lock(CRYPTO_LOCK_EX_DATA);
	if (classes == NULL) {
		classes = classes_new;
		classes_new = NULL;
	}
	CRYPTO_w_unlock(CRYPTO_LOCK_EX_DATA);

	free(classes_new);

	return 1;
}

static struct crypto_ex_data_class *
crypto_ex_data_class_lookup(int class_index)
{
	struct crypto_ex_data_class *class;

	if (classes == NULL)
		return NULL;
	if (class_index < 0 || class_index >= CRYPTO_EX_INDEX__COUNT)
		return NULL;

	CRYPTO_r_lock(CRYPTO_LOCK_EX_DATA);
	class = classes[class_index];
	CRYPTO_r_unlock(CRYPTO_LOCK_EX_DATA);

	return class;
}

int
CRYPTO_get_ex_new_index(int class_index, long argl, void *argp,
    CRYPTO_EX_new *new_func, CRYPTO_EX_dup *dup_func, CRYPTO_EX_free *free_func)
{
	struct crypto_ex_data_class *new_class = NULL;
	struct crypto_ex_data_index *index = NULL;
	struct crypto_ex_data_class *class;
	int idx = -1;

	if (!crypto_ex_data_classes_init())
		goto err;

	if (class_index < 0 || class_index >= CRYPTO_EX_INDEX__COUNT)
		goto err;

	if ((class = classes[class_index]) == NULL) {
		if ((new_class = calloc(1, sizeof(*new_class))) == NULL)
			goto err;
		if ((new_class->indexes = calloc(CRYPTO_EX_DATA_MAX_INDEX,
                    sizeof(*new_class->indexes))) == NULL)
			goto err;
		new_class->indexes_len = CRYPTO_EX_DATA_MAX_INDEX;
		new_class->next_index = 1;

		CRYPTO_w_lock(CRYPTO_LOCK_EX_DATA);
		if (classes[class_index] == NULL) {
			classes[class_index] = new_class;
			new_class = NULL;
		}
		CRYPTO_w_unlock(CRYPTO_LOCK_EX_DATA);

		class = classes[class_index];
	}

	if ((index = calloc(1, sizeof(*index))) == NULL)
		goto err;

	index->new_func = new_func;
	index->dup_func = dup_func;
	index->free_func = free_func;

	index->argl = argl;
	index->argp = argp;

	CRYPTO_w_lock(CRYPTO_LOCK_EX_DATA);
	if (class->next_index < class->indexes_len) {
		idx = class->next_index++;
		class->indexes[idx] = index;
		index = NULL;
	}
	CRYPTO_w_unlock(CRYPTO_LOCK_EX_DATA);


 err:
	if (new_class != NULL) {
		free(new_class->indexes);
		free(new_class);
	}
	free(index);

	return idx;
}
LCRYPTO_ALIAS(CRYPTO_get_ex_new_index);

void
CRYPTO_cleanup_all_ex_data(void)
{
	struct crypto_ex_data_class *class;
	int i, j;

	if (classes == NULL)
		return;

	for (i = 0; i < CRYPTO_EX_INDEX__COUNT; i++) {
		if ((class = classes[i]) == NULL)
			continue;

		if (class->indexes != NULL) {
			for (j = 0; j < CRYPTO_EX_DATA_MAX_INDEX; j++)
				free(class->indexes[j]);
			free(class->indexes);
		}

		free(class);
	}

	free(classes);
	classes = NULL;
}
LCRYPTO_ALIAS(CRYPTO_cleanup_all_ex_data);

static void
crypto_ex_data_clear(CRYPTO_EX_DATA *exdata)
{
	struct crypto_ex_data *ced;

	if (exdata == NULL)
		return;

	if ((ced = exdata->sk) != NULL) {
		freezero(ced->slots, ced->slots_len * sizeof(void *));
		freezero(ced, sizeof(*ced));
	}

	exdata->sk = NULL;
}

static int
crypto_ex_data_init(CRYPTO_EX_DATA *exdata)
{
	struct crypto_ex_data *ced = NULL;

	if (exdata->sk != NULL)
		goto err;

	if ((ced = calloc(1, sizeof(*ced))) == NULL)
		goto err;

	ced->class_index = -1;

	if ((ced->slots = calloc(CRYPTO_EX_DATA_MAX_INDEX, sizeof(*ced->slots))) == NULL)
		goto err;
	ced->slots_len = CRYPTO_EX_DATA_MAX_INDEX;

	exdata->sk = ced;

	return 1;

 err:
	if (ced != NULL) {
		free(ced->slots);
		free(ced);
	}
	crypto_ex_data_clear(exdata);

	return 0;
}

int
CRYPTO_new_ex_data(int class_index, void *parent, CRYPTO_EX_DATA *exdata)
{
	struct crypto_ex_data_class *class;
	struct crypto_ex_data_index *index;
	struct crypto_ex_data *ced;
	size_t i, last_index;

	if (!crypto_ex_data_init(exdata))
		goto err;
	if ((ced = exdata->sk) == NULL)
		goto err;

	if (!crypto_ex_data_classes_init())
		goto err;
	if ((class = crypto_ex_data_class_lookup(class_index)) == NULL)
		goto done;

	ced->class_index = class_index;

	/* Existing indexes are immutable, we just have to know when to stop. */
	CRYPTO_r_lock(CRYPTO_LOCK_EX_DATA);
	last_index = class->next_index;
	CRYPTO_r_unlock(CRYPTO_LOCK_EX_DATA);

	for (i = 0; i < last_index; i++) {
		if ((index = class->indexes[i]) == NULL)
			continue;
		if (index->new_func == NULL)
			continue;
		if (!index->new_func(parent, NULL, exdata, i, index->argl,
		    index->argp))
			goto err;
	}

 done:
	return 1;

 err:
	CRYPTO_free_ex_data(class_index, parent, exdata);

	return 0;
}
LCRYPTO_ALIAS(CRYPTO_new_ex_data);

int
CRYPTO_dup_ex_data(int class_index, CRYPTO_EX_DATA *dst, CRYPTO_EX_DATA *src)
{
	struct crypto_ex_data *dst_ced, *src_ced;
	struct crypto_ex_data_class *class;
	struct crypto_ex_data_index *index;
	size_t i, last_index;
	void *val;

	if (dst == NULL || src == NULL)
		goto err;

	/*
	 * Some code calls CRYPTO_new_ex_data() before dup, others never call
	 * CRYPTO_new_ex_data()... so we get to handle both.
	 */
	/* XXX - parent == NULL? */
	CRYPTO_free_ex_data(class_index, NULL, dst);

	if (!crypto_ex_data_init(dst))
		goto err;

	if ((dst_ced = dst->sk) == NULL)
		goto err;
	if ((src_ced = src->sk) == NULL)
		goto err;

	if ((class = crypto_ex_data_class_lookup(class_index)) == NULL) {
		for (i = 0; i < CRYPTO_EX_DATA_MAX_INDEX; i++)
			dst_ced->slots[i] = src_ced->slots[i];
		goto done;
	}

	OPENSSL_assert(src_ced->class_index == class_index);

	dst_ced->class_index = class_index;

	/* Existing indexes are immutable, we just have to know when to stop. */
	CRYPTO_r_lock(CRYPTO_LOCK_EX_DATA);
	last_index = class->next_index;
	CRYPTO_r_unlock(CRYPTO_LOCK_EX_DATA);

	for (i = 0; i < last_index; i++) {
		if ((index = class->indexes[i]) == NULL)
			continue;

		/* If there is no dup function, we copy the pointer. */
		val = src_ced->slots[i];
		if (index->dup_func != NULL) {
			if (!index->dup_func(dst, src, &val, i, index->argl,
			    index->argp))
				goto err;
		}
		/* If the dup function set data, we will potentially leak. */
		if (dst_ced->slots[i] != NULL)
			goto err;
		dst_ced->slots[i] = val;
	}

 done:
	return 1;

 err:
	/* XXX - parent == NULL? */
	CRYPTO_free_ex_data(class_index, NULL, dst);

	return 0;
}
LCRYPTO_ALIAS(CRYPTO_dup_ex_data);

void
CRYPTO_free_ex_data(int class_index, void *parent, CRYPTO_EX_DATA *exdata)
{
	struct crypto_ex_data_class *class;
	struct crypto_ex_data_index *index;
	struct crypto_ex_data *ced;
	size_t i, last_index;

	if (exdata == NULL)
		return;
	if ((ced = exdata->sk) == NULL)
		goto done;
	if (ced->class_index == -1)
		goto done;

	if ((class = crypto_ex_data_class_lookup(class_index)) == NULL)
		goto done;

	OPENSSL_assert(ced->class_index == class_index);

	/* Existing indexes are immutable, we just have to know when to stop. */
	CRYPTO_r_lock(CRYPTO_LOCK_EX_DATA);
	last_index = class->next_index;
	CRYPTO_r_unlock(CRYPTO_LOCK_EX_DATA);

	for (i = 0; i < last_index; i++) {
		if ((index = class->indexes[i]) == NULL)
			continue;
		if (index->free_func == NULL)
			continue;
		index->free_func(parent, ced->slots[i], exdata, i, index->argl,
		    index->argp);
	}

 done:
	crypto_ex_data_clear(exdata);
}
LCRYPTO_ALIAS(CRYPTO_free_ex_data);

int
CRYPTO_set_ex_data(CRYPTO_EX_DATA *exdata, int idx, void *val)
{
	struct crypto_ex_data *ced;

	/*
	 * Preserve horrible historical behaviour - allow set to work even if
	 * new has not been called first.
	 */
	if ((ced = exdata->sk) == NULL) {
		if (!crypto_ex_data_init(exdata))
			return 0;
		ced = exdata->sk;
	}

	/* XXX - consider preventing set for an unallocated index. */

	if (idx < 0 || idx >= ced->slots_len)
		return 0;

	ced->slots[idx] = val;

	return 1;
}
LCRYPTO_ALIAS(CRYPTO_set_ex_data);

void *
CRYPTO_get_ex_data(const CRYPTO_EX_DATA *exdata, int idx)
{
	struct crypto_ex_data *ced;

	if ((ced = exdata->sk) == NULL)
		return NULL;
	if (idx < 0 || idx >= ced->slots_len)
		return NULL;

	return ced->slots[idx];
}
LCRYPTO_ALIAS(CRYPTO_get_ex_data);
