if (bytes >= 512) {
    __m256i y0, y1, y2, y3, y4, y5, y6, y7, y8, y9, y10, y11, y12, y13, y14,
        y15;

    /* the naive way seems as fast (if not a bit faster) than the vector way */
    __m256i z0  = _mm256_set1_epi32(x[0]);
    __m256i z5  = _mm256_set1_epi32(x[1]);
    __m256i z10 = _mm256_set1_epi32(x[2]);
    __m256i z15 = _mm256_set1_epi32(x[3]);
    __m256i z12 = _mm256_set1_epi32(x[4]);
    __m256i z1  = _mm256_set1_epi32(x[5]);
    __m256i z6  = _mm256_set1_epi32(x[6]);
    __m256i z11 = _mm256_set1_epi32(x[7]);
    __m256i z8; /* useless */
    __m256i z13 = _mm256_set1_epi32(x[9]);
    __m256i z2  = _mm256_set1_epi32(x[10]);
    __m256i z7  = _mm256_set1_epi32(x[11]);
    __m256i z4  = _mm256_set1_epi32(x[12]);
    __m256i z9; /* useless */
    __m256i z14 = _mm256_set1_epi32(x[14]);
    __m256i z3  = _mm256_set1_epi32(x[15]);

    __m256i orig0 = z0;
    __m256i orig1 = z1;
    __m256i orig2 = z2;
    __m256i orig3 = z3;
    __m256i orig4 = z4;
    __m256i orig5 = z5;
    __m256i orig6 = z6;
    __m256i orig7 = z7;
    __m256i orig8;
    __m256i orig9;
    __m256i orig10 = z10;
    __m256i orig11 = z11;
    __m256i orig12 = z12;
    __m256i orig13 = z13;
    __m256i orig14 = z14;
    __m256i orig15 = z15;

    uint32_t in8;
    uint32_t in9;
    int      i;

    while (bytes >= 512) {
        /* vector implementation for z8 and z9 */
        /* faster than the naive version for 8 blocks */
        const __m256i addv8   = _mm256_set_epi64x(3, 2, 1, 0);
        const __m256i addv9   = _mm256_set_epi64x(7, 6, 5, 4);
        const __m256i permute = _mm256_set_epi32(7, 6, 3, 2, 5, 4, 1, 0);

        __m256i  t8, t9;
        uint64_t in89;

        in8  = x[8];
        in9  = x[13]; /* see arrays above for the address translation */
        in89 = ((uint64_t) in8) | (((uint64_t) in9) << 32);

        z8 = z9 = _mm256_broadcastq_epi64(_mm_cvtsi64_si128(in89));

        t8 = _mm256_add_epi64(addv8, z8);
        t9 = _mm256_add_epi64(addv9, z9);

        z8 = _mm256_unpacklo_epi32(t8, t9);
        z9 = _mm256_unpackhi_epi32(t8, t9);

        t8 = _mm256_unpacklo_epi32(z8, z9);
        t9 = _mm256_unpackhi_epi32(z8, z9);

        /* required because unpack* are intra-lane */
        z8 = _mm256_permutevar8x32_epi32(t8, permute);
        z9 = _mm256_permutevar8x32_epi32(t9, permute);

        orig8 = z8;
        orig9 = z9;

        in89 += 8;

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
            __m256i r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13,
                r14, r15;

            y4 = z12;
            y4 = _mm256_add_epi32(y4, z0);
            r4 = y4;
            y4 = _mm256_slli_epi32(y4, 7);
            z4 = _mm256_xor_si256(z4, y4);
            r4 = _mm256_srli_epi32(r4, 25);
            z4 = _mm256_xor_si256(z4, r4);

            y9 = z1;
            y9 = _mm256_add_epi32(y9, z5);
            r9 = y9;
            y9 = _mm256_slli_epi32(y9, 7);
            z9 = _mm256_xor_si256(z9, y9);
            r9 = _mm256_srli_epi32(r9, 25);
            z9 = _mm256_xor_si256(z9, r9);

            y8 = z0;
            y8 = _mm256_add_epi32(y8, z4);
            r8 = y8;
            y8 = _mm256_slli_epi32(y8, 9);
            z8 = _mm256_xor_si256(z8, y8);
            r8 = _mm256_srli_epi32(r8, 23);
            z8 = _mm256_xor_si256(z8, r8);

            y13 = z5;
            y13 = _mm256_add_epi32(y13, z9);
            r13 = y13;
            y13 = _mm256_slli_epi32(y13, 9);
            z13 = _mm256_xor_si256(z13, y13);
            r13 = _mm256_srli_epi32(r13, 23);
            z13 = _mm256_xor_si256(z13, r13);

            y12 = z4;
            y12 = _mm256_add_epi32(y12, z8);
            r12 = y12;
            y12 = _mm256_slli_epi32(y12, 13);
            z12 = _mm256_xor_si256(z12, y12);
            r12 = _mm256_srli_epi32(r12, 19);
            z12 = _mm256_xor_si256(z12, r12);

            y1 = z9;
            y1 = _mm256_add_epi32(y1, z13);
            r1 = y1;
            y1 = _mm256_slli_epi32(y1, 13);
            z1 = _mm256_xor_si256(z1, y1);
            r1 = _mm256_srli_epi32(r1, 19);
            z1 = _mm256_xor_si256(z1, r1);

            y0 = z8;
            y0 = _mm256_add_epi32(y0, z12);
            r0 = y0;
            y0 = _mm256_slli_epi32(y0, 18);
            z0 = _mm256_xor_si256(z0, y0);
            r0 = _mm256_srli_epi32(r0, 14);
            z0 = _mm256_xor_si256(z0, r0);

            y5 = z13;
            y5 = _mm256_add_epi32(y5, z1);
            r5 = y5;
            y5 = _mm256_slli_epi32(y5, 18);
            z5 = _mm256_xor_si256(z5, y5);
            r5 = _mm256_srli_epi32(r5, 14);
            z5 = _mm256_xor_si256(z5, r5);

            y14 = z6;
            y14 = _mm256_add_epi32(y14, z10);
            r14 = y14;
            y14 = _mm256_slli_epi32(y14, 7);
            z14 = _mm256_xor_si256(z14, y14);
            r14 = _mm256_srli_epi32(r14, 25);
            z14 = _mm256_xor_si256(z14, r14);

            y3 = z11;
            y3 = _mm256_add_epi32(y3, z15);
            r3 = y3;
            y3 = _mm256_slli_epi32(y3, 7);
            z3 = _mm256_xor_si256(z3, y3);
            r3 = _mm256_srli_epi32(r3, 25);
            z3 = _mm256_xor_si256(z3, r3);

            y2 = z10;
            y2 = _mm256_add_epi32(y2, z14);
            r2 = y2;
            y2 = _mm256_slli_epi32(y2, 9);
            z2 = _mm256_xor_si256(z2, y2);
            r2 = _mm256_srli_epi32(r2, 23);
            z2 = _mm256_xor_si256(z2, r2);

            y7 = z15;
            y7 = _mm256_add_epi32(y7, z3);
            r7 = y7;
            y7 = _mm256_slli_epi32(y7, 9);
            z7 = _mm256_xor_si256(z7, y7);
            r7 = _mm256_srli_epi32(r7, 23);
            z7 = _mm256_xor_si256(z7, r7);

            y6 = z14;
            y6 = _mm256_add_epi32(y6, z2);
            r6 = y6;
            y6 = _mm256_slli_epi32(y6, 13);
            z6 = _mm256_xor_si256(z6, y6);
            r6 = _mm256_srli_epi32(r6, 19);
            z6 = _mm256_xor_si256(z6, r6);

            y11 = z3;
            y11 = _mm256_add_epi32(y11, z7);
            r11 = y11;
            y11 = _mm256_slli_epi32(y11, 13);
            z11 = _mm256_xor_si256(z11, y11);
            r11 = _mm256_srli_epi32(r11, 19);
            z11 = _mm256_xor_si256(z11, r11);

            y10 = z2;
            y10 = _mm256_add_epi32(y10, z6);
            r10 = y10;
            y10 = _mm256_slli_epi32(y10, 18);
            z10 = _mm256_xor_si256(z10, y10);
            r10 = _mm256_srli_epi32(r10, 14);
            z10 = _mm256_xor_si256(z10, r10);

            y1 = z3;
            y1 = _mm256_add_epi32(y1, z0);
            r1 = y1;
            y1 = _mm256_slli_epi32(y1, 7);
            z1 = _mm256_xor_si256(z1, y1);
            r1 = _mm256_srli_epi32(r1, 25);
            z1 = _mm256_xor_si256(z1, r1);

            y15 = z7;
            y15 = _mm256_add_epi32(y15, z11);
            r15 = y15;
            y15 = _mm256_slli_epi32(y15, 18);
            z15 = _mm256_xor_si256(z15, y15);
            r15 = _mm256_srli_epi32(r15, 14);
            z15 = _mm256_xor_si256(z15, r15);

            y6 = z4;
            y6 = _mm256_add_epi32(y6, z5);
            r6 = y6;
            y6 = _mm256_slli_epi32(y6, 7);
            z6 = _mm256_xor_si256(z6, y6);
            r6 = _mm256_srli_epi32(r6, 25);
            z6 = _mm256_xor_si256(z6, r6);

            y2 = z0;
            y2 = _mm256_add_epi32(y2, z1);
            r2 = y2;
            y2 = _mm256_slli_epi32(y2, 9);
            z2 = _mm256_xor_si256(z2, y2);
            r2 = _mm256_srli_epi32(r2, 23);
            z2 = _mm256_xor_si256(z2, r2);

            y7 = z5;
            y7 = _mm256_add_epi32(y7, z6);
            r7 = y7;
            y7 = _mm256_slli_epi32(y7, 9);
            z7 = _mm256_xor_si256(z7, y7);
            r7 = _mm256_srli_epi32(r7, 23);
            z7 = _mm256_xor_si256(z7, r7);

            y3 = z1;
            y3 = _mm256_add_epi32(y3, z2);
            r3 = y3;
            y3 = _mm256_slli_epi32(y3, 13);
            z3 = _mm256_xor_si256(z3, y3);
            r3 = _mm256_srli_epi32(r3, 19);
            z3 = _mm256_xor_si256(z3, r3);

            y4 = z6;
            y4 = _mm256_add_epi32(y4, z7);
            r4 = y4;
            y4 = _mm256_slli_epi32(y4, 13);
            z4 = _mm256_xor_si256(z4, y4);
            r4 = _mm256_srli_epi32(r4, 19);
            z4 = _mm256_xor_si256(z4, r4);

            y0 = z2;
            y0 = _mm256_add_epi32(y0, z3);
            r0 = y0;
            y0 = _mm256_slli_epi32(y0, 18);
            z0 = _mm256_xor_si256(z0, y0);
            r0 = _mm256_srli_epi32(r0, 14);
            z0 = _mm256_xor_si256(z0, r0);

            y5 = z7;
            y5 = _mm256_add_epi32(y5, z4);
            r5 = y5;
            y5 = _mm256_slli_epi32(y5, 18);
            z5 = _mm256_xor_si256(z5, y5);
            r5 = _mm256_srli_epi32(r5, 14);
            z5 = _mm256_xor_si256(z5, r5);

            y11 = z9;
            y11 = _mm256_add_epi32(y11, z10);
            r11 = y11;
            y11 = _mm256_slli_epi32(y11, 7);
            z11 = _mm256_xor_si256(z11, y11);
            r11 = _mm256_srli_epi32(r11, 25);
            z11 = _mm256_xor_si256(z11, r11);

            y12 = z14;
            y12 = _mm256_add_epi32(y12, z15);
            r12 = y12;
            y12 = _mm256_slli_epi32(y12, 7);
            z12 = _mm256_xor_si256(z12, y12);
            r12 = _mm256_srli_epi32(r12, 25);
            z12 = _mm256_xor_si256(z12, r12);

            y8 = z10;
            y8 = _mm256_add_epi32(y8, z11);
            r8 = y8;
            y8 = _mm256_slli_epi32(y8, 9);
            z8 = _mm256_xor_si256(z8, y8);
            r8 = _mm256_srli_epi32(r8, 23);
            z8 = _mm256_xor_si256(z8, r8);

            y13 = z15;
            y13 = _mm256_add_epi32(y13, z12);
            r13 = y13;
            y13 = _mm256_slli_epi32(y13, 9);
            z13 = _mm256_xor_si256(z13, y13);
            r13 = _mm256_srli_epi32(r13, 23);
            z13 = _mm256_xor_si256(z13, r13);

            y9 = z11;
            y9 = _mm256_add_epi32(y9, z8);
            r9 = y9;
            y9 = _mm256_slli_epi32(y9, 13);
            z9 = _mm256_xor_si256(z9, y9);
            r9 = _mm256_srli_epi32(r9, 19);
            z9 = _mm256_xor_si256(z9, r9);

            y14 = z12;
            y14 = _mm256_add_epi32(y14, z13);
            r14 = y14;
            y14 = _mm256_slli_epi32(y14, 13);
            z14 = _mm256_xor_si256(z14, y14);
            r14 = _mm256_srli_epi32(r14, 19);
            z14 = _mm256_xor_si256(z14, r14);

            y10 = z8;
            y10 = _mm256_add_epi32(y10, z9);
            r10 = y10;
            y10 = _mm256_slli_epi32(y10, 18);
            z10 = _mm256_xor_si256(z10, y10);
            r10 = _mm256_srli_epi32(r10, 14);
            z10 = _mm256_xor_si256(z10, r10);

            y15 = z13;
            y15 = _mm256_add_epi32(y15, z14);
            r15 = y15;
            y15 = _mm256_slli_epi32(y15, 18);
            z15 = _mm256_xor_si256(z15, y15);
            r15 = _mm256_srli_epi32(r15, 14);
            z15 = _mm256_xor_si256(z15, r15);
        }

/* store data ; this macro first transpose data in-registers, and then store
 * them in memory. much faster with icc. */
#define ONEQUAD_TRANSPOSE(A, B, C, D)                              \
    {                                                              \
        __m128i t0, t1, t2, t3;                                    \
        z##A = _mm256_add_epi32(z##A, orig##A);                    \
        z##B = _mm256_add_epi32(z##B, orig##B);                    \
        z##C = _mm256_add_epi32(z##C, orig##C);                    \
        z##D = _mm256_add_epi32(z##D, orig##D);                    \
        y##A = _mm256_unpacklo_epi32(z##A, z##B);                  \
        y##B = _mm256_unpacklo_epi32(z##C, z##D);                  \
        y##C = _mm256_unpackhi_epi32(z##A, z##B);                  \
        y##D = _mm256_unpackhi_epi32(z##C, z##D);                  \
        z##A = _mm256_unpacklo_epi64(y##A, y##B);                  \
        z##B = _mm256_unpackhi_epi64(y##A, y##B);                  \
        z##C = _mm256_unpacklo_epi64(y##C, y##D);                  \
        z##D = _mm256_unpackhi_epi64(y##C, y##D);                  \
        t0   = _mm_xor_si128(_mm256_extracti128_si256(z##A, 0),    \
                           _mm_loadu_si128((__m128i*) (m + 0)));   \
        _mm_storeu_si128((__m128i*) (c + 0), t0);                  \
        t1 = _mm_xor_si128(_mm256_extracti128_si256(z##B, 0),      \
                           _mm_loadu_si128((__m128i*) (m + 64)));  \
        _mm_storeu_si128((__m128i*) (c + 64), t1);                 \
        t2 = _mm_xor_si128(_mm256_extracti128_si256(z##C, 0),      \
                           _mm_loadu_si128((__m128i*) (m + 128))); \
        _mm_storeu_si128((__m128i*) (c + 128), t2);                \
        t3 = _mm_xor_si128(_mm256_extracti128_si256(z##D, 0),      \
                           _mm_loadu_si128((__m128i*) (m + 192))); \
        _mm_storeu_si128((__m128i*) (c + 192), t3);                \
        t0 = _mm_xor_si128(_mm256_extracti128_si256(z##A, 1),      \
                           _mm_loadu_si128((__m128i*) (m + 256))); \
        _mm_storeu_si128((__m128i*) (c + 256), t0);                \
        t1 = _mm_xor_si128(_mm256_extracti128_si256(z##B, 1),      \
                           _mm_loadu_si128((__m128i*) (m + 320))); \
        _mm_storeu_si128((__m128i*) (c + 320), t1);                \
        t2 = _mm_xor_si128(_mm256_extracti128_si256(z##C, 1),      \
                           _mm_loadu_si128((__m128i*) (m + 384))); \
        _mm_storeu_si128((__m128i*) (c + 384), t2);                \
        t3 = _mm_xor_si128(_mm256_extracti128_si256(z##D, 1),      \
                           _mm_loadu_si128((__m128i*) (m + 448))); \
        _mm_storeu_si128((__m128i*) (c + 448), t3);                \
    }

#define ONEQUAD(A, B, C, D) ONEQUAD_TRANSPOSE(A, B, C, D)

#define ONEQUAD_UNPCK(A, B, C, D)                 \
    {                                             \
        z##A = _mm256_add_epi32(z##A, orig##A);   \
        z##B = _mm256_add_epi32(z##B, orig##B);   \
        z##C = _mm256_add_epi32(z##C, orig##C);   \
        z##D = _mm256_add_epi32(z##D, orig##D);   \
        y##A = _mm256_unpacklo_epi32(z##A, z##B); \
        y##B = _mm256_unpacklo_epi32(z##C, z##D); \
        y##C = _mm256_unpackhi_epi32(z##A, z##B); \
        y##D = _mm256_unpackhi_epi32(z##C, z##D); \
        z##A = _mm256_unpacklo_epi64(y##A, y##B); \
        z##B = _mm256_unpackhi_epi64(y##A, y##B); \
        z##C = _mm256_unpacklo_epi64(y##C, y##D); \
        z##D = _mm256_unpackhi_epi64(y##C, y##D); \
    }

#define ONEOCTO(A, B, C, D, A2, B2, C2, D2)                                     \
    {                                                                           \
        ONEQUAD_UNPCK(A, B, C, D);                                              \
        ONEQUAD_UNPCK(A2, B2, C2, D2);                                          \
        y##A  = _mm256_permute2x128_si256(z##A, z##A2, 0x20);                   \
        y##A2 = _mm256_permute2x128_si256(z##A, z##A2, 0x31);                   \
        y##B  = _mm256_permute2x128_si256(z##B, z##B2, 0x20);                   \
        y##B2 = _mm256_permute2x128_si256(z##B, z##B2, 0x31);                   \
        y##C  = _mm256_permute2x128_si256(z##C, z##C2, 0x20);                   \
        y##C2 = _mm256_permute2x128_si256(z##C, z##C2, 0x31);                   \
        y##D  = _mm256_permute2x128_si256(z##D, z##D2, 0x20);                   \
        y##D2 = _mm256_permute2x128_si256(z##D, z##D2, 0x31);                   \
        y##A  = _mm256_xor_si256(y##A, _mm256_loadu_si256((__m256i*) (m + 0))); \
        y##B =                                                                  \
            _mm256_xor_si256(y##B, _mm256_loadu_si256((__m256i*) (m + 64)));    \
        y##C =                                                                  \
            _mm256_xor_si256(y##C, _mm256_loadu_si256((__m256i*) (m + 128)));   \
        y##D =                                                                  \
            _mm256_xor_si256(y##D, _mm256_loadu_si256((__m256i*) (m + 192)));   \
        y##A2 =                                                                 \
            _mm256_xor_si256(y##A2, _mm256_loadu_si256((__m256i*) (m + 256)));  \
        y##B2 =                                                                 \
            _mm256_xor_si256(y##B2, _mm256_loadu_si256((__m256i*) (m + 320)));  \
        y##C2 =                                                                 \
            _mm256_xor_si256(y##C2, _mm256_loadu_si256((__m256i*) (m + 384)));  \
        y##D2 =                                                                 \
            _mm256_xor_si256(y##D2, _mm256_loadu_si256((__m256i*) (m + 448)));  \
        _mm256_storeu_si256((__m256i*) (c + 0), y##A);                          \
        _mm256_storeu_si256((__m256i*) (c + 64), y##B);                         \
        _mm256_storeu_si256((__m256i*) (c + 128), y##C);                        \
        _mm256_storeu_si256((__m256i*) (c + 192), y##D);                        \
        _mm256_storeu_si256((__m256i*) (c + 256), y##A2);                       \
        _mm256_storeu_si256((__m256i*) (c + 320), y##B2);                       \
        _mm256_storeu_si256((__m256i*) (c + 384), y##C2);                       \
        _mm256_storeu_si256((__m256i*) (c + 448), y##D2);                       \
    }

        ONEOCTO(0, 1, 2, 3, 4, 5, 6, 7);
        m += 32;
        c += 32;
        ONEOCTO(8, 9, 10, 11, 12, 13, 14, 15);
        m -= 32;
        c -= 32;

#undef ONEQUAD
#undef ONEQUAD_TRANSPOSE
#undef ONEQUAD_UNPCK
#undef ONEOCTO

        bytes -= 512;
        c += 512;
        m += 512;
    }
}
