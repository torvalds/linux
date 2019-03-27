/*
 * Argon2 source code package
 *
 * Written by Daniel Dinu and Dmitry Khovratovich, 2015
 *
 * This work is licensed under a Creative Commons CC0 1.0 License/Waiver.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along
 * with
 * this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#ifdef HAVE_SYS_MMAN_H
# include <sys/mman.h>
#endif

#include "crypto_generichash_blake2b.h"
#include "private/common.h"
#include "private/implementations.h"
#include "runtime.h"
#include "utils.h"

#include "argon2-core.h"
#include "blake2b-long.h"

#if !defined(MAP_ANON) && defined(MAP_ANONYMOUS)
# define MAP_ANON MAP_ANONYMOUS
#endif
#ifndef MAP_NOCORE
# define MAP_NOCORE 0
#endif
#ifndef MAP_POPULATE
# define MAP_POPULATE 0
#endif

static fill_segment_fn fill_segment = fill_segment_ref;

static void
load_block(block *dst, const void *input)
{
    unsigned i;
    for (i = 0; i < ARGON2_QWORDS_IN_BLOCK; ++i) {
        dst->v[i] = LOAD64_LE((const uint8_t *) input + i * sizeof(dst->v[i]));
    }
}

static void
store_block(void *output, const block *src)
{
    unsigned i;
    for (i = 0; i < ARGON2_QWORDS_IN_BLOCK; ++i) {
        STORE64_LE((uint8_t *) output + i * sizeof(src->v[i]), src->v[i]);
    }
}

/***************Memory allocators*****************/
/* Allocates memory to the given pointer
 * @param memory pointer to the pointer to the memory
 * @param m_cost number of blocks to allocate in the memory
 * @return ARGON2_OK if @memory is a valid pointer and memory is allocated
 */
static int allocate_memory(block_region **region, uint32_t m_cost);

static int
allocate_memory(block_region **region, uint32_t m_cost)
{
    void * base;
    block *memory;
    size_t memory_size;

    if (region == NULL) {
        return ARGON2_MEMORY_ALLOCATION_ERROR; /* LCOV_EXCL_LINE */
    }
    memory_size = sizeof(block) * m_cost;
    if (m_cost == 0 ||
        memory_size / m_cost !=
            sizeof(block)) { /*1. Check for multiplication overflow*/
        return ARGON2_MEMORY_ALLOCATION_ERROR; /* LCOV_EXCL_LINE */
    }
    *region = (block_region *) malloc(
        sizeof(block_region)); /*2. Try to allocate region*/
    if (!*region) {
        return ARGON2_MEMORY_ALLOCATION_ERROR; /* LCOV_EXCL_LINE */
    }
    (*region)->base = (*region)->memory = NULL;

#if defined(MAP_ANON) && defined(HAVE_MMAP)
    if ((base = mmap(NULL, memory_size, PROT_READ | PROT_WRITE,
                     MAP_ANON | MAP_PRIVATE | MAP_NOCORE | MAP_POPULATE,
                     -1, 0)) == MAP_FAILED) {
        base = NULL; /* LCOV_EXCL_LINE */
    }                /* LCOV_EXCL_LINE */
    memcpy(&memory, &base, sizeof memory);
#elif defined(HAVE_POSIX_MEMALIGN)
    if ((errno = posix_memalign((void **) &base, 64, memory_size)) != 0) {
        base = NULL;
    }
    memcpy(&memory, &base, sizeof memory);
#else
    memory = NULL;
    if (memory_size + 63 < memory_size) {
        base  = NULL;
        errno = ENOMEM;
    } else if ((base = malloc(memory_size + 63)) != NULL) {
        uint8_t *aligned = ((uint8_t *) base) + 63;
        aligned -= (uintptr_t) aligned & 63;
        memcpy(&memory, &aligned, sizeof memory);
    }
#endif
    if (base == NULL) {
        return ARGON2_MEMORY_ALLOCATION_ERROR; /* LCOV_EXCL_LINE */
    }
    (*region)->base   = base;
    (*region)->memory = memory;
    (*region)->size   = memory_size;

    return ARGON2_OK;
}

/*********Memory functions*/

/* Clears memory
 * @param instance pointer to the current instance
 * @param clear_memory indicates if we clear the memory with zeros.
 */
static void clear_memory(argon2_instance_t *instance, int clear);

static void
clear_memory(argon2_instance_t *instance, int clear)
{
    /* LCOV_EXCL_START */
    if (clear) {
        if (instance->region != NULL) {
            sodium_memzero(instance->region->memory,
                           sizeof(block) * instance->memory_blocks);
        }
        if (instance->pseudo_rands != NULL) {
            sodium_memzero(instance->pseudo_rands,
                           sizeof(uint64_t) * instance->segment_length);
        }
    }
    /* LCOV_EXCL_STOP */
}

