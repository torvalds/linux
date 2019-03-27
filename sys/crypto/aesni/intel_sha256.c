/*******************************************************************************
* Copyright (c) 2013, Intel Corporation 
* 
* All rights reserved. 
* 
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met: 
* 
* * Redistributions of source code must retain the above copyright
*   notice, this list of conditions and the following disclaimer.  
* 
* * Redistributions in binary form must reproduce the above copyright
*   notice, this list of conditions and the following disclaimer in the
*   documentation and/or other materials provided with the
*   distribution. 
* 
* * Neither the name of the Intel Corporation nor the names of its
*   contributors may be used to endorse or promote products derived from
*   this software without specific prior written permission. 
* 
* 
* THIS SOFTWARE IS PROVIDED BY INTEL CORPORATION ""AS IS"" AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
* PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL CORPORATION OR
* CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
* EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
* PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
********************************************************************************
*
* Intel SHA Extensions optimized implementation of a SHA-256 update function 
*
* The function takes a pointer to the current hash values, a pointer to the 
* input data, and a number of 64 byte blocks to process.  Once all blocks have 
* been processed, the digest pointer is  updated with the resulting hash value.
* The function only processes complete blocks, there is no functionality to 
* store partial blocks.  All message padding and hash value initialization must
* be done outside the update function.  
*
* The indented lines in the loop are instructions related to rounds processing.
* The non-indented lines are instructions related to the message schedule.
*
* Author: Sean Gulley <sean.m.gulley@intel.com>
* Date:   July 2013
*
********************************************************************************
*
* Example complier command line:
* icc intel_sha_extensions_sha256_intrinsic.c
* gcc -msha -msse4 intel_sha_extensions_sha256_intrinsic.c
*
*******************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <crypto/aesni/aesni_os.h>
#include <crypto/aesni/sha_sse.h>

#include <immintrin.h>

void intel_sha256_step(uint32_t *digest, const char *data, uint32_t num_blks) {
   __m128i state0, state1;
   __m128i msg;
   __m128i msgtmp0, msgtmp1, msgtmp2, msgtmp3;
   __m128i tmp;
   __m128i shuf_mask;
   __m128i abef_save, cdgh_save;

   // Load initial hash values
   // Need to reorder these appropriately
   // DCBA, HGFE -> ABEF, CDGH
   tmp    = _mm_loadu_si128((__m128i*) digest);
   state1 = _mm_loadu_si128((__m128i*) (digest+4));

   tmp    = _mm_shuffle_epi32(tmp, 0xB1);       // CDAB
   state1 = _mm_shuffle_epi32(state1, 0x1B);    // EFGH
   state0 = _mm_alignr_epi8(tmp, state1, 8);    // ABEF
   state1 = _mm_blend_epi16(state1, tmp, 0xF0); // CDGH

   shuf_mask = _mm_set_epi64x(0x0c0d0e0f08090a0bull, 0x0405060700010203ull);

   while (num_blks > 0) {
      // Save hash values for addition after rounds
      abef_save = state0;
      cdgh_save = state1;

      // Rounds 0-3
      msg     = _mm_loadu_si128((const __m128i*) data);
      msgtmp0 = _mm_shuffle_epi8(msg, shuf_mask);
         msg    = _mm_add_epi32(msgtmp0, 
                  _mm_set_epi64x(0xE9B5DBA5B5C0FBCFull, 0x71374491428A2F98ull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);

      // Rounds 4-7
      msgtmp1 = _mm_loadu_si128((const __m128i*) (data+16));
      msgtmp1 = _mm_shuffle_epi8(msgtmp1, shuf_mask);
         msg    = _mm_add_epi32(msgtmp1, 
                  _mm_set_epi64x(0xAB1C5ED5923F82A4ull, 0x59F111F13956C25Bull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
      msgtmp0 = _mm_sha256msg1_epu32(msgtmp0, msgtmp1);

      // Rounds 8-11
      msgtmp2 = _mm_loadu_si128((const __m128i*) (data+32));
      msgtmp2 = _mm_shuffle_epi8(msgtmp2, shuf_mask);
         msg    = _mm_add_epi32(msgtmp2, 
                  _mm_set_epi64x(0x550C7DC3243185BEull, 0x12835B01D807AA98ull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
      msgtmp1 = _mm_sha256msg1_epu32(msgtmp1, msgtmp2);

      // Rounds 12-15
      msgtmp3 = _mm_loadu_si128((const __m128i*) (data+48));
      msgtmp3 = _mm_shuffle_epi8(msgtmp3, shuf_mask);
         msg    = _mm_add_epi32(msgtmp3, 
                  _mm_set_epi64x(0xC19BF1749BDC06A7ull, 0x80DEB1FE72BE5D74ull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
      tmp     = _mm_alignr_epi8(msgtmp3, msgtmp2, 4);
      msgtmp0 = _mm_add_epi32(msgtmp0, tmp);
      msgtmp0 = _mm_sha256msg2_epu32(msgtmp0, msgtmp3);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
      msgtmp2 = _mm_sha256msg1_epu32(msgtmp2, msgtmp3);

      // Rounds 16-19
         msg    = _mm_add_epi32(msgtmp0, 
                  _mm_set_epi64x(0x240CA1CC0FC19DC6ull, 0xEFBE4786E49B69C1ull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
      tmp     = _mm_alignr_epi8(msgtmp0, msgtmp3, 4);
      msgtmp1 = _mm_add_epi32(msgtmp1, tmp);
      msgtmp1 = _mm_sha256msg2_epu32(msgtmp1, msgtmp0);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
      msgtmp3 = _mm_sha256msg1_epu32(msgtmp3, msgtmp0);

      // Rounds 20-23
         msg    = _mm_add_epi32(msgtmp1, 
                  _mm_set_epi64x(0x76F988DA5CB0A9DCull, 0x4A7484AA2DE92C6Full));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
      tmp     = _mm_alignr_epi8(msgtmp1, msgtmp0, 4);
      msgtmp2 = _mm_add_epi32(msgtmp2, tmp);
      msgtmp2 = _mm_sha256msg2_epu32(msgtmp2, msgtmp1);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
      msgtmp0 = _mm_sha256msg1_epu32(msgtmp0, msgtmp1);

      // Rounds 24-27
         msg    = _mm_add_epi32(msgtmp2, 
                  _mm_set_epi64x(0xBF597FC7B00327C8ull, 0xA831C66D983E5152ull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
      tmp     = _mm_alignr_epi8(msgtmp2, msgtmp1, 4);
      msgtmp3 = _mm_add_epi32(msgtmp3, tmp);
      msgtmp3 = _mm_sha256msg2_epu32(msgtmp3, msgtmp2);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
      msgtmp1 = _mm_sha256msg1_epu32(msgtmp1, msgtmp2);

      // Rounds 28-31
         msg    = _mm_add_epi32(msgtmp3, 
                  _mm_set_epi64x(0x1429296706CA6351ull, 0xD5A79147C6E00BF3ull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
      tmp     = _mm_alignr_epi8(msgtmp3, msgtmp2, 4);
      msgtmp0 = _mm_add_epi32(msgtmp0, tmp);
      msgtmp0 = _mm_sha256msg2_epu32(msgtmp0, msgtmp3);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
      msgtmp2 = _mm_sha256msg1_epu32(msgtmp2, msgtmp3);

      // Rounds 32-35
         msg    = _mm_add_epi32(msgtmp0, 
                  _mm_set_epi64x(0x53380D134D2C6DFCull, 0x2E1B213827B70A85ull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
      tmp     = _mm_alignr_epi8(msgtmp0, msgtmp3, 4);
      msgtmp1 = _mm_add_epi32(msgtmp1, tmp);
      msgtmp1 = _mm_sha256msg2_epu32(msgtmp1, msgtmp0);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
      msgtmp3 = _mm_sha256msg1_epu32(msgtmp3, msgtmp0);

      // Rounds 36-39
         msg    = _mm_add_epi32(msgtmp1, 
                  _mm_set_epi64x(0x92722C8581C2C92Eull, 0x766A0ABB650A7354ull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
      tmp     = _mm_alignr_epi8(msgtmp1, msgtmp0, 4);
      msgtmp2 = _mm_add_epi32(msgtmp2, tmp);
      msgtmp2 = _mm_sha256msg2_epu32(msgtmp2, msgtmp1);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
      msgtmp0 = _mm_sha256msg1_epu32(msgtmp0, msgtmp1);

      // Rounds 40-43
         msg    = _mm_add_epi32(msgtmp2, 
                  _mm_set_epi64x(0xC76C51A3C24B8B70ull, 0xA81A664BA2BFE8A1ull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
      tmp     = _mm_alignr_epi8(msgtmp2, msgtmp1, 4);
      msgtmp3 = _mm_add_epi32(msgtmp3, tmp);
      msgtmp3 = _mm_sha256msg2_epu32(msgtmp3, msgtmp2);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
      msgtmp1 = _mm_sha256msg1_epu32(msgtmp1, msgtmp2);

      // Rounds 44-47
         msg    = _mm_add_epi32(msgtmp3, 
                  _mm_set_epi64x(0x106AA070F40E3585ull, 0xD6990624D192E819ull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
      tmp     = _mm_alignr_epi8(msgtmp3, msgtmp2, 4);
      msgtmp0 = _mm_add_epi32(msgtmp0, tmp);
      msgtmp0 = _mm_sha256msg2_epu32(msgtmp0, msgtmp3);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
      msgtmp2 = _mm_sha256msg1_epu32(msgtmp2, msgtmp3);

      // Rounds 48-51
         msg    = _mm_add_epi32(msgtmp0, 
                  _mm_set_epi64x(0x34B0BCB52748774Cull, 0x1E376C0819A4C116ull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
      tmp     = _mm_alignr_epi8(msgtmp0, msgtmp3, 4);
      msgtmp1 = _mm_add_epi32(msgtmp1, tmp);
      msgtmp1 = _mm_sha256msg2_epu32(msgtmp1, msgtmp0);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
      msgtmp3 = _mm_sha256msg1_epu32(msgtmp3, msgtmp0);

      // Rounds 52-55
         msg    = _mm_add_epi32(msgtmp1, 
                  _mm_set_epi64x(0x682E6FF35B9CCA4Full, 0x4ED8AA4A391C0CB3ull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
      tmp     = _mm_alignr_epi8(msgtmp1, msgtmp0, 4);
      msgtmp2 = _mm_add_epi32(msgtmp2, tmp);
      msgtmp2 = _mm_sha256msg2_epu32(msgtmp2, msgtmp1);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);

      // Rounds 56-59
         msg    = _mm_add_epi32(msgtmp2, 
                  _mm_set_epi64x(0x8CC7020884C87814ull, 0x78A5636F748F82EEull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
      tmp     = _mm_alignr_epi8(msgtmp2, msgtmp1, 4);
      msgtmp3 = _mm_add_epi32(msgtmp3, tmp);
      msgtmp3 = _mm_sha256msg2_epu32(msgtmp3, msgtmp2);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);

      // Rounds 60-63
         msg    = _mm_add_epi32(msgtmp3, 
                  _mm_set_epi64x(0xC67178F2BEF9A3F7ull, 0xA4506CEB90BEFFFAull));
         state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
         msg    = _mm_shuffle_epi32(msg, 0x0E);
         state0 = _mm_sha256rnds2_epu32(state0, state1, msg);

      // Add current hash values with previously saved
      state0 = _mm_add_epi32(state0, abef_save);
      state1 = _mm_add_epi32(state1, cdgh_save);

      data += 64;
      num_blks--;
   }

   // Write hash values back in the correct order
   tmp    = _mm_shuffle_epi32(state0, 0x1B);    // FEBA
   state1 = _mm_shuffle_epi32(state1, 0xB1);    // DCHG
   state0 = _mm_blend_epi16(tmp, state1, 0xF0); // DCBA
   state1 = _mm_alignr_epi8(state1, tmp, 8);    // ABEF

   _mm_store_si128((__m128i*) digest, state0);
   _mm_store_si128((__m128i*) (digest+4), state1);
}

