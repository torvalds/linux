/*
 * Copyright (c) 2019 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#ifndef _FIDO_BIO_H
#define _FIDO_BIO_H

#include <stdint.h>
#include <stdlib.h>

#ifdef _FIDO_INTERNAL
#include "blob.h"
#include "fido/err.h"
#include "fido/param.h"
#include "fido/types.h"
#else
#include <fido.h>
#include <fido/err.h>
#include <fido/param.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef _FIDO_INTERNAL
struct fido_bio_template {
	fido_blob_t id;
	char *name;
};

struct fido_bio_template_array {
	struct fido_bio_template *ptr;
	size_t n_alloc; /* number of allocated entries */
	size_t n_rx;    /* number of populated entries */
};

struct fido_bio_enroll {
	uint8_t remaining_samples;
	uint8_t last_status;
	fido_blob_t *token;
};

struct fido_bio_info {
	uint8_t type;
	uint8_t max_samples;
};
#endif

typedef struct fido_bio_template fido_bio_template_t;
typedef struct fido_bio_template_array fido_bio_template_array_t;
typedef struct fido_bio_enroll fido_bio_enroll_t;
typedef struct fido_bio_info fido_bio_info_t;

#define FIDO_BIO_ENROLL_FP_GOOD				0x00
#define FIDO_BIO_ENROLL_FP_TOO_HIGH			0x01
#define FIDO_BIO_ENROLL_FP_TOO_LOW			0x02
#define FIDO_BIO_ENROLL_FP_TOO_LEFT			0x03
#define FIDO_BIO_ENROLL_FP_TOO_RIGHT			0x04
#define FIDO_BIO_ENROLL_FP_TOO_FAST			0x05
#define FIDO_BIO_ENROLL_FP_TOO_SLOW			0x06
#define FIDO_BIO_ENROLL_FP_POOR_QUALITY			0x07
#define FIDO_BIO_ENROLL_FP_TOO_SKEWED			0x08
#define FIDO_BIO_ENROLL_FP_TOO_SHORT			0x09
#define FIDO_BIO_ENROLL_FP_MERGE_FAILURE		0x0a
#define FIDO_BIO_ENROLL_FP_EXISTS			0x0b
#define FIDO_BIO_ENROLL_FP_DATABASE_FULL		0x0c
#define FIDO_BIO_ENROLL_NO_USER_ACTIVITY		0x0d
#define FIDO_BIO_ENROLL_NO_USER_PRESENCE_TRANSITION	0x0e

const char *fido_bio_template_name(const fido_bio_template_t *);
const fido_bio_template_t *fido_bio_template(const fido_bio_template_array_t *,
    size_t);
const unsigned char *fido_bio_template_id_ptr(const fido_bio_template_t *);
fido_bio_enroll_t *fido_bio_enroll_new(void);
fido_bio_info_t *fido_bio_info_new(void);
fido_bio_template_array_t *fido_bio_template_array_new(void);
fido_bio_template_t *fido_bio_template_new(void);
int fido_bio_dev_enroll_begin(fido_dev_t *, fido_bio_template_t *,
    fido_bio_enroll_t *, uint32_t, const char *);
int fido_bio_dev_enroll_cancel(fido_dev_t *);
int fido_bio_dev_enroll_continue(fido_dev_t *, const fido_bio_template_t *,
    fido_bio_enroll_t *, uint32_t);
int fido_bio_dev_enroll_remove(fido_dev_t *, const fido_bio_template_t *,
    const char *);
int fido_bio_dev_get_info(fido_dev_t *, fido_bio_info_t *);
int fido_bio_dev_get_template_array(fido_dev_t *, fido_bio_template_array_t *,
    const char *);
int fido_bio_dev_set_template_name(fido_dev_t *, const fido_bio_template_t *,
    const char *);
int fido_bio_template_set_id(fido_bio_template_t *, const unsigned char *,
    size_t);
int fido_bio_template_set_name(fido_bio_template_t *, const char *);
size_t fido_bio_template_array_count(const fido_bio_template_array_t *);
size_t fido_bio_template_id_len(const fido_bio_template_t *);
uint8_t fido_bio_enroll_last_status(const fido_bio_enroll_t *);
uint8_t fido_bio_enroll_remaining_samples(const fido_bio_enroll_t *);
uint8_t fido_bio_info_max_samples(const fido_bio_info_t *);
uint8_t fido_bio_info_type(const fido_bio_info_t *);
void fido_bio_enroll_free(fido_bio_enroll_t **);
void fido_bio_info_free(fido_bio_info_t **);
void fido_bio_template_array_free(fido_bio_template_array_t **);
void fido_bio_template_free(fido_bio_template_t **);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* !_FIDO_BIO_H */
