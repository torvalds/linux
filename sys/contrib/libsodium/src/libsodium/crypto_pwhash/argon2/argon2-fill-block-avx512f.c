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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "argon2-core.h"
#include "argon2.h"
#include "private/common.h"
#include "private/sse2_64_32.h"

#if defined(HAVE_AVX512FINTRIN_H) && defined(HAVE_AVX2INTRIN_H) && \
    defined(HAVE_EMMINTRIN_H) &&  defined(HAVE_TMMINTRIN_H) && defined(HAVE_SMMINTRIN_H)

# ifdef __GNUC__
#  pragma GCC target("sse2")
#  pragma GCC target("ssse3")
#  pragma GCC target("sse4.1")
#  pragma GCC target("avx2")
#  pragma GCC target("avx512f")
# endif

# ifdef _MSC_VER
#  include <intrin.h> /* for _mm_set_epi64x */
# endif
#include <emmintrin.h>
#include <immintrin.h>
#include <smmintrin.h>
#include <tmmintrin.h>

# include "blamka-round-avx512f.h"

static void
fill_block(__m512i *state, const uint8_t *ref_block, uint8_t *next_block)
{
    __m512i  block_XY[ARGON2_512BIT_WORDS_IN_BLOCK];
    uint32_t i;

    for (i = 0; i < ARGON2_512BIT_WORDS_IN_BLOCK; i++) {
        block_XY[i] = state[i] = _mm512_xor_si512(
            state[i], _mm512_loadu_si512((__m512i const *) (&ref_block[64 * i])));
    }

    for (i = 0; i < 2; ++i) {
        BLAKE2_ROUND_1(
            state[8 * i + 0], state[8 * i + 1], state[8 * i + 2], state[8 * i + 3],
            state[8 * i + 4], state[8 * i + 5], state[8 * i + 6], state[8 * i + 7]);
    }

    for (i = 0; i < 2; ++i) {
        BLAKE2_ROUND_2(
            state[2 * 0 + i], state[2 * 1 + i], state[2 * 2 + i], state[2 * 3 + i],
            state[2 * 4 + i], state[2 * 5 + i], state[2 * 6 + i], state[2 * 7 + i]);
    }

    for (i = 0; i < ARGON2_512BIT_WORDS_IN_BLOCK; i++) {
        state[i] = _mm512_xor_si512(state[i], block_XY[i]);
        _mm512_storeu_si512((__m512i *) (&next_block[64 * i]), state[i]);
    }
}

static void
fill_block_with_xor(__m512i *state, const uint8_t *ref_block,
                    uint8_t *next_block)
{
    __m512i  block_XY[ARGON2_512BIT_WORDS_IN_BLOCK];
    uint32_t i;

    for (i = 0; i < ARGON2_512BIT_WORDS_IN_BLOCK; i++) {
        state[i] = _mm512_xor_si512(
            state[i], _mm512_loadu_si512((__m512i const *) (&ref_block[64 * i])));
        block_XY[i] = _mm512_xor_si512(
            state[i], _mm512_loadu_si512((__m512i const *) (&next_block[64 * i])));
    }

    for (i = 0; i < 2; ++i) {
        BLAKE2_ROUND_1(
            state[8 * i + 0], state[8 * i + 1], state[8 * i + 2], state[8 * i + 3],
            state[8 * i + 4], state[8 * i + 5], state[8 * i + 6], state[8 * i + 7]);
    }

    for (i = 0; i < 2; ++i) {
        BLAKE2_ROUND_2(
            state[2 * 0 + i], state[2 * 1 + i], state[2 * 2 + i], state[2 * 3 + i],
            state[2 * 4 + i], state[2 * 5 + i], state[2 * 6 + i], state[2 * 7 + i]);
    }

    for (i = 0; i < ARGON2_512BIT_WORDS_IN_BLOCK; i++) {
        state[i] = _mm512_xor_si512(state[i], block_XY[i]);
        _mm512_storeu_si512((__m512i *) (&next_block[64 * i]), state[i]);
    }
}

