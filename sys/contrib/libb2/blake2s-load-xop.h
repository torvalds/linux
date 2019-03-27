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
#ifndef __BLAKE2S_LOAD_XOP_H__
#define __BLAKE2S_LOAD_XOP_H__

#define TOB(x) ((x)*4*0x01010101 + 0x03020100) // ..or not TOB

/* Basic VPPERM emulation, for testing purposes */
/*static __m128i _mm_perm_epi8(const __m128i src1, const __m128i src2, const __m128i sel)
{
   const __m128i sixteen = _mm_set1_epi8(16);
   const __m128i t0 = _mm_shuffle_epi8(src1, sel);
   const __m128i s1 = _mm_shuffle_epi8(src2, _mm_sub_epi8(sel, sixteen));
   const __m128i mask = _mm_or_si128(_mm_cmpeq_epi8(sel, sixteen),
                                     _mm_cmpgt_epi8(sel, sixteen)); // (>=16) = 0xff : 00
   return _mm_blendv_epi8(t0, s1, mask);
}*/

#define LOAD_MSG_0_1(buf) \
buf = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(6),TOB(4),TOB(2),TOB(0)) );

#define LOAD_MSG_0_2(buf) \
buf = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(7),TOB(5),TOB(3),TOB(1)) );

#define LOAD_MSG_0_3(buf) \
buf = _mm_perm_epi8(m2, m3, _mm_set_epi32(TOB(6),TOB(4),TOB(2),TOB(0)) );

#define LOAD_MSG_0_4(buf) \
buf = _mm_perm_epi8(m2, m3, _mm_set_epi32(TOB(7),TOB(5),TOB(3),TOB(1)) );

#define LOAD_MSG_1_1(buf) \
t0 = _mm_perm_epi8(m1, m2, _mm_set_epi32(TOB(0),TOB(5),TOB(0),TOB(0)) ); \
buf = _mm_perm_epi8(t0, m3, _mm_set_epi32(TOB(5),TOB(2),TOB(1),TOB(6)) );

#define LOAD_MSG_1_2(buf) \
t1 = _mm_perm_epi8(m1, m2, _mm_set_epi32(TOB(2),TOB(0),TOB(4),TOB(6)) ); \
buf = _mm_perm_epi8(t1, m3, _mm_set_epi32(TOB(3),TOB(7),TOB(1),TOB(0)) );

#define LOAD_MSG_1_3(buf) \
t0 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(5),TOB(0),TOB(0),TOB(1)) ); \
buf = _mm_perm_epi8(t0, m2, _mm_set_epi32(TOB(3),TOB(7),TOB(1),TOB(0)) );

#define LOAD_MSG_1_4(buf) \
t1 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(3),TOB(7),TOB(2),TOB(0)) ); \
buf = _mm_perm_epi8(t1, m3, _mm_set_epi32(TOB(3),TOB(2),TOB(1),TOB(4)) );

#define LOAD_MSG_2_1(buf) \
t0 = _mm_perm_epi8(m1, m2, _mm_set_epi32(TOB(0),TOB(1),TOB(0),TOB(7)) ); \
buf = _mm_perm_epi8(t0, m3, _mm_set_epi32(TOB(7),TOB(2),TOB(4),TOB(0)) );

#define LOAD_MSG_2_2(buf) \
t1 = _mm_perm_epi8(m0, m2, _mm_set_epi32(TOB(0),TOB(2),TOB(0),TOB(4)) ); \
buf = _mm_perm_epi8(t1, m3, _mm_set_epi32(TOB(5),TOB(2),TOB(1),TOB(0)) );

#define LOAD_MSG_2_3(buf) \
t0 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(0),TOB(7),TOB(3),TOB(0)) ); \
buf = _mm_perm_epi8(t0, m2, _mm_set_epi32(TOB(5),TOB(2),TOB(1),TOB(6)) );

#define LOAD_MSG_2_4(buf) \
t1 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(4),TOB(1),TOB(6),TOB(0)) ); \
buf = _mm_perm_epi8(t1, m3, _mm_set_epi32(TOB(3),TOB(2),TOB(1),TOB(6)) );

