#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include "core.h"
#include "crypto_aead_chacha20poly1305.h"
#include "crypto_aead_xchacha20poly1305.h"
#include "crypto_core_hchacha20.h"
#include "crypto_onetimeauth_poly1305.h"
#include "crypto_secretstream_xchacha20poly1305.h"
#include "randombytes.h"
#include "utils.h"

#include "private/common.h"

#define crypto_secretstream_xchacha20poly1305_COUNTERBYTES  4U
#define crypto_secretstream_xchacha20poly1305_INONCEBYTES   8U

#define STATE_COUNTER(STATE) ((STATE)->nonce)
#define STATE_INONCE(STATE)  ((STATE)->nonce + \
                              crypto_secretstream_xchacha20poly1305_COUNTERBYTES)

static const unsigned char _pad0[16] = { 0 };

static inline void
_crypto_secretstream_xchacha20poly1305_counter_reset
    (crypto_secretstream_xchacha20poly1305_state *state)
{
    memset(STATE_COUNTER(state), 0,
           crypto_secretstream_xchacha20poly1305_COUNTERBYTES);
    STATE_COUNTER(state)[0] = 1;
}

void
crypto_secretstream_xchacha20poly1305_keygen
   (unsigned char k[crypto_secretstream_xchacha20poly1305_KEYBYTES])
{
    randombytes_buf(k, crypto_secretstream_xchacha20poly1305_KEYBYTES);
}