/* Deallocates memory
 * @param memory pointer to the blocks
 */
static void free_memory(block_region *region);

static void
free_memory(block_region *region)
{
    if (region && region->base) {
#if defined(MAP_ANON) && defined(HAVE_MMAP)
        if (munmap(region->base, region->size)) {
            return; /* LCOV_EXCL_LINE */
        }
#else
        free(region->base);
#endif
    }
    free(region);
}

void
free_instance(argon2_instance_t *instance, int flags)
{
    /* Clear memory */
    clear_memory(instance, flags & ARGON2_FLAG_CLEAR_MEMORY);

    /* Deallocate the memory */
    free(instance->pseudo_rands);
    instance->pseudo_rands = NULL;
    free_memory(instance->region);
    instance->region = NULL;
}

void
finalize(const argon2_context *context, argon2_instance_t *instance)
{
    if (context != NULL && instance != NULL) {
        block    blockhash;
        uint32_t l;

        copy_block(&blockhash,
                   instance->region->memory + instance->lane_length - 1);

        /* XOR the last blocks */
        for (l = 1; l < instance->lanes; ++l) {
            uint32_t last_block_in_lane =
                l * instance->lane_length + (instance->lane_length - 1);
            xor_block(&blockhash,
                      instance->region->memory + last_block_in_lane);
        }

        /* Hash the result */
        {
            uint8_t blockhash_bytes[ARGON2_BLOCK_SIZE];
            store_block(blockhash_bytes, &blockhash);
            blake2b_long(context->out, context->outlen, blockhash_bytes,
                         ARGON2_BLOCK_SIZE);
            sodium_memzero(blockhash.v,
                           ARGON2_BLOCK_SIZE); /* clear blockhash */
            sodium_memzero(blockhash_bytes,
                           ARGON2_BLOCK_SIZE); /* clear blockhash_bytes */
        }

        free_instance(instance, context->flags);
    }
}

void
fill_memory_blocks(argon2_instance_t *instance, uint32_t pass)
{
    argon2_position_t position;
    uint32_t l;
    uint32_t s;

    if (instance == NULL || instance->lanes == 0) {
        return; /* LCOV_EXCL_LINE */
    }

    position.pass = pass;
    for (s = 0; s < ARGON2_SYNC_POINTS; ++s) {
        position.slice = (uint8_t) s;
        for (l = 0; l < instance->lanes; ++l) {
            position.lane  = l;
            position.index = 0;
            fill_segment(instance, position);
        }
    }
}

int
validate_inputs(const argon2_context *context)
{
    /* LCOV_EXCL_START */
    if (NULL == context) {
        return ARGON2_INCORRECT_PARAMETER;
    }

    if (NULL == context->out) {
        return ARGON2_OUTPUT_PTR_NULL;
    }

    /* Validate output length */
    if (ARGON2_MIN_OUTLEN > context->outlen) {
        return ARGON2_OUTPUT_TOO_SHORT;
    }

    if (ARGON2_MAX_OUTLEN < context->outlen) {
        return ARGON2_OUTPUT_TOO_LONG;
    }

    /* Validate password (required param) */
    if (NULL == context->pwd) {
        if (0 != context->pwdlen) {
            return ARGON2_PWD_PTR_MISMATCH;
        }
    }

    if (ARGON2_MIN_PWD_LENGTH > context->pwdlen) {
        return ARGON2_PWD_TOO_SHORT;
    }

    if (ARGON2_MAX_PWD_LENGTH < context->pwdlen) {
        return ARGON2_PWD_TOO_LONG;
    }

    /* Validate salt (required param) */
    if (NULL == context->salt) {
        if (0 != context->saltlen) {
            return ARGON2_SALT_PTR_MISMATCH;
        }
    }

    if (ARGON2_MIN_SALT_LENGTH > context->saltlen) {
        return ARGON2_SALT_TOO_SHORT;
    }

    if (ARGON2_MAX_SALT_LENGTH < context->saltlen) {
        return ARGON2_SALT_TOO_LONG;
    }

    /* Validate secret (optional param) */
    if (NULL == context->secret) {
        if (0 != context->secretlen) {
            return ARGON2_SECRET_PTR_MISMATCH;
        }
    } else {
        if (ARGON2_MIN_SECRET > context->secretlen) {
            return ARGON2_SECRET_TOO_SHORT;
        }

        if (ARGON2_MAX_SECRET < context->secretlen) {
            return ARGON2_SECRET_TOO_LONG;
        }
    }

    /* Validate associated data (optional param) */
    if (NULL == context->ad) {
        if (0 != context->adlen) {
            return ARGON2_AD_PTR_MISMATCH;
        }
    } else {
        if (ARGON2_MIN_AD_LENGTH > context->adlen) {
            return ARGON2_AD_TOO_SHORT;
        }

        if (ARGON2_MAX_AD_LENGTH < context->adlen) {
            return ARGON2_AD_TOO_LONG;
        }
    }

    /* Validate memory cost */
    if (ARGON2_MIN_MEMORY > context->m_cost) {
        return ARGON2_MEMORY_TOO_LITTLE;
    }

    if (ARGON2_MAX_MEMORY < context->m_cost) {
        return ARGON2_MEMORY_TOO_MUCH;
    }

    if (context->m_cost < 8 * context->lanes) {
        return ARGON2_MEMORY_TOO_LITTLE;
    }

    /* Validate time cost */
    if (ARGON2_MIN_TIME > context->t_cost) {
        return ARGON2_TIME_TOO_SMALL;
    }

    if (ARGON2_MAX_TIME < context->t_cost) {
        return ARGON2_TIME_TOO_LARGE;
    }

    /* Validate lanes */
    if (ARGON2_MIN_LANES > context->lanes) {
        return ARGON2_LANES_TOO_FEW;
    }

    if (ARGON2_MAX_LANES < context->lanes) {
        return ARGON2_LANES_TOO_MANY;
    }

    /* Validate threads */
    if (ARGON2_MIN_THREADS > context->threads) {
        return ARGON2_THREADS_TOO_FEW;
    }

    if (ARGON2_MAX_THREADS < context->threads) {
        return ARGON2_THREADS_TOO_MANY;
    }
    /* LCOV_EXCL_STOP */

    return ARGON2_OK;
}