static void
generate_addresses(const argon2_instance_t *instance,
                   const argon2_position_t *position, uint64_t *pseudo_rands)
{
    block    address_block, input_block, tmp_block;
    uint32_t i;

    init_block_value(&address_block, 0);
    init_block_value(&input_block, 0);

    if (instance != NULL && position != NULL) {
        input_block.v[0] = position->pass;
        input_block.v[1] = position->lane;
        input_block.v[2] = position->slice;
        input_block.v[3] = instance->memory_blocks;
        input_block.v[4] = instance->passes;
        input_block.v[5] = instance->type;

        for (i = 0; i < instance->segment_length; ++i) {
            if (i % ARGON2_ADDRESSES_IN_BLOCK == 0) {
                /* Temporary zero-initialized blocks */
                __m512i zero_block[ARGON2_512BIT_WORDS_IN_BLOCK];
                __m512i zero2_block[ARGON2_512BIT_WORDS_IN_BLOCK];

                memset(zero_block, 0, sizeof(zero_block));
                memset(zero2_block, 0, sizeof(zero2_block));
                init_block_value(&address_block, 0);
                init_block_value(&tmp_block, 0);
                /* Increasing index counter */
                input_block.v[6]++;
                /* First iteration of G */
                fill_block_with_xor(zero_block, (uint8_t *) &input_block.v,
                                    (uint8_t *) &tmp_block.v);
                /* Second iteration of G */
                fill_block_with_xor(zero2_block, (uint8_t *) &tmp_block.v,
                                    (uint8_t *) &address_block.v);
            }

            pseudo_rands[i] = address_block.v[i % ARGON2_ADDRESSES_IN_BLOCK];
        }
    }
}

void
fill_segment_avx512f(const argon2_instance_t *instance,
                     argon2_position_t        position)
{
    block    *ref_block = NULL, *curr_block = NULL;
    uint64_t  pseudo_rand, ref_index, ref_lane;
    uint32_t  prev_offset, curr_offset;
    uint32_t  starting_index, i;
    __m512i   state[ARGON2_512BIT_WORDS_IN_BLOCK];
    int       data_independent_addressing = 1;

    /* Pseudo-random values that determine the reference block position */
    uint64_t *pseudo_rands = NULL;

    if (instance == NULL) {
        return;
    }

    if (instance->type == Argon2_id &&
        (position.pass != 0 || position.slice >= ARGON2_SYNC_POINTS / 2)) {
        data_independent_addressing = 0;
    }

    pseudo_rands = instance->pseudo_rands;

    if (data_independent_addressing) {
        generate_addresses(instance, &position, pseudo_rands);
    }

    starting_index = 0;

    if ((0 == position.pass) && (0 == position.slice)) {
        starting_index = 2; /* we have already generated the first two blocks */
    }

    /* Offset of the current block */
    curr_offset = position.lane * instance->lane_length +
                  position.slice * instance->segment_length + starting_index;

    if (0 == curr_offset % instance->lane_length) {
        /* Last block in this lane */
        prev_offset = curr_offset + instance->lane_length - 1;
    } else {
        /* Previous block */
        prev_offset = curr_offset - 1;
    }

    memcpy(state, ((instance->region->memory + prev_offset)->v),
           ARGON2_BLOCK_SIZE);

    for (i = starting_index; i < instance->segment_length;
         ++i, ++curr_offset, ++prev_offset) {
        /*1.1 Rotating prev_offset if needed */
        if (curr_offset % instance->lane_length == 1) {
            prev_offset = curr_offset - 1;
        }

        /* 1.2 Computing the index of the reference block */
        /* 1.2.1 Taking pseudo-random value from the previous block */
        if (data_independent_addressing) {
#pragma warning(push)
#pragma warning(disable : 6385)
            pseudo_rand = pseudo_rands[i];
#pragma warning(pop)
        } else {
            pseudo_rand = instance->region->memory[prev_offset].v[0];
        }

        /* 1.2.2 Computing the lane of the reference block */
        ref_lane = ((pseudo_rand >> 32)) % instance->lanes;

        if ((position.pass == 0) && (position.slice == 0)) {
            /* Can not reference other lanes yet */
            ref_lane = position.lane;
        }

        /* 1.2.3 Computing the number of possible reference block within the
         * lane.
         */
        position.index = i;
        ref_index = index_alpha(instance, &position, pseudo_rand & 0xFFFFFFFF,
                                ref_lane == position.lane);

        /* 2 Creating a new block */
        ref_block = instance->region->memory +
                    instance->lane_length * ref_lane + ref_index;
        curr_block = instance->region->memory + curr_offset;
        if (position.pass != 0) {
            fill_block_with_xor(state, (uint8_t *) ref_block->v,
                                (uint8_t *) curr_block->v);
        } else {
            fill_block(state, (uint8_t *) ref_block->v,
                       (uint8_t *) curr_block->v);
        }
    }
}
#endif