#define LOAD_MSG_3_1(buf) \
t0 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(0),TOB(0),TOB(3),TOB(7)) ); \
t0 = _mm_perm_epi8(t0, m2, _mm_set_epi32(TOB(7),TOB(2),TOB(1),TOB(0)) ); \
buf = _mm_perm_epi8(t0, m3, _mm_set_epi32(TOB(3),TOB(5),TOB(1),TOB(0)) );

#define LOAD_MSG_3_2(buf) \
t1 = _mm_perm_epi8(m0, m2, _mm_set_epi32(TOB(0),TOB(0),TOB(1),TOB(5)) ); \
buf = _mm_perm_epi8(t1, m3, _mm_set_epi32(TOB(6),TOB(4),TOB(1),TOB(0)) );

#define LOAD_MSG_3_3(buf) \
t0 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(0),TOB(4),TOB(5),TOB(2)) ); \
buf = _mm_perm_epi8(t0, m3, _mm_set_epi32(TOB(7),TOB(2),TOB(1),TOB(0)) );

#define LOAD_MSG_3_4(buf) \
t1 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(0),TOB(0),TOB(0),TOB(6)) ); \
buf = _mm_perm_epi8(t1, m2, _mm_set_epi32(TOB(4),TOB(2),TOB(6),TOB(0)) );

#define LOAD_MSG_4_1(buf) \
t0 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(0),TOB(2),TOB(5),TOB(0)) ); \
buf = _mm_perm_epi8(t0, m2, _mm_set_epi32(TOB(6),TOB(2),TOB(1),TOB(5)) );

#define LOAD_MSG_4_2(buf) \
t1 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(0),TOB(4),TOB(7),TOB(0)) ); \
buf = _mm_perm_epi8(t1, m3, _mm_set_epi32(TOB(7),TOB(2),TOB(1),TOB(0)) );

#define LOAD_MSG_4_3(buf) \
t0 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(3),TOB(6),TOB(0),TOB(0)) ); \
t0 = _mm_perm_epi8(t0, m2, _mm_set_epi32(TOB(3),TOB(2),TOB(7),TOB(0)) ); \
buf = _mm_perm_epi8(t0, m3, _mm_set_epi32(TOB(3),TOB(2),TOB(1),TOB(6)) );

#define LOAD_MSG_4_4(buf) \
t1 = _mm_perm_epi8(m0, m2, _mm_set_epi32(TOB(0),TOB(4),TOB(0),TOB(1)) ); \
buf = _mm_perm_epi8(t1, m3, _mm_set_epi32(TOB(5),TOB(2),TOB(4),TOB(0)) );

#define LOAD_MSG_5_1(buf) \
t0 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(0),TOB(0),TOB(6),TOB(2)) ); \
buf = _mm_perm_epi8(t0, m2, _mm_set_epi32(TOB(4),TOB(2),TOB(1),TOB(0)) );

#define LOAD_MSG_5_2(buf) \
t1 = _mm_perm_epi8(m0, m2, _mm_set_epi32(TOB(3),TOB(7),TOB(6),TOB(0)) ); \
buf = _mm_perm_epi8(t1, m3, _mm_set_epi32(TOB(3),TOB(2),TOB(1),TOB(4)) );

#define LOAD_MSG_5_3(buf) \
t0 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(1),TOB(0),TOB(7),TOB(4)) ); \
buf = _mm_perm_epi8(t0, m3, _mm_set_epi32(TOB(3),TOB(7),TOB(1),TOB(0)) );

#define LOAD_MSG_5_4(buf) \
t1 = _mm_perm_epi8(m1, m2, _mm_set_epi32(TOB(5),TOB(0),TOB(1),TOB(0)) ); \
buf = _mm_perm_epi8(t1, m3, _mm_set_epi32(TOB(3),TOB(6),TOB(1),TOB(5)) );

#define LOAD_MSG_6_1(buf) \
t0 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(4),TOB(0),TOB(1),TOB(0)) ); \
buf = _mm_perm_epi8(t0, m3, _mm_set_epi32(TOB(3),TOB(6),TOB(1),TOB(4)) );

#define LOAD_MSG_6_2(buf) \
t1 = _mm_perm_epi8(m1, m2, _mm_set_epi32(TOB(6),TOB(0),TOB(0),TOB(1)) ); \
buf = _mm_perm_epi8(t1, m3, _mm_set_epi32(TOB(3),TOB(5),TOB(7),TOB(0)) );