void
fill_first_blocks(uint8_t *blockhash, const argon2_instance_t *instance)
{
    uint32_t l;
    /* Make the first and second block in each lane as G(H0||i||0) or
       G(H0||i||1) */
    uint8_t blockhash_bytes[ARGON2_BLOCK_SIZE];
    for (l = 0; l < instance->lanes; ++l) {
        STORE32_LE(blockhash + ARGON2_PREHASH_DIGEST_LENGTH, 0);
        STORE32_LE(blockhash + ARGON2_PREHASH_DIGEST_LENGTH + 4, l);
        blake2b_long(blockhash_bytes, ARGON2_BLOCK_SIZE, blockhash,
                     ARGON2_PREHASH_SEED_LENGTH);
        load_block(&instance->region->memory[l * instance->lane_length + 0],
                   blockhash_bytes);

        STORE32_LE(blockhash + ARGON2_PREHASH_DIGEST_LENGTH, 1);
        blake2b_long(blockhash_bytes, ARGON2_BLOCK_SIZE, blockhash,
                     ARGON2_PREHASH_SEED_LENGTH);
        load_block(&instance->region->memory[l * instance->lane_length + 1],
                   blockhash_bytes);
    }
    sodium_memzero(blockhash_bytes, ARGON2_BLOCK_SIZE);
}

