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
#ifndef __BLAKE2S_LOAD_SSE41_H__
#define __BLAKE2S_LOAD_SSE41_H__

#define LOAD_MSG_0_1(buf) \
buf = TOI(_mm_shuffle_ps(TOF(m0), TOF(m1), _MM_SHUFFLE(2,0,2,0)));

#define LOAD_MSG_0_2(buf) \
buf = TOI(_mm_shuffle_ps(TOF(m0), TOF(m1), _MM_SHUFFLE(3,1,3,1)));

#define LOAD_MSG_0_3(buf) \
buf = TOI(_mm_shuffle_ps(TOF(m2), TOF(m3), _MM_SHUFFLE(2,0,2,0)));

#define LOAD_MSG_0_4(buf) \
buf = TOI(_mm_shuffle_ps(TOF(m2), TOF(m3), _MM_SHUFFLE(3,1,3,1)));

#define LOAD_MSG_1_1(buf) \
t0 = _mm_blend_epi16(m1, m2, 0x0C); \
t1 = _mm_slli_si128(m3, 4); \
t2 = _mm_blend_epi16(t0, t1, 0xF0); \
buf = _mm_shuffle_epi32(t2, _MM_SHUFFLE(2,1,0,3));

#define LOAD_MSG_1_2(buf) \
t0 = _mm_shuffle_epi32(m2,_MM_SHUFFLE(0,0,2,0)); \
t1 = _mm_blend_epi16(m1,m3,0xC0); \
t2 = _mm_blend_epi16(t0, t1, 0xF0); \
buf = _mm_shuffle_epi32(t2, _MM_SHUFFLE(2,3,0,1));

#define LOAD_MSG_1_3(buf) \
t0 = _mm_slli_si128(m1, 4); \
t1 = _mm_blend_epi16(m2, t0, 0x30); \
t2 = _mm_blend_epi16(m0, t1, 0xF0); \
buf = _mm_shuffle_epi32(t2, _MM_SHUFFLE(2,3,0,1));

#define LOAD_MSG_1_4(buf) \
t0 = _mm_unpackhi_epi32(m0,m1); \
t1 = _mm_slli_si128(m3, 4); \
t2 = _mm_blend_epi16(t0, t1, 0x0C); \
buf = _mm_shuffle_epi32(t2, _MM_SHUFFLE(2,3,0,1));

#define LOAD_MSG_2_1(buf) \
t0 = _mm_unpackhi_epi32(m2,m3); \
t1 = _mm_blend_epi16(m3,m1,0x0C); \
t2 = _mm_blend_epi16(t0, t1, 0x0F); \
buf = _mm_shuffle_epi32(t2, _MM_SHUFFLE(3,1,0,2));

#define LOAD_MSG_2_2(buf) \
t0 = _mm_unpacklo_epi32(m2,m0); \
t1 = _mm_blend_epi16(t0, m0, 0xF0); \
t2 = _mm_slli_si128(m3, 8); \
buf = _mm_blend_epi16(t1, t2, 0xC0);

#define LOAD_MSG_2_3(buf) \
t0 = _mm_blend_epi16(m0, m2, 0x3C); \
t1 = _mm_srli_si128(m1, 12); \
t2 = _mm_blend_epi16(t0,t1,0x03); \
buf = _mm_shuffle_epi32(t2, _MM_SHUFFLE(1,0,3,2));

#define LOAD_MSG_2_4(buf) \
t0 = _mm_slli_si128(m3, 4); \
t1 = _mm_blend_epi16(m0, m1, 0x33); \
t2 = _mm_blend_epi16(t1, t0, 0xC0); \
buf = _mm_shuffle_epi32(t2, _MM_SHUFFLE(0,1,2,3));

#define LOAD_MSG_3_1(buf) \
t0 = _mm_unpackhi_epi32(m0,m1); \
t1 = _mm_unpackhi_epi32(t0, m2); \
t2 = _mm_blend_epi16(t1, m3, 0x0C); \
buf = _mm_shuffle_epi32(t2, _MM_SHUFFLE(3,1,0,2));

