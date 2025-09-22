/*	$OpenBSD: rrdp_util.c,v 1.2 2023/11/24 14:05:47 job Exp $ */
/*
 * Copyright (c) 2020 Nils Fisher <nils_fisher@hotmail.com>
 * Copyright (c) 2021 Claudio Jeker <claudio@openbsd.org>
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
#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include <expat.h>
#include <openssl/sha.h>

#include "extern.h"
#include "rrdp.h"

/*
 * Both snapshots and deltas use publish_xml to store the publish and
 * withdraw records. Once all the content is added the request is sent
 * to the main process where it is processed.
 */
struct publish_xml *
new_publish_xml(enum publish_type type, char *uri, char *hash, size_t hlen)
{
	struct publish_xml *pxml;

	if ((pxml = calloc(1, sizeof(*pxml))) == NULL)
		err(1, "%s", __func__);

	pxml->type = type;
	pxml->uri = uri;
	if (hlen > 0) {
		assert(hlen == sizeof(pxml->hash));
		memcpy(pxml->hash, hash, hlen);
	}

	return pxml;
}

void
free_publish_xml(struct publish_xml *pxml)
{
	if (pxml == NULL)
		return;

	free(pxml->uri);
	free(pxml->data);
	free(pxml);
}

/*
 * Add buf to the base64 data string, ensure that this remains a proper
 * string by NUL-terminating the string.
 */
int
publish_add_content(struct publish_xml *pxml, const char *buf, int length)
{
	size_t newlen, outlen;

	/*
	 * optmisiation, this often gets called with '\n' as the
	 * only data... seems wasteful
	 */
	if (length == 1 && buf[0] == '\n')
		return 0;

	/* append content to data */
	if (SIZE_MAX - length - 1 <= pxml->data_length)
		return -1;
	newlen = pxml->data_length + length;
	if (base64_decode_len(newlen, &outlen) == -1 ||
	    outlen > MAX_FILE_SIZE)
		return -1;

	pxml->data = realloc(pxml->data, newlen + 1);
	if (pxml->data == NULL)
		err(1, "%s", __func__);

	memcpy(pxml->data + pxml->data_length, buf, length);
	pxml->data[newlen] = '\0';
	pxml->data_length = newlen;
	return 0;
}

/*
 * Base64 decode the data blob and send the file to the main process
 * where the hash is validated and the file stored in the repository.
 * Increase the file_pending counter to ensure the RRDP process waits
 * until all files have been processed before moving to the next stage.
 * Returns 0 on success or -1 on errors (base64 decode failed).
 */
int
publish_done(struct rrdp *s, struct publish_xml *pxml)
{
	unsigned char *data = NULL;
	size_t datasz = 0;

	switch (pxml->type) {
	case PUB_ADD:
	case PUB_UPD:
		if (base64_decode_len(pxml->data_length, &datasz) == -1)
			return -1;
		if (datasz < MIN_FILE_SIZE)
			return -1;
		if ((base64_decode(pxml->data, pxml->data_length,
		    &data, &datasz)) == -1)
			return -1;
		break;
	case PUB_DEL:
		if (pxml->data_length != 0)
			return -1;
		break;
	}

	rrdp_publish_file(s, pxml, data, datasz);

	free(data);
	free_publish_xml(pxml);
	return 0;
}
