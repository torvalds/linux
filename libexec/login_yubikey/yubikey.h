/* $OpenBSD: yubikey.h,v 1.3 2013/06/04 18:49:12 mcbride Exp $ */

/*
 * Written by Simon Josefsson <simon@josefsson.org>.
 * Copyright (c) 2006, 2007, 2008, 2009, 2010 Yubico AB
 * Copyright (c) 2010 Daniel Hartmeier <daniel@benzedrine.cx>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials provided
 *   with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef __YUBIKEY_H__
#define __YUBIKEY_H__

#include <stdint.h>
#include <string.h>

#define	YUBIKEY_BLOCK_SIZE	16
#define	YUBIKEY_KEY_SIZE	16
#define	YUBIKEY_TOKEN_SIZE	32
#define	YUBIKEY_UID_SIZE	6
#define YUBIKEY_CRC_OK_RESIDUE	0xf0b8

#define yubikey_counter(ctr) ((ctr) & 0x7FFF)
#define yubikey_capslock(ctr) ((ctr) & 0x8000)
#define yubikey_crc_ok_p(tok) \
	(yubikey_crc16 ((tok), YUBIKEY_BLOCK_SIZE) == YUBIKEY_CRC_OK_RESIDUE)

typedef struct {
	uint8_t		uid[YUBIKEY_UID_SIZE];
	uint16_t	ctr;
	uint16_t	tstpl;
	uint8_t		tstph;
	uint8_t		use;
	uint16_t	rnd;
	uint16_t	crc;
} yubikey_token_st;

typedef yubikey_token_st *yubikey_token_t;

extern int yubikey_parse(const uint8_t token[YUBIKEY_BLOCK_SIZE],
    const uint8_t key[YUBIKEY_KEY_SIZE], yubikey_token_t out, int index);

extern void yubikey_modhex_decode(char *dst, const char *src, size_t dstsize);

extern void yubikey_hex_encode(char *dst, const char *src, size_t srcsize);
extern void yubikey_hex_decode(char *dst, const char *src, size_t dstsize);

extern uint16_t yubikey_crc16(const uint8_t *buf, size_t buf_size);

extern void yubikey_aes_decrypt(uint8_t *state, const uint8_t *key);

#endif
