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
#ifndef __BLAKE2_IMPL_H__
#define __BLAKE2_IMPL_H__

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "config.h"

#define BLAKE2_IMPL_CAT(x,y) x ## y
#define BLAKE2_IMPL_EVAL(x,y)  BLAKE2_IMPL_CAT(x,y)
#define BLAKE2_IMPL_NAME(fun)  BLAKE2_IMPL_EVAL(fun, SUFFIX)

static inline uint32_t load32( const void *src )
{
#if defined(NATIVE_LITTLE_ENDIAN) && !defined(HAVE_ALIGNED_ACCESS_REQUIRED)
  return *( uint32_t * )( src );
#else
  const uint8_t *p = ( uint8_t * )src;
  uint32_t w = *p++;
  w |= ( uint32_t )( *p++ ) <<  8;
  w |= ( uint32_t )( *p++ ) << 16;
  w |= ( uint32_t )( *p++ ) << 24;
  return w;
#endif
}

static inline uint64_t load64( const void *src )
{
#if defined(NATIVE_LITTLE_ENDIAN) && !defined(HAVE_ALIGNED_ACCESS_REQUIRED)
  return *( uint64_t * )( src );
#else
  const uint8_t *p = ( uint8_t * )src;
  uint64_t w = *p++;
  w |= ( uint64_t )( *p++ ) <<  8;
  w |= ( uint64_t )( *p++ ) << 16;
  w |= ( uint64_t )( *p++ ) << 24;
  w |= ( uint64_t )( *p++ ) << 32;
  w |= ( uint64_t )( *p++ ) << 40;
  w |= ( uint64_t )( *p++ ) << 48;
  w |= ( uint64_t )( *p++ ) << 56;
  return w;
#endif
}

static inline void store32( void *dst, uint32_t w )
{
#if defined(NATIVE_LITTLE_ENDIAN) && !defined(HAVE_ALIGNED_ACCESS_REQUIRED)
  *( uint32_t * )( dst ) = w;
#else
  uint8_t *p = ( uint8_t * )dst;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w;
#endif
}

static inline void store64( void *dst, uint64_t w )
{
#if defined(NATIVE_LITTLE_ENDIAN) && !defined(HAVE_ALIGNED_ACCESS_REQUIRED)
  *( uint64_t * )( dst ) = w;
#else
  uint8_t *p = ( uint8_t * )dst;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w;
#endif
}

static inline uint64_t load48( const void *src )
{
  const uint8_t *p = ( const uint8_t * )src;
  uint64_t w = *p++;
  w |= ( uint64_t )( *p++ ) <<  8;
  w |= ( uint64_t )( *p++ ) << 16;
  w |= ( uint64_t )( *p++ ) << 24;
  w |= ( uint64_t )( *p++ ) << 32;
  w |= ( uint64_t )( *p++ ) << 40;
  return w;
}

static inline void store48( void *dst, uint64_t w )
{
  uint8_t *p = ( uint8_t * )dst;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w; w >>= 8;
  *p++ = ( uint8_t )w;
}

static inline uint32_t rotl32( const uint32_t w, const unsigned c )
{
  return ( w << c ) | ( w >> ( 32 - c ) );
}

static inline uint64_t rotl64( const uint64_t w, const unsigned c )
{
  return ( w << c ) | ( w >> ( 64 - c ) );
}

static inline uint32_t rotr32( const uint32_t w, const unsigned c )
{
  return ( w >> c ) | ( w << ( 32 - c ) );
}

static inline uint64_t rotr64( const uint64_t w, const unsigned c )
{
  return ( w >> c ) | ( w << ( 64 - c ) );
}

/* prevents compiler optimizing out memset() */
static inline void secure_zero_memory(void *v, size_t n)
{
#if defined(_WIN32) || defined(WIN32)
  SecureZeroMemory(v, n);
#else
// prioritize first the general C11 call
#if defined(HAVE_MEMSET_S)
  memset_s(v, n, 0, n);
#elif defined(HAVE_EXPLICIT_BZERO)
  explicit_bzero(v, n);
#elif defined(HAVE_EXPLICIT_MEMSET)
  explicit_memset(v, 0, n);
#else
  memset(v, 0, n);
  __asm__ __volatile__("" :: "r"(v) : "memory");
#endif
#endif
}

#endif

