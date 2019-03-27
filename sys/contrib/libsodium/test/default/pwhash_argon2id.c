
#define TEST_NAME "pwhash_argon2id"
#include "cmptest.h"

#define OUT_LEN 128
#define OPSLIMIT 3
#define MEMLIMIT 5000000

static void
tv(void)
{
    static struct {
        const char *       passwd_hex;
        size_t             passwd_len;
        const char *       salt_hex;
        size_t             outlen;
        unsigned long long opslimit;
        size_t             memlimit;
        unsigned int       lanes;
    } tests[] = {
        { "a347ae92bce9f80f6f595a4480fc9c2fe7e7d7148d371e9487d75f5c23008ffae0"
          "65577a928febd9b1973a5a95073acdbeb6a030cfc0d79caa2dc5cd011cef02c08d"
          "a232d76d52dfbca38ca8dcbd665b17d1665f7cf5fe59772ec909733b24de97d6f5"
          "8d220b20c60d7c07ec1fd93c52c31020300c6c1facd77937a597c7a6",
          127,
          "5541fbc995d5c197ba290346d2c559dedf405cf97e5f95482143202f9e74f5c2",
          155, 5, 7256678, 1 },
        { "e125cee61c8cb7778d9e5ad0a6f5d978ce9f84de213a8556d9ffe202020ab4a6ed"
          "9074a4eb3416f9b168f137510f3a30b70b96cbfa219ff99f6c6eaffb15c06b60e0"
          "0cc2890277f0fd3c622115772f7048adaebed86e",
          86,
          "f1192dd5dc2368b9cd421338b22433455ee0a3699f9379a08b9650ea2c126f0d",
          250, 4, 7849083, 1 },
        { "92263cbf6ac376499f68a4289d3bb59e5a22335eba63a32e6410249155b956b6a3"
          "b48d4a44906b18b897127300b375b8f834f1ceffc70880a885f47c33876717e392"
          "be57f7da3ae58da4fd1f43daa7e44bb82d3717af4319349c24cd31e46d295856b0"
          "441b6b289992a11ced1cc3bf3011604590244a3eb737ff221129215e4e4347f491"
          "5d41292b5173d196eb9add693be5319fdadc242906178bb6c0286c9b6ca6012746"
          "711f58c8c392016b2fdfc09c64f0f6b6ab7b",
          183,
          "3b840e20e9555e9fb031c4ba1f1747ce25cc1d0ff664be676b9b4a90641ff194",
          249, 3, 7994791, 1 },
        { "027b6d8e8c8c474e9b69c7d9ed4f9971e8e1ce2f6ba95048414c3970f0f09b70e3"
          "b6c5ae05872b3d8678705b7d381829c351a5a9c88c233569b35d6b0b809df44b64"
          "51a9c273f1150e2ef8a0b5437eb701e373474cd44b97ef0248ebce2ca0400e1b53"
          "f3d86221eca3f18eb45b702b9172440f774a82cbf1f6f525df30a6e293c873cce6"
          "9bb078ed1f0d31e7f9b8062409f37f19f8550aae",
          152,
          "eb2a3056a09ad2d7d7f975bcd707598f24cd32518cde3069f2e403b34bfee8a5", 5,
          4, 1397645, 1 },
        { "4a857e2ee8aa9b6056f2424e84d24a72473378906ee04a46cb05311502d5250b82"
          "ad86b83c8f20a23dbb74f6da60b0b6ecffd67134d45946ac8ebfb3064294bc097d"
          "43ced68642bfb8bbbdd0f50b30118f5e",
          82,
          "39d82eef32010b8b79cc5ba88ed539fbaba741100f2edbeca7cc171ffeabf258",
          190, 3, 1432947, 1 },
        { "c7b09aec680e7b42fedd7fc792e78b2f6c1bea8f4a884320b648f81e8cf515e8ba"
          "9dcfb11d43c4aae114c1734aa69ca82d44998365db9c93744fa28b63fd16000e82"
          "61cbbe083e7e2da1e5f696bde0834fe53146d7e0e35e7de9920d041f5a5621aabe"
          "02da3e2b09b405b77937efef3197bd5772e41fdb73fb5294478e45208063b5f58e"
          "089dbeb6d6342a909c1307b3fff5fe2cf4da56bdae50848f",
          156,
          "039c056d933b475032777edbaffac50f143f64c123329ed9cf59e3b65d3f43b6",
          178, 3, 4886999, 1 },
        { "b540beb016a5366524d4605156493f9874514a5aa58818cd0c6dfffaa9e90205f1"
          "7b",
          34,
          "44071f6d181561670bda728d43fb79b443bb805afdebaf98622b5165e01b15fb",
          231, 1, 1631659, 1 },
        { "a14975c26c088755a8b715ff2528d647cd343987fcf4aa25e7194a8417fb2b4b3f"
          "7268da9f3182b4cfb22d138b2749d673a47ecc7525dd15a0a3c66046971784bb63"
          "d7eae24cc84f2631712075a10e10a96b0e0ee67c43e01c423cb9c44e5371017e9c"
          "496956b632158da3fe12addecb88912e6759bc37f9af2f45af72c5cae3b179ffb6"
          "76a697de6ebe45cd4c16d4a9d642d29ddc0186a0a48cb6cd62bfc3dd229d313b30"
          "1560971e740e2cf1f99a9a090a5b283f35475057e96d7064e2e0fc81984591068d"
          "55a3b4169f22cccb0745a2689407ea1901a0a766eb99",
          220,
          "3d968b2752b8838431165059319f3ff8910b7b8ecb54ea01d3f54769e9d98daf",
          167, 3, 1784128, 1 },
    };
    char          passwd[256];
    unsigned char salt[crypto_pwhash_SALTBYTES];
    unsigned char out[256];
    char          out_hex[256 * 2 + 1];
    size_t        i = 0U;

    do {
        sodium_hex2bin((unsigned char *) passwd, sizeof passwd,
                       tests[i].passwd_hex, strlen(tests[i].passwd_hex), NULL,
                       NULL, NULL);
        sodium_hex2bin(salt, sizeof salt, tests[i].salt_hex,
                       strlen(tests[i].salt_hex), NULL, NULL, NULL);
        if (crypto_pwhash(out, (unsigned long long) tests[i].outlen, passwd,
                          tests[i].passwd_len, (const unsigned char *) salt,
                          tests[i].opslimit, tests[i].memlimit,
                          crypto_pwhash_alg_default()) != 0) {
            printf("[tv] pwhash failure (maybe intentional): [%u]\n",
                   (unsigned int) i);
            continue;
        }
        sodium_bin2hex(out_hex, sizeof out_hex, out, tests[i].outlen);
        printf("%s\n", out_hex);
    } while (++i < (sizeof tests) / (sizeof tests[0]));
}

