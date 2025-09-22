/*
 * validator/val_secalgo.h - validator security algorithm functions.
 *
 * Copyright (c) 2012, NLnet Labs. All rights reserved.
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
 * This file contains helper functions for the validator module.
 * The functions take buffers with raw data and convert to library calls.
 */

#ifndef VALIDATOR_VAL_SECALGO_H
#define VALIDATOR_VAL_SECALGO_H
struct sldns_buffer;
struct secalgo_hash;

/** Return size of nsec3 hash algorithm, 0 if not supported */
size_t nsec3_hash_algo_size_supported(int id);

/**
 * Hash a single hash call of an NSEC3 hash algorithm.
 * Iterations and salt are done by the caller.
 * @param algo: nsec3 hash algorithm.
 * @param buf: the buffer to digest
 * @param len: length of buffer to digest.
 * @param res: result stored here (must have sufficient space).
 * @return false on failure.
*/
int secalgo_nsec3_hash(int algo, unsigned char* buf, size_t len,
        unsigned char* res);

/**
 * Calculate the sha256 hash for the data buffer into the result.
 * @param buf: buffer to digest.
 * @param len: length of the buffer to digest.
 * @param res: result is stored here (space 256/8 bytes).
 */
void secalgo_hash_sha256(unsigned char* buf, size_t len, unsigned char* res);

/**
 * Start a hash of type sha384. Allocates structure, then inits it,
 * so that a series of updates can be performed, before the final result.
 * @return hash structure.  NULL on malloc failure or no support.
 */
struct secalgo_hash* secalgo_hash_create_sha384(void);

/**
 * Start a hash of type sha512. Allocates structure, then inits it,
 * so that a series of updates can be performed, before the final result.
 * @return hash structure.  NULL on malloc failure or no support.
 */
struct secalgo_hash* secalgo_hash_create_sha512(void);

/**
 * Update a hash with more information to add to it.
 * @param hash: the hash that is updated.
 * @param data: data to add.
 * @param len: length of data.
 * @return false on failure.
 */
int secalgo_hash_update(struct secalgo_hash* hash, uint8_t* data, size_t len);

/**
 * Get the final result of the hash.
 * @param hash: the hash that has had updates to it.
 * @param result: where to store the result.
 * @param maxlen: length of the result buffer, eg. size of the allocation.
 *	If not large enough the routine fails.
 * @param resultlen: the length of the result, returned to the caller.
 *	How much of maxlen is used.
 * @return false on failure.
 */
int secalgo_hash_final(struct secalgo_hash* hash, uint8_t* result,
	size_t maxlen, size_t* resultlen);

/**
 * Delete the hash structure.
 * @param hash: the hash to delete.
 */
void secalgo_hash_delete(struct secalgo_hash* hash);

/**
 * Return size of DS digest according to its hash algorithm.
 * @param algo: DS digest algo.
 * @return size in bytes of digest, or 0 if not supported. 
 */
size_t ds_digest_size_supported(int algo);

/**
 * @param algo: the DS digest algo
 * @param buf: the buffer to digest
 * @param len: length of buffer to digest.
 * @param res: result stored here (must have sufficient space).
 * @return false on failure.
 */
int secalgo_ds_digest(int algo, unsigned char* buf, size_t len,
	unsigned char* res);

/** return true if DNSKEY algorithm id is supported */
int dnskey_algo_id_is_supported(int id);

/**
 * Check a canonical sig+rrset and signature against a dnskey
 * @param buf: buffer with data to verify, the first rrsig part and the
 *	canonicalized rrset.
 * @param algo: DNSKEY algorithm.
 * @param sigblock: signature rdata field from RRSIG
 * @param sigblock_len: length of sigblock data.
 * @param key: public key data from DNSKEY RR.
 * @param keylen: length of keydata.
 * @param reason: bogus reason in more detail.
 * @return secure if verification succeeded, bogus on crypto failure,
 *	unchecked on format errors and alloc failures.
 */
enum sec_status verify_canonrrset(struct sldns_buffer* buf, int algo,
	unsigned char* sigblock, unsigned int sigblock_len,
	unsigned char* key, unsigned int keylen, char** reason);

#endif /* VALIDATOR_VAL_SECALGO_H */
