/*
 * Copyright (c) 2020 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#ifndef _FIDO_CONFIG_H
#define _FIDO_CONFIG_H

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

int fido_dev_enable_entattest(fido_dev_t *, const char *);
int fido_dev_force_pin_change(fido_dev_t *, const char *);
int fido_dev_toggle_always_uv(fido_dev_t *, const char *);
int fido_dev_set_pin_minlen(fido_dev_t *, size_t, const char *);
int fido_dev_set_pin_minlen_rpid(fido_dev_t *, const char * const *, size_t,
    const char *);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* !_FIDO_CONFIG_H */