static void
tv2(void)
{
    static struct {
        const char *       passwd_hex;
        size_t             passwd_len;
        const char *       salt_hex;
        size_t             outlen;
        unsigned long long opslimit;
        size_t             memlimit;
        unsigned int       lanes;
    } tests[] = {
        { "a347ae92bce9f80f6f595a4480fc9c2fe7e7d7148d371e9487d75f5c23008ffae0"
          "65577a928febd9b1973a5a95073acdbeb6a030cfc0d79caa2dc5cd011cef02c08d"
          "a232d76d52dfbca38ca8dcbd665b17d1665f7cf5fe59772ec909733b24de97d6f5"
          "8d220b20c60d7c07ec1fd93c52c31020300c6c1facd77937a597c7a6",
          127,
          "5541fbc995d5c197ba290346d2c559dedf405cf97e5f95482143202f9e74f5c2",
          155, 4, 397645, 1 },
        { "a347ae92bce9f80f6f595a4480fc9c2fe7e7d7148d371e9487d75f5c23008ffae0"
          "65577a928febd9b1973a5a95073acdbeb6a030cfc0d79caa2dc5cd011cef02c08d"
          "a232d76d52dfbca38ca8dcbd665b17d1665f7cf5fe59772ec909733b24de97d6f5"
          "8d220b20c60d7c07ec1fd93c52c31020300c6c1facd77937a597c7a6",
          127,
          "5541fbc995d5c197ba290346d2c559dedf405cf97e5f95482143202f9e74f5c2",
          155, 3, 397645, 1 },
    };
    char          passwd[256];
    unsigned char salt[crypto_pwhash_SALTBYTES];
    unsigned char out[256];
    char          out_hex[256 * 2 + 1];
    size_t        i = 0U;

    do {
        sodium_hex2bin((unsigned char *) passwd, sizeof passwd,
                       tests[i].passwd_hex, strlen(tests[i].passwd_hex), NULL,
                       NULL, NULL);
        sodium_hex2bin(salt, sizeof salt, tests[i].salt_hex,
                       strlen(tests[i].salt_hex), NULL, NULL, NULL);
        if (crypto_pwhash(out, (unsigned long long) tests[i].outlen, passwd,
                          tests[i].passwd_len, (const unsigned char *) salt,
                          tests[i].opslimit, tests[i].memlimit,
                          crypto_pwhash_alg_default()) != 0) {
            printf("[tv2] pwhash failure: [%u]\n", (unsigned int) i);
            continue;
        }
        sodium_bin2hex(out_hex, sizeof out_hex, out, tests[i].outlen);
        printf("%s\n", out_hex);
    } while (++i < (sizeof tests) / (sizeof tests[0]));

    if (crypto_pwhash_argon2id(out, sizeof out, "password", strlen("password"), salt, 3,
                               1ULL << 12, 0) != -1) {
        printf("[tv2] pwhash should have failed (0)\n");
    }
    if (crypto_pwhash_argon2id(out, sizeof out, "password", strlen("password"), salt, 3,
                               1, crypto_pwhash_argon2id_alg_argon2id13()) != -1) {
        printf("[tv2] pwhash should have failed (1)\n");
    }
    if (crypto_pwhash_argon2id(out, sizeof out, "password", strlen("password"), salt, 3,
                               1ULL << 12, crypto_pwhash_argon2id_alg_argon2id13()) != -1) {
        printf("[tv2] pwhash should have failed (2)\n");
    }
    if (crypto_pwhash_argon2id(out, sizeof out, "password", strlen("password"), salt, 2,
                               1ULL << 12, crypto_pwhash_argon2id_alg_argon2id13()) != -1) {
        printf("[tv2] pwhash should have failed (3)\n");
    }
    if (crypto_pwhash_argon2id(out, 15, "password", strlen("password"), salt, 3,
                               1ULL << 12, crypto_pwhash_argon2id_alg_argon2id13()) != -1) {
        printf("[tv2] pwhash with a short output length should have failed\n");
    }
    if (crypto_pwhash_argon2id(out, sizeof out, "password", 0x100000000ULL, salt, 3,
                               1ULL << 12, crypto_pwhash_argon2id_alg_argon2id13()) != -1) {
        printf("[tv2] pwhash with a long password length should have failed\n");
    }
    assert(crypto_pwhash_argon2id(out, sizeof out, "password", strlen("password"), salt,
                                  OPSLIMIT, MEMLIMIT, crypto_pwhash_alg_argon2i13()) == -1);
}