#define LOAD_MSG_3_2(buf) \
t0 = _mm_slli_si128(m2, 8); \
t1 = _mm_blend_epi16(m3,m0,0x0C); \
t2 = _mm_blend_epi16(t1, t0, 0xC0); \
buf = _mm_shuffle_epi32(t2, _MM_SHUFFLE(2,0,1,3));

#define LOAD_MSG_3_3(buf) \
t0 = _mm_blend_epi16(m0,m1,0x0F); \
t1 = _mm_blend_epi16(t0, m3, 0xC0); \
buf = _mm_shuffle_epi32(t1, _MM_SHUFFLE(3,0,1,2));

#define LOAD_MSG_3_4(buf) \
t0 = _mm_unpacklo_epi32(m0,m2); \
t1 = _mm_unpackhi_epi32(m1,m2); \
buf = _mm_unpacklo_epi64(t1,t0);

#define LOAD_MSG_4_1(buf) \
t0 = _mm_unpacklo_epi64(m1,m2); \
t1 = _mm_unpackhi_epi64(m0,m2); \
t2 = _mm_blend_epi16(t0,t1,0x33); \
buf = _mm_shuffle_epi32(t2, _MM_SHUFFLE(2,0,1,3));

#define LOAD_MSG_4_2(buf) \
t0 = _mm_unpackhi_epi64(m1,m3); \
t1 = _mm_unpacklo_epi64(m0,m1); \
buf = _mm_blend_epi16(t0,t1,0x33);

#define LOAD_MSG_4_3(buf) \
t0 = _mm_unpackhi_epi64(m3,m1); \
t1 = _mm_unpackhi_epi64(m2,m0); \
buf = _mm_blend_epi16(t1,t0,0x33);

#define LOAD_MSG_4_4(buf) \
t0 = _mm_blend_epi16(m0,m2,0x03); \
t1 = _mm_slli_si128(t0, 8); \
t2 = _mm_blend_epi16(t1,m3,0x0F); \
buf = _mm_shuffle_epi32(t2, _MM_SHUFFLE(1,2,0,3));

#define LOAD_MSG_5_1(buf) \
t0 = _mm_unpackhi_epi32(m0,m1); \
t1 = _mm_unpacklo_epi32(m0,m2); \
buf = _mm_unpacklo_epi64(t0,t1);

#define LOAD_MSG_5_2(buf) \
t0 = _mm_srli_si128(m2, 4); \
t1 = _mm_blend_epi16(m0,m3,0x03); \
buf = _mm_blend_epi16(t1,t0,0x3C);

#define LOAD_MSG_5_3(buf) \
t0 = _mm_blend_epi16(m1,m0,0x0C); \
t1 = _mm_srli_si128(m3, 4); \
t2 = _mm_blend_epi16(t0,t1,0x30); \
buf = _mm_shuffle_epi32(t2, _MM_SHUFFLE(1,2,3,0));

#define LOAD_MSG_5_4(buf) \
t0 = _mm_unpacklo_epi64(m1,m2); \
t1= _mm_shuffle_epi32(m3, _MM_SHUFFLE(0,2,0,1)); \
buf = _mm_blend_epi16(t0,t1,0x33);

#define LOAD_MSG_6_1(buf) \
t0 = _mm_slli_si128(m1, 12); \
t1 = _mm_blend_epi16(m0,m3,0x33); \
buf = _mm_blend_epi16(t1,t0,0xC0);

#define LOAD_MSG_6_2(buf) \
t0 = _mm_blend_epi16(m3,m2,0x30); \
t1 = _mm_srli_si128(m1, 4); \
t2 = _mm_blend_epi16(t0,t1,0x03); \
buf = _mm_shuffle_epi32(t2, _MM_SHUFFLE(2,1,3,0));

#define LOAD_MSG_6_3(buf) \
t0 = _mm_unpacklo_epi64(m0,m2); \
t1 = _mm_srli_si128(m1, 4); \
buf = _mm_shuffle_epi32(_mm_blend_epi16(t0,t1,0x0C), _MM_SHUFFLE(2,3,1,0));

