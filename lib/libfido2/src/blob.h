/*
 * Copyright (c) 2018 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#ifndef _BLOB_H
#define _BLOB_H

#include <cbor.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct fido_blob {
	unsigned char	*ptr;
	size_t		 len;
} fido_blob_t;

typedef struct fido_blob_array {
	fido_blob_t	*ptr;
	size_t		 len;
} fido_blob_array_t;

cbor_item_t *fido_blob_encode(const fido_blob_t *);
fido_blob_t *fido_blob_new(void);
int fido_blob_decode(const cbor_item_t *, fido_blob_t *);
int fido_blob_is_empty(const fido_blob_t *);
int fido_blob_set(fido_blob_t *, const u_char *, size_t);
int fido_blob_append(fido_blob_t *, const u_char *, size_t);
void fido_blob_free(fido_blob_t **);
void fido_blob_reset(fido_blob_t *);
void fido_free_blob_array(fido_blob_array_t *);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* !_BLOB_H */
