/*
 *  Diffie-Hellman-Merkle key exchange
 *
 *  Copyright (C) 2006-2007  Christophe Devine
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of XySSL nor the names of its contributors may be
 *      used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *  
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 *  Reference:
 *
 *  http://www.cacr.math.uwaterloo.ca/hac/ (chapter 12)
 */

#include <string.h>

#include "includes.h"
#include "common.h"
#include "dhm_xyssl.h"


/*
 * helper to validate the mpi size and import it
 */
static int dhm_read_bignum( mpi *X,
                            unsigned char **p,
                            unsigned char *end )
{
    int ret, n;

    if( end - *p < 2 )
        return( XYSSL_ERR_DHM_BAD_INPUT_DATA );

    n = ( (*p)[0] << 8 ) | (*p)[1];
    (*p) += 2;

    if( (int)( end - *p ) < n )
        return( XYSSL_ERR_DHM_BAD_INPUT_DATA );

    if( ( ret = mpi_read_binary( X, *p, n ) ) != 0 )
        return( XYSSL_ERR_DHM_READ_PARAMS_FAILED | ret );

    (*p) += n;

    return( 0 );
}

/*
 * Parse the ServerKeyExchange parameters
 */
int dhm_read_params( dhm_context *ctx,
                     unsigned char **p,
                     unsigned char *end )
{
    int ret, n;

    os_memset( ctx, 0, sizeof( dhm_context ) );

    if( ( ret = dhm_read_bignum( &ctx->P,  p, end ) ) != 0 ||
        ( ret = dhm_read_bignum( &ctx->G,  p, end ) ) != 0 ||
        ( ret = dhm_read_bignum( &ctx->GY, p, end ) ) != 0 )
        return( ret );

    ctx->len = mpi_size( &ctx->P );

    if( end - *p < 2 )
        return( XYSSL_ERR_DHM_BAD_INPUT_DATA );

    n = ( (*p)[0] << 8 ) | (*p)[1];
    (*p) += 2;

    if( end != *p + n )
        return( XYSSL_ERR_DHM_BAD_INPUT_DATA );

    return( 0 );
}

/*
 * Setup and write the ServerKeyExchange parameters
 */
int dhm_make_params( dhm_context *ctx, int x_size,
                     unsigned char *output, int *olen,
                     int (*f_rng)(void *), void *p_rng )
{
    int i, ret, n, n1, n2, n3;
    unsigned char *p;

    /*
     * generate X and calculate GX = G^X mod P
     */
    n = x_size / sizeof( t_int_xyssl );
    MPI_CHK( mpi_grow( &ctx->X, n ) );
    MPI_CHK( mpi_lset( &ctx->X, 0 ) );

    n = x_size >> 3;
    p = (unsigned char *) ctx->X.p;
    for( i = 0; i < n; i++ )
        *p++ = (unsigned char) f_rng( p_rng );

    while( mpi_cmp_mpi( &ctx->X, &ctx->P ) >= 0 )
           mpi_shift_r( &ctx->X, 1 );

    MPI_CHK( mpi_exp_mod( &ctx->GX, &ctx->G, &ctx->X, &ctx->P , &ctx->TRP ) );

    /*
     * export P, G, GX
     */
#define DHM_MPI_EXPORT(X,n)                     \
    MPI_CHK( mpi_write_binary( X, p + 2, n ) ); \
    *p++ = (unsigned char)( n >> 8 );           \
    *p++ = (unsigned char)( n      ); p += n;

    n1 = mpi_size( &ctx->P  );
    n2 = mpi_size( &ctx->G  );
    n3 = mpi_size( &ctx->GX );

    p = output;
    DHM_MPI_EXPORT( &ctx->P , n1 );
    DHM_MPI_EXPORT( &ctx->G , n2 );
    DHM_MPI_EXPORT( &ctx->GX, n3 );

    *olen  = p - output;

    ctx->len = n1;

cleanup:

    if( ret != 0 )
        return( ret | XYSSL_ERR_DHM_MAKE_PARAMS_FAILED );

    return( 0 );
}

/*
 * Import the peer's public value G^Y
 */
int dhm_read_public( dhm_context *ctx,
                     unsigned char *input, int ilen )
{
    int ret;

    if( ctx == NULL || ilen < 1 || ilen > ctx->len )
        return( XYSSL_ERR_DHM_BAD_INPUT_DATA );

    if( ( ret = mpi_read_binary( &ctx->GY, input, ilen ) ) != 0 )
        return( XYSSL_ERR_DHM_READ_PUBLIC_FAILED | ret );

    return( 0 );
}

/*
 * Create own private value X and export G^X
 */
int dhm_make_public( dhm_context *ctx, int x_size,
                     unsigned char *output, int olen,
                     int (*f_rng)(void *), void *p_rng )
{
    int ret, i, n;
    unsigned char *p;

    if( ctx == NULL || olen < 1 || olen > ctx->len )
        return( XYSSL_ERR_DHM_BAD_INPUT_DATA );

    /*
     * generate X and calculate GX = G^X mod P
     */
    n = x_size / sizeof( t_int_xyssl );
    MPI_CHK( mpi_grow( &ctx->X, n ) );
    MPI_CHK( mpi_lset( &ctx->X, 0 ) );

    n = x_size >> 3;
    p = (unsigned char *) ctx->X.p;
    for( i = 0; i < n; i++ )
        *p++ = (unsigned char) f_rng( p_rng );

    while( mpi_cmp_mpi( &ctx->X, &ctx->P ) >= 0 )
           mpi_shift_r( &ctx->X, 1 );

    MPI_CHK( mpi_exp_mod( &ctx->GX, &ctx->G, &ctx->X,
                          &ctx->P , &ctx->TRP ) );

    MPI_CHK( mpi_write_binary( &ctx->GX, output, olen ) );

cleanup:

    if( ret != 0 )
        return( XYSSL_ERR_DHM_MAKE_PUBLIC_FAILED | ret );

    return( 0 );
}

/*
 * Derive and export the shared secret (G^Y)^X mod P
 */
int dhm_calc_secret( dhm_context *ctx,
                     unsigned char *output, int *olen )
{
    int ret;

    if( ctx == NULL || *olen < ctx->len )
        return( XYSSL_ERR_DHM_BAD_INPUT_DATA );

    MPI_CHK( mpi_exp_mod( &ctx->K, &ctx->GY, &ctx->X,
                          &ctx->P, &ctx->TRP ) );

    *olen = mpi_size( &ctx->K );

    MPI_CHK( mpi_write_binary( &ctx->K, output, *olen ) );

cleanup:

    if( ret != 0 )
        return( XYSSL_ERR_DHM_CALC_SECRET_FAILED | ret );

    return( 0 );
}

/*
 * Free the components of a DHM key
 */
void dhm_free( dhm_context *ctx )
{
    mpi_free( &ctx->TRP, &ctx->K, &ctx->GY,
              &ctx->GX, &ctx->X, &ctx->G,
              &ctx->P, NULL );    
}

#if defined(XYSSL_SELF_TEST)

/*
 * Checkup routine
 */
int dhm_self_test( int verbose )
{
    return( verbose++ );
}

#endif
