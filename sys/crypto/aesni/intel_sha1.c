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
* Intel SHA Extensions optimized implementation of a SHA-1 update function 
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
* icc intel_sha_extensions_sha1_intrinsic.c
* gcc -msha -msse4 intel_sha_extensions_sha1_intrinsic.c
*
*******************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <crypto/aesni/aesni_os.h>
#include <crypto/aesni/sha_sse.h>

#include <immintrin.h>

void intel_sha1_step(uint32_t *digest, const char *data, uint32_t num_blks) {
   __m128i abcd, e0, e1;
   __m128i abcd_save, e_save;
   __m128i msg0, msg1, msg2, msg3;
   __m128i shuf_mask, e_mask;

#if 0
   e_mask    = _mm_set_epi64x(0xFFFFFFFF00000000ull, 0x0000000000000000ull);
#else
   (void)e_mask;
   e0        = _mm_set_epi64x(0, 0);
#endif
   shuf_mask = _mm_set_epi64x(0x0001020304050607ull, 0x08090a0b0c0d0e0full);

   // Load initial hash values
   abcd      = _mm_loadu_si128((__m128i*) digest);
   e0        = _mm_insert_epi32(e0, *(digest+4), 3);
   abcd      = _mm_shuffle_epi32(abcd, 0x1B);
#if 0
   e0        = _mm_and_si128(e0, e_mask);
#endif

   while (num_blks > 0) {
      // Save hash values for addition after rounds
      abcd_save = abcd;
      e_save    = e0;

      // Rounds 0-3
      msg0 = _mm_loadu_si128((const __m128i*) data);
      msg0 = _mm_shuffle_epi8(msg0, shuf_mask);
         e0   = _mm_add_epi32(e0, msg0);
         e1   = abcd;
         abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);

      // Rounds 4-7
      msg1 = _mm_loadu_si128((const __m128i*) (data+16));
      msg1 = _mm_shuffle_epi8(msg1, shuf_mask);
         e1   = _mm_sha1nexte_epu32(e1, msg1);
         e0   = abcd;
         abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);
      msg0 = _mm_sha1msg1_epu32(msg0, msg1);

      // Rounds 8-11
      msg2 = _mm_loadu_si128((const __m128i*) (data+32));
      msg2 = _mm_shuffle_epi8(msg2, shuf_mask);
         e0   = _mm_sha1nexte_epu32(e0, msg2);
         e1   = abcd;
         abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);
      msg1 = _mm_sha1msg1_epu32(msg1, msg2);
      msg0 = _mm_xor_si128(msg0, msg2);

      // Rounds 12-15
      msg3 = _mm_loadu_si128((const __m128i*) (data+48));
      msg3 = _mm_shuffle_epi8(msg3, shuf_mask);
         e1   = _mm_sha1nexte_epu32(e1, msg3);
         e0   = abcd;
      msg0 = _mm_sha1msg2_epu32(msg0, msg3);
         abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);
      msg2 = _mm_sha1msg1_epu32(msg2, msg3);
      msg1 = _mm_xor_si128(msg1, msg3);

      // Rounds 16-19
         e0   = _mm_sha1nexte_epu32(e0, msg0);
         e1   = abcd;
      msg1 = _mm_sha1msg2_epu32(msg1, msg0);
         abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);
      msg3 = _mm_sha1msg1_epu32(msg3, msg0);
      msg2 = _mm_xor_si128(msg2, msg0);

      // Rounds 20-23
         e1   = _mm_sha1nexte_epu32(e1, msg1);
         e0   = abcd;
      msg2 = _mm_sha1msg2_epu32(msg2, msg1);
         abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
      msg0 = _mm_sha1msg1_epu32(msg0, msg1);
      msg3 = _mm_xor_si128(msg3, msg1);
	
      // Rounds 24-27
         e0   = _mm_sha1nexte_epu32(e0, msg2);
         e1   = abcd;
      msg3 = _mm_sha1msg2_epu32(msg3, msg2);
         abcd = _mm_sha1rnds4_epu32(abcd, e0, 1);
      msg1 = _mm_sha1msg1_epu32(msg1, msg2);
      msg0 = _mm_xor_si128(msg0, msg2);

      // Rounds 28-31
         e1   = _mm_sha1nexte_epu32(e1, msg3);
         e0   = abcd;
      msg0 = _mm_sha1msg2_epu32(msg0, msg3);
         abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
      msg2 = _mm_sha1msg1_epu32(msg2, msg3);
      msg1 = _mm_xor_si128(msg1, msg3);

      // Rounds 32-35
         e0   = _mm_sha1nexte_epu32(e0, msg0);
         e1   = abcd;
      msg1 = _mm_sha1msg2_epu32(msg1, msg0);
         abcd = _mm_sha1rnds4_epu32(abcd, e0, 1);
      msg3 = _mm_sha1msg1_epu32(msg3, msg0);
      msg2 = _mm_xor_si128(msg2, msg0);

      // Rounds 36-39
         e1   = _mm_sha1nexte_epu32(e1, msg1);
         e0   = abcd;
      msg2 = _mm_sha1msg2_epu32(msg2, msg1);
         abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
      msg0 = _mm_sha1msg1_epu32(msg0, msg1);
      msg3 = _mm_xor_si128(msg3, msg1);
	
      // Rounds 40-43
         e0   = _mm_sha1nexte_epu32(e0, msg2);
         e1   = abcd;
      msg3 = _mm_sha1msg2_epu32(msg3, msg2);
         abcd = _mm_sha1rnds4_epu32(abcd, e0, 2);
      msg1 = _mm_sha1msg1_epu32(msg1, msg2);
      msg0 = _mm_xor_si128(msg0, msg2);

      // Rounds 44-47
         e1   = _mm_sha1nexte_epu32(e1, msg3);
         e0   = abcd;
      msg0 = _mm_sha1msg2_epu32(msg0, msg3);
         abcd = _mm_sha1rnds4_epu32(abcd, e1, 2);
      msg2 = _mm_sha1msg1_epu32(msg2, msg3);
      msg1 = _mm_xor_si128(msg1, msg3);

      // Rounds 48-51
         e0   = _mm_sha1nexte_epu32(e0, msg0);
         e1   = abcd;
      msg1 = _mm_sha1msg2_epu32(msg1, msg0);
         abcd = _mm_sha1rnds4_epu32(abcd, e0, 2);
      msg3 = _mm_sha1msg1_epu32(msg3, msg0);
      msg2 = _mm_xor_si128(msg2, msg0);

      // Rounds 52-55
         e1   = _mm_sha1nexte_epu32(e1, msg1);
         e0   = abcd;
      msg2 = _mm_sha1msg2_epu32(msg2, msg1);
         abcd = _mm_sha1rnds4_epu32(abcd, e1, 2);
      msg0 = _mm_sha1msg1_epu32(msg0, msg1);
      msg3 = _mm_xor_si128(msg3, msg1);
	
      // Rounds 56-59
         e0   = _mm_sha1nexte_epu32(e0, msg2);
         e1   = abcd;
      msg3 = _mm_sha1msg2_epu32(msg3, msg2);
         abcd = _mm_sha1rnds4_epu32(abcd, e0, 2);
      msg1 = _mm_sha1msg1_epu32(msg1, msg2);
      msg0 = _mm_xor_si128(msg0, msg2);

      // Rounds 60-63
         e1   = _mm_sha1nexte_epu32(e1, msg3);
         e0   = abcd;
      msg0 = _mm_sha1msg2_epu32(msg0, msg3);
         abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);
      msg2 = _mm_sha1msg1_epu32(msg2, msg3);
      msg1 = _mm_xor_si128(msg1, msg3);

      // Rounds 64-67
         e0   = _mm_sha1nexte_epu32(e0, msg0);
         e1   = abcd;
      msg1 = _mm_sha1msg2_epu32(msg1, msg0);
         abcd = _mm_sha1rnds4_epu32(abcd, e0, 3);
      msg3 = _mm_sha1msg1_epu32(msg3, msg0);
      msg2 = _mm_xor_si128(msg2, msg0);

      // Rounds 68-71
         e1   = _mm_sha1nexte_epu32(e1, msg1);
         e0   = abcd;
      msg2 = _mm_sha1msg2_epu32(msg2, msg1);
         abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);
      msg3 = _mm_xor_si128(msg3, msg1);
	
      // Rounds 72-75
         e0   = _mm_sha1nexte_epu32(e0, msg2);
         e1   = abcd;
      msg3 = _mm_sha1msg2_epu32(msg3, msg2);
         abcd = _mm_sha1rnds4_epu32(abcd, e0, 3);

      // Rounds 76-79
         e1   = _mm_sha1nexte_epu32(e1, msg3);
         e0   = abcd;
         abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);

      // Add current hash values with previously saved
      e0   = _mm_sha1nexte_epu32(e0, e_save);
      abcd = _mm_add_epi32(abcd, abcd_save);

      data += 64;
      num_blks--;
   }

   abcd = _mm_shuffle_epi32(abcd, 0x1B);
   _mm_store_si128((__m128i*) digest, abcd);
   *(digest+4) = _mm_extract_epi32(e0, 3);
}