static void
tv3(void)
{
    static struct {
        const char *passwd;
        const char *out;
    } tests[] = {
        { "",
          "$argon2id$v=19$m=4096,t=0,p=1$X1NhbHQAAAAAAAAAAAAAAA$bWh++MKN1OiFHKgIWTLvIi1iHicmHH7+Fv3K88ifFfI" },
        { "",
          "$argon2id$v=19$m=2048,t=4,p=1$SWkxaUhpY21ISDcrRnYzSw$Mbg/Eck1kpZir5T9io7C64cpffdTBaORgyriLQFgQj8" },
        { "",
          "$argon2id$v=19$m=4882,t=2,p=1$bA81arsiXysd3WbTRzmEOw$Nm8QBM+7RH1DXo9rvp5cwKEOOOfD2g6JuxlXihoNcpE" },
        { "^T5H$JYt39n%K*j:W]!1s?vg!:jGi]Ax?..l7[p0v:1jHTpla9;]bUN;?bWyCbtqg ",
          "$argon2id$v=19$m=4096,t=0,p=1$PkEgMTYtYnl0ZXMgc2FsdA$ltB/ue1kPtBMBGfsysMpPigE6hiNEKZ9vs8vLNVDQGA" },
        { "^T5H$JYt39n%K*j:W]!1s?vg!:jGi]Ax?..l7[p0v:1jHTpla9;]bUN;?bWyCbtqg ",
          "$argon2id$v=19$m=4096,t=19,p=1$PkEgMTYtYnl0ZXMgc2FsdA$ltB/ue1kPtBMBGfsysMpPigE6hiNEKZ9vs8vLNVDQGA" },
        { "K3S=KyH#)36_?]LxeR8QNKw6X=gFbxai$C%29V*",
          "$argon2id$v=19$m=4096,t=1,p=3$PkEgcHJldHR5IGxvbmcgc2FsdA$HUqx5Z1b/ZypnUrvvJ5UC2Q+T6Q1WwASK/Kr9dRbGA0" }
    };
    char   *out;
    char   *passwd;
    size_t  i = 0U;
    int     ret;

    do {
        out = (char *) sodium_malloc(strlen(tests[i].out) + 1U);
        assert(out != NULL);
        memcpy(out, tests[i].out, strlen(tests[i].out) + 1U);
        passwd = (char *) sodium_malloc(strlen(tests[i].passwd) + 1U);
        assert(passwd != NULL);
        memcpy(passwd, tests[i].passwd, strlen(tests[i].passwd) + 1U);
        ret = crypto_pwhash_str_verify(out, passwd, strlen(passwd));
        sodium_free(out);
        sodium_free(passwd);
        if (ret != 0) {
            printf("[tv3] pwhash_argon2id_str failure (maybe intentional): [%u]\n",
                   (unsigned int) i);
        }
    } while (++i < (sizeof tests) / (sizeof tests[0]));
}

