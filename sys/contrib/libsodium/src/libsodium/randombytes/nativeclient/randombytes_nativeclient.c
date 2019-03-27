
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __native_client__
# include <irt.h>

# include "core.h"
# include "utils.h"
# include "randombytes.h"
# include "randombytes_nativeclient.h"

static void
randombytes_nativeclient_buf(void * const buf, const size_t size)
{
    unsigned char          *buf_ = (unsigned char *) buf;
    struct nacl_irt_random  rand_intf;
    size_t                  readnb = (size_t) 0U;
    size_t                  toread = size;

    if (nacl_interface_query(NACL_IRT_RANDOM_v0_1, &rand_intf,
                             sizeof rand_intf) != sizeof rand_intf) {
        sodium_misuse();
    }
    while (toread > (size_t) 0U) {
        if (rand_intf.get_random_bytes(buf_, size, &readnb) != 0 ||
            readnb > size) {
            sodium_misuse();
        }
        toread -= readnb;
        buf_ += readnb;
    }
}

static uint32_t
randombytes_nativeclient_random(void)
{
    uint32_t r;

    randombytes_nativeclient_buf(&r, sizeof r);

    return r;
}

static const char *
randombytes_nativeclient_implementation_name(void)
{
    return "nativeclient";
}

struct randombytes_implementation randombytes_nativeclient_implementation = {
    SODIUM_C99(.implementation_name =) randombytes_nativeclient_implementation_name,
    SODIUM_C99(.random =) randombytes_nativeclient_random,
    SODIUM_C99(.stir =) NULL,
    SODIUM_C99(.uniform =) NULL,
    SODIUM_C99(.buf =) randombytes_nativeclient_buf,
    SODIUM_C99(.close =) NULL
};

#endif
