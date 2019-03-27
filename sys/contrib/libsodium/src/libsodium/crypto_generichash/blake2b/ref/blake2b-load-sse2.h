/*
   BLAKE2 reference source code package - optimized C implementations

   Written in 2012 by Samuel Neves <sneves@dei.uc.pt>

   To the extent possible under law, the author(s) have dedicated all copyright
   and related and neighboring rights to this software to the public domain
   worldwide. This software is distributed without any warranty.

   You should have received a copy of the CC0 Public Domain Dedication along
   with
   this software. If not, see
   <http://creativecommons.org/publicdomain/zero/1.0/>.
*/

#ifndef blake2b_load_sse2_H
#define blake2b_load_sse2_H

#define LOAD_MSG_0_1(b0, b1)     \
    b0 = _mm_set_epi64x(m2, m0); \
    b1 = _mm_set_epi64x(m6, m4)
#define LOAD_MSG_0_2(b0, b1)     \
    b0 = _mm_set_epi64x(m3, m1); \
    b1 = _mm_set_epi64x(m7, m5)
#define LOAD_MSG_0_3(b0, b1)      \
    b0 = _mm_set_epi64x(m10, m8); \
    b1 = _mm_set_epi64x(m14, m12)
#define LOAD_MSG_0_4(b0, b1)      \
    b0 = _mm_set_epi64x(m11, m9); \
    b1 = _mm_set_epi64x(m15, m13)
#define LOAD_MSG_1_1(b0, b1)      \
    b0 = _mm_set_epi64x(m4, m14); \
    b1 = _mm_set_epi64x(m13, m9)
#define LOAD_MSG_1_2(b0, b1)      \
    b0 = _mm_set_epi64x(m8, m10); \
    b1 = _mm_set_epi64x(m6, m15)
#define LOAD_MSG_1_3(b0, b1)     \
    b0 = _mm_set_epi64x(m0, m1); \
    b1 = _mm_set_epi64x(m5, m11)
#define LOAD_MSG_1_4(b0, b1)      \
    b0 = _mm_set_epi64x(m2, m12); \
    b1 = _mm_set_epi64x(m3, m7)
#define LOAD_MSG_2_1(b0, b1)       \
    b0 = _mm_set_epi64x(m12, m11); \
    b1 = _mm_set_epi64x(m15, m5)
#define LOAD_MSG_2_2(b0, b1)     \
    b0 = _mm_set_epi64x(m0, m8); \
    b1 = _mm_set_epi64x(m13, m2)
#define LOAD_MSG_2_3(b0, b1)      \
    b0 = _mm_set_epi64x(m3, m10); \
    b1 = _mm_set_epi64x(m9, m7)
#define LOAD_MSG_2_4(b0, b1)      \
    b0 = _mm_set_epi64x(m6, m14); \
    b1 = _mm_set_epi64x(m4, m1)
#define LOAD_MSG_3_1(b0, b1)     \
    b0 = _mm_set_epi64x(m3, m7); \
    b1 = _mm_set_epi64x(m11, m13)
#define LOAD_MSG_3_2(b0, b1)     \
    b0 = _mm_set_epi64x(m1, m9); \
    b1 = _mm_set_epi64x(m14, m12)
#define LOAD_MSG_3_3(b0, b1)     \
    b0 = _mm_set_epi64x(m5, m2); \
    b1 = _mm_set_epi64x(m15, m4)
#define LOAD_MSG_3_4(b0, b1)      \
    b0 = _mm_set_epi64x(m10, m6); \
    b1 = _mm_set_epi64x(m8, m0)
#define LOAD_MSG_4_1(b0, b1)     \
    b0 = _mm_set_epi64x(m5, m9); \
    b1 = _mm_set_epi64x(m10, m2)
#define LOAD_MSG_4_2(b0, b1)     \
    b0 = _mm_set_epi64x(m7, m0); \
    b1 = _mm_set_epi64x(m15, m4)
#define LOAD_MSG_4_3(b0, b1)       \
    b0 = _mm_set_epi64x(m11, m14); \
    b1 = _mm_set_epi64x(m3, m6)
#define LOAD_MSG_4_4(b0, b1)      \
    b0 = _mm_set_epi64x(m12, m1); \
    b1 = _mm_set_epi64x(m13, m8)
#define LOAD_MSG_5_1(b0, b1)     \
    b0 = _mm_set_epi64x(m6, m2); \
    b1 = _mm_set_epi64x(m8, m0)
#define LOAD_MSG_5_2(b0, b1)       \
    b0 = _mm_set_epi64x(m10, m12); \
    b1 = _mm_set_epi64x(m3, m11)
