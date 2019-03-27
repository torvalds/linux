#include "crypto_core_hsalsa20.h"

size_t
crypto_core_hsalsa20_outputbytes(void) {
    return crypto_core_hsalsa20_OUTPUTBYTES;
}

size_t
crypto_core_hsalsa20_inputbytes(void) {
    return crypto_core_hsalsa20_INPUTBYTES;
}

size_t
crypto_core_hsalsa20_keybytes(void) {
    return crypto_core_hsalsa20_KEYBYTES;
}

size_t
crypto_core_hsalsa20_constbytes(void) {
    return crypto_core_hsalsa20_CONSTBYTES;
}