void
initial_hash(uint8_t *blockhash, argon2_context *context, argon2_type type)
{
    crypto_generichash_blake2b_state BlakeHash;
    uint8_t                          value[4U /* sizeof(uint32_t) */];

    if (NULL == context || NULL == blockhash) {
        return; /* LCOV_EXCL_LINE */
    }

    crypto_generichash_blake2b_init(&BlakeHash, NULL, 0U,
                                    ARGON2_PREHASH_DIGEST_LENGTH);

    STORE32_LE(value, context->lanes);
    crypto_generichash_blake2b_update(&BlakeHash, value, sizeof(value));

    STORE32_LE(value, context->outlen);
    crypto_generichash_blake2b_update(&BlakeHash, value, sizeof(value));

    STORE32_LE(value, context->m_cost);
    crypto_generichash_blake2b_update(&BlakeHash, value, sizeof(value));

    STORE32_LE(value, context->t_cost);
    crypto_generichash_blake2b_update(&BlakeHash, value, sizeof(value));

    STORE32_LE(value, ARGON2_VERSION_NUMBER);
    crypto_generichash_blake2b_update(&BlakeHash, value, sizeof(value));

    STORE32_LE(value, (uint32_t) type);
    crypto_generichash_blake2b_update(&BlakeHash, value, sizeof(value));

    STORE32_LE(value, context->pwdlen);
    crypto_generichash_blake2b_update(&BlakeHash, value, sizeof(value));

    if (context->pwd != NULL) {
        crypto_generichash_blake2b_update(
            &BlakeHash, (const uint8_t *) context->pwd, context->pwdlen);

        /* LCOV_EXCL_START */
        if (context->flags & ARGON2_FLAG_CLEAR_PASSWORD) {
            sodium_memzero(context->pwd, context->pwdlen);
            context->pwdlen = 0;
        }
        /* LCOV_EXCL_STOP */
    }

    STORE32_LE(value, context->saltlen);
    crypto_generichash_blake2b_update(&BlakeHash, value, sizeof(value));

    if (context->salt != NULL) {
        crypto_generichash_blake2b_update(
            &BlakeHash, (const uint8_t *) context->salt, context->saltlen);
    }

    STORE32_LE(value, context->secretlen);
    crypto_generichash_blake2b_update(&BlakeHash, value, sizeof(value));

    /* LCOV_EXCL_START */
    if (context->secret != NULL) {
        crypto_generichash_blake2b_update(
            &BlakeHash, (const uint8_t *) context->secret, context->secretlen);

        if (context->flags & ARGON2_FLAG_CLEAR_SECRET) {
            sodium_memzero(context->secret, context->secretlen);
            context->secretlen = 0;
        }
    }
    /* LCOV_EXCL_STOP */

    STORE32_LE(value, context->adlen);
    crypto_generichash_blake2b_update(&BlakeHash, value, sizeof(value));

    /* LCOV_EXCL_START */
    if (context->ad != NULL) {
        crypto_generichash_blake2b_update(
            &BlakeHash, (const uint8_t *) context->ad, context->adlen);
    }
    /* LCOV_EXCL_STOP */

    crypto_generichash_blake2b_final(&BlakeHash, blockhash,
                                     ARGON2_PREHASH_DIGEST_LENGTH);
}

int
initialize(argon2_instance_t *instance, argon2_context *context)
{
    uint8_t blockhash[ARGON2_PREHASH_SEED_LENGTH];
    int     result = ARGON2_OK;

    if (instance == NULL || context == NULL) {
        return ARGON2_INCORRECT_PARAMETER;
    }

    /* 1. Memory allocation */

    if ((instance->pseudo_rands = (uint64_t *)
         malloc(sizeof(uint64_t) * instance->segment_length)) == NULL) {
        return ARGON2_MEMORY_ALLOCATION_ERROR;
    }

    result = allocate_memory(&(instance->region), instance->memory_blocks);
    if (ARGON2_OK != result) {
        free_instance(instance, context->flags);
        return result;
    }

    /* 2. Initial hashing */
    /* H_0 + 8 extra bytes to produce the first blocks */
    /* uint8_t blockhash[ARGON2_PREHASH_SEED_LENGTH]; */
    /* Hashing all inputs */
    initial_hash(blockhash, context, instance->type);
    /* Zeroing 8 extra bytes */
    sodium_memzero(blockhash + ARGON2_PREHASH_DIGEST_LENGTH,
                   ARGON2_PREHASH_SEED_LENGTH - ARGON2_PREHASH_DIGEST_LENGTH);

    /* 3. Creating first blocks, we always have at least two blocks in a slice
     */
    fill_first_blocks(blockhash, instance);
    /* Clearing the hash */
    sodium_memzero(blockhash, ARGON2_PREHASH_SEED_LENGTH);

    return ARGON2_OK;
}

int
argon2_pick_best_implementation(void)
{
/* LCOV_EXCL_START */
#if defined(HAVE_AVX512FINTRIN_H) && defined(HAVE_AVX2INTRIN_H) && \
    defined(HAVE_TMMINTRIN_H) && defined(HAVE_SMMINTRIN_H)
    if (sodium_runtime_has_avx512f()) {
        fill_segment = fill_segment_avx512f;
        return 0;
    }
#endif
#if defined(HAVE_AVX2INTRIN_H) && defined(HAVE_TMMINTRIN_H) && \
    defined(HAVE_SMMINTRIN_H)
    if (sodium_runtime_has_avx2()) {
        fill_segment = fill_segment_avx2;
        return 0;
    }
#endif
#if defined(HAVE_EMMINTRIN_H) && defined(HAVE_TMMINTRIN_H)
    if (sodium_runtime_has_ssse3()) {
        fill_segment = fill_segment_ssse3;
        return 0;
    }
#endif
    fill_segment = fill_segment_ref;

    return 0;
    /* LCOV_EXCL_STOP */
}

int
_crypto_pwhash_argon2_pick_best_implementation(void)
{
    return argon2_pick_best_implementation();
}
