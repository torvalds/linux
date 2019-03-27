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
#if defined(WIN32)
#include <windows.h>
#endif
#include "blake2.h"

#if defined(__x86_64__) || defined(__i386__) || defined(_M_IX86) || defined(_M_X64)
#define HAVE_X86
#endif

typedef enum
{
  NONE  = 0,
#if defined(HAVE_X86)
  SSE2  = 1,
  SSSE3 = 2,
  SSE41 = 3,
  AVX   = 4,
  XOP   = 5,
  /* AVX2  = 6, */
#endif
} cpu_feature_t;

static const char feature_names[][8] =
{
  "none",
#if defined(HAVE_X86)
  "sse2",
  "ssse3",
  "sse41",
  "avx",
  "xop",
  /* "avx2" */
#endif
};

#if defined(HAVE_X86)

#if defined(__GNUC__)
static inline void cpuid( uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx )
{
  __asm__ __volatile__(
#if defined(__i386__) /* This is needed for -fPIC to work on i386 */
    "movl %%ebx, %%esi\n\t"
#endif
    "cpuid\n\t"
#if defined(__i386__)
    "xchgl %%ebx, %%esi\n\t"
    : "=a"( *eax ), "=S"( *ebx ), "=c"( *ecx ), "=d"( *edx ) : "a"( *eax ) );
#else
    : "=a"( *eax ), "=b"( *ebx ), "=c"( *ecx ), "=d"( *edx ) : "a"( *eax ) );
#endif
}

static inline uint64_t xgetbv(uint32_t xcr)
{
  uint32_t a, d;
  __asm__ __volatile__(
    "xgetbv"
    :  "=a"(a),"=d"(d)
    : "c"(xcr)
  );
  return ((uint64_t)d << 32) | a;
}

#elif defined(_MSC_VER)
#include <intrin.h>
static inline void cpuid( uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx )
{
  int regs[4];
  __cpuid( regs, *eax );
  *eax = regs[0];
  *ebx = regs[1];
  *ecx = regs[2];
  *edx = regs[3];
}
#else
#error "Don't know how to call cpuid on this compiler!"
#endif

#endif /* HAVE_X86 */

static inline cpu_feature_t get_cpu_features( void )
{
#if defined(HAVE_X86)
  static volatile int initialized = 0;
  static cpu_feature_t feature = NONE; // Safe default
  uint32_t eax, ecx, edx, ebx;

  if( initialized )
    return feature;

  eax = 1;
  cpuid( &eax, &ebx, &ecx, &edx );

  if( 1 & ( edx >> 26 ) )
    feature = SSE2;

  if( 1 & ( ecx >> 9 ) )
    feature = SSSE3;

  if( 1 & ( ecx >> 19 ) )
    feature = SSE41;

#if defined(WIN32) /* Work around the fact that Windows <7 does NOT support AVX... */
  if( IsProcessorFeaturePresent(17) ) /* Some environments don't know about PF_XSAVE_ENABLED */
#endif
  {
    /* check for AVX and OSXSAVE bits */
    if( 1 & ( ecx >> 28 ) & (ecx >> 27) ) {
#if !defined(WIN32) /* Already checked for this in WIN32 */
    if( (xgetbv(0) & 6) == 6 ) /* XCR0 */
#endif
      feature = AVX;
    }


    eax = 0x80000001;
    cpuid( &eax, &ebx, &ecx, &edx );

    if( 1 & ( ecx >> 11 ) )
      feature = XOP;
  }

  /* For future architectures */
  /* 
      eax = 7; ecx = 0;
      cpuid(&eax, &ebx, &ecx, &edx);

      if(1&(ebx >> 5))
        feature = AVX2;
  */
  /* fprintf( stderr, "Using %s engine\n", feature_names[feature] ); */
  initialized = 1;
  return feature;
#else
  return NONE;
#endif
}



