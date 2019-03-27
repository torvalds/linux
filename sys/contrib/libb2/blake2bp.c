/*
   BLAKE2 reference source code package - optimized C implementations

   Written in 2012 by Samuel Neves <sneves@dei.uc.pt>

   To the extent possible under law, the author(s) have dedicated all copyright
   and related and neighboring rights to this software to the public domain
   worldwide. This software is distributed without any warranty.

   You should have received a copy of the CC0 Public Domain Dedication along with
   this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined(_OPENMP)
#include <omp.h>
#endif

#include "blake2.h"
#include "blake2-impl.h"

#define PARALLELISM_DEGREE 4

static int blake2bp_init_leaf( blake2b_state *S, uint8_t outlen, uint8_t keylen, uint64_t offset )
{
  blake2b_param P[1];
  P->digest_length = outlen;
  P->key_length = keylen;
  P->fanout = PARALLELISM_DEGREE;
  P->depth = 2;
  store32(&P->leaf_length, 0);
  store64(&P->node_offset, offset);
  P->node_depth = 0;
  P->inner_length = BLAKE2B_OUTBYTES;
  memset( P->reserved, 0, sizeof( P->reserved ) );
  memset( P->salt, 0, sizeof( P->salt ) );
  memset( P->personal, 0, sizeof( P->personal ) );
  blake2b_init_param( S, P );
  S->outlen = P->inner_length;
  return 0;
}

static int blake2bp_init_root( blake2b_state *S, uint8_t outlen, uint8_t keylen )
{
  blake2b_param P[1];
  P->digest_length = outlen;
  P->key_length = keylen;
  P->fanout = PARALLELISM_DEGREE;
  P->depth = 2;
  store32(&P->leaf_length, 0);
  store64(&P->node_offset, 0);
  P->node_depth = 1;
  P->inner_length = BLAKE2B_OUTBYTES;
  memset( P->reserved, 0, sizeof( P->reserved ) );
  memset( P->salt, 0, sizeof( P->salt ) );
  memset( P->personal, 0, sizeof( P->personal ) );
  blake2b_init_param( S, P );
  S->outlen = P->digest_length;
  return 0;
}


int blake2bp_init( blake2bp_state *S, size_t outlen )
{
  if( !outlen || outlen > BLAKE2B_OUTBYTES ) return -1;

  memset( S->buf, 0, sizeof( S->buf ) );
  S->buflen = 0;

  if( blake2bp_init_root( S->R, ( uint8_t ) outlen, 0 ) < 0 )
    return -1;

  for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
    if( blake2bp_init_leaf( S->S[i], ( uint8_t ) outlen, 0, i ) < 0 ) return -1;

  S->R->last_node = 1;
  S->S[PARALLELISM_DEGREE - 1]->last_node = 1;
  S->outlen = ( uint8_t ) outlen;
  return 0;
}

int blake2bp_init_key( blake2bp_state *S, size_t outlen, const void *key, size_t keylen )
{
  if( !outlen || outlen > BLAKE2B_OUTBYTES ) return -1;

  if( !key || !keylen || keylen > BLAKE2B_KEYBYTES ) return -1;

  memset( S->buf, 0, sizeof( S->buf ) );
  S->buflen = 0;

  if( blake2bp_init_root( S->R, ( uint8_t ) outlen, ( uint8_t ) keylen ) < 0 )
    return -1;

  for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
    if( blake2bp_init_leaf( S->S[i], ( uint8_t ) outlen, ( uint8_t ) keylen, i ) < 0 )
      return -1;

  S->R->last_node = 1;
  S->S[PARALLELISM_DEGREE - 1]->last_node = 1;
  S->outlen = ( uint8_t ) outlen;
  {
    uint8_t block[BLAKE2B_BLOCKBYTES];
    memset( block, 0, BLAKE2B_BLOCKBYTES );
    memcpy( block, key, keylen );

    for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
      blake2b_update( S->S[i], block, BLAKE2B_BLOCKBYTES );

    secure_zero_memory( block, BLAKE2B_BLOCKBYTES ); /* Burn the key from stack */
  }
  return 0;
}


