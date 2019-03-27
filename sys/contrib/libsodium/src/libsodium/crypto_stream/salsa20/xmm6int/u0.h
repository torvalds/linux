if (bytes > 0) {
    __m128i diag0 = _mm_loadu_si128((__m128i *) (x + 0));
    __m128i diag1 = _mm_loadu_si128((__m128i *) (x + 4));
    __m128i diag2 = _mm_loadu_si128((__m128i *) (x + 8));
    __m128i diag3 = _mm_loadu_si128((__m128i *) (x + 12));
    __m128i a0, a1, a2, a3, a4, a5, a6, a7;
    __m128i b0, b1, b2, b3, b4, b5, b6, b7;
    uint8_t partialblock[64];

    unsigned int i;

    a0 = diag1;
    for (i = 0; i < ROUNDS; i += 4) {
        a0    = _mm_add_epi32(a0, diag0);
        a1    = diag0;
        b0    = a0;
        a0    = _mm_slli_epi32(a0, 7);
        b0    = _mm_srli_epi32(b0, 25);
        diag3 = _mm_xor_si128(diag3, a0);

        diag3 = _mm_xor_si128(diag3, b0);

        a1    = _mm_add_epi32(a1, diag3);
        a2    = diag3;
        b1    = a1;
        a1    = _mm_slli_epi32(a1, 9);
        b1    = _mm_srli_epi32(b1, 23);
        diag2 = _mm_xor_si128(diag2, a1);
        diag3 = _mm_shuffle_epi32(diag3, 0x93);
        diag2 = _mm_xor_si128(diag2, b1);

        a2    = _mm_add_epi32(a2, diag2);
        a3    = diag2;
        b2    = a2;
        a2    = _mm_slli_epi32(a2, 13);
        b2    = _mm_srli_epi32(b2, 19);
        diag1 = _mm_xor_si128(diag1, a2);
        diag2 = _mm_shuffle_epi32(diag2, 0x4e);
        diag1 = _mm_xor_si128(diag1, b2);

        a3    = _mm_add_epi32(a3, diag1);
        a4    = diag3;
        b3    = a3;
        a3    = _mm_slli_epi32(a3, 18);
        b3    = _mm_srli_epi32(b3, 14);
        diag0 = _mm_xor_si128(diag0, a3);
        diag1 = _mm_shuffle_epi32(diag1, 0x39);
        diag0 = _mm_xor_si128(diag0, b3);

        a4    = _mm_add_epi32(a4, diag0);
        a5    = diag0;
        b4    = a4;
        a4    = _mm_slli_epi32(a4, 7);
        b4    = _mm_srli_epi32(b4, 25);
        diag1 = _mm_xor_si128(diag1, a4);

        diag1 = _mm_xor_si128(diag1, b4);

        a5    = _mm_add_epi32(a5, diag1);
        a6    = diag1;
        b5    = a5;
        a5    = _mm_slli_epi32(a5, 9);
        b5    = _mm_srli_epi32(b5, 23);
        diag2 = _mm_xor_si128(diag2, a5);
        diag1 = _mm_shuffle_epi32(diag1, 0x93);
        diag2 = _mm_xor_si128(diag2, b5);

        a6    = _mm_add_epi32(a6, diag2);
        a7    = diag2;
        b6    = a6;
        a6    = _mm_slli_epi32(a6, 13);
        b6    = _mm_srli_epi32(b6, 19);
        diag3 = _mm_xor_si128(diag3, a6);
        diag2 = _mm_shuffle_epi32(diag2, 0x4e);
        diag3 = _mm_xor_si128(diag3, b6);

        a7    = _mm_add_epi32(a7, diag3);
        a0    = diag1;
        b7    = a7;
        a7    = _mm_slli_epi32(a7, 18);
        b7    = _mm_srli_epi32(b7, 14);
        diag0 = _mm_xor_si128(diag0, a7);
        diag3 = _mm_shuffle_epi32(diag3, 0x39);
        diag0 = _mm_xor_si128(diag0, b7);

        a0    = _mm_add_epi32(a0, diag0);
        a1    = diag0;
        b0    = a0;
        a0    = _mm_slli_epi32(a0, 7);
        b0    = _mm_srli_epi32(b0, 25);
        diag3 = _mm_xor_si128(diag3, a0);

        diag3 = _mm_xor_si128(diag3, b0);

        a1    = _mm_add_epi32(a1, diag3);
        a2    = diag3;
        b1    = a1;
        a1    = _mm_slli_epi32(a1, 9);
        b1    = _mm_srli_epi32(b1, 23);
        diag2 = _mm_xor_si128(diag2, a1);
        diag3 = _mm_shuffle_epi32(diag3, 0x93);
        diag2 = _mm_xor_si128(diag2, b1);

        a2    = _mm_add_epi32(a2, diag2);
        a3    = diag2;
        b2    = a2;
        a2    = _mm_slli_epi32(a2, 13);
        b2    = _mm_srli_epi32(b2, 19);
        diag1 = _mm_xor_si128(diag1, a2);
        diag2 = _mm_shuffle_epi32(diag2, 0x4e);
        diag1 = _mm_xor_si128(diag1, b2);

        a3    = _mm_add_epi32(a3, diag1);
        a4    = diag3;
        b3    = a3;
        a3    = _mm_slli_epi32(a3, 18);
        b3    = _mm_srli_epi32(b3, 14);
        diag0 = _mm_xor_si128(diag0, a3);
        diag1 = _mm_shuffle_epi32(diag1, 0x39);
        diag0 = _mm_xor_si128(diag0, b3);

        a4    = _mm_add_epi32(a4, diag0);
        a5    = diag0;
        b4    = a4;
        a4    = _mm_slli_epi32(a4, 7);
        b4    = _mm_srli_epi32(b4, 25);
        diag1 = _mm_xor_si128(diag1, a4);

        diag1 = _mm_xor_si128(diag1, b4);

        a5    = _mm_add_epi32(a5, diag1);
        a6    = diag1;
        b5    = a5;
        a5    = _mm_slli_epi32(a5, 9);
        b5    = _mm_srli_epi32(b5, 23);
        diag2 = _mm_xor_si128(diag2, a5);
        diag1 = _mm_shuffle_epi32(diag1, 0x93);
        diag2 = _mm_xor_si128(diag2, b5);

        a6    = _mm_add_epi32(a6, diag2);
        a7    = diag2;
        b6    = a6;
        a6    = _mm_slli_epi32(a6, 13);
        b6    = _mm_srli_epi32(b6, 19);
        diag3 = _mm_xor_si128(diag3, a6);
        diag2 = _mm_shuffle_epi32(diag2, 0x4e);
        diag3 = _mm_xor_si128(diag3, b6);

        a7    = _mm_add_epi32(a7, diag3);
        a0    = diag1;
        b7    = a7;
        a7    = _mm_slli_epi32(a7, 18);
        b7    = _mm_srli_epi32(b7, 14);
        diag0 = _mm_xor_si128(diag0, a7);
        diag3 = _mm_shuffle_epi32(diag3, 0x39);
        diag0 = _mm_xor_si128(diag0, b7);
    }

    diag0 = _mm_add_epi32(diag0, _mm_loadu_si128((__m128i *) (x + 0)));
    diag1 = _mm_add_epi32(diag1, _mm_loadu_si128((__m128i *) (x + 4)));
    diag2 = _mm_add_epi32(diag2, _mm_loadu_si128((__m128i *) (x + 8)));
    diag3 = _mm_add_epi32(diag3, _mm_loadu_si128((__m128i *) (x + 12)));

#define ONEQUAD_SHUFFLE(A, B, C, D)                      \
    do {                                                 \
        uint32_t in##A = _mm_cvtsi128_si32(diag0);       \
        uint32_t in##B = _mm_cvtsi128_si32(diag1);       \
        uint32_t in##C = _mm_cvtsi128_si32(diag2);       \
        uint32_t in##D = _mm_cvtsi128_si32(diag3);       \
        diag0          = _mm_shuffle_epi32(diag0, 0x39); \
        diag1          = _mm_shuffle_epi32(diag1, 0x39); \
        diag2          = _mm_shuffle_epi32(diag2, 0x39); \
        diag3          = _mm_shuffle_epi32(diag3, 0x39); \
        *(uint32_t *) (partialblock + (A * 4)) = in##A;  \
        *(uint32_t *) (partialblock + (B * 4)) = in##B;  \
        *(uint32_t *) (partialblock + (C * 4)) = in##C;  \
        *(uint32_t *) (partialblock + (D * 4)) = in##D;  \
    } while (0)

#define ONEQUAD(A, B, C, D) ONEQUAD_SHUFFLE(A, B, C, D)

    ONEQUAD(0, 12, 8, 4);
    ONEQUAD(5, 1, 13, 9);
    ONEQUAD(10, 6, 2, 14);
    ONEQUAD(15, 11, 7, 3);

#undef ONEQUAD
#undef ONEQUAD_SHUFFLE

    for (i = 0; i < bytes; i++) {
        c[i] = m[i] ^ partialblock[i];
    }

    sodium_memzero(partialblock, sizeof partialblock);
}