static void
str_tests(void)
{
    char       *str_out;
    char       *str_out2;
    char       *salt;
    const char *passwd = "Correct Horse Battery Staple";

    salt     = (char *) sodium_malloc(crypto_pwhash_argon2id_SALTBYTES);
    str_out  = (char *) sodium_malloc(crypto_pwhash_argon2id_STRBYTES);
    str_out2 = (char *) sodium_malloc(crypto_pwhash_argon2id_STRBYTES);
    memcpy(salt, ">A 16-bytes salt", crypto_pwhash_argon2id_SALTBYTES);
    if (crypto_pwhash_str(str_out, passwd, strlen(passwd), OPSLIMIT,
                          MEMLIMIT) != 0) {
        printf("pwhash_str failure\n");
    }
    if (crypto_pwhash_str(str_out2, passwd, strlen(passwd), OPSLIMIT,
                          MEMLIMIT) != 0) {
        printf("pwhash_str(2) failure\n");
    }
    if (strcmp(str_out, str_out2) == 0) {
        printf("pwhash_str() doesn't generate different salts\n");
    }
    if (crypto_pwhash_str_needs_rehash(str_out, OPSLIMIT, MEMLIMIT) != 0 ||
        crypto_pwhash_str_needs_rehash(str_out, OPSLIMIT, MEMLIMIT) != 0) {
        printf("needs_rehash() false positive\n");
    }
    if (crypto_pwhash_str_needs_rehash(str_out, OPSLIMIT, MEMLIMIT / 2) != 1 ||
        crypto_pwhash_str_needs_rehash(str_out, OPSLIMIT - 1, MEMLIMIT) != 1 ||
        crypto_pwhash_str_needs_rehash(str_out, OPSLIMIT, MEMLIMIT * 2) != 1 ||
        crypto_pwhash_str_needs_rehash(str_out, OPSLIMIT + 1, MEMLIMIT) != 1) {
        printf("needs_rehash() false negative (0)\n");
    }
    if (crypto_pwhash_argon2id_str_needs_rehash(str_out, OPSLIMIT, MEMLIMIT / 2) != 1 ||
        crypto_pwhash_argon2id_str_needs_rehash(str_out, OPSLIMIT - 1, MEMLIMIT) != 1 ||
        crypto_pwhash_argon2id_str_needs_rehash(str_out, OPSLIMIT, MEMLIMIT * 2) != 1 ||
        crypto_pwhash_argon2id_str_needs_rehash(str_out, OPSLIMIT + 1, MEMLIMIT) != 1) {
        printf("needs_rehash() false negative (1)\n");
    }
    if (crypto_pwhash_argon2i_str_needs_rehash(str_out, OPSLIMIT, MEMLIMIT / 2) != -1 ||
        crypto_pwhash_argon2i_str_needs_rehash(str_out, OPSLIMIT - 1, MEMLIMIT) != -1 ||
        crypto_pwhash_argon2i_str_needs_rehash(str_out, OPSLIMIT, MEMLIMIT * 2) != -1 ||
        crypto_pwhash_argon2i_str_needs_rehash(str_out, OPSLIMIT + 1, MEMLIMIT) != -1) {
        printf("needs_rehash() false negative (2)\n");
    }
    if (crypto_pwhash_str_needs_rehash(str_out, OPSLIMIT, MEMLIMIT / 2) != 1) {
        printf("pwhash_str_needs_rehash() didn't handle argon2id\n");
    }
    if (crypto_pwhash_str_needs_rehash(str_out + 1, OPSLIMIT, MEMLIMIT) != -1 ||
        crypto_pwhash_argon2id_str_needs_rehash(str_out + 1, OPSLIMIT, MEMLIMIT) != -1) {
        printf("needs_rehash() didn't fail with an invalid hash string\n");
    }
    if (sodium_is_zero((const unsigned char *) str_out + strlen(str_out),
                       crypto_pwhash_STRBYTES - strlen(str_out)) != 1 ||
        sodium_is_zero((const unsigned char *) str_out2 + strlen(str_out2),
                       crypto_pwhash_STRBYTES - strlen(str_out2)) != 1) {
        printf("pwhash_argon2id_str() doesn't properly pad with zeros\n");
    }
    if (crypto_pwhash_argon2id_str_verify(str_out, passwd, strlen(passwd)) != 0) {
        printf("pwhash_argon2id_str_verify(1) failure\n");
    }
    if (crypto_pwhash_str_verify(str_out, passwd, strlen(passwd)) != 0) {
        printf("pwhash_str_verify(1') failure\n");
    }
    str_out[14]++;
    if (crypto_pwhash_str_verify(str_out, passwd, strlen(passwd)) != -1) {
        printf("pwhash_argon2id_str_verify(2) failure\n");
    }
    str_out[14]--;
    assert(str_out[crypto_pwhash_argon2id_STRBYTES - 1U] == 0);

    if (crypto_pwhash_str(str_out2, passwd, 0x100000000ULL, OPSLIMIT,
                          MEMLIMIT) != -1) {
        printf("pwhash_str() with a large password should have failed\n");
    }
    if (crypto_pwhash_str(str_out2, passwd, strlen(passwd), 1, MEMLIMIT) != 0) {
        printf("pwhash_str() with a small opslimit should not have failed\n");
    }
    if (crypto_pwhash_str(str_out2, passwd, strlen(passwd), 0, MEMLIMIT) != -1) {
        printf("pwhash_argon2id_str() with a null opslimit should have failed\n");
    }
    if (crypto_pwhash_str_verify("$argon2id$m=65536,t=2,p=1c29tZXNhbHQ"
                                 "$9sTbSlTio3Biev89thdrlKKiCaYsjjYVJxGAL3swxpQ",
                                 "password", 0x100000000ULL) != -1) {
        printf("pwhash_str_verify(invalid(0)) failure\n");
    }
    if (crypto_pwhash_str_verify("$argon2id$m=65536,t=2,p=1c29tZXNhbHQ"
                                 "$9sTbSlTio3Biev89thdrlKKiCaYsjjYVJxGAL3swxpQ",
                                 "password", strlen("password")) != -1) {
        printf("pwhash_str_verify(invalid(1)) failure %d\n", errno);
    }
    if (crypto_pwhash_str_verify("$argon2id$m=65536,t=2,p=1$c29tZXNhbHQ"
                                 "9sTbSlTio3Biev89thdrlKKiCaYsjjYVJxGAL3swxpQ",
                                 "password", strlen("password")) != -1) {
        printf("pwhash_str_verify(invalid(2)) failure\n");
    }
    if (crypto_pwhash_str_verify("$argon2id$m=65536,t=2,p=1$c29tZXNhbHQ"
                                 "$b2G3seW+uPzerwQQC+/E1K50CLLO7YXy0JRcaTuswRo",
                                 "password", strlen("password")) != -1) {
        printf("pwhash_str_verify(invalid(3)) failure\n");
    }
    if (crypto_pwhash_str_verify("$argon2id$v=19$m=65536,t=2,p=1c29tZXNhbHQ"
                                 "$wWKIMhR9lyDFvRz9YTZweHKfbftvj+qf+YFY4NeBbtA",
                                 "password", strlen("password")) != -1) {
        printf("pwhash_str_verify(invalid(4)) failure\n");
    }
    if (crypto_pwhash_str_verify("$argon2id$v=19$m=65536,t=2,p=1$c29tZXNhbHQ"
                                 "wWKIMhR9lyDFvRz9YTZweHKfbftvj+qf+YFY4NeBbtA",
                                 "password", strlen("password")) != -1) {
        printf("pwhash_str_verify(invalid(5)) failure\n");
    }
    if (crypto_pwhash_str_verify("$argon2id$v=19$m=65536,t=2,p=1$c29tZXNhbHQ"
                                 "$8iIuixkI73Js3G1uMbezQXD0b8LG4SXGsOwoQkdAQIM",
                                 "password", strlen("password")) != -1) {
        printf("pwhash_str_verify(invalid(6)) failure\n");
    }
    if (crypto_pwhash_str_verify("$argon2id$v=19$m=256,t=3,p=1$MDEyMzQ1Njc"
                                 "$G5ajKFCoUzaXRLdz7UJb5wGkb2Xt+X5/GQjUYtS2+TE",
                                 "password", strlen("password")) != 0) {
        printf("pwhash_str_verify(valid(7)) failure\n");
    }
    if (crypto_pwhash_str_verify("$argon2id$v=19$m=256,t=3,p=1$MDEyMzQ1Njc"
                                 "$G5ajKFCoUzaXRLdz7UJb5wGkb2Xt+X5/GQjUYtS2+TE",
                                 "passwore", strlen("passwore")) != -1 || errno != EINVAL) {
        printf("pwhash_str_verify(invalid(7)) failure\n");
    }
    if (crypto_pwhash_str_verify("$Argon2id$v=19$m=256,t=3,p=1$MDEyMzQ1Njc"
                                 "$G5ajKFCoUzaXRLdz7UJb5wGkb2Xt+X5/GQjUYtS2+TE",
                                 "password", strlen("password")) != -1 || errno != EINVAL) {
        printf("pwhash_str_verify(invalid(8)) failure\n");
    }
    if (crypto_pwhash_str_verify("$argon2id$v=19$m=256,t=3,p=2$MDEyMzQ1Njc"
                                 "$G5ajKFCoUzaXRLdz7UJb5wGkb2Xt+X5/GQjUYtS2+TE",
                                 "password", strlen("password")) != -1 || errno != EINVAL) {
        printf("pwhash_str_verify(invalid(9)) failure\n");
    }
    assert(crypto_pwhash_str_alg(str_out, "test", 4, OPSLIMIT, MEMLIMIT,
                                 crypto_pwhash_ALG_ARGON2ID13) == 0);
    assert(crypto_pwhash_argon2id_str_verify(str_out, "test", 4) == 0);
    assert(crypto_pwhash_argon2id_str_needs_rehash(str_out,
                                                   OPSLIMIT, MEMLIMIT) == 0);
    assert(crypto_pwhash_argon2id_str_needs_rehash(str_out,
                                                   OPSLIMIT / 2, MEMLIMIT) == 1);
    assert(crypto_pwhash_argon2id_str_needs_rehash(str_out,
                                                   OPSLIMIT, MEMLIMIT / 2) == 1);
    assert(crypto_pwhash_argon2id_str_needs_rehash(str_out, 0, 0) == 1);
    assert(crypto_pwhash_argon2i_str_needs_rehash(str_out, 0, 0) == -1);
    assert(crypto_pwhash_argon2id_str_needs_rehash(str_out + 1,
                                                   OPSLIMIT, MEMLIMIT) == -1);
    assert(crypto_pwhash_argon2i_str_needs_rehash(str_out, 0, 0) == -1);
    assert(crypto_pwhash_argon2i_str_needs_rehash("", OPSLIMIT, MEMLIMIT) == -1);
    assert(crypto_pwhash_str_alg(str_out, "test", 4, OPSLIMIT, MEMLIMIT,
                                 crypto_pwhash_ALG_ARGON2I13) == 0);
    assert(crypto_pwhash_argon2i_str_verify(str_out, "test", 4) == 0);
    assert(crypto_pwhash_argon2i_str_needs_rehash(str_out,
                                                  OPSLIMIT, MEMLIMIT) == 0);
    assert(crypto_pwhash_argon2i_str_needs_rehash(str_out,
                                                  OPSLIMIT / 2, MEMLIMIT) == 1);
    assert(crypto_pwhash_argon2i_str_needs_rehash(str_out,
                                                  OPSLIMIT, MEMLIMIT / 2) == 1);
    assert(crypto_pwhash_argon2i_str_needs_rehash(str_out, 0, 0) == 1);
    assert(crypto_pwhash_argon2id_str_needs_rehash(str_out, 0, 0) == -1);
    assert(crypto_pwhash_argon2i_str_needs_rehash("", OPSLIMIT, MEMLIMIT) == -1);
    assert(crypto_pwhash_argon2i_str_needs_rehash(str_out + 1,
                                                  OPSLIMIT, MEMLIMIT) == -1);
    sodium_free(salt);
    sodium_free(str_out);
    sodium_free(str_out2);
}