#define LOAD_MSG_6_4(buf) \
t0 = _mm_unpackhi_epi32(m1,m2); \
t1 = _mm_unpackhi_epi64(m0,t0); \
buf = _mm_shuffle_epi32(t1, _MM_SHUFFLE(3,0,1,2));

#define LOAD_MSG_7_1(buf) \
t0 = _mm_unpackhi_epi32(m0,m1); \
t1 = _mm_blend_epi16(t0,m3,0x0F); \
buf = _mm_shuffle_epi32(t1,_MM_SHUFFLE(2,0,3,1));

#define LOAD_MSG_7_2(buf) \
t0 = _mm_blend_epi16(m2,m3,0x30); \
t1 = _mm_srli_si128(m0,4); \
t2 = _mm_blend_epi16(t0,t1,0x03); \
buf = _mm_shuffle_epi32(t2, _MM_SHUFFLE(1,0,2,3));

#define LOAD_MSG_7_3(buf) \
t0 = _mm_unpackhi_epi64(m0,m3); \
t1 = _mm_unpacklo_epi64(m1,m2); \
t2 = _mm_blend_epi16(t0,t1,0x3C); \
buf = _mm_shuffle_epi32(t2,_MM_SHUFFLE(0,2,3,1));

#define LOAD_MSG_7_4(buf) \
t0 = _mm_unpacklo_epi32(m0,m1); \
t1 = _mm_unpackhi_epi32(m1,m2); \
buf = _mm_unpacklo_epi64(t0,t1);

#define LOAD_MSG_8_1(buf) \
t0 = _mm_unpackhi_epi32(m1,m3); \
t1 = _mm_unpacklo_epi64(t0,m0); \
t2 = _mm_blend_epi16(t1,m2,0xC0); \
buf = _mm_shufflehi_epi16(t2,_MM_SHUFFLE(1,0,3,2));

#define LOAD_MSG_8_2(buf) \
t0 = _mm_unpackhi_epi32(m0,m3); \
t1 = _mm_blend_epi16(m2,t0,0xF0); \
buf = _mm_shuffle_epi32(t1,_MM_SHUFFLE(0,2,1,3));

#define LOAD_MSG_8_3(buf) \
t0 = _mm_blend_epi16(m2,m0,0x0C); \
t1 = _mm_slli_si128(t0,4); \
buf = _mm_blend_epi16(t1,m3,0x0F);

#define LOAD_MSG_8_4(buf) \
t0 = _mm_blend_epi16(m1,m0,0x30); \
buf = _mm_shuffle_epi32(t0,_MM_SHUFFLE(1,0,3,2));

#define LOAD_MSG_9_1(buf) \
t0 = _mm_blend_epi16(m0,m2,0x03); \
t1 = _mm_blend_epi16(m1,m2,0x30); \
t2 = _mm_blend_epi16(t1,t0,0x0F); \
buf = _mm_shuffle_epi32(t2,_MM_SHUFFLE(1,3,0,2));

#define LOAD_MSG_9_2(buf) \
t0 = _mm_slli_si128(m0,4); \
t1 = _mm_blend_epi16(m1,t0,0xC0); \
buf = _mm_shuffle_epi32(t1,_MM_SHUFFLE(1,2,0,3));

#define LOAD_MSG_9_3(buf) \
t0 = _mm_unpackhi_epi32(m0,m3); \
t1 = _mm_unpacklo_epi32(m2,m3); \
t2 = _mm_unpackhi_epi64(t0,t1); \
buf = _mm_shuffle_epi32(t2,_MM_SHUFFLE(3,0,2,1));

#define LOAD_MSG_9_4(buf) \
t0 = _mm_blend_epi16(m3,m2,0xC0); \
t1 = _mm_unpacklo_epi32(m0,m3); \
t2 = _mm_blend_epi16(t0,t1,0x0F); \
buf = _mm_shuffle_epi32(t2,_MM_SHUFFLE(0,1,2,3));

#endif