#if defined(__cplusplus)
extern "C" {
#endif
  int blake2b_init_ref( blake2b_state *S, size_t outlen );
  int blake2b_init_key_ref( blake2b_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2b_init_param_ref( blake2b_state *S, const blake2b_param *P );
  int blake2b_update_ref( blake2b_state *S, const uint8_t *in, size_t inlen );
  int blake2b_final_ref( blake2b_state *S, uint8_t *out, size_t outlen );
  int blake2b_ref( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );

#if defined(HAVE_X86)

  int blake2b_init_sse2( blake2b_state *S, size_t outlen );
  int blake2b_init_key_sse2( blake2b_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2b_init_param_sse2( blake2b_state *S, const blake2b_param *P );
  int blake2b_update_sse2( blake2b_state *S, const uint8_t *in, size_t inlen );
  int blake2b_final_sse2( blake2b_state *S, uint8_t *out, size_t outlen );
  int blake2b_sse2( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );

  int blake2b_init_ssse3( blake2b_state *S, size_t outlen );
  int blake2b_init_key_ssse3( blake2b_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2b_init_param_ssse3( blake2b_state *S, const blake2b_param *P );
  int blake2b_update_ssse3( blake2b_state *S, const uint8_t *in, size_t inlen );
  int blake2b_final_ssse3( blake2b_state *S, uint8_t *out, size_t outlen );
  int blake2b_ssse3( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );

  int blake2b_init_sse41( blake2b_state *S, size_t outlen );
  int blake2b_init_key_sse41( blake2b_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2b_init_param_sse41( blake2b_state *S, const blake2b_param *P );
  int blake2b_update_sse41( blake2b_state *S, const uint8_t *in, size_t inlen );
  int blake2b_final_sse41( blake2b_state *S, uint8_t *out, size_t outlen );
  int blake2b_sse41( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );

  int blake2b_init_avx( blake2b_state *S, size_t outlen );
  int blake2b_init_key_avx( blake2b_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2b_init_param_avx( blake2b_state *S, const blake2b_param *P );
  int blake2b_update_avx( blake2b_state *S, const uint8_t *in, size_t inlen );
  int blake2b_final_avx( blake2b_state *S, uint8_t *out, size_t outlen );
  int blake2b_avx( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );

  int blake2b_init_xop( blake2b_state *S, size_t outlen );
  int blake2b_init_key_xop( blake2b_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2b_init_param_xop( blake2b_state *S, const blake2b_param *P );
  int blake2b_update_xop( blake2b_state *S, const uint8_t *in, size_t inlen );
  int blake2b_final_xop( blake2b_state *S, uint8_t *out, size_t outlen );
  int blake2b_xop( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );

#endif /* HAVE_X86 */

  int blake2s_init_ref( blake2s_state *S, size_t outlen );
  int blake2s_init_key_ref( blake2s_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2s_init_param_ref( blake2s_state *S, const blake2s_param *P );
  int blake2s_update_ref( blake2s_state *S, const uint8_t *in, size_t inlen );
  int blake2s_final_ref( blake2s_state *S, uint8_t *out, size_t outlen );
  int blake2s_ref( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );

#if defined(HAVE_X86)

  int blake2s_init_sse2( blake2s_state *S, size_t outlen );
  int blake2s_init_key_sse2( blake2s_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2s_init_param_sse2( blake2s_state *S, const blake2s_param *P );
  int blake2s_update_sse2( blake2s_state *S, const uint8_t *in, size_t inlen );
  int blake2s_final_sse2( blake2s_state *S, uint8_t *out, size_t outlen );
  int blake2s_sse2( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );

  int blake2s_init_ssse3( blake2s_state *S, size_t outlen );
  int blake2s_init_key_ssse3( blake2s_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2s_init_param_ssse3( blake2s_state *S, const blake2s_param *P );
  int blake2s_update_ssse3( blake2s_state *S, const uint8_t *in, size_t inlen );
  int blake2s_final_ssse3( blake2s_state *S, uint8_t *out, size_t outlen );
  int blake2s_ssse3( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );

  int blake2s_init_sse41( blake2s_state *S, size_t outlen );
  int blake2s_init_key_sse41( blake2s_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2s_init_param_sse41( blake2s_state *S, const blake2s_param *P );
  int blake2s_update_sse41( blake2s_state *S, const uint8_t *in, size_t inlen );
  int blake2s_final_sse41( blake2s_state *S, uint8_t *out, size_t outlen );
  int blake2s_sse41( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );

  int blake2s_init_avx( blake2s_state *S, size_t outlen );
  int blake2s_init_key_avx( blake2s_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2s_init_param_avx( blake2s_state *S, const blake2s_param *P );
  int blake2s_update_avx( blake2s_state *S, const uint8_t *in, size_t inlen );
  int blake2s_final_avx( blake2s_state *S, uint8_t *out, size_t outlen );
  int blake2s_avx( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );

  int blake2s_init_xop( blake2s_state *S, size_t outlen );
  int blake2s_init_key_xop( blake2s_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2s_init_param_xop( blake2s_state *S, const blake2s_param *P );
  int blake2s_update_xop( blake2s_state *S, const uint8_t *in, size_t inlen );
  int blake2s_final_xop( blake2s_state *S, uint8_t *out, size_t outlen );
  int blake2s_xop( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );

#endif /* HAVE_X86 */

#if defined(__cplusplus)
}
#endif

typedef int ( *blake2b_init_fn )( blake2b_state *, size_t );
typedef int ( *blake2b_init_key_fn )( blake2b_state *, size_t, const void *, size_t );
typedef int ( *blake2b_init_param_fn )( blake2b_state *, const blake2b_param * );
typedef int ( *blake2b_update_fn )( blake2b_state *, const uint8_t *, size_t );
typedef int ( *blake2b_final_fn )( blake2b_state *, uint8_t *, size_t );
typedef int ( *blake2b_fn )( uint8_t *, const void *, const void *, size_t, size_t, size_t );

typedef int ( *blake2s_init_fn )( blake2s_state *, size_t );
typedef int ( *blake2s_init_key_fn )( blake2s_state *, size_t, const void *, size_t );
typedef int ( *blake2s_init_param_fn )( blake2s_state *, const blake2s_param * );
typedef int ( *blake2s_update_fn )( blake2s_state *, const uint8_t *, size_t );
typedef int ( *blake2s_final_fn )( blake2s_state *, uint8_t *, size_t );
typedef int ( *blake2s_fn )( uint8_t *, const void *, const void *, size_t, size_t, size_t );

static const blake2b_init_fn blake2b_init_table[] =
{
  blake2b_init_ref,
#if defined(HAVE_X86)
  blake2b_init_sse2,
  blake2b_init_ssse3,
  blake2b_init_sse41,
  blake2b_init_avx,
  blake2b_init_xop
#endif
};

static const blake2b_init_key_fn blake2b_init_key_table[] =
{
  blake2b_init_key_ref,
#if defined(HAVE_X86)
  blake2b_init_key_sse2,
  blake2b_init_key_ssse3,
  blake2b_init_key_sse41,
  blake2b_init_key_avx,
  blake2b_init_key_xop
#endif
};

static const blake2b_init_param_fn blake2b_init_param_table[] =
{
  blake2b_init_param_ref,
#if defined(HAVE_X86)
  blake2b_init_param_sse2,
  blake2b_init_param_ssse3,
  blake2b_init_param_sse41,
  blake2b_init_param_avx,
  blake2b_init_param_xop
#endif
};

static const blake2b_update_fn blake2b_update_table[] =
{
  blake2b_update_ref,
#if defined(HAVE_X86)
  blake2b_update_sse2,
  blake2b_update_ssse3,
  blake2b_update_sse41,
  blake2b_update_avx,
  blake2b_update_xop
#endif
};

static const blake2b_final_fn blake2b_final_table[] =
{
  blake2b_final_ref,
#if defined(HAVE_X86)
  blake2b_final_sse2,
  blake2b_final_ssse3,
  blake2b_final_sse41,
  blake2b_final_avx,
  blake2b_final_xop
#endif
};

static const blake2b_fn blake2b_table[] =
{
  blake2b_ref,
#if defined(HAVE_X86)
  blake2b_sse2,
  blake2b_ssse3,
  blake2b_sse41,
  blake2b_avx,
  blake2b_xop
#endif
};

static const blake2s_init_fn blake2s_init_table[] =
{
  blake2s_init_ref,
#if defined(HAVE_X86)
  blake2s_init_sse2,
  blake2s_init_ssse3,
  blake2s_init_sse41,
  blake2s_init_avx,
  blake2s_init_xop
#endif
};

static const blake2s_init_key_fn blake2s_init_key_table[] =
{
  blake2s_init_key_ref,
#if defined(HAVE_X86)
  blake2s_init_key_sse2,
  blake2s_init_key_ssse3,
  blake2s_init_key_sse41,
  blake2s_init_key_avx,
  blake2s_init_key_xop
#endif
};

static const blake2s_init_param_fn blake2s_init_param_table[] =
{
  blake2s_init_param_ref,
#if defined(HAVE_X86)
  blake2s_init_param_sse2,
  blake2s_init_param_ssse3,
  blake2s_init_param_sse41,
  blake2s_init_param_avx,
  blake2s_init_param_xop
#endif
};

static const blake2s_update_fn blake2s_update_table[] =
{
  blake2s_update_ref,
#if defined(HAVE_X86)
  blake2s_update_sse2,
  blake2s_update_ssse3,
  blake2s_update_sse41,
  blake2s_update_avx,
  blake2s_update_xop
#endif
};

static const blake2s_final_fn blake2s_final_table[] =
{
  blake2s_final_ref,
#if defined(HAVE_X86)
  blake2s_final_sse2,
  blake2s_final_ssse3,
  blake2s_final_sse41,
  blake2s_final_avx,
  blake2s_final_xop
#endif
};

static const blake2s_fn blake2s_table[] =
{
  blake2s_ref,
#if defined(HAVE_X86)
  blake2s_sse2,
  blake2s_ssse3,
  blake2s_sse41,
  blake2s_avx,
  blake2s_xop
#endif
};

#if defined(__cplusplus)
extern "C" {
#endif
  int blake2b_init_dispatch( blake2b_state *S, size_t outlen );
  int blake2b_init_key_dispatch( blake2b_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2b_init_param_dispatch( blake2b_state *S, const blake2b_param *P );
  int blake2b_update_dispatch( blake2b_state *S, const uint8_t *in, size_t inlen );
  int blake2b_final_dispatch( blake2b_state *S, uint8_t *out, size_t outlen );
  int blake2b_dispatch( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );

  int blake2s_init_dispatch( blake2s_state *S, size_t outlen );
  int blake2s_init_key_dispatch( blake2s_state *S, size_t outlen, const void *key, size_t keylen );
  int blake2s_init_param_dispatch( blake2s_state *S, const blake2s_param *P );
  int blake2s_update_dispatch( blake2s_state *S, const uint8_t *in, size_t inlen );
  int blake2s_final_dispatch( blake2s_state *S, uint8_t *out, size_t outlen );
  int blake2s_dispatch( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen );
#if defined(__cplusplus)
}
#endif

static blake2b_init_fn blake2b_init_ptr = blake2b_init_dispatch;
static blake2b_init_key_fn blake2b_init_key_ptr = blake2b_init_key_dispatch;
static blake2b_init_param_fn blake2b_init_param_ptr = blake2b_init_param_dispatch;
static blake2b_update_fn blake2b_update_ptr = blake2b_update_dispatch;
static blake2b_final_fn blake2b_final_ptr = blake2b_final_dispatch;
static blake2b_fn blake2b_ptr = blake2b_dispatch;

static blake2s_init_fn blake2s_init_ptr = blake2s_init_dispatch;
static blake2s_init_key_fn blake2s_init_key_ptr = blake2s_init_key_dispatch;
static blake2s_init_param_fn blake2s_init_param_ptr = blake2s_init_param_dispatch;
static blake2s_update_fn blake2s_update_ptr = blake2s_update_dispatch;
static blake2s_final_fn blake2s_final_ptr = blake2s_final_dispatch;
static blake2s_fn blake2s_ptr = blake2s_dispatch;

int blake2b_init_dispatch( blake2b_state *S, size_t outlen )
{
  blake2b_init_ptr = blake2b_init_table[get_cpu_features()];
  return blake2b_init_ptr( S, outlen );
}

int blake2b_init_key_dispatch( blake2b_state *S, size_t outlen, const void *key, size_t keylen )
{
  blake2b_init_key_ptr = blake2b_init_key_table[get_cpu_features()];
  return blake2b_init_key_ptr( S, outlen, key, keylen );
}

int blake2b_init_param_dispatch( blake2b_state *S, const blake2b_param *P )
{
  blake2b_init_param_ptr = blake2b_init_param_table[get_cpu_features()];
  return blake2b_init_param_ptr( S, P );
}

int blake2b_update_dispatch( blake2b_state *S, const uint8_t *in, size_t inlen )
{
  blake2b_update_ptr = blake2b_update_table[get_cpu_features()];
  return blake2b_update_ptr( S, in, inlen );
}

int blake2b_final_dispatch( blake2b_state *S, uint8_t *out, size_t outlen )
{
  blake2b_final_ptr = blake2b_final_table[get_cpu_features()];
  return blake2b_final_ptr( S, out, outlen );
}

int blake2b_dispatch( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen )
{
  blake2b_ptr = blake2b_table[get_cpu_features()];
  return blake2b_ptr( out, in, key, outlen, inlen, keylen );
}

BLAKE2_API int blake2b_init( blake2b_state *S, size_t outlen )
{
  return blake2b_init_ptr( S, outlen );
}

BLAKE2_API int blake2b_init_key( blake2b_state *S, size_t outlen, const void *key, size_t keylen )
{
  return blake2b_init_key_ptr( S, outlen, key, keylen );
}

BLAKE2_API int blake2b_init_param( blake2b_state *S, const blake2b_param *P )
{
  return blake2b_init_param_ptr( S, P );
}

BLAKE2_API int blake2b_update( blake2b_state *S, const uint8_t *in, size_t inlen )
{
  return blake2b_update_ptr( S, in, inlen );
}

BLAKE2_API int blake2b_final( blake2b_state *S, uint8_t *out, size_t outlen )
{
  return blake2b_final_ptr( S, out, outlen );
}

BLAKE2_API int blake2b( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen )
{
  return blake2b_ptr( out, in, key, outlen, inlen, keylen );
}

int blake2s_init_dispatch( blake2s_state *S, size_t outlen )
{
  blake2s_init_ptr = blake2s_init_table[get_cpu_features()];
  return blake2s_init_ptr( S, outlen );
}

int blake2s_init_key_dispatch( blake2s_state *S, size_t outlen, const void *key, size_t keylen )
{
  blake2s_init_key_ptr = blake2s_init_key_table[get_cpu_features()];
  return blake2s_init_key_ptr( S, outlen, key, keylen );
}

int blake2s_init_param_dispatch( blake2s_state *S, const blake2s_param *P )
{
  blake2s_init_param_ptr = blake2s_init_param_table[get_cpu_features()];
  return blake2s_init_param_ptr( S, P );
}

int blake2s_update_dispatch( blake2s_state *S, const uint8_t *in, size_t inlen )
{
  blake2s_update_ptr = blake2s_update_table[get_cpu_features()];
  return blake2s_update_ptr( S, in, inlen );
}

int blake2s_final_dispatch( blake2s_state *S, uint8_t *out, size_t outlen )
{
  blake2s_final_ptr = blake2s_final_table[get_cpu_features()];
  return blake2s_final_ptr( S, out, outlen );
}

int blake2s_dispatch( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen )
{
  blake2s_ptr = blake2s_table[get_cpu_features()];
  return blake2s_ptr( out, in, key, outlen, inlen, keylen );
}

BLAKE2_API int blake2s_init( blake2s_state *S, size_t outlen )
{
  return blake2s_init_ptr( S, outlen );
}

BLAKE2_API int blake2s_init_key( blake2s_state *S, size_t outlen, const void *key, size_t keylen )
{
  return blake2s_init_key_ptr( S, outlen, key, keylen );
}

BLAKE2_API int blake2s_init_param( blake2s_state *S, const blake2s_param *P )
{
  return blake2s_init_param_ptr( S, P );
}

BLAKE2_API int blake2s_update( blake2s_state *S, const uint8_t *in, size_t inlen )
{
  return blake2s_update_ptr( S, in, inlen );
}

BLAKE2_API int blake2s_final( blake2s_state *S, uint8_t *out, size_t outlen )
{
  return blake2s_final_ptr( S, out, outlen );
}

BLAKE2_API int blake2s( uint8_t *out, const void *in, const void *key, size_t outlen, size_t inlen, size_t keylen )
{
  return blake2s_ptr( out, in, key, outlen, inlen, keylen );
}

