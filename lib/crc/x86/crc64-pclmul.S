/* SPDX-License-Identifier: GPL-2.0-or-later */
// Copyright 2025 Google LLC

#include "crc-pclmul-template.S"

DEFINE_CRC_PCLMUL_FUNCS(crc64_msb, /* bits= */ 64, /* lsb= */ 0)
DEFINE_CRC_PCLMUL_FUNCS(crc64_lsb, /* bits= */ 64, /* lsb= */ 1)
