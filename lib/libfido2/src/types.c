/*
 * Copyright (c) 2018-2021 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include "fido.h"

void
fido_str_array_free(fido_str_array_t *sa)
{
	for (size_t i = 0; i < sa->len; i++)
		free(sa->ptr[i]);

	free(sa->ptr);
	sa->ptr = NULL;
	sa->len = 0;
}

void
fido_opt_array_free(fido_opt_array_t *oa)
{
	for (size_t i = 0; i < oa->len; i++)
		free(oa->name[i]);

	free(oa->name);
	free(oa->value);
	oa->name = NULL;
	oa->value = NULL;
}

void
fido_byte_array_free(fido_byte_array_t *ba)
{
	free(ba->ptr);

	ba->ptr = NULL;
	ba->len = 0;
}

void
fido_algo_free(fido_algo_t *a)
{
	free(a->type);
	a->type = NULL;
	a->cose = 0;
}

void
fido_algo_array_free(fido_algo_array_t *aa)
{
	for (size_t i = 0; i < aa->len; i++)
		fido_algo_free(&aa->ptr[i]);

	free(aa->ptr);
	aa->ptr = NULL;
	aa->len = 0;
}

int
fido_str_array_pack(fido_str_array_t *sa, const char * const *v, size_t n)
{
	if ((sa->ptr = calloc(n, sizeof(char *))) == NULL) {
		fido_log_debug("%s: calloc", __func__);
		return -1;
	}
	for (size_t i = 0; i < n; i++) {
		if ((sa->ptr[i] = strdup(v[i])) == NULL) {
			fido_log_debug("%s: strdup", __func__);
			return -1;
		}
		sa->len++;
	}

	return 0;
}