#define LOAD_MSG_6_3(buf) \
t0 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(0),TOB(0),TOB(6),TOB(0)) ); \
buf = _mm_perm_epi8(t0, m2, _mm_set_epi32(TOB(4),TOB(5),TOB(1),TOB(0)) );

#define LOAD_MSG_6_4(buf) \
t1 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(0),TOB(2),TOB(3),TOB(7)) ); \
buf = _mm_perm_epi8(t1, m2, _mm_set_epi32(TOB(7),TOB(2),TOB(1),TOB(0)) );

#define LOAD_MSG_7_1(buf) \
t0 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(3),TOB(0),TOB(7),TOB(0)) ); \
buf = _mm_perm_epi8(t0, m3, _mm_set_epi32(TOB(3),TOB(4),TOB(1),TOB(5)) );

#define LOAD_MSG_7_2(buf) \
t1 = _mm_perm_epi8(m0, m2, _mm_set_epi32(TOB(5),TOB(1),TOB(0),TOB(7)) ); \
buf = _mm_perm_epi8(t1, m3, _mm_set_epi32(TOB(3),TOB(2),TOB(6),TOB(0)) );

#define LOAD_MSG_7_3(buf) \
t0 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(2),TOB(0),TOB(0),TOB(5)) ); \
t0 = _mm_perm_epi8(t0, m2, _mm_set_epi32(TOB(3),TOB(4),TOB(1),TOB(0)) ); \
buf = _mm_perm_epi8(t0, m3, _mm_set_epi32(TOB(3),TOB(2),TOB(7),TOB(0)) );

#define LOAD_MSG_7_4(buf) \
t1 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(0),TOB(6),TOB(4),TOB(0)) ); \
buf = _mm_perm_epi8(t1, m2, _mm_set_epi32(TOB(6),TOB(2),TOB(1),TOB(0)) );

#define LOAD_MSG_8_1(buf) \
t0 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(0),TOB(0),TOB(0),TOB(6)) ); \
t0 = _mm_perm_epi8(t0, m2, _mm_set_epi32(TOB(3),TOB(7),TOB(1),TOB(0)) ); \
buf = _mm_perm_epi8(t0, m3, _mm_set_epi32(TOB(3),TOB(2),TOB(6),TOB(0)) );

#define LOAD_MSG_8_2(buf) \
t1 = _mm_perm_epi8(m0, m2, _mm_set_epi32(TOB(4),TOB(3),TOB(5),TOB(0)) ); \
buf = _mm_perm_epi8(t1, m3, _mm_set_epi32(TOB(3),TOB(2),TOB(1),TOB(7)) );

#define LOAD_MSG_8_3(buf) \
t0 = _mm_perm_epi8(m0, m2, _mm_set_epi32(TOB(6),TOB(1),TOB(0),TOB(0)) ); \
buf = _mm_perm_epi8(t0, m3, _mm_set_epi32(TOB(3),TOB(2),TOB(5),TOB(4)) ); \
 
#define LOAD_MSG_8_4(buf) \
buf = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(5),TOB(4),TOB(7),TOB(2)) );

#define LOAD_MSG_9_1(buf) \
t0 = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(1),TOB(7),TOB(0),TOB(0)) ); \
buf = _mm_perm_epi8(t0, m2, _mm_set_epi32(TOB(3),TOB(2),TOB(4),TOB(6)) );

#define LOAD_MSG_9_2(buf) \
buf = _mm_perm_epi8(m0, m1, _mm_set_epi32(TOB(5),TOB(6),TOB(4),TOB(2)) );

#define LOAD_MSG_9_3(buf) \
t0 = _mm_perm_epi8(m0, m2, _mm_set_epi32(TOB(0),TOB(3),TOB(5),TOB(0)) ); \
buf = _mm_perm_epi8(t0, m3, _mm_set_epi32(TOB(5),TOB(2),TOB(1),TOB(7)) );

#define LOAD_MSG_9_4(buf) \
t1 = _mm_perm_epi8(m0, m2, _mm_set_epi32(TOB(0),TOB(0),TOB(0),TOB(7)) ); \
buf = _mm_perm_epi8(t1, m3, _mm_set_epi32(TOB(3),TOB(4),TOB(6),TOB(0)) );

#endif