#define LOAD_MSG_5_3(b0, b1)     \
    b0 = _mm_set_epi64x(m7, m4); \
    b1 = _mm_set_epi64x(m1, m15)
#define LOAD_MSG_5_4(b0, b1)      \
    b0 = _mm_set_epi64x(m5, m13); \
    b1 = _mm_set_epi64x(m9, m14)
#define LOAD_MSG_6_1(b0, b1)      \
    b0 = _mm_set_epi64x(m1, m12); \
    b1 = _mm_set_epi64x(m4, m14)
#define LOAD_MSG_6_2(b0, b1)      \
    b0 = _mm_set_epi64x(m15, m5); \
    b1 = _mm_set_epi64x(m10, m13)
#define LOAD_MSG_6_3(b0, b1)     \
    b0 = _mm_set_epi64x(m6, m0); \
    b1 = _mm_set_epi64x(m8, m9)
#define LOAD_MSG_6_4(b0, b1)     \
    b0 = _mm_set_epi64x(m3, m7); \
    b1 = _mm_set_epi64x(m11, m2)
#define LOAD_MSG_7_1(b0, b1)      \
    b0 = _mm_set_epi64x(m7, m13); \
    b1 = _mm_set_epi64x(m3, m12)
#define LOAD_MSG_7_2(b0, b1)       \
    b0 = _mm_set_epi64x(m14, m11); \
    b1 = _mm_set_epi64x(m9, m1)
#define LOAD_MSG_7_3(b0, b1)      \
    b0 = _mm_set_epi64x(m15, m5); \
    b1 = _mm_set_epi64x(m2, m8)
#define LOAD_MSG_7_4(b0, b1)     \
    b0 = _mm_set_epi64x(m4, m0); \
    b1 = _mm_set_epi64x(m10, m6)
#define LOAD_MSG_8_1(b0, b1)      \
    b0 = _mm_set_epi64x(m14, m6); \
    b1 = _mm_set_epi64x(m0, m11)
#define LOAD_MSG_8_2(b0, b1)      \
    b0 = _mm_set_epi64x(m9, m15); \
    b1 = _mm_set_epi64x(m8, m3)
#define LOAD_MSG_8_3(b0, b1)       \
    b0 = _mm_set_epi64x(m13, m12); \
    b1 = _mm_set_epi64x(m10, m1)
#define LOAD_MSG_8_4(b0, b1)     \
    b0 = _mm_set_epi64x(m7, m2); \
    b1 = _mm_set_epi64x(m5, m4)
#define LOAD_MSG_9_1(b0, b1)      \
    b0 = _mm_set_epi64x(m8, m10); \
    b1 = _mm_set_epi64x(m1, m7)
#define LOAD_MSG_9_2(b0, b1)     \
    b0 = _mm_set_epi64x(m4, m2); \
    b1 = _mm_set_epi64x(m5, m6)
#define LOAD_MSG_9_3(b0, b1)      \
    b0 = _mm_set_epi64x(m9, m15); \
    b1 = _mm_set_epi64x(m13, m3)
#define LOAD_MSG_9_4(b0, b1)       \
    b0 = _mm_set_epi64x(m14, m11); \
    b1 = _mm_set_epi64x(m0, m12)
#define LOAD_MSG_10_1(b0, b1)    \
    b0 = _mm_set_epi64x(m2, m0); \
    b1 = _mm_set_epi64x(m6, m4)
#define LOAD_MSG_10_2(b0, b1)    \
    b0 = _mm_set_epi64x(m3, m1); \
    b1 = _mm_set_epi64x(m7, m5)
#define LOAD_MSG_10_3(b0, b1)     \
    b0 = _mm_set_epi64x(m10, m8); \
    b1 = _mm_set_epi64x(m14, m12)
#define LOAD_MSG_10_4(b0, b1)     \
    b0 = _mm_set_epi64x(m11, m9); \
    b1 = _mm_set_epi64x(m15, m13)
#define LOAD_MSG_11_1(b0, b1)     \
    b0 = _mm_set_epi64x(m4, m14); \
    b1 = _mm_set_epi64x(m13, m9)
#define LOAD_MSG_11_2(b0, b1)     \
    b0 = _mm_set_epi64x(m8, m10); \
    b1 = _mm_set_epi64x(m6, m15)
#define LOAD_MSG_11_3(b0, b1)    \
    b0 = _mm_set_epi64x(m0, m1); \
    b1 = _mm_set_epi64x(m5, m11)
#define LOAD_MSG_11_4(b0, b1)     \
    b0 = _mm_set_epi64x(m2, m12); \
    b1 = _mm_set_epi64x(m3, m7)

#endif
