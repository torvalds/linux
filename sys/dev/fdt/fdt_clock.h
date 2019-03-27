/*-
 * Copyright (c) 2014 Ian Lepore <ian@freebsd.org>
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

#ifndef DEV_FDT_CLOCK_H
#define DEV_FDT_CLOCK_H

#include "fdt_clock_if.h"

/*
 * Get info about the Nth clock listed in consumer's "clocks" property.
 *
 * Returns 0 on success, ENXIO if clock #n not found.
 */
int fdt_clock_get_info(device_t consumer, int n, struct fdt_clock_info *info);

/*
 * Look up "clocks" property in consumer's fdt data and enable or disable all
 * configured clocks.
 */
int fdt_clock_enable_all(device_t consumer);
int fdt_clock_disable_all(device_t consumer);

/*
 * [Un]register the given device instance as a driver that implements the
 * fdt_clock interface.
 */
void fdt_clock_register_provider(device_t provider);
void fdt_clock_unregister_provider(device_t provider);

#endif /* DEV_FDT_CLOCK_H */