int
main(void)
{
    tv();
    tv2();
    tv3();
    str_tests();

    assert(crypto_pwhash_bytes_min() > 0U);
    assert(crypto_pwhash_bytes_max() > crypto_pwhash_bytes_min());
    assert(crypto_pwhash_passwd_max() > crypto_pwhash_passwd_min());
    assert(crypto_pwhash_saltbytes() > 0U);
    assert(crypto_pwhash_strbytes() > 1U);
    assert(crypto_pwhash_strbytes() > strlen(crypto_pwhash_strprefix()));

    assert(crypto_pwhash_opslimit_min() > 0U);
    assert(crypto_pwhash_opslimit_max() > 0U);
    assert(crypto_pwhash_memlimit_min() > 0U);
    assert(crypto_pwhash_memlimit_max() > 0U);
    assert(crypto_pwhash_opslimit_interactive() > 0U);
    assert(crypto_pwhash_memlimit_interactive() > 0U);
    assert(crypto_pwhash_opslimit_moderate() > 0U);
    assert(crypto_pwhash_memlimit_moderate() > 0U);
    assert(crypto_pwhash_opslimit_sensitive() > 0U);
    assert(crypto_pwhash_memlimit_sensitive() > 0U);
    assert(strcmp(crypto_pwhash_primitive(), "argon2i") == 0);

    assert(crypto_pwhash_bytes_min() == crypto_pwhash_BYTES_MIN);
    assert(crypto_pwhash_bytes_max() == crypto_pwhash_BYTES_MAX);
    assert(crypto_pwhash_passwd_min() == crypto_pwhash_PASSWD_MIN);
    assert(crypto_pwhash_passwd_max() == crypto_pwhash_PASSWD_MAX);
    assert(crypto_pwhash_saltbytes() == crypto_pwhash_SALTBYTES);
    assert(crypto_pwhash_strbytes() == crypto_pwhash_STRBYTES);

    assert(crypto_pwhash_opslimit_min() == crypto_pwhash_OPSLIMIT_MIN);
    assert(crypto_pwhash_opslimit_max() == crypto_pwhash_OPSLIMIT_MAX);
    assert(crypto_pwhash_memlimit_min() == crypto_pwhash_MEMLIMIT_MIN);
    assert(crypto_pwhash_memlimit_max() == crypto_pwhash_MEMLIMIT_MAX);
    assert(crypto_pwhash_opslimit_interactive() ==
           crypto_pwhash_OPSLIMIT_INTERACTIVE);
    assert(crypto_pwhash_memlimit_interactive() ==
           crypto_pwhash_MEMLIMIT_INTERACTIVE);
    assert(crypto_pwhash_opslimit_moderate() ==
           crypto_pwhash_OPSLIMIT_MODERATE);
    assert(crypto_pwhash_memlimit_moderate() ==
           crypto_pwhash_MEMLIMIT_MODERATE);
    assert(crypto_pwhash_opslimit_sensitive() ==
           crypto_pwhash_OPSLIMIT_SENSITIVE);
    assert(crypto_pwhash_memlimit_sensitive() ==
           crypto_pwhash_MEMLIMIT_SENSITIVE);

    assert(crypto_pwhash_argon2id_bytes_min() == crypto_pwhash_bytes_min());
    assert(crypto_pwhash_argon2id_bytes_max() == crypto_pwhash_bytes_max());
    assert(crypto_pwhash_argon2id_passwd_min() == crypto_pwhash_passwd_min());
    assert(crypto_pwhash_argon2id_passwd_max() == crypto_pwhash_passwd_max());
    assert(crypto_pwhash_argon2id_saltbytes() == crypto_pwhash_saltbytes());
    assert(crypto_pwhash_argon2id_strbytes() == crypto_pwhash_strbytes());
    assert(strcmp(crypto_pwhash_argon2id_strprefix(),
                  crypto_pwhash_strprefix()) == 0);
    assert(crypto_pwhash_argon2id_opslimit_min() ==
           crypto_pwhash_opslimit_min());
    assert(crypto_pwhash_argon2id_opslimit_max() ==
           crypto_pwhash_opslimit_max());
    assert(crypto_pwhash_argon2id_memlimit_min() ==
           crypto_pwhash_memlimit_min());
    assert(crypto_pwhash_argon2id_memlimit_max() ==
           crypto_pwhash_memlimit_max());
    assert(crypto_pwhash_argon2id_opslimit_interactive() ==
           crypto_pwhash_opslimit_interactive());
    assert(crypto_pwhash_argon2id_opslimit_moderate() ==
           crypto_pwhash_opslimit_moderate());
    assert(crypto_pwhash_argon2id_opslimit_sensitive() ==
           crypto_pwhash_opslimit_sensitive());
    assert(crypto_pwhash_argon2id_memlimit_interactive() ==
           crypto_pwhash_memlimit_interactive());
    assert(crypto_pwhash_argon2id_memlimit_moderate() ==
           crypto_pwhash_memlimit_moderate());
    assert(crypto_pwhash_argon2id_memlimit_sensitive() ==
           crypto_pwhash_memlimit_sensitive());
    assert(crypto_pwhash_alg_argon2id13() ==
           crypto_pwhash_argon2id_alg_argon2id13());
    assert(crypto_pwhash_alg_argon2i13() == crypto_pwhash_ALG_ARGON2I13);
    assert(crypto_pwhash_alg_argon2i13() != crypto_pwhash_alg_default());
    assert(crypto_pwhash_alg_argon2id13() == crypto_pwhash_ALG_ARGON2ID13);
    assert(crypto_pwhash_alg_argon2id13() != crypto_pwhash_alg_argon2i13());
    assert(crypto_pwhash_alg_argon2id13() == crypto_pwhash_alg_default());

    assert(crypto_pwhash_argon2id(NULL, 0, NULL, 0, NULL,
                                  crypto_pwhash_argon2id_OPSLIMIT_INTERACTIVE,
                                  crypto_pwhash_argon2id_MEMLIMIT_INTERACTIVE,
                                  0) == -1);
    assert(crypto_pwhash_argon2id(NULL, 0, NULL, 0, NULL,
                                 crypto_pwhash_argon2id_OPSLIMIT_INTERACTIVE,
                                 crypto_pwhash_argon2id_MEMLIMIT_INTERACTIVE,
                                 crypto_pwhash_ALG_ARGON2I13) == -1);
    assert(crypto_pwhash_argon2i(NULL, 0, NULL, 0, NULL,
                                 crypto_pwhash_argon2id_OPSLIMIT_INTERACTIVE,
                                 crypto_pwhash_argon2id_MEMLIMIT_INTERACTIVE,
                                 0) == -1);
    assert(crypto_pwhash_argon2i(NULL, 0, NULL, 0, NULL,
                                 crypto_pwhash_argon2id_OPSLIMIT_INTERACTIVE,
                                 crypto_pwhash_argon2id_MEMLIMIT_INTERACTIVE,
                                 crypto_pwhash_ALG_ARGON2ID13) == -1);

    printf("OK\n");

    return 0;
}
