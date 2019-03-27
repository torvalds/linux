
#define TEST_NAME "xchacha20"
#include "cmptest.h"

typedef struct HChaCha20TV_ {
    const char key[crypto_core_hchacha20_KEYBYTES * 2 + 1];
    const char in[crypto_core_hchacha20_INPUTBYTES * 2 + 1];
    const char out[crypto_core_hchacha20_OUTPUTBYTES * 2 + 1];
} HChaCha20TV;

static const unsigned char small_order_p[crypto_scalarmult_BYTES] = {
    0xe0, 0xeb, 0x7a, 0x7c, 0x3b, 0x41, 0xb8, 0xae, 0x16, 0x56, 0xe3,
    0xfa, 0xf1, 0x9f, 0xc4, 0x6a, 0xda, 0x09, 0x8d, 0xeb, 0x9c, 0x32,
    0xb1, 0xfd, 0x86, 0x62, 0x05, 0x16, 0x5f, 0x49, 0xb8, 0x00
};

static void
tv_hchacha20(void)
{
    static const HChaCha20TV tvs[] = {
        { "24f11cce8a1b3d61e441561a696c1c1b7e173d084fd4812425435a8896a013dc", "d9660c5900ae19ddad28d6e06e45fe5e", "5966b3eec3bff1189f831f06afe4d4e3be97fa9235ec8c20d08acfbbb4e851e3" },
        { "80a5f6272031e18bb9bcd84f3385da65e7731b7039f13f5e3d475364cd4d42f7", "c0eccc384b44c88e92c57eb2d5ca4dfa", "6ed11741f724009a640a44fce7320954c46e18e0d7ae063bdbc8d7cf372709df" },
        { "cb1fc686c0eec11a89438b6f4013bf110e7171dace3297f3a657a309b3199629", "fcd49b93e5f8f299227e64d40dc864a3", "84b7e96937a1a0a406bb7162eeaad34308d49de60fd2f7ec9dc6a79cbab2ca34" },
        { "6640f4d80af5496ca1bc2cfff1fefbe99638dbceaabd7d0ade118999d45f053d", "31f59ceeeafdbfe8cae7914caeba90d6", "9af4697d2f5574a44834a2c2ae1a0505af9f5d869dbe381a994a18eb374c36a0" },
        { "0693ff36d971225a44ac92c092c60b399e672e4cc5aafd5e31426f123787ac27", "3a6293da061da405db45be1731d5fc4d", "f87b38609142c01095bfc425573bb3c698f9ae866b7e4216840b9c4caf3b0865" },
        { "809539bd2639a23bf83578700f055f313561c7785a4a19fc9114086915eee551", "780c65d6a3318e479c02141d3f0b3918", "902ea8ce4680c09395ce71874d242f84274243a156938aaa2dd37ac5be382b42" },
        { "1a170ddf25a4fd69b648926e6d794e73408805835c64b2c70efddd8cd1c56ce0", "05dbee10de87eb0c5acb2b66ebbe67d3", "a4e20b634c77d7db908d387b48ec2b370059db916e8ea7716dc07238532d5981" },
        { "3b354e4bb69b5b4a1126f509e84cad49f18c9f5f29f0be0c821316a6986e15a6", "d8a89af02f4b8b2901d8321796388b6c", "9816cb1a5b61993735a4b161b51ed2265b696e7ded5309c229a5a99f53534fbc" },
        { "4b9a818892e15a530db50dd2832e95ee192e5ed6afffb408bd624a0c4e12a081", "a9079c551de70501be0286d1bc78b045", "ebc5224cf41ea97473683b6c2f38a084bf6e1feaaeff62676db59d5b719d999b" },
        { "c49758f00003714c38f1d4972bde57ee8271f543b91e07ebce56b554eb7fa6a7", "31f0204e10cf4f2035f9e62bb5ba7303", "0dd8cc400f702d2c06ed920be52048a287076b86480ae273c6d568a2e9e7518c" }
    };
    const HChaCha20TV *tv;
    unsigned char     *constant;
    unsigned char     *key;
    unsigned char     *in;
    unsigned char     *out;
    unsigned char     *out2;
    size_t             i;

    constant = (unsigned char *) sodium_malloc(crypto_core_hchacha20_CONSTBYTES);
    key = (unsigned char *) sodium_malloc(crypto_core_hchacha20_KEYBYTES);
    in = (unsigned char *) sodium_malloc(crypto_core_hchacha20_INPUTBYTES);
    out = (unsigned char *) sodium_malloc(crypto_core_hchacha20_OUTPUTBYTES);
    out2 = (unsigned char *) sodium_malloc(crypto_core_hchacha20_OUTPUTBYTES);
    for (i = 0; i < (sizeof tvs) / (sizeof tvs[0]); i++) {
        tv = &tvs[i];
        sodium_hex2bin(key, crypto_core_hchacha20_KEYBYTES,
                       tv->key, strlen(tv->key), NULL, NULL, NULL);
        sodium_hex2bin(in, crypto_core_hchacha20_INPUTBYTES,
                       tv->in, strlen(tv->in), NULL, NULL, NULL);
        sodium_hex2bin(out, crypto_core_hchacha20_OUTPUTBYTES,
                       tv->out, strlen(tv->out), NULL, NULL, NULL);
        crypto_core_hchacha20(out2, in, key, NULL);
        assert(memcmp(out, out2, crypto_core_hchacha20_OUTPUTBYTES) == 0);
    }

    sodium_hex2bin(constant, crypto_core_hchacha20_CONSTBYTES,
                   "0d29b795c1ca70c1652e823364d32417",
                   crypto_core_hchacha20_CONSTBYTES * 2 + 1, NULL, NULL, NULL);
    sodium_hex2bin(out, crypto_core_hchacha20_OUTPUTBYTES,
                   "934d941d78eb9bfc2f0376f7ccd4a11ecf0c6a44104618a9749ef47fe97037a2",
                   crypto_core_hchacha20_OUTPUTBYTES * 2 + 1, NULL, NULL, NULL);

    crypto_core_hchacha20(out2, in, key, constant);
    assert(memcmp(out, out2, crypto_core_hchacha20_OUTPUTBYTES) == 0);

    sodium_free(out2);
    sodium_free(out);
    sodium_free(in);
    sodium_free(key);
    sodium_free(constant);

    assert(crypto_core_hchacha20_outputbytes() == crypto_core_hchacha20_OUTPUTBYTES);
    assert(crypto_core_hchacha20_inputbytes() == crypto_core_hchacha20_INPUTBYTES);
    assert(crypto_core_hchacha20_keybytes() == crypto_core_hchacha20_KEYBYTES);
    assert(crypto_core_hchacha20_constbytes() == crypto_core_hchacha20_CONSTBYTES);

    printf("tv_hchacha20: ok\n");
}

