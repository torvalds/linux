/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ctld.h"

struct keys *
keys_new(void)
{
	struct keys *keys;

	keys = calloc(1, sizeof(*keys));
	if (keys == NULL)
		log_err(1, "calloc");

	return (keys);
}

void
keys_delete(struct keys *keys)
{

	free(keys->keys_data);
	free(keys);
}

void
keys_load(struct keys *keys, const struct pdu *pdu)
{
	int i;
	char *pair;
	size_t pair_len;

	if (pdu->pdu_data_len == 0)
		return;

	if (pdu->pdu_data[pdu->pdu_data_len - 1] != '\0')
		log_errx(1, "protocol error: key not NULL-terminated\n");

	assert(keys->keys_data == NULL);
	keys->keys_data_len = pdu->pdu_data_len;
	keys->keys_data = malloc(keys->keys_data_len);
	if (keys->keys_data == NULL)
		log_err(1, "malloc");
	memcpy(keys->keys_data, pdu->pdu_data, keys->keys_data_len);

	/*
	 * XXX: Review this carefully.
	 */
	pair = keys->keys_data;
	for (i = 0;; i++) {
		if (i >= KEYS_MAX)
			log_errx(1, "too many keys received");

		pair_len = strlen(pair);

		keys->keys_values[i] = pair;
		keys->keys_names[i] = strsep(&keys->keys_values[i], "=");
		if (keys->keys_names[i] == NULL || keys->keys_values[i] == NULL)
			log_errx(1, "malformed keys");
		log_debugx("key received: \"%s=%s\"",
		    keys->keys_names[i], keys->keys_values[i]);

		pair += pair_len + 1; /* +1 to skip the terminating '\0'. */
		if (pair == keys->keys_data + keys->keys_data_len)
			break;
		assert(pair < keys->keys_data + keys->keys_data_len);
	}
}

void
keys_save(struct keys *keys, struct pdu *pdu)
{
	char *data;
	size_t len;
	int i;

	/*
	 * XXX: Not particularly efficient.
	 */
	len = 0;
	for (i = 0; i < KEYS_MAX; i++) {
		if (keys->keys_names[i] == NULL)
			break;
		/*
		 * +1 for '=', +1 for '\0'.
		 */
		len += strlen(keys->keys_names[i]) +
		    strlen(keys->keys_values[i]) + 2;
	}

	if (len == 0)
		return;

	data = malloc(len);
	if (data == NULL)
		log_err(1, "malloc");

	pdu->pdu_data = data;
	pdu->pdu_data_len = len;

	for (i = 0; i < KEYS_MAX; i++) {
		if (keys->keys_names[i] == NULL)
			break;
		data += sprintf(data, "%s=%s",
		    keys->keys_names[i], keys->keys_values[i]);
		data += 1; /* for '\0'. */
	}
}

const char *
keys_find(struct keys *keys, const char *name)
{
	int i;

	/*
	 * Note that we don't handle duplicated key names here,
	 * as they are not supposed to happen in requests, and if they do,
	 * it's an initiator error.
	 */
	for (i = 0; i < KEYS_MAX; i++) {
		if (keys->keys_names[i] == NULL)
			return (NULL);
		if (strcmp(keys->keys_names[i], name) == 0)
			return (keys->keys_values[i]);
	}
	return (NULL);
}

void
keys_add(struct keys *keys, const char *name, const char *value)
{
	int i;

	log_debugx("key to send: \"%s=%s\"", name, value);

	/*
	 * Note that we don't check for duplicates here, as they are perfectly
	 * fine in responses, e.g. the "TargetName" keys in discovery sesion
	 * response.
	 */
	for (i = 0; i < KEYS_MAX; i++) {
		if (keys->keys_names[i] == NULL) {
			keys->keys_names[i] = checked_strdup(name);
			keys->keys_values[i] = checked_strdup(value);
			return;
		}
	}
	log_errx(1, "too many keys");
}

void
keys_add_int(struct keys *keys, const char *name, int value)
{
	char *str;
	int ret;

	ret = asprintf(&str, "%d", value);
	if (ret <= 0)
		log_err(1, "asprintf");

	keys_add(keys, name, str);
	free(str);
}
