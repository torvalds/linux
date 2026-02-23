/* SPDX-License-Identifier: Apache-2.0 OR BSD-2-Clause */
//
// This file is dual-licensed, meaning that you can use it under your
// choice of either of the following two licenses:
//
// Copyright 2023 The OpenSSL Project Authors. All Rights Reserved.
//
// Licensed under the Apache License 2.0 (the "License"). You can obtain
// a copy in the file LICENSE in the source distribution or at
// https://www.openssl.org/source/license.html
//
// or
//
// Copyright (c) 2023, Christoph Müllner <christoph.muellner@vrull.eu>
// Copyright (c) 2023, Phoebe Chen <phoebe.chen@sifive.com>
// Copyright (c) 2023, Jerry Shih <jerry.shih@sifive.com>
// Copyright 2024 Google LLC
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The generated code of this file depends on the following RISC-V extensions:
// - RV64I
// - RISC-V Vector ('V') with VLEN >= 128
// - RISC-V Vector AES block cipher extension ('Zvkned')

#include <linux/linkage.h>

.text
.option arch, +zvkned

#include "../../arch/riscv/crypto/aes-macros.S"

#define RNDKEYS		a0
#define KEY_LEN		a1
#define OUTP		a2
#define INP		a3

.macro	__aes_crypt_zvkned	enc, keybits
	vle32.v		v16, (INP)
	aes_crypt	v16, \enc, \keybits
	vse32.v		v16, (OUTP)
	ret
.endm

.macro	aes_crypt_zvkned	enc
	aes_begin	RNDKEYS, 128f, 192f, KEY_LEN
	__aes_crypt_zvkned	\enc, 256
128:
	__aes_crypt_zvkned	\enc, 128
192:
	__aes_crypt_zvkned	\enc, 192
.endm

// void aes_encrypt_zvkned(const u32 rndkeys[], int key_len,
//			   u8 out[AES_BLOCK_SIZE], const u8 in[AES_BLOCK_SIZE]);
SYM_FUNC_START(aes_encrypt_zvkned)
	aes_crypt_zvkned	1
SYM_FUNC_END(aes_encrypt_zvkned)

// void aes_decrypt_zvkned(const u32 rndkeys[], int key_len,
//			   u8 out[AES_BLOCK_SIZE], const u8 in[AES_BLOCK_SIZE]);
SYM_FUNC_START(aes_decrypt_zvkned)
	aes_crypt_zvkned	0
SYM_FUNC_END(aes_decrypt_zvkned)
