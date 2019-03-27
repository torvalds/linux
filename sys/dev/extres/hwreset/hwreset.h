/*-
 * Copyright 2016 Michal Meloun <mmel@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef DEV_EXTRES_HWRESET_HWRESET_H
#define DEV_EXTRES_HWRESET_HWRESET_H

#include "opt_platform.h"
#include <sys/types.h>
#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#endif

typedef struct hwreset *hwreset_t;

/*
 * Provider interface
 */
#ifdef FDT
void hwreset_register_ofw_provider(device_t provider_dev);
void hwreset_unregister_ofw_provider(device_t provider_dev);
#endif

/*
 * Consumer interface
 */
int hwreset_get_by_id(device_t consumer_dev, device_t provider_dev, intptr_t id,
    hwreset_t *rst);
void hwreset_release(hwreset_t rst);

int hwreset_assert(hwreset_t rst);
int hwreset_deassert(hwreset_t rst);
int hwreset_is_asserted(hwreset_t rst, bool *value);

#ifdef FDT
int hwreset_get_by_ofw_name(device_t consumer_dev, phandle_t node, char *name,
    hwreset_t *rst);
int hwreset_get_by_ofw_idx(device_t consumer_dev, phandle_t node, int idx,
    hwreset_t *rst);
#endif



#endif /* DEV_EXTRES_HWRESET_HWRESET_H */
