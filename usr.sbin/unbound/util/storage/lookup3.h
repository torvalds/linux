/*
 * util/storage/lookup3.h - header file for hashing functions.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains header definitions for the hash functions we use.
 * The hash functions are public domain (see lookup3.c).
 */

#ifndef UTIL_STORAGE_LOOKUP3_H
#define UTIL_STORAGE_LOOKUP3_H

/**
 * Hash key made of 4byte chunks.
 * @param k: the key, an array of uint32_t values
 * @param length: the length of the key, in uint32_ts
 * @param initval: the previous hash, or an arbitrary value
 * @return: hash value.
 */
uint32_t hashword(const uint32_t *k, size_t length, uint32_t initval);

/**
 * Hash key data.
 * @param k: the key, array of uint8_t
 * @param length: the length of the key, in uint8_ts
 * @param initval: the previous hash, or an arbitrary value
 * @return: hash value.
 */
uint32_t hashlittle(const void *k, size_t length, uint32_t initval);

/**
 * Set the randomisation initial value, set this before threads start,
 * and before hashing stuff (because it changes subsequent results).
 * @param v: value
 */
void hash_set_raninit(uint32_t v);

#endif /* UTIL_STORAGE_LOOKUP3_H */
