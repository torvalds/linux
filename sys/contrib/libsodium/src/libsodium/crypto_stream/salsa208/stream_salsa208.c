#include "crypto_stream_salsa208.h"
#include "randombytes.h"

size_t
crypto_stream_salsa208_keybytes(void)
{
    return crypto_stream_salsa208_KEYBYTES;
}

size_t
crypto_stream_salsa208_noncebytes(void)
{
    return crypto_stream_salsa208_NONCEBYTES;
}

size_t
crypto_stream_salsa208_messagebytes_max(void)
{
    return crypto_stream_salsa208_MESSAGEBYTES_MAX;
}

void
crypto_stream_salsa208_keygen(unsigned char k[crypto_stream_salsa208_KEYBYTES])
{
    randombytes_buf(k, crypto_stream_salsa208_KEYBYTES);
}