#define XCHACHA20_OUT_MAX 100

typedef struct XChaCha20TV_ {
    const char key[crypto_stream_xchacha20_KEYBYTES * 2 + 1];
    const char nonce[crypto_stream_xchacha20_NONCEBYTES * 2 + 1];
    const char out[XCHACHA20_OUT_MAX * 2 + 1];
} XChaCha20TV;

static void
tv_stream_xchacha20(void)
{
    static const XChaCha20TV tvs[] = {
        { "79c99798ac67300bbb2704c95c341e3245f3dcb21761b98e52ff45b24f304fc4", "b33ffd3096479bcfbc9aee49417688a0a2554f8d95389419", "c6e9758160083ac604ef90e712ce6e75d7797590744e0cf060f013739c" },
        { "ddf7784fee099612c40700862189d0397fcc4cc4b3cc02b5456b3a97d1186173", "a9a04491e7bf00c3ca91ac7c2d38a777d88993a7047dfcc4", "2f289d371f6f0abc3cb60d11d9b7b29adf6bc5ad843e8493e928448d" },
        { "3d12800e7b014e88d68a73f0a95b04b435719936feba60473f02a9e61ae60682", "56bed2599eac99fb27ebf4ffcb770a64772dec4d5849ea2d", "a2c3c1406f33c054a92760a8e0666b84f84fa3a618f0" },
        { "5f5763ff9a30c95da5c9f2a8dfd7cc6efd9dfb431812c075aa3e4f32e04f53e4", "a5fa890efa3b9a034d377926ce0e08ee6d7faccaee41b771", "8a1a5ba898bdbcff602b1036e469a18a5e45789d0e8d9837d81a2388a52b0b6a0f51891528f424c4a7f492a8dd7bce8bac19fbdbe1fb379ac0" },
        { "eadc0e27f77113b5241f8ca9d6f9a5e7f09eee68d8a5cf30700563bf01060b4e", "a171a4ef3fde7c4794c5b86170dc5a099b478f1b852f7b64", "23839f61795c3cdbcee2c749a92543baeeea3cbb721402aa42e6cae140447575f2916c5d71108e3b13357eaf86f060cb" },
        { "91319c9545c7c804ba6b712e22294c386fe31c4ff3d278827637b959d3dbaab2", "410e854b2a911f174aaf1a56540fc3855851f41c65967a4e", "cbe7d24177119b7fdfa8b06ee04dade4256ba7d35ffda6b89f014e479faef6" },
        { "6a6d3f412fc86c4450fc31f89f64ed46baa3256ffcf8616e8c23a06c422842b6", "6b7773fce3c2546a5db4829f53a9165f41b08faae2fb72d5", "8b23e35b3cdd5f3f75525fc37960ec2b68918e8c046d8a832b9838f1546be662e54feb1203e2" },
        { "d45e56368ebc7ba9be7c55cfd2da0feb633c1d86cab67cd5627514fd20c2b391", "fd37da2db31e0c738754463edadc7dafb0833bd45da497fc", "47950efa8217e3dec437454bd6b6a80a287e2570f0a48b3fa1ea3eb868be3d486f6516606d85e5643becc473b370871ab9ef8e2a728f73b92bd98e6e26ea7c8ff96ec5a9e8de95e1eee9300c" },
        { "aface41a64a9a40cbc604d42bd363523bd762eb717f3e08fe2e0b4611eb4dcf3", "6906e0383b895ab9f1cf3803f42f27c79ad47b681c552c63", "a5fa7c0190792ee17675d52ad7570f1fb0892239c76d6e802c26b5b3544d13151e67513b8aaa1ac5af2d7fd0d5e4216964324838" },
        { "9d23bd4149cb979ccf3c5c94dd217e9808cb0e50cd0f67812235eaaf601d6232", "c047548266b7c370d33566a2425cbf30d82d1eaf5294109e", "a21209096594de8c5667b1d13ad93f744106d054df210e4782cd396fec692d3515a20bf351eec011a92c367888bc464c32f0807acd6c203a247e0db854148468e9f96bee4cf718d68d5f637cbd5a376457788e6fae90fc31097cfc" },
    };
    const XChaCha20TV *tv;
    char              *hex;
    unsigned char     *key;
    unsigned char     *nonce;
    unsigned char     *out;
    unsigned char     *out2;
    size_t             out_len;
    size_t             i;

    key = (unsigned char *) sodium_malloc(crypto_stream_xchacha20_KEYBYTES);
    nonce = (unsigned char *) sodium_malloc(crypto_stream_xchacha20_NONCEBYTES);
    out = (unsigned char *) sodium_malloc(XCHACHA20_OUT_MAX);
    for (i = 0; i < (sizeof tvs) / (sizeof tvs[0]); i++) {
        tv = &tvs[i];

        sodium_hex2bin(key, crypto_stream_xchacha20_KEYBYTES,
                       tv->key, strlen(tv->key), NULL, NULL, NULL);
        sodium_hex2bin(nonce, crypto_stream_xchacha20_NONCEBYTES,
                       tv->nonce, strlen(tv->nonce), NULL, NULL, NULL);
        sodium_hex2bin(out, XCHACHA20_OUT_MAX,
                       tv->out, strlen(tv->out), NULL, &out_len, NULL);
        out2 = (unsigned char *) sodium_malloc(out_len);
        crypto_stream_xchacha20(out2, out_len, nonce, key);
        assert(memcmp(out, out2, out_len) == 0);
        crypto_stream_xchacha20_xor(out2, out, out_len, nonce, key);
        assert(sodium_is_zero(out2, out_len));
        crypto_stream_xchacha20_xor_ic(out2, out, out_len, nonce, 0, key);
        assert(sodium_is_zero(out2, out_len));
        crypto_stream_xchacha20_xor_ic(out2, out, out_len, nonce, 1, key);
        assert(!sodium_is_zero(out2, out_len));
        crypto_stream_xchacha20_xor(out, out, out_len, nonce, key);
        assert(sodium_is_zero(out, out_len));
        sodium_free(out2);
    }

    out2 = (unsigned char *) sodium_malloc(0);
    crypto_stream_xchacha20(out2, 0, nonce, key);
    crypto_stream_xchacha20_xor(out2, out2, 0, nonce, key);
    crypto_stream_xchacha20_xor_ic(out2, out2, 0, nonce, 1, key);
    sodium_free(out2);
    sodium_free(out);

    out = (unsigned char *) sodium_malloc(64);
    out2 = (unsigned char *) sodium_malloc(128);
    randombytes_buf(out, 64);
    randombytes_buf(out2, 64);
    memcpy(out2 + 64, out, 64);
    crypto_stream_xchacha20_xor_ic(out, out, 64, nonce, 1, key);
    crypto_stream_xchacha20_xor(out2, out2, 128, nonce, key);
    assert(memcmp(out, out2 + 64, 64) == 0);
    sodium_free(out);
    sodium_free(out2);

    out = (unsigned char *) sodium_malloc(192);
    out2 = (unsigned char *) sodium_malloc(192);
    memset(out, 0, 192);
    memset(out2, 0, 192);
    crypto_stream_xchacha20_xor_ic(out2, out2, 192, nonce,
                                   (1ULL << 32) - 1ULL, key);
    crypto_stream_xchacha20_xor_ic(out, out, 64, nonce,
                                   (1ULL << 32) - 1ULL, key);
    crypto_stream_xchacha20_xor_ic(out + 64, out + 64, 64, nonce,
                                   (1ULL << 32), key);
    crypto_stream_xchacha20_xor_ic(out + 128, out + 128, 64, nonce,
                                   (1ULL << 32) + 1, key);
    assert(memcmp(out, out2, 192) == 0);
    hex = (char *) sodium_malloc(192 * 2 + 1);
    sodium_bin2hex(hex, 192 * 2 + 1, out, 192);
    printf("%s\n", hex);

    memset(key, 0, crypto_stream_xchacha20_KEYBYTES);
    crypto_stream_xchacha20_keygen(key);
    assert(sodium_is_zero(key, crypto_stream_xchacha20_KEYBYTES) == 0);

    sodium_free(hex);
    sodium_free(out);
    sodium_free(out2);

    sodium_free(nonce);
    sodium_free(key);

    assert(crypto_stream_xchacha20_keybytes() == crypto_stream_xchacha20_KEYBYTES);
    assert(crypto_stream_xchacha20_noncebytes() == crypto_stream_xchacha20_NONCEBYTES);
    assert(crypto_stream_xchacha20_messagebytes_max() == crypto_stream_xchacha20_MESSAGEBYTES_MAX);

    printf("tv_stream_xchacha20: ok\n");
}

