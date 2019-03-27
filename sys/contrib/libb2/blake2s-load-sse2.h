/*
   BLAKE2 reference source code package - optimized C implementations

   Written in 2012 by Samuel Neves <sneves@dei.uc.pt>

   To the extent possible under law, the author(s) have dedicated all copyright
   and related and neighboring rights to this software to the public domain
   worldwide. This software is distributed without any warranty.

   You should have received a copy of the CC0 Public Domain Dedication along with
   this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
*/
#pragma once
#ifndef __BLAKE2S_LOAD_SSE2_H__
#define __BLAKE2S_LOAD_SSE2_H__

#define LOAD_MSG_0_1(buf) buf = _mm_set_epi32(m6,m4,m2,m0)
#define LOAD_MSG_0_2(buf) buf = _mm_set_epi32(m7,m5,m3,m1)
#define LOAD_MSG_0_3(buf) buf = _mm_set_epi32(m14,m12,m10,m8)
#define LOAD_MSG_0_4(buf) buf = _mm_set_epi32(m15,m13,m11,m9)
#define LOAD_MSG_1_1(buf) buf = _mm_set_epi32(m13,m9,m4,m14)
#define LOAD_MSG_1_2(buf) buf = _mm_set_epi32(m6,m15,m8,m10)
#define LOAD_MSG_1_3(buf) buf = _mm_set_epi32(m5,m11,m0,m1)
#define LOAD_MSG_1_4(buf) buf = _mm_set_epi32(m3,m7,m2,m12)
#define LOAD_MSG_2_1(buf) buf = _mm_set_epi32(m15,m5,m12,m11)
#define LOAD_MSG_2_2(buf) buf = _mm_set_epi32(m13,m2,m0,m8)
#define LOAD_MSG_2_3(buf) buf = _mm_set_epi32(m9,m7,m3,m10)
#define LOAD_MSG_2_4(buf) buf = _mm_set_epi32(m4,m1,m6,m14)
#define LOAD_MSG_3_1(buf) buf = _mm_set_epi32(m11,m13,m3,m7)
#define LOAD_MSG_3_2(buf) buf = _mm_set_epi32(m14,m12,m1,m9)
#define LOAD_MSG_3_3(buf) buf = _mm_set_epi32(m15,m4,m5,m2)
#define LOAD_MSG_3_4(buf) buf = _mm_set_epi32(m8,m0,m10,m6)
#define LOAD_MSG_4_1(buf) buf = _mm_set_epi32(m10,m2,m5,m9)
#define LOAD_MSG_4_2(buf) buf = _mm_set_epi32(m15,m4,m7,m0)
#define LOAD_MSG_4_3(buf) buf = _mm_set_epi32(m3,m6,m11,m14)
#define LOAD_MSG_4_4(buf) buf = _mm_set_epi32(m13,m8,m12,m1)
#define LOAD_MSG_5_1(buf) buf = _mm_set_epi32(m8,m0,m6,m2)
#define LOAD_MSG_5_2(buf) buf = _mm_set_epi32(m3,m11,m10,m12)
#define LOAD_MSG_5_3(buf) buf = _mm_set_epi32(m1,m15,m7,m4)
#define LOAD_MSG_5_4(buf) buf = _mm_set_epi32(m9,m14,m5,m13)
#define LOAD_MSG_6_1(buf) buf = _mm_set_epi32(m4,m14,m1,m12)
#define LOAD_MSG_6_2(buf) buf = _mm_set_epi32(m10,m13,m15,m5)
#define LOAD_MSG_6_3(buf) buf = _mm_set_epi32(m8,m9,m6,m0)
#define LOAD_MSG_6_4(buf) buf = _mm_set_epi32(m11,m2,m3,m7)
#define LOAD_MSG_7_1(buf) buf = _mm_set_epi32(m3,m12,m7,m13)
#define LOAD_MSG_7_2(buf) buf = _mm_set_epi32(m9,m1,m14,m11)
#define LOAD_MSG_7_3(buf) buf = _mm_set_epi32(m2,m8,m15,m5)
#define LOAD_MSG_7_4(buf) buf = _mm_set_epi32(m10,m6,m4,m0)
#define LOAD_MSG_8_1(buf) buf = _mm_set_epi32(m0,m11,m14,m6)
#define LOAD_MSG_8_2(buf) buf = _mm_set_epi32(m8,m3,m9,m15)
#define LOAD_MSG_8_3(buf) buf = _mm_set_epi32(m10,m1,m13,m12)
#define LOAD_MSG_8_4(buf) buf = _mm_set_epi32(m5,m4,m7,m2)
#define LOAD_MSG_9_1(buf) buf = _mm_set_epi32(m1,m7,m8,m10)
#define LOAD_MSG_9_2(buf) buf = _mm_set_epi32(m5,m6,m4,m2)
#define LOAD_MSG_9_3(buf) buf = _mm_set_epi32(m13,m3,m9,m15)
#define LOAD_MSG_9_4(buf) buf = _mm_set_epi32(m0,m12,m14,m11)


#endif