int
crypto_secretstream_xchacha20poly1305_init_push
   (crypto_secretstream_xchacha20poly1305_state *state,
    unsigned char out[crypto_secretstream_xchacha20poly1305_HEADERBYTES],
    const unsigned char k[crypto_secretstream_xchacha20poly1305_KEYBYTES])
{
    COMPILER_ASSERT(crypto_secretstream_xchacha20poly1305_HEADERBYTES ==
                    crypto_core_hchacha20_INPUTBYTES +
                    crypto_secretstream_xchacha20poly1305_INONCEBYTES);
    COMPILER_ASSERT(crypto_secretstream_xchacha20poly1305_HEADERBYTES ==
                    crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    COMPILER_ASSERT(sizeof state->nonce ==
                    crypto_secretstream_xchacha20poly1305_INONCEBYTES +
                    crypto_secretstream_xchacha20poly1305_COUNTERBYTES);

    randombytes_buf(out, crypto_secretstream_xchacha20poly1305_HEADERBYTES);
    crypto_core_hchacha20(state->k, out, k, NULL);
    _crypto_secretstream_xchacha20poly1305_counter_reset(state);
    memcpy(STATE_INONCE(state), out + crypto_core_hchacha20_INPUTBYTES,
           crypto_secretstream_xchacha20poly1305_INONCEBYTES);
    memset(state->_pad, 0, sizeof state->_pad);

    return 0;
}

int
crypto_secretstream_xchacha20poly1305_init_pull
   (crypto_secretstream_xchacha20poly1305_state *state,
    const unsigned char in[crypto_secretstream_xchacha20poly1305_HEADERBYTES],
    const unsigned char k[crypto_secretstream_xchacha20poly1305_KEYBYTES])
{
    crypto_core_hchacha20(state->k, in, k, NULL);
    _crypto_secretstream_xchacha20poly1305_counter_reset(state);
    memcpy(STATE_INONCE(state), in + crypto_core_hchacha20_INPUTBYTES,
           crypto_secretstream_xchacha20poly1305_INONCEBYTES);
    memset(state->_pad, 0, sizeof state->_pad);

    return 0;
}

void
crypto_secretstream_xchacha20poly1305_rekey
    (crypto_secretstream_xchacha20poly1305_state *state)
{
    unsigned char new_key_and_inonce[crypto_stream_chacha20_ietf_KEYBYTES +
                                     crypto_secretstream_xchacha20poly1305_INONCEBYTES];
    size_t        i;

    for (i = 0U; i < crypto_stream_chacha20_ietf_KEYBYTES; i++) {
        new_key_and_inonce[i] = state->k[i];
    }
    for (i = 0U; i < crypto_secretstream_xchacha20poly1305_INONCEBYTES; i++) {
        new_key_and_inonce[crypto_stream_chacha20_ietf_KEYBYTES + i] =
            STATE_INONCE(state)[i];
    }
    crypto_stream_chacha20_ietf_xor(new_key_and_inonce, new_key_and_inonce,
                                    sizeof new_key_and_inonce,
                                    state->nonce, state->k);
    for (i = 0U; i < crypto_stream_chacha20_ietf_KEYBYTES; i++) {
        state->k[i] = new_key_and_inonce[i];
    }
    for (i = 0U; i < crypto_secretstream_xchacha20poly1305_INONCEBYTES; i++) {
        STATE_INONCE(state)[i] =
            new_key_and_inonce[crypto_stream_chacha20_ietf_KEYBYTES + i];
    }
    _crypto_secretstream_xchacha20poly1305_counter_reset(state);
}

int
crypto_secretstream_xchacha20poly1305_push
   (crypto_secretstream_xchacha20poly1305_state *state,
    unsigned char *out, unsigned long long *outlen_p,
    const unsigned char *m, unsigned long long mlen,
    const unsigned char *ad, unsigned long long adlen, unsigned char tag)
{
    crypto_onetimeauth_poly1305_state poly1305_state;
    unsigned char                     block[64U];
    unsigned char                     slen[8U];
    unsigned char                    *c;
    unsigned char                    *mac;

    if (outlen_p != NULL) {
        *outlen_p = 0U;
    }
    if (mlen > crypto_secretstream_xchacha20poly1305_MESSAGEBYTES_MAX) {
        sodium_misuse();
    }
    crypto_stream_chacha20_ietf(block, sizeof block, state->nonce, state->k);
    crypto_onetimeauth_poly1305_init(&poly1305_state, block);
    sodium_memzero(block, sizeof block);

    crypto_onetimeauth_poly1305_update(&poly1305_state, ad, adlen);
    crypto_onetimeauth_poly1305_update(&poly1305_state, _pad0,
                                       (0x10 - adlen) & 0xf);
    memset(block, 0, sizeof block);
    block[0] = tag;

    crypto_stream_chacha20_ietf_xor_ic(block, block, sizeof block,
                                       state->nonce, 1U, state->k);
    crypto_onetimeauth_poly1305_update(&poly1305_state, block, sizeof block);
    out[0] = block[0];

    c = out + (sizeof tag);
    crypto_stream_chacha20_ietf_xor_ic(c, m, mlen, state->nonce, 2U, state->k);
    crypto_onetimeauth_poly1305_update(&poly1305_state, c, mlen);
    crypto_onetimeauth_poly1305_update
        (&poly1305_state, _pad0, (0x10 - (sizeof block) + mlen) & 0xf);

    STORE64_LE(slen, (uint64_t) adlen);
    crypto_onetimeauth_poly1305_update(&poly1305_state, slen, sizeof slen);
    STORE64_LE(slen, (sizeof block) + mlen);
    crypto_onetimeauth_poly1305_update(&poly1305_state, slen, sizeof slen);

    mac = c + mlen;
    crypto_onetimeauth_poly1305_final(&poly1305_state, mac);
    sodium_memzero(&poly1305_state, sizeof poly1305_state);

    COMPILER_ASSERT(crypto_onetimeauth_poly1305_BYTES >=
                    crypto_secretstream_xchacha20poly1305_INONCEBYTES);
    XOR_BUF(STATE_INONCE(state), mac,
            crypto_secretstream_xchacha20poly1305_INONCEBYTES);
    sodium_increment(STATE_COUNTER(state),
                     crypto_secretstream_xchacha20poly1305_COUNTERBYTES);
    if ((tag & crypto_secretstream_xchacha20poly1305_TAG_REKEY) != 0 ||
        sodium_is_zero(STATE_COUNTER(state),
                       crypto_secretstream_xchacha20poly1305_COUNTERBYTES)) {
        crypto_secretstream_xchacha20poly1305_rekey(state);
    }
    if (outlen_p != NULL) {
        *outlen_p = crypto_secretstream_xchacha20poly1305_ABYTES + mlen;
    }
    return 0;
}

int
crypto_secretstream_xchacha20poly1305_pull
   (crypto_secretstream_xchacha20poly1305_state *state,
    unsigned char *m, unsigned long long *mlen_p, unsigned char *tag_p,
    const unsigned char *in, unsigned long long inlen,
    const unsigned char *ad, unsigned long long adlen)
{
    crypto_onetimeauth_poly1305_state poly1305_state;
    unsigned char                     block[64U];
    unsigned char                     slen[8U];
    unsigned char                     mac[crypto_onetimeauth_poly1305_BYTES];
    const unsigned char              *c;
    const unsigned char              *stored_mac;
    unsigned long long                mlen;
    unsigned char                     tag;

    if (mlen_p != NULL) {
        *mlen_p = 0U;
    }
    if (tag_p != NULL) {
        *tag_p = 0xff;
    }
    if (inlen < crypto_secretstream_xchacha20poly1305_ABYTES) {
        return -1;
    }
    mlen = inlen - crypto_secretstream_xchacha20poly1305_ABYTES;
    if (mlen > crypto_secretstream_xchacha20poly1305_MESSAGEBYTES_MAX) {
        sodium_misuse();
    }
    crypto_stream_chacha20_ietf(block, sizeof block, state->nonce, state->k);
    crypto_onetimeauth_poly1305_init(&poly1305_state, block);
    sodium_memzero(block, sizeof block);

    crypto_onetimeauth_poly1305_update(&poly1305_state, ad, adlen);
    crypto_onetimeauth_poly1305_update(&poly1305_state, _pad0,
                                       (0x10 - adlen) & 0xf);

    memset(block, 0, sizeof block);
    block[0] = in[0];
    crypto_stream_chacha20_ietf_xor_ic(block, block, sizeof block,
                                       state->nonce, 1U, state->k);
    tag = block[0];
    block[0] = in[0];
    crypto_onetimeauth_poly1305_update(&poly1305_state, block, sizeof block);

    c = in + (sizeof tag);
    crypto_onetimeauth_poly1305_update(&poly1305_state, c, mlen);
    crypto_onetimeauth_poly1305_update
        (&poly1305_state, _pad0, (0x10 - (sizeof block) + mlen) & 0xf);

    STORE64_LE(slen, (uint64_t) adlen);
    crypto_onetimeauth_poly1305_update(&poly1305_state, slen, sizeof slen);
    STORE64_LE(slen, (sizeof block) + mlen);
    crypto_onetimeauth_poly1305_update(&poly1305_state, slen, sizeof slen);

    crypto_onetimeauth_poly1305_final(&poly1305_state, mac);
    sodium_memzero(&poly1305_state, sizeof poly1305_state);

    stored_mac = c + mlen;
    if (sodium_memcmp(mac, stored_mac, sizeof mac) != 0) {
        sodium_memzero(mac, sizeof mac);
        return -1;
    }

    crypto_stream_chacha20_ietf_xor_ic(m, c, mlen, state->nonce, 2U, state->k);
    XOR_BUF(STATE_INONCE(state), mac,
            crypto_secretstream_xchacha20poly1305_INONCEBYTES);
    sodium_increment(STATE_COUNTER(state),
                     crypto_secretstream_xchacha20poly1305_COUNTERBYTES);
    if ((tag & crypto_secretstream_xchacha20poly1305_TAG_REKEY) != 0 ||
        sodium_is_zero(STATE_COUNTER(state),
                       crypto_secretstream_xchacha20poly1305_COUNTERBYTES)) {
        crypto_secretstream_xchacha20poly1305_rekey(state);
    }
    if (mlen_p != NULL) {
        *mlen_p = mlen;
    }
    if (tag_p != NULL) {
        *tag_p = tag;
    }
    return 0;
}

size_t
crypto_secretstream_xchacha20poly1305_statebytes(void)
{
    return sizeof(crypto_secretstream_xchacha20poly1305_state);
}

size_t
crypto_secretstream_xchacha20poly1305_abytes(void)
{
    return crypto_secretstream_xchacha20poly1305_ABYTES;
}

size_t
crypto_secretstream_xchacha20poly1305_headerbytes(void)
{
    return crypto_secretstream_xchacha20poly1305_HEADERBYTES;
}

size_t
crypto_secretstream_xchacha20poly1305_keybytes(void)
{
    return crypto_secretstream_xchacha20poly1305_KEYBYTES;
}

size_t
crypto_secretstream_xchacha20poly1305_messagebytes_max(void)
{
    return crypto_secretstream_xchacha20poly1305_MESSAGEBYTES_MAX;
}

unsigned char
crypto_secretstream_xchacha20poly1305_tag_message(void)
{
    return crypto_secretstream_xchacha20poly1305_TAG_MESSAGE;
}

unsigned char
crypto_secretstream_xchacha20poly1305_tag_push(void)
{
    return crypto_secretstream_xchacha20poly1305_TAG_PUSH;
}

unsigned char
crypto_secretstream_xchacha20poly1305_tag_rekey(void)
{
    return crypto_secretstream_xchacha20poly1305_TAG_REKEY;
}

unsigned char
crypto_secretstream_xchacha20poly1305_tag_final(void)
{
    return crypto_secretstream_xchacha20poly1305_TAG_FINAL;
}