typedef struct XChaCha20Poly1305TV_ {
    const char  key[crypto_secretbox_xchacha20poly1305_KEYBYTES * 2 + 1];
    const char  nonce[crypto_secretbox_xchacha20poly1305_NONCEBYTES * 2 + 1];
    const char *m;
    const char *out;
} XChaCha20Poly1305TV;

static void
tv_secretbox_xchacha20poly1305(void)
{
    static const XChaCha20Poly1305TV tvs[] = {
        { "065ff46a9dddb1ab047ee5914d6d575a828b8cc1f454b24e8cd0f57efdc49a34", "f83262646ce01293b9923a65a073df78c54b2e799cd6c4e5", "", "4c72340416339dcdea01b760db5adaf7" },
        { "d3c71d54e6b13506e07aa2e7b412a17a7a1f34df3d3148cd3f45b91ccaa5f4d9", "943b454a853aa514c63cf99b1e197bbb99da24b2e2d93e47", "76bd706e07741e713d90efdb34ad202067263f984942aae8bda159f30dfccc72200f8093520b85c5ad124ff7c8b2d920946e5cfff4b819abf84c7b35a6205ca72c9f8747c3044dd73fb4bebda1b476", "0384276f1cfa5c82c3e58f0f2acc1f821c6f526d2c19557cf8bd270fcde43fba1d88890663f7b2f5c6b1d7deccf5c91b4df5865dc55cc7e04d6793fc2db8f9e3b418f95cb796d67a7f3f7e097150cb607c435dacf82eac3d669866e5092ace" },
        { "9498fdb922e0596e32af7f8108def2068f5a32a5ac70bd33ade371701f3d98d0", "a0056f24be0d20106fe750e2ee3684d4457cbdcb3a74e566", "b1bc9cfedb340fb06a37eba80439189e48aa0cfd37020eec0afa09165af12864671b3fbddbbb20ac18f586f2f66d13b3ca40c9a7e21c4513a5d87a95319f8ca3c2151e2a1b8b86a35653e77f90b9e63d2a84be9b9603876a89d60fd708edcd64b41be1064b8ad1046553aaeb51dc70b8112c9915d94f2a5dad1e14e7009db6c703c843a4f64b77d44b179b9579ac497dac2d33", "4918790d46893fa3dca74d8abc57eef7fca2c6393d1beef5efa845ac20475db38d1a068debf4c5dbd8614eb072877c565dc52bd40941f0b590d2079a5028e426bf50bcbaadcbebf278bddceedc578a5e31379523dee15026ec82d34e56f2871fdf13255db199ac48f163d5ee7e4f4e09a39451356959d9242a39aea33990ab960a4c25346e3d9397fc5e7cb6266c2476411cd331f2bcb4486750c746947ec6401865d5" },
        { "fa2d915e044d0519248150e7c815b01f0f2a691c626f8d22c3ef61e7f16eea47", "c946065dc8befa9cc9f292ea2cf28f0256285565051792b7", "d5be1a24c7872115dc5c5b4234dbee35a6f89ae3a91b3e33d75249a0aecfed252341295f49296f7ee14d64de1ea6355cb8facd065052d869aeb1763cda7e418a7e33b6f7a81327181df6cd4de3a126d9df1b5e8b0b1a6b281e63f2", "6d32e3571afec58b0acabb54a287118b3ed6691f56cc8ead12d735352c9a050c2ca173c78b6092f9ad4b7c21c36fb0ce18560956395bab3099c54760a743051ac6a898a0b0034b5e953340c975cf7a873c56b27e66bca2bff1dd977addefc7935bb7550753dd13d1f1a43d" },
        { "6f149c2ec27af45176030c8dd7ab0e1e488f5803f26f75045d7a56f59a587a85", "952aff2f39bc70016f04ac7fb8b55fd22764ba16b56e255d", "8fde598c4bde5786abdc6ab83fce66d59782b6ce36afe028c447ad4086a748764afa88a520e837a9d56d0b7693b0476649f24c2aa44b94615a1efc75", "9bccf07974836fa4609d32d9527d928d184d9c6c0823af2f703e0e257a162d26d3678fa15ab1c4db76ac42084d32cefca8efaf77814c199b310999e327a3e3daa2e235b175979504ede87b58" },
        { "b964b7fdf442efbcc2cd3e4cd596035bdfb05ed7d44f7fd4dce2d5614af5c8c4", "2886fbfa4b35b68f28d31df6243a4fbc56475b69e24820a4", "", "b83fbdd112bf0f7d62eff96c9faa8850" },
        { "10c0ad4054b48d7d1de1d9ab6f782ca883d886573e9d18c1d47b6ee6b5208189", "977edf57428d0e0247a3c88c9a9ec321bbaae1a4da8353b5", "518e4a27949812424b2a381c3efea6055ee5e75eff", "0c801a037c2ed0500d6ef68e8d195eceb05a15f8edb68b35773e81ac2aca18e9be53416f9a" },
        { "7db0a81d01699c86f47a3ec76d46aa32660adad7f9ac72cf8396419f789f6bb1", "e7cb57132ce954e28f4470cca1dbda20b534cdf32fbe3658", "ee6511d403539e611ab312205f0c3b8f36a33d36f1dc44bb33d6836f0ab93b9f1747167bf0150f045fcd12a39479641d8bdde6fe01475196e8fe2c435e834e30a59f6aaa01ebcd", "ae8b1d4df4f982b2702626feca07590fedd0dfa7ae34e6a098372a1aa32f9fbf0ce2a88b5c16a571ef48f3c9fda689ce8ebb9947c9e2a28e01b1191efc81ad2ce0ed6e6fc7c164b1fc7f3d50b7f5e47a895db3c1fc46c0" },
        { "7b043dd27476cf5a2baf2907541d8241ecd8b97d38d08911737e69b0846732fb", "74706a2855f946ed600e9b453c1ac372520b6a76a3c48a76", "dbf165bb8352d6823991b99f3981ba9c8153635e5695477cba54e96a2a8c4dc5f9dbe817887d7340e3f48a", "ce57261afba90a9598de15481c43f26f7b8c8cb2806c7c977752dba898dc51b92a3f1a62ebf696747bfccf72e0edda97f2ccd6d496f55aefbb3ec2" },
        { "e588e418d658df1b2b1583122e26f74ca3506b425087bea895d81021168f8164", "4f4d0ffd699268cd841ce4f603fe0cd27b8069fcf8215fbb", "f91bcdcf4d08ba8598407ba8ef661e66c59ca9d89f3c0a3542e47246c777091e4864e63e1e3911dc01257255e551527a53a34481be", "22dc88de7cacd4d9ce73359f7d6e16e74caeaa7b0d1ef2bb10fda4e79c3d5a9aa04b8b03575fd27bc970c9ed0dc80346162469e0547030ddccb8cdc95981400907c87c9442" }
    };
    const XChaCha20Poly1305TV *tv;
    unsigned char             *m;
    unsigned char             *nonce;
    unsigned char             *key;
    unsigned char             *out;
    unsigned char             *out2;
    size_t                     m_len;
    size_t                     n;
    size_t                     i;

    key = (unsigned char *) sodium_malloc
        (crypto_secretbox_xchacha20poly1305_KEYBYTES);
    nonce = (unsigned char *) sodium_malloc
        (crypto_secretbox_xchacha20poly1305_NONCEBYTES);
    for (i = 0; i < (sizeof tvs) / (sizeof tvs[0]); i++) {
        tv = &tvs[i];
        m_len = strlen(tv->m) / 2;
        m = (unsigned char *) sodium_malloc(m_len);
        sodium_hex2bin(key, crypto_secretbox_xchacha20poly1305_KEYBYTES,
                       tv->key, strlen(tv->key), NULL, NULL, NULL);
        sodium_hex2bin(nonce, crypto_secretbox_xchacha20poly1305_NONCEBYTES,
                       tv->nonce, strlen(tv->nonce), NULL, NULL, NULL);
        sodium_hex2bin(m, m_len, tv->m, strlen(tv->m), NULL, NULL, NULL);
        out = (unsigned char *) sodium_malloc
            (crypto_secretbox_xchacha20poly1305_MACBYTES + m_len);
        out2 = (unsigned char *) sodium_malloc
            (crypto_secretbox_xchacha20poly1305_MACBYTES + m_len);
        sodium_hex2bin(out, crypto_secretbox_xchacha20poly1305_MACBYTES + m_len,
                       tv->out, strlen(tv->out), NULL, NULL, NULL);
        assert(crypto_secretbox_xchacha20poly1305_easy(out2, m, 0, nonce, key) == 0);
        assert(crypto_secretbox_xchacha20poly1305_easy(out2, m, m_len, nonce, key) == 0);
        assert(memcmp(out, out2,
                      crypto_secretbox_xchacha20poly1305_MACBYTES + m_len) == 0);
        n = randombytes_uniform(crypto_secretbox_xchacha20poly1305_MACBYTES + (uint32_t) m_len);
        assert(crypto_secretbox_xchacha20poly1305_open_easy
               (out2, out2, crypto_secretbox_xchacha20poly1305_MACBYTES - 1,
                nonce, key) == -1);
        assert(crypto_secretbox_xchacha20poly1305_open_easy
               (out2, out2, 0,
                nonce, key) == -1);
        out2[n]++;
        assert(crypto_secretbox_xchacha20poly1305_open_easy
               (out2, out2, crypto_secretbox_xchacha20poly1305_MACBYTES + m_len,
                nonce, key) == -1);
        out2[n]--;
        nonce[0]++;
        assert(crypto_secretbox_xchacha20poly1305_open_easy
               (out2, out2, crypto_secretbox_xchacha20poly1305_MACBYTES + m_len,
                nonce, key) == -1);
        nonce[0]--;
        assert(crypto_secretbox_xchacha20poly1305_open_easy
               (out2, out2, crypto_secretbox_xchacha20poly1305_MACBYTES + m_len,
                nonce, key) == 0);
        assert(crypto_secretbox_xchacha20poly1305_open_easy
               (out2, out2, crypto_secretbox_xchacha20poly1305_MACBYTES - 1,
                nonce, key) == -1);
        assert(crypto_secretbox_xchacha20poly1305_open_easy
               (out2, out2, 0, nonce, key) == -1);
        assert(memcmp(m, out2, m_len) == 0);
        assert(crypto_secretbox_xchacha20poly1305_open_detached
               (out2, out + crypto_secretbox_xchacha20poly1305_MACBYTES, out,
                m_len, nonce, key) == 0);
        assert(crypto_secretbox_xchacha20poly1305_open_detached
               (NULL, out + crypto_secretbox_xchacha20poly1305_MACBYTES, out,
                m_len, nonce, key) == 0);
        crypto_secretbox_xchacha20poly1305_detached
            (out2 + crypto_secretbox_xchacha20poly1305_MACBYTES, out2, m,
             m_len, nonce, key);
        assert(memcmp(out, out2,
                      crypto_secretbox_xchacha20poly1305_MACBYTES + m_len) == 0);
        sodium_free(out);
        sodium_free(out2);
        sodium_free(m);
    }
    sodium_free(nonce);
    sodium_free(key);

    assert(crypto_secretbox_xchacha20poly1305_keybytes() == crypto_secretbox_xchacha20poly1305_KEYBYTES);
    assert(crypto_secretbox_xchacha20poly1305_noncebytes() == crypto_secretbox_xchacha20poly1305_NONCEBYTES);
    assert(crypto_secretbox_xchacha20poly1305_macbytes() == crypto_secretbox_xchacha20poly1305_MACBYTES);
    assert(crypto_secretbox_xchacha20poly1305_messagebytes_max() == crypto_secretbox_xchacha20poly1305_MESSAGEBYTES_MAX);

    printf("tv_secretbox_xchacha20: ok\n");
}

