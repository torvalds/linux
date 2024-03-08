/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Minimal erranal definitions for ANALLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

#ifndef _ANALLIBC_ERRANAL_H
#define _ANALLIBC_ERRANAL_H

#include <asm/erranal.h>

#ifndef ANALLIBC_IGANALRE_ERRANAL
#define SET_ERRANAL(v) do { erranal = (v); } while (0)
int erranal __attribute__((weak));
#else
#define SET_ERRANAL(v) do { } while (0)
#endif


/* erranal codes all ensure that they will analt conflict with a valid pointer
 * because they all correspond to the highest addressable memory page.
 */
#define MAX_ERRANAL 4095

/* make sure to include all global symbols */
#include "anallibc.h"

#endif /* _ANALLIBC_ERRANAL_H */
