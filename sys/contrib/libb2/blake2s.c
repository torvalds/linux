/*
   BLAKE2 reference source code package - optimized C implementations

   Written in 2012 by Samuel Neves <sneves@dei.uc.pt>

   To the extent possible under law, the author(s) have dedicated all copyright
   and related and neighboring rights to this software to the public domain
   worldwide. This software is distributed without any warranty.

   You should have received a copy of the CC0 Public Domain Dedication along with
   this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
*/

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "blake2.h"
#include "blake2-impl.h"

#include "blake2-config.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(HAVE_SSE2)
#include <emmintrin.h>
// MSVC only defines  _mm_set_epi64x for x86_64...
#if defined(_MSC_VER) && !defined(_M_X64)
static inline __m128i _mm_set_epi64x( const uint64_t u1, const uint64_t u0 )
{
  return _mm_set_epi32( u1 >> 32, u1, u0 >> 32, u0 );
}
#endif
#endif


#if defined(HAVE_SSSE3)
#include <tmmintrin.h>
#endif
#if defined(HAVE_SSE4_1)
#include <smmintrin.h>
#endif
#if defined(HAVE_AVX)
#include <immintrin.h>
#endif
#if defined(HAVE_XOP) && !defined(_MSC_VER)
#include <x86intrin.h>
#endif

#include "blake2s-round.h"

static const uint32_t blake2s_IV[8] =
{
  0x6A09E667UL, 0xBB67AE85UL, 0x3C6EF372UL, 0xA54FF53AUL,
  0x510E527FUL, 0x9B05688CUL, 0x1F83D9ABUL, 0x5BE0CD19UL
};

static const uint8_t blake2s_sigma[10][16] =
{
  {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 } ,
  { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 } ,
  { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 } ,
  {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 } ,
  {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 } ,
  {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 } ,
  { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 } ,
  { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 } ,
  {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 } ,
  { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13 , 0 } ,
};


/* Some helper functions, not necessarily useful */
static inline int blake2s_set_lastnode( blake2s_state *S )
{
  S->f[1] = ~0U;
  return 0;
}

static inline int blake2s_clear_lastnode( blake2s_state *S )
{
  S->f[1] = 0U;
  return 0;
}

static inline int blake2s_set_lastblock( blake2s_state *S )
{
  if( S->last_node ) blake2s_set_lastnode( S );

  S->f[0] = ~0U;
  return 0;
}

static inline int blake2s_clear_lastblock( blake2s_state *S )
{
  if( S->last_node ) blake2s_clear_lastnode( S );

  S->f[0] = 0U;
  return 0;
}

static inline int blake2s_increment_counter( blake2s_state *S, const uint32_t inc )
{
  uint64_t t = ( ( uint64_t )S->t[1] << 32 ) | S->t[0];
  t += inc;
  S->t[0] = ( uint32_t )( t >>  0 );
  S->t[1] = ( uint32_t )( t >> 32 );
  return 0;
}


// Parameter-related functions
static inline int blake2s_param_set_digest_length( blake2s_param *P, const uint8_t digest_length )
{
  P->digest_length = digest_length;
  return 0;
}

static inline int blake2s_param_set_fanout( blake2s_param *P, const uint8_t fanout )
{
  P->fanout = fanout;
  return 0;
}

static inline int blake2s_param_set_max_depth( blake2s_param *P, const uint8_t depth )
{
  P->depth = depth;
  return 0;
}

static inline int blake2s_param_set_leaf_length( blake2s_param *P, const uint32_t leaf_length )
{
  P->leaf_length = leaf_length;
  return 0;
}

static inline int blake2s_param_set_node_offset( blake2s_param *P, const uint64_t node_offset )
{
  store48( P->node_offset, node_offset );
  return 0;
}

static inline int blake2s_param_set_node_depth( blake2s_param *P, const uint8_t node_depth )
{
  P->node_depth = node_depth;
  return 0;
}

static inline int blake2s_param_set_inner_length( blake2s_param *P, const uint8_t inner_length )
{
  P->inner_length = inner_length;
  return 0;
}

static inline int blake2s_param_set_salt( blake2s_param *P, const uint8_t salt[BLAKE2S_SALTBYTES] )
{
  memcpy( P->salt, salt, BLAKE2S_SALTBYTES );
  return 0;
}

static inline int blake2s_param_set_personal( blake2s_param *P, const uint8_t personal[BLAKE2S_PERSONALBYTES] )
{
  memcpy( P->personal, personal, BLAKE2S_PERSONALBYTES );
  return 0;
}

static inline int blake2s_init0( blake2s_state *S )
{
  memset( S, 0, sizeof( blake2s_state ) );

  for( int i = 0; i < 8; ++i ) S->h[i] = blake2s_IV[i];

  return 0;
}

