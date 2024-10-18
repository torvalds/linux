// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SIMD testing utility functions
 *
 * Copyright 2024 Google LLC
 */

#include <crypto/internal/simd.h>

DEFINE_PER_CPU(bool, crypto_simd_disabled_for_test);
EXPORT_PER_CPU_SYMBOL_GPL(crypto_simd_disabled_for_test);