static void
tv_box_xchacha20poly1305(void)
{
    char           hex[65];
    unsigned char *pk;
    unsigned char *sk;
    unsigned char *m;
    unsigned char *m2;
    unsigned char *mac;
    unsigned char *nonce;
    unsigned char *out;
    unsigned char *pc;
    unsigned char *seed;
    size_t         m_len;
    int            i;

    pk = (unsigned char *) sodium_malloc(crypto_box_curve25519xchacha20poly1305_PUBLICKEYBYTES);
    sk = (unsigned char *) sodium_malloc(crypto_box_curve25519xchacha20poly1305_SECRETKEYBYTES);
    nonce = (unsigned char *) sodium_malloc(crypto_box_curve25519xchacha20poly1305_NONCEBYTES);
    mac = (unsigned char *) sodium_malloc(crypto_box_curve25519xchacha20poly1305_MACBYTES);
    pc = (unsigned char *) sodium_malloc(crypto_box_curve25519xchacha20poly1305_BEFORENMBYTES);
    for (i = 0; i < 10; i++) {
        m_len = (i == 0) ? 0 : randombytes_uniform(150);
        m = (unsigned char *) sodium_malloc(m_len);
        m2 = (unsigned char *) sodium_malloc(m_len);

        out = (unsigned char *) sodium_malloc
            (crypto_box_curve25519xchacha20poly1305_MACBYTES + m_len);
        randombytes_buf(nonce, crypto_box_curve25519xchacha20poly1305_NONCEBYTES);
        randombytes_buf(m, m_len);
        assert(crypto_box_curve25519xchacha20poly1305_keypair(pk, sk) == 0);
        assert(crypto_box_curve25519xchacha20poly1305_easy(out, m, 0, nonce,
                                                           pk, sk) == 0);
        assert(crypto_box_curve25519xchacha20poly1305_easy(out, m, m_len, nonce,
                                                           pk, sk) == 0);
        assert(crypto_box_curve25519xchacha20poly1305_open_easy
               (m2, out, crypto_box_curve25519xchacha20poly1305_MACBYTES + m_len,
                nonce, small_order_p, sk) == -1);
        assert(crypto_box_curve25519xchacha20poly1305_open_easy
               (m2, out, crypto_box_curve25519xchacha20poly1305_MACBYTES - 1,
                nonce, pk, sk) == -1);
        assert(crypto_box_curve25519xchacha20poly1305_open_easy
               (m2, out, 0, nonce, pk, sk) == -1);
        assert(crypto_box_curve25519xchacha20poly1305_open_easy
               (m2, out, crypto_box_curve25519xchacha20poly1305_MACBYTES + m_len,
                nonce, pk, sk) == 0);
        assert(memcmp(m2, m, m_len) == 0);
        sodium_free(out);

        out = (unsigned char *) sodium_malloc
            (crypto_box_curve25519xchacha20poly1305_MACBYTES + m_len);
        assert(crypto_box_curve25519xchacha20poly1305_beforenm(pc, small_order_p, sk) == -1);
        assert(crypto_box_curve25519xchacha20poly1305_beforenm(pc, pk, sk) == 0);
        assert(crypto_box_curve25519xchacha20poly1305_easy_afternm
               (out, m, 0, nonce, pc) == 0);
        assert(crypto_box_curve25519xchacha20poly1305_easy_afternm
               (out, m, m_len, nonce, pc) == 0);
        assert(crypto_box_curve25519xchacha20poly1305_open_easy_afternm
               (m2, out, crypto_box_curve25519xchacha20poly1305_MACBYTES - 1,
                nonce, pc) == -1);
        assert(crypto_box_curve25519xchacha20poly1305_open_easy_afternm
               (m2, out, 0,
                nonce, pc) == -1);
        assert(crypto_box_curve25519xchacha20poly1305_open_easy_afternm
               (m2, out, crypto_box_curve25519xchacha20poly1305_MACBYTES + m_len,
                nonce, pc) == 0);
        assert(memcmp(m2, m, m_len) == 0);
        sodium_free(out);

        out = (unsigned char *) sodium_malloc(m_len);
        assert(crypto_box_curve25519xchacha20poly1305_detached(out, mac, m, m_len,
                                                               nonce, small_order_p, sk) == -1);
        assert(crypto_box_curve25519xchacha20poly1305_detached(out, mac, m, m_len,
                                                               nonce, pk, sk) == 0);
        assert(crypto_box_curve25519xchacha20poly1305_open_detached
               (m2, out, mac, m_len, nonce, small_order_p, sk) == -1);
        assert(crypto_box_curve25519xchacha20poly1305_open_detached
               (m2, out, mac, m_len, nonce, pk, sk) == 0);
        sodium_free(out);

        out = (unsigned char *) sodium_malloc(m_len);
        assert(crypto_box_curve25519xchacha20poly1305_detached_afternm
               (out, mac, m, m_len, nonce, pc) == 0);
        assert(crypto_box_curve25519xchacha20poly1305_open_detached_afternm
               (m2, out, mac, m_len, nonce, pc) == 0);
        sodium_free(out);

        sodium_free(m2);
        sodium_free(m);
    }
    sodium_free(pc);
    sodium_free(mac);
    sodium_free(nonce);

    seed = (unsigned char *) sodium_malloc
        (crypto_box_curve25519xchacha20poly1305_SEEDBYTES);
    for (i = 0; i <(int)  crypto_box_curve25519xchacha20poly1305_SEEDBYTES; i++) {
        seed[i] = (unsigned char) i;
    }
    crypto_box_curve25519xchacha20poly1305_seed_keypair(pk, sk, seed);
    sodium_bin2hex(hex, sizeof hex, pk, crypto_box_curve25519xchacha20poly1305_PUBLICKEYBYTES);
    assert(strcmp(hex, "4701d08488451f545a409fb58ae3e58581ca40ac3f7f114698cd71deac73ca01") == 0);
    sodium_bin2hex(hex, sizeof hex, sk, crypto_box_curve25519xchacha20poly1305_SECRETKEYBYTES);
    assert(strcmp(hex, "3d94eea49c580aef816935762be049559d6d1440dede12e6a125f1841fff8e6f") == 0);
    sodium_free(seed);

    sodium_free(sk);
    sodium_free(pk);

    assert(crypto_box_curve25519xchacha20poly1305_seedbytes() == crypto_box_curve25519xchacha20poly1305_SEEDBYTES);
    assert(crypto_box_curve25519xchacha20poly1305_publickeybytes() == crypto_box_curve25519xchacha20poly1305_PUBLICKEYBYTES);
    assert(crypto_box_curve25519xchacha20poly1305_secretkeybytes() == crypto_box_curve25519xchacha20poly1305_SECRETKEYBYTES);
    assert(crypto_box_curve25519xchacha20poly1305_beforenmbytes() == crypto_box_curve25519xchacha20poly1305_BEFORENMBYTES);
    assert(crypto_box_curve25519xchacha20poly1305_noncebytes() == crypto_box_curve25519xchacha20poly1305_NONCEBYTES);
    assert(crypto_box_curve25519xchacha20poly1305_macbytes() == crypto_box_curve25519xchacha20poly1305_MACBYTES);
    assert(crypto_box_curve25519xchacha20poly1305_messagebytes_max() == crypto_box_curve25519xchacha20poly1305_MESSAGEBYTES_MAX);

    printf("tv_box_xchacha20poly1305: ok\n");
}

int
main(void)
{
    tv_hchacha20();
    tv_stream_xchacha20();
    tv_secretbox_xchacha20poly1305();
    tv_box_xchacha20poly1305();

    return 0;
}