#define blake2s_init BLAKE2_IMPL_NAME(blake2s_init)
#define blake2s_init_param BLAKE2_IMPL_NAME(blake2s_init_param)
#define blake2s_init_key BLAKE2_IMPL_NAME(blake2s_init_key)
#define blake2s_update BLAKE2_IMPL_NAME(blake2s_update)
#define blake2s_final BLAKE2_IMPL_NAME(blake2s_final)
#define blake2s BLAKE2_IMPL_NAME(blake2s)

#if defined(__cplusplus)
extern "C" {
#endif
  int blake2s_init( blake2s_state *S, size_t outlen );
  int blake2s_init_param( blake2s_state *S, const blake2s_param *P );
  int blake2s_init_key( blake2s_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2s_update( blake2s_state *S, const uint8_t *in, size_t inlen );
  int blake2s_final( blake2s_state *S, uint8_t *out, size_t outlen );
  int blake2s( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );
#if defined(__cplusplus)
}
#endif


/* init2 xors IV with input parameter block */
int blake2s_init_param( blake2s_state *S, const blake2s_param *P )
{
  uint8_t *p, *h, *v;
  //blake2s_init0( S );
  v = ( uint8_t * )( blake2s_IV );
  h = ( uint8_t * )( S->h );
  p = ( uint8_t * )( P );
  /* IV XOR ParamBlock */
  memset( S, 0, sizeof( blake2s_state ) );

  for( int i = 0; i < BLAKE2S_OUTBYTES; ++i ) h[i] = v[i] ^ p[i];

  S->outlen = P->digest_length;
  return 0;
}


/* Some sort of default parameter block initialization, for sequential blake2s */
int blake2s_init( blake2s_state *S, size_t outlen )
{
  if ( ( !outlen ) || ( outlen > BLAKE2S_OUTBYTES ) ) return -1;

  const blake2s_param P =
  {
    outlen,
    0,
    1,
    1,
    0,
    {0},
    0,
    0,
    {0},
    {0}
  };
  return blake2s_init_param( S, &P );
}


int blake2s_init_key( blake2s_state *S, size_t outlen, const void *key, size_t keylen )
{
  if ( ( !outlen ) || ( outlen > BLAKE2S_OUTBYTES ) ) return -1;

  if ( ( !key ) || ( !keylen ) || keylen > BLAKE2S_KEYBYTES ) return -1;

  const blake2s_param P =
  {
    outlen,
    keylen,
    1,
    1,
    0,
    {0},
    0,
    0,
    {0},
    {0}
  };

  if( blake2s_init_param( S, &P ) < 0 )
    return -1;

  {
    uint8_t block[BLAKE2S_BLOCKBYTES];
    memset( block, 0, BLAKE2S_BLOCKBYTES );
    memcpy( block, key, keylen );
    blake2s_update( S, block, BLAKE2S_BLOCKBYTES );
    secure_zero_memory( block, BLAKE2S_BLOCKBYTES ); /* Burn the key from stack */
  }
  return 0;
}


static inline int blake2s_compress( blake2s_state *S, const uint8_t block[BLAKE2S_BLOCKBYTES] )
{
  __m128i row1, row2, row3, row4;
  __m128i buf1, buf2, buf3, buf4;
#if defined(HAVE_SSE4_1)
  __m128i t0, t1;
#if !defined(HAVE_XOP)
  __m128i t2;
#endif
#endif
  __m128i ff0, ff1;
#if defined(HAVE_SSSE3) && !defined(HAVE_XOP)
  const __m128i r8 = _mm_set_epi8( 12, 15, 14, 13, 8, 11, 10, 9, 4, 7, 6, 5, 0, 3, 2, 1 );
  const __m128i r16 = _mm_set_epi8( 13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2 );
#endif
#if defined(HAVE_SSE4_1)
  const __m128i m0 = LOADU( block +  00 );
  const __m128i m1 = LOADU( block +  16 );
  const __m128i m2 = LOADU( block +  32 );
  const __m128i m3 = LOADU( block +  48 );
#else
  const uint32_t  m0 = ( ( uint32_t * )block )[ 0];
  const uint32_t  m1 = ( ( uint32_t * )block )[ 1];
  const uint32_t  m2 = ( ( uint32_t * )block )[ 2];
  const uint32_t  m3 = ( ( uint32_t * )block )[ 3];
  const uint32_t  m4 = ( ( uint32_t * )block )[ 4];
  const uint32_t  m5 = ( ( uint32_t * )block )[ 5];
  const uint32_t  m6 = ( ( uint32_t * )block )[ 6];
  const uint32_t  m7 = ( ( uint32_t * )block )[ 7];
  const uint32_t  m8 = ( ( uint32_t * )block )[ 8];
  const uint32_t  m9 = ( ( uint32_t * )block )[ 9];
  const uint32_t m10 = ( ( uint32_t * )block )[10];
  const uint32_t m11 = ( ( uint32_t * )block )[11];
  const uint32_t m12 = ( ( uint32_t * )block )[12];
  const uint32_t m13 = ( ( uint32_t * )block )[13];
  const uint32_t m14 = ( ( uint32_t * )block )[14];
  const uint32_t m15 = ( ( uint32_t * )block )[15];
#endif
  row1 = ff0 = LOADU( &S->h[0] );
  row2 = ff1 = LOADU( &S->h[4] );
  row3 = _mm_setr_epi32( 0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A );
  row4 = _mm_xor_si128( _mm_setr_epi32( 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19 ), LOADU( &S->t[0] ) );
  ROUND( 0 );
  ROUND( 1 );
  ROUND( 2 );
  ROUND( 3 );
  ROUND( 4 );
  ROUND( 5 );
  ROUND( 6 );
  ROUND( 7 );
  ROUND( 8 );
  ROUND( 9 );
  STOREU( &S->h[0], _mm_xor_si128( ff0, _mm_xor_si128( row1, row3 ) ) );
  STOREU( &S->h[4], _mm_xor_si128( ff1, _mm_xor_si128( row2, row4 ) ) );
  return 0;
}


int blake2s_update( blake2s_state *S, const uint8_t *in, size_t inlen )
{
  while( inlen > 0 )
  {
    size_t left = S->buflen;
    size_t fill = 2 * BLAKE2S_BLOCKBYTES - left;

    if( inlen > fill )
    {
      memcpy( S->buf + left, in, fill ); // Fill buffer
      S->buflen += fill;
      blake2s_increment_counter( S, BLAKE2S_BLOCKBYTES );
      blake2s_compress( S, S->buf ); // Compress
      memcpy( S->buf, S->buf + BLAKE2S_BLOCKBYTES, BLAKE2S_BLOCKBYTES ); // Shift buffer left
      S->buflen -= BLAKE2S_BLOCKBYTES;
      in += fill;
      inlen -= fill;
    }
    else /* inlen <= fill */
    {
      memcpy( S->buf + left, in, inlen );
      S->buflen += inlen; // Be lazy, do not compress
      in += inlen;
      inlen -= inlen;
    }
  }

  return 0;
}


int blake2s_final( blake2s_state *S, uint8_t *out, size_t outlen )
{
  uint8_t buffer[BLAKE2S_OUTBYTES];

  if(outlen != S->outlen ) return -1;

  if( S->buflen > BLAKE2S_BLOCKBYTES )
  {
    blake2s_increment_counter( S, BLAKE2S_BLOCKBYTES );
    blake2s_compress( S, S->buf );
    S->buflen -= BLAKE2S_BLOCKBYTES;
    memcpy( S->buf, S->buf + BLAKE2S_BLOCKBYTES, S->buflen );
  }

  blake2s_increment_counter( S, ( uint32_t )S->buflen );
  blake2s_set_lastblock( S );
  memset( S->buf + S->buflen, 0, 2 * BLAKE2S_BLOCKBYTES - S->buflen ); /* Padding */
  blake2s_compress( S, S->buf );

  for( int i = 0; i < 8; ++i ) /* Output full hash to temp buffer */
    store32( buffer + sizeof( S->h[i] ) * i, S->h[i] );

  memcpy( out, buffer, outlen );
  return 0;
}

int blake2s( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen )
{
  blake2s_state S[1];

  /* Verify parameters */
  if ( NULL == in && inlen > 0 ) return -1;

  if ( NULL == out ) return -1;

  if ( NULL == key && keylen > 0) return -1;

  if( !outlen || outlen > BLAKE2S_OUTBYTES ) return -1;

  if( keylen > BLAKE2S_KEYBYTES ) return -1;

  if( keylen > 0 )
  {
    if( blake2s_init_key( S, outlen, key, keylen ) < 0 ) return -1;
  }
  else
  {
    if( blake2s_init( S, outlen ) < 0 ) return -1;
  }

  if( blake2s_update( S, ( uint8_t * )in, inlen ) < 0) return -1;
  return blake2s_final( S, out, outlen );
}

#if defined(SUPERCOP)
int crypto_hash( unsigned char *out, unsigned char *in, unsigned long long inlen )
{
  return blake2s( out, in, NULL, BLAKE2S_OUTBYTES, (size_t)inlen, 0 );
}
#endif

