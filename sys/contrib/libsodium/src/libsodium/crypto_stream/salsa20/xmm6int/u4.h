if (bytes >= 256) {
    __m128i y0, y1, y2, y3, y4, y5, y6, y7, y8, y9, y10, y11, y12, y13, y14,
        y15;
    __m128i z0, z1, z2, z3, z4, z5, z6, z7, z8, z9, z10, z11, z12, z13, z14,
        z15;
    __m128i orig0, orig1, orig2, orig3, orig4, orig5, orig6, orig7, orig8,
        orig9, orig10, orig11, orig12, orig13, orig14, orig15;

    uint32_t in8;
    uint32_t in9;
    int      i;

    /* element broadcast immediate for _mm_shuffle_epi32 are in order:
       0x00, 0x55, 0xaa, 0xff */
    z0  = _mm_loadu_si128((__m128i *) (x + 0));
    z5  = _mm_shuffle_epi32(z0, 0x55);
    z10 = _mm_shuffle_epi32(z0, 0xaa);
    z15 = _mm_shuffle_epi32(z0, 0xff);
    z0  = _mm_shuffle_epi32(z0, 0x00);
    z1  = _mm_loadu_si128((__m128i *) (x + 4));
    z6  = _mm_shuffle_epi32(z1, 0xaa);
    z11 = _mm_shuffle_epi32(z1, 0xff);
    z12 = _mm_shuffle_epi32(z1, 0x00);
    z1  = _mm_shuffle_epi32(z1, 0x55);
    z2  = _mm_loadu_si128((__m128i *) (x + 8));
    z7  = _mm_shuffle_epi32(z2, 0xff);
    z13 = _mm_shuffle_epi32(z2, 0x55);
    z2  = _mm_shuffle_epi32(z2, 0xaa);
    /* no z8 -> first half of the nonce, will fill later */
    z3  = _mm_loadu_si128((__m128i *) (x + 12));
    z4  = _mm_shuffle_epi32(z3, 0x00);
    z14 = _mm_shuffle_epi32(z3, 0xaa);
    z3  = _mm_shuffle_epi32(z3, 0xff);
    /* no z9 -> second half of the nonce, will fill later */
    orig0  = z0;
    orig1  = z1;
    orig2  = z2;
    orig3  = z3;
    orig4  = z4;
    orig5  = z5;
    orig6  = z6;
    orig7  = z7;
    orig10 = z10;
    orig11 = z11;
    orig12 = z12;
    orig13 = z13;
    orig14 = z14;
    orig15 = z15;

    while (bytes >= 256) {
        /* vector implementation for z8 and z9 */
        /* not sure if it helps for only 4 blocks */
        const __m128i addv8 = _mm_set_epi64x(1, 0);
        const __m128i addv9 = _mm_set_epi64x(3, 2);
        __m128i       t8, t9;
        uint64_t      in89;

        in8  = x[8];
        in9  = x[13];
        in89 = ((uint64_t) in8) | (((uint64_t) in9) << 32);
        t8   = _mm_set1_epi64x(in89);
        t9   = _mm_set1_epi64x(in89);

        z8 = _mm_add_epi64(addv8, t8);
        z9 = _mm_add_epi64(addv9, t9);

        t8 = _mm_unpacklo_epi32(z8, z9);
        t9 = _mm_unpackhi_epi32(z8, z9);

        z8 = _mm_unpacklo_epi32(t8, t9);
        z9 = _mm_unpackhi_epi32(t8, t9);

        orig8 = z8;
        orig9 = z9;

        in89 += 4;

        x[8]  = in89 & 0xFFFFFFFF;
        x[13] = (in89 >> 32) & 0xFFFFFFFF;

        z5  = orig5;
        z10 = orig10;
        z15 = orig15;
        z14 = orig14;
        z3  = orig3;
        z6  = orig6;
        z11 = orig11;
        z1  = orig1;

        z7  = orig7;
        z13 = orig13;
        z2  = orig2;
        z9  = orig9;
        z0  = orig0;
        z12 = orig12;
        z4  = orig4;
        z8  = orig8;

        for (i = 0; i < ROUNDS; i += 2) {
            /* the inner loop is a direct translation (regexp search/replace)
             * from the amd64-xmm6 ASM */
            __m128i r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13,
                r14, r15;

            y4 = z12;
            y4 = _mm_add_epi32(y4, z0);
            r4 = y4;
            y4 = _mm_slli_epi32(y4, 7);
            z4 = _mm_xor_si128(z4, y4);
            r4 = _mm_srli_epi32(r4, 25);
            z4 = _mm_xor_si128(z4, r4);

            y9 = z1;
            y9 = _mm_add_epi32(y9, z5);
            r9 = y9;
            y9 = _mm_slli_epi32(y9, 7);
            z9 = _mm_xor_si128(z9, y9);
            r9 = _mm_srli_epi32(r9, 25);
            z9 = _mm_xor_si128(z9, r9);

            y8 = z0;
            y8 = _mm_add_epi32(y8, z4);
            r8 = y8;
            y8 = _mm_slli_epi32(y8, 9);
            z8 = _mm_xor_si128(z8, y8);
            r8 = _mm_srli_epi32(r8, 23);
            z8 = _mm_xor_si128(z8, r8);

            y13 = z5;
            y13 = _mm_add_epi32(y13, z9);
            r13 = y13;
            y13 = _mm_slli_epi32(y13, 9);
            z13 = _mm_xor_si128(z13, y13);
            r13 = _mm_srli_epi32(r13, 23);
            z13 = _mm_xor_si128(z13, r13);

            y12 = z4;
            y12 = _mm_add_epi32(y12, z8);
            r12 = y12;
            y12 = _mm_slli_epi32(y12, 13);
            z12 = _mm_xor_si128(z12, y12);
            r12 = _mm_srli_epi32(r12, 19);
            z12 = _mm_xor_si128(z12, r12);

            y1 = z9;
            y1 = _mm_add_epi32(y1, z13);
            r1 = y1;
            y1 = _mm_slli_epi32(y1, 13);
            z1 = _mm_xor_si128(z1, y1);
            r1 = _mm_srli_epi32(r1, 19);
            z1 = _mm_xor_si128(z1, r1);

            y0 = z8;
            y0 = _mm_add_epi32(y0, z12);
            r0 = y0;
            y0 = _mm_slli_epi32(y0, 18);
            z0 = _mm_xor_si128(z0, y0);
            r0 = _mm_srli_epi32(r0, 14);
            z0 = _mm_xor_si128(z0, r0);

            y5 = z13;
            y5 = _mm_add_epi32(y5, z1);
            r5 = y5;
            y5 = _mm_slli_epi32(y5, 18);
            z5 = _mm_xor_si128(z5, y5);
            r5 = _mm_srli_epi32(r5, 14);
            z5 = _mm_xor_si128(z5, r5);

            y14 = z6;
            y14 = _mm_add_epi32(y14, z10);
            r14 = y14;
            y14 = _mm_slli_epi32(y14, 7);
            z14 = _mm_xor_si128(z14, y14);
            r14 = _mm_srli_epi32(r14, 25);
            z14 = _mm_xor_si128(z14, r14);

            y3 = z11;
            y3 = _mm_add_epi32(y3, z15);
            r3 = y3;
            y3 = _mm_slli_epi32(y3, 7);
            z3 = _mm_xor_si128(z3, y3);
            r3 = _mm_srli_epi32(r3, 25);
            z3 = _mm_xor_si128(z3, r3);

            y2 = z10;
            y2 = _mm_add_epi32(y2, z14);
            r2 = y2;
            y2 = _mm_slli_epi32(y2, 9);
            z2 = _mm_xor_si128(z2, y2);
            r2 = _mm_srli_epi32(r2, 23);
            z2 = _mm_xor_si128(z2, r2);

            y7 = z15;
            y7 = _mm_add_epi32(y7, z3);
            r7 = y7;
            y7 = _mm_slli_epi32(y7, 9);
            z7 = _mm_xor_si128(z7, y7);
            r7 = _mm_srli_epi32(r7, 23);
            z7 = _mm_xor_si128(z7, r7);

            y6 = z14;
            y6 = _mm_add_epi32(y6, z2);
            r6 = y6;
            y6 = _mm_slli_epi32(y6, 13);
            z6 = _mm_xor_si128(z6, y6);
            r6 = _mm_srli_epi32(r6, 19);
            z6 = _mm_xor_si128(z6, r6);

            y11 = z3;
            y11 = _mm_add_epi32(y11, z7);
            r11 = y11;
            y11 = _mm_slli_epi32(y11, 13);
            z11 = _mm_xor_si128(z11, y11);
            r11 = _mm_srli_epi32(r11, 19);
            z11 = _mm_xor_si128(z11, r11);

            y10 = z2;
            y10 = _mm_add_epi32(y10, z6);
            r10 = y10;
            y10 = _mm_slli_epi32(y10, 18);
            z10 = _mm_xor_si128(z10, y10);
            r10 = _mm_srli_epi32(r10, 14);
            z10 = _mm_xor_si128(z10, r10);

            y1 = z3;
            y1 = _mm_add_epi32(y1, z0);
            r1 = y1;
            y1 = _mm_slli_epi32(y1, 7);
            z1 = _mm_xor_si128(z1, y1);
            r1 = _mm_srli_epi32(r1, 25);
            z1 = _mm_xor_si128(z1, r1);

            y15 = z7;
            y15 = _mm_add_epi32(y15, z11);
            r15 = y15;
            y15 = _mm_slli_epi32(y15, 18);
            z15 = _mm_xor_si128(z15, y15);
            r15 = _mm_srli_epi32(r15, 14);
            z15 = _mm_xor_si128(z15, r15);

            y6 = z4;
            y6 = _mm_add_epi32(y6, z5);
            r6 = y6;
            y6 = _mm_slli_epi32(y6, 7);
            z6 = _mm_xor_si128(z6, y6);
            r6 = _mm_srli_epi32(r6, 25);
            z6 = _mm_xor_si128(z6, r6);

            y2 = z0;
            y2 = _mm_add_epi32(y2, z1);
            r2 = y2;
            y2 = _mm_slli_epi32(y2, 9);
            z2 = _mm_xor_si128(z2, y2);
            r2 = _mm_srli_epi32(r2, 23);
            z2 = _mm_xor_si128(z2, r2);

            y7 = z5;
            y7 = _mm_add_epi32(y7, z6);
            r7 = y7;
            y7 = _mm_slli_epi32(y7, 9);
            z7 = _mm_xor_si128(z7, y7);
            r7 = _mm_srli_epi32(r7, 23);
            z7 = _mm_xor_si128(z7, r7);

            y3 = z1;
            y3 = _mm_add_epi32(y3, z2);
            r3 = y3;
            y3 = _mm_slli_epi32(y3, 13);
            z3 = _mm_xor_si128(z3, y3);
            r3 = _mm_srli_epi32(r3, 19);
            z3 = _mm_xor_si128(z3, r3);

            y4 = z6;
            y4 = _mm_add_epi32(y4, z7);
            r4 = y4;
            y4 = _mm_slli_epi32(y4, 13);
            z4 = _mm_xor_si128(z4, y4);
            r4 = _mm_srli_epi32(r4, 19);
            z4 = _mm_xor_si128(z4, r4);

            y0 = z2;
            y0 = _mm_add_epi32(y0, z3);
            r0 = y0;
            y0 = _mm_slli_epi32(y0, 18);
            z0 = _mm_xor_si128(z0, y0);
            r0 = _mm_srli_epi32(r0, 14);
            z0 = _mm_xor_si128(z0, r0);

            y5 = z7;
            y5 = _mm_add_epi32(y5, z4);
            r5 = y5;
            y5 = _mm_slli_epi32(y5, 18);
            z5 = _mm_xor_si128(z5, y5);
            r5 = _mm_srli_epi32(r5, 14);
            z5 = _mm_xor_si128(z5, r5);

            y11 = z9;
            y11 = _mm_add_epi32(y11, z10);
            r11 = y11;
            y11 = _mm_slli_epi32(y11, 7);
            z11 = _mm_xor_si128(z11, y11);
            r11 = _mm_srli_epi32(r11, 25);
            z11 = _mm_xor_si128(z11, r11);

            y12 = z14;
            y12 = _mm_add_epi32(y12, z15);
            r12 = y12;
            y12 = _mm_slli_epi32(y12, 7);
            z12 = _mm_xor_si128(z12, y12);
            r12 = _mm_srli_epi32(r12, 25);
            z12 = _mm_xor_si128(z12, r12);

            y8 = z10;
            y8 = _mm_add_epi32(y8, z11);
            r8 = y8;
            y8 = _mm_slli_epi32(y8, 9);
            z8 = _mm_xor_si128(z8, y8);
            r8 = _mm_srli_epi32(r8, 23);
            z8 = _mm_xor_si128(z8, r8);

            y13 = z15;
            y13 = _mm_add_epi32(y13, z12);
            r13 = y13;
            y13 = _mm_slli_epi32(y13, 9);
            z13 = _mm_xor_si128(z13, y13);
            r13 = _mm_srli_epi32(r13, 23);
            z13 = _mm_xor_si128(z13, r13);

            y9 = z11;
            y9 = _mm_add_epi32(y9, z8);
            r9 = y9;
            y9 = _mm_slli_epi32(y9, 13);
            z9 = _mm_xor_si128(z9, y9);
            r9 = _mm_srli_epi32(r9, 19);
            z9 = _mm_xor_si128(z9, r9);

            y14 = z12;
            y14 = _mm_add_epi32(y14, z13);
            r14 = y14;
            y14 = _mm_slli_epi32(y14, 13);
            z14 = _mm_xor_si128(z14, y14);
            r14 = _mm_srli_epi32(r14, 19);
            z14 = _mm_xor_si128(z14, r14);

            y10 = z8;
            y10 = _mm_add_epi32(y10, z9);
            r10 = y10;
            y10 = _mm_slli_epi32(y10, 18);
            z10 = _mm_xor_si128(z10, y10);
            r10 = _mm_srli_epi32(r10, 14);
            z10 = _mm_xor_si128(z10, r10);

            y15 = z13;
            y15 = _mm_add_epi32(y15, z14);
            r15 = y15;
            y15 = _mm_slli_epi32(y15, 18);
            z15 = _mm_xor_si128(z15, y15);
            r15 = _mm_srli_epi32(r15, 14);
            z15 = _mm_xor_si128(z15, r15);
        }

/* store data ; this macro replicates the original amd64-xmm6 code */
#define ONEQUAD_SHUFFLE(A, B, C, D)        \
    z##A  = _mm_add_epi32(z##A, orig##A);  \
    z##B  = _mm_add_epi32(z##B, orig##B);  \
    z##C  = _mm_add_epi32(z##C, orig##C);  \
    z##D  = _mm_add_epi32(z##D, orig##D);  \
    in##A = _mm_cvtsi128_si32(z##A);       \
    in##B = _mm_cvtsi128_si32(z##B);       \
    in##C = _mm_cvtsi128_si32(z##C);       \
    in##D = _mm_cvtsi128_si32(z##D);       \
    z##A  = _mm_shuffle_epi32(z##A, 0x39); \
    z##B  = _mm_shuffle_epi32(z##B, 0x39); \
    z##C  = _mm_shuffle_epi32(z##C, 0x39); \
    z##D  = _mm_shuffle_epi32(z##D, 0x39); \
                                           \
    in##A ^= *(uint32_t *) (m + 0);        \
    in##B ^= *(uint32_t *) (m + 4);        \
    in##C ^= *(uint32_t *) (m + 8);        \
    in##D ^= *(uint32_t *) (m + 12);       \
                                           \
    *(uint32_t *) (c + 0)  = in##A;        \
    *(uint32_t *) (c + 4)  = in##B;        \
    *(uint32_t *) (c + 8)  = in##C;        \
    *(uint32_t *) (c + 12) = in##D;        \
                                           \
    in##A = _mm_cvtsi128_si32(z##A);       \
    in##B = _mm_cvtsi128_si32(z##B);       \
    in##C = _mm_cvtsi128_si32(z##C);       \
    in##D = _mm_cvtsi128_si32(z##D);       \
    z##A  = _mm_shuffle_epi32(z##A, 0x39); \
    z##B  = _mm_shuffle_epi32(z##B, 0x39); \
    z##C  = _mm_shuffle_epi32(z##C, 0x39); \
    z##D  = _mm_shuffle_epi32(z##D, 0x39); \
                                           \
    in##A ^= *(uint32_t *) (m + 64);       \
    in##B ^= *(uint32_t *) (m + 68);       \
    in##C ^= *(uint32_t *) (m + 72);       \
    in##D ^= *(uint32_t *) (m + 76);       \
    *(uint32_t *) (c + 64) = in##A;        \
    *(uint32_t *) (c + 68) = in##B;        \
    *(uint32_t *) (c + 72) = in##C;        \
    *(uint32_t *) (c + 76) = in##D;        \
                                           \
    in##A = _mm_cvtsi128_si32(z##A);       \
    in##B = _mm_cvtsi128_si32(z##B);       \
    in##C = _mm_cvtsi128_si32(z##C);       \
    in##D = _mm_cvtsi128_si32(z##D);       \
    z##A  = _mm_shuffle_epi32(z##A, 0x39); \
    z##B  = _mm_shuffle_epi32(z##B, 0x39); \
    z##C  = _mm_shuffle_epi32(z##C, 0x39); \
    z##D  = _mm_shuffle_epi32(z##D, 0x39); \
                                           \
    in##A ^= *(uint32_t *) (m + 128);      \
    in##B ^= *(uint32_t *) (m + 132);      \
    in##C ^= *(uint32_t *) (m + 136);      \
    in##D ^= *(uint32_t *) (m + 140);      \
    *(uint32_t *) (c + 128) = in##A;       \
    *(uint32_t *) (c + 132) = in##B;       \
    *(uint32_t *) (c + 136) = in##C;       \
    *(uint32_t *) (c + 140) = in##D;       \
                                           \
    in##A = _mm_cvtsi128_si32(z##A);       \
    in##B = _mm_cvtsi128_si32(z##B);       \
    in##C = _mm_cvtsi128_si32(z##C);       \
    in##D = _mm_cvtsi128_si32(z##D);       \
                                           \
    in##A ^= *(uint32_t *) (m + 192);      \
    in##B ^= *(uint32_t *) (m + 196);      \
    in##C ^= *(uint32_t *) (m + 200);      \
    in##D ^= *(uint32_t *) (m + 204);      \
    *(uint32_t *) (c + 192) = in##A;       \
    *(uint32_t *) (c + 196) = in##B;       \
    *(uint32_t *) (c + 200) = in##C;       \
    *(uint32_t *) (c + 204) = in##D

/* store data ; this macro replaces shuffle+mov by a direct extract; not much
 * difference */
#define ONEQUAD_EXTRACT(A, B, C, D)       \
    z##A  = _mm_add_epi32(z##A, orig##A); \
    z##B  = _mm_add_epi32(z##B, orig##B); \
    z##C  = _mm_add_epi32(z##C, orig##C); \
    z##D  = _mm_add_epi32(z##D, orig##D); \
    in##A = _mm_cvtsi128_si32(z##A);      \
    in##B = _mm_cvtsi128_si32(z##B);      \
    in##C = _mm_cvtsi128_si32(z##C);      \
    in##D = _mm_cvtsi128_si32(z##D);      \
    in##A ^= *(uint32_t *) (m + 0);       \
    in##B ^= *(uint32_t *) (m + 4);       \
    in##C ^= *(uint32_t *) (m + 8);       \
    in##D ^= *(uint32_t *) (m + 12);      \
    *(uint32_t *) (c + 0)  = in##A;       \
    *(uint32_t *) (c + 4)  = in##B;       \
    *(uint32_t *) (c + 8)  = in##C;       \
    *(uint32_t *) (c + 12) = in##D;       \
                                          \
    in##A = _mm_extract_epi32(z##A, 1);   \
    in##B = _mm_extract_epi32(z##B, 1);   \
    in##C = _mm_extract_epi32(z##C, 1);   \
    in##D = _mm_extract_epi32(z##D, 1);   \
                                          \
    in##A ^= *(uint32_t *) (m + 64);      \
    in##B ^= *(uint32_t *) (m + 68);      \
    in##C ^= *(uint32_t *) (m + 72);      \
    in##D ^= *(uint32_t *) (m + 76);      \
    *(uint32_t *) (c + 64) = in##A;       \
    *(uint32_t *) (c + 68) = in##B;       \
    *(uint32_t *) (c + 72) = in##C;       \
    *(uint32_t *) (c + 76) = in##D;       \
                                          \
    in##A = _mm_extract_epi32(z##A, 2);   \
    in##B = _mm_extract_epi32(z##B, 2);   \
    in##C = _mm_extract_epi32(z##C, 2);   \
    in##D = _mm_extract_epi32(z##D, 2);   \
                                          \
    in##A ^= *(uint32_t *) (m + 128);     \
    in##B ^= *(uint32_t *) (m + 132);     \
    in##C ^= *(uint32_t *) (m + 136);     \
    in##D ^= *(uint32_t *) (m + 140);     \
    *(uint32_t *) (c + 128) = in##A;      \
    *(uint32_t *) (c + 132) = in##B;      \
    *(uint32_t *) (c + 136) = in##C;      \
    *(uint32_t *) (c + 140) = in##D;      \
                                          \
    in##A = _mm_extract_epi32(z##A, 3);   \
    in##B = _mm_extract_epi32(z##B, 3);   \
    in##C = _mm_extract_epi32(z##C, 3);   \
    in##D = _mm_extract_epi32(z##D, 3);   \
                                          \
    in##A ^= *(uint32_t *) (m + 192);     \
    in##B ^= *(uint32_t *) (m + 196);     \
    in##C ^= *(uint32_t *) (m + 200);     \
    in##D ^= *(uint32_t *) (m + 204);     \
    *(uint32_t *) (c + 192) = in##A;      \
    *(uint32_t *) (c + 196) = in##B;      \
    *(uint32_t *) (c + 200) = in##C;      \
    *(uint32_t *) (c + 204) = in##D

/* store data ; this macro first transpose data in-registers, and then store
 * them in memory. much faster with icc. */
#define ONEQUAD_TRANSPOSE(A, B, C, D)                                   \
    z##A = _mm_add_epi32(z##A, orig##A);                                \
    z##B = _mm_add_epi32(z##B, orig##B);                                \
    z##C = _mm_add_epi32(z##C, orig##C);                                \
    z##D = _mm_add_epi32(z##D, orig##D);                                \
    y##A = _mm_unpacklo_epi32(z##A, z##B);                              \
    y##B = _mm_unpacklo_epi32(z##C, z##D);                              \
    y##C = _mm_unpackhi_epi32(z##A, z##B);                              \
    y##D = _mm_unpackhi_epi32(z##C, z##D);                              \
    z##A = _mm_unpacklo_epi64(y##A, y##B);                              \
    z##B = _mm_unpackhi_epi64(y##A, y##B);                              \
    z##C = _mm_unpacklo_epi64(y##C, y##D);                              \
    z##D = _mm_unpackhi_epi64(y##C, y##D);                              \
    y##A = _mm_xor_si128(z##A, _mm_loadu_si128((__m128i *) (m + 0)));   \
    _mm_storeu_si128((__m128i *) (c + 0), y##A);                        \
    y##B = _mm_xor_si128(z##B, _mm_loadu_si128((__m128i *) (m + 64)));  \
    _mm_storeu_si128((__m128i *) (c + 64), y##B);                       \
    y##C = _mm_xor_si128(z##C, _mm_loadu_si128((__m128i *) (m + 128))); \
    _mm_storeu_si128((__m128i *) (c + 128), y##C);                      \
    y##D = _mm_xor_si128(z##D, _mm_loadu_si128((__m128i *) (m + 192))); \
    _mm_storeu_si128((__m128i *) (c + 192), y##D)

#define ONEQUAD(A, B, C, D) ONEQUAD_TRANSPOSE(A, B, C, D)

        ONEQUAD(0, 1, 2, 3);
        m += 16;
        c += 16;
        ONEQUAD(4, 5, 6, 7);
        m += 16;
        c += 16;
        ONEQUAD(8, 9, 10, 11);
        m += 16;
        c += 16;
        ONEQUAD(12, 13, 14, 15);
        m -= 48;
        c -= 48;

#undef ONEQUAD
#undef ONEQUAD_TRANSPOSE
#undef ONEQUAD_EXTRACT
#undef ONEQUAD_SHUFFLE

        bytes -= 256;
        c += 256;
        m += 256;
    }
}
