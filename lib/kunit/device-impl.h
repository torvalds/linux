/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KUnit internal header for device helpers
 *
 * Header for KUnit-internal driver / bus management.
 *
 * Copyright (C) 2023, Google LLC.
 * Author: David Gow <davidgow@google.com>
 */

#ifndef _KUNIT_DEVICE_IMPL_H
#define _KUNIT_DEVICE_IMPL_H

// For internal use only -- registers the kunit_bus.
int kunit_bus_init(void);

#endif //_KUNIT_DEVICE_IMPL_H
