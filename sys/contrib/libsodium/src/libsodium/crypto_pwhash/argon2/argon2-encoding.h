#ifndef argon2_encoding_H
#define argon2_encoding_H

#include "argon2.h"

/*
 * encode an Argon2 hash string into the provided buffer. 'dst_len'
 * contains the size, in characters, of the 'dst' buffer; if 'dst_len'
 * is less than the number of required characters (including the
 * terminating 0), then this function returns 0.
 *
 * if ctx->outlen is 0, then the hash string will be a salt string
 * (no output). if ctx->saltlen is also 0, then the string will be a
 * parameter-only string (no salt and no output).
 *
 * On success, ARGON2_OK is returned.
 *
 * No other parameters are checked
 */
int encode_string(char *dst, size_t dst_len, argon2_context *ctx,
                  argon2_type type);

/*
 * Decodes an Argon2 hash string into the provided structure 'ctx'.
 * The fields ctx.saltlen, ctx.adlen, ctx.outlen set the maximal salt, ad, out
 * length values
 * that are allowed; invalid input string causes an error
 *
 * Returned value is ARGON2_OK on success.
 */
int decode_string(argon2_context *ctx, const char *str, argon2_type type);

#endif
