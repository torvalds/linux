/*
 * if_strip.h --
 *
 *      Definitions for the STRIP interface
 *
 * Copyright 1996 The Board of Trustees of The Leland Stanford
 * Junior University. All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  Stanford University
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef __LINUX_STRIP_H
#define __LINUX_STRIP_H

#include <linux/types.h>

typedef struct {
    __u8 c[6];
} MetricomAddress;

#endif