int blake2bp_update( blake2bp_state *S, const uint8_t *in, size_t inlen )
{
  size_t left = S->buflen;
  size_t fill = sizeof( S->buf ) - left;

  if( left && inlen >= fill )
  {
    memcpy( S->buf + left, in, fill );

    for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
      blake2b_update( S->S[i], S->buf + i * BLAKE2B_BLOCKBYTES, BLAKE2B_BLOCKBYTES );

    in += fill;
    inlen -= fill;
    left = 0;
  }

#if defined(_OPENMP)
  omp_set_num_threads(PARALLELISM_DEGREE);
  #pragma omp parallel shared(S)
#else
  for( size_t id__ = 0; id__ < PARALLELISM_DEGREE; ++id__ )
#endif
  {
#if defined(_OPENMP)
    size_t      id__ = ( size_t ) omp_get_thread_num();
#endif
    size_t inlen__ = inlen;
    const uint8_t *in__ = ( const uint8_t * )in;
    in__ += id__ * BLAKE2B_BLOCKBYTES;

    while( inlen__ >= PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES )
    {
      blake2b_update( S->S[id__], in__, BLAKE2B_BLOCKBYTES );
      in__ += PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES;
      inlen__ -= PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES;
    }
  }

  in += inlen - inlen % ( PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES );
  inlen %= PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES;

  if( inlen > 0 )
    memcpy( S->buf + left, in, inlen );

  S->buflen = ( uint32_t ) left + ( uint32_t ) inlen;
  return 0;
}



int blake2bp_final( blake2bp_state *S, uint8_t *out, size_t outlen )
{
  uint8_t hash[PARALLELISM_DEGREE][BLAKE2B_OUTBYTES];

  if(S->outlen != outlen) return -1;

  for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
  {
    if( S->buflen > i * BLAKE2B_BLOCKBYTES )
    {
      size_t left = S->buflen - i * BLAKE2B_BLOCKBYTES;

      if( left > BLAKE2B_BLOCKBYTES ) left = BLAKE2B_BLOCKBYTES;

      blake2b_update( S->S[i], S->buf + i * BLAKE2B_BLOCKBYTES, left );
    }

    blake2b_final( S->S[i], hash[i], BLAKE2B_OUTBYTES );
  }

  for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
    blake2b_update( S->R, hash[i], BLAKE2B_OUTBYTES );

  return blake2b_final( S->R, out, outlen );
}

int blake2bp( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen )
{
  uint8_t hash[PARALLELISM_DEGREE][BLAKE2B_OUTBYTES];
  blake2b_state S[PARALLELISM_DEGREE][1];
  blake2b_state FS[1];

  /* Verify parameters */
  if ( NULL == in && inlen > 0 ) return -1;

  if ( NULL == out ) return -1;

  if ( NULL == key && keylen > 0) return -1;

  if( !outlen || outlen > BLAKE2B_OUTBYTES ) return -1;

  if( keylen > BLAKE2B_KEYBYTES ) return -1;

  for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
    if( blake2bp_init_leaf( S[i], ( uint8_t ) outlen, ( uint8_t ) keylen, i ) < 0 )
      return -1;

  S[PARALLELISM_DEGREE - 1]->last_node = 1; // mark last node

  if( keylen > 0 )
  {
    uint8_t block[BLAKE2B_BLOCKBYTES];
    memset( block, 0, BLAKE2B_BLOCKBYTES );
    memcpy( block, key, keylen );

    for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
      blake2b_update( S[i], block, BLAKE2B_BLOCKBYTES );

    secure_zero_memory( block, BLAKE2B_BLOCKBYTES ); /* Burn the key from stack */
  }

#if defined(_OPENMP)
  omp_set_num_threads(PARALLELISM_DEGREE);
  #pragma omp parallel shared(S,hash)
#else
  for( size_t id__ = 0; id__ < PARALLELISM_DEGREE; ++id__ )
#endif
  {
#if defined(_OPENMP)
    size_t      id__ = ( size_t ) omp_get_thread_num();
#endif
    size_t inlen__ = inlen;
    const uint8_t *in__ = ( const uint8_t * )in;
    in__ += id__ * BLAKE2B_BLOCKBYTES;

    while( inlen__ >= PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES )
    {
      blake2b_update( S[id__], in__, BLAKE2B_BLOCKBYTES );
      in__ += PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES;
      inlen__ -= PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES;
    }

    if( inlen__ > id__ * BLAKE2B_BLOCKBYTES )
    {
      const size_t left = inlen__ - id__ * BLAKE2B_BLOCKBYTES;
      const size_t len = left <= BLAKE2B_BLOCKBYTES ? left : BLAKE2B_BLOCKBYTES;
      blake2b_update( S[id__], in__, len );
    }

    blake2b_final( S[id__], hash[id__], BLAKE2B_OUTBYTES );
  }

  if( blake2bp_init_root( FS, ( uint8_t ) outlen, ( uint8_t ) keylen ) < 0 )
    return -1;

  FS->last_node = 1; // Mark as last node

  for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
    blake2b_update( FS, hash[i], BLAKE2B_OUTBYTES );

  return blake2b_final( FS, out, outlen );
}



