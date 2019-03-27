#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <opencrypto/cbc_mac.h>
#include <opencrypto/xform_auth.h>

/* Authentication instances */
struct auth_hash auth_hash_ccm_cbc_mac_128 = {
	.type = CRYPTO_AES_CCM_CBC_MAC,
	.name = "CBC-CCM-AES-128",
	.keysize = AES_128_CBC_MAC_KEY_LEN,
	.hashsize = AES_CBC_MAC_HASH_LEN,
	.ctxsize = sizeof(struct aes_cbc_mac_ctx),
	.blocksize = CCM_CBC_BLOCK_LEN,
	.Init = (void (*)(void *)) AES_CBC_MAC_Init,
	.Setkey =
	    (void (*)(void *, const u_int8_t *, u_int16_t))AES_CBC_MAC_Setkey,
	.Reinit =
	    (void (*)(void *, const u_int8_t *, u_int16_t)) AES_CBC_MAC_Reinit,
	.Update =
	    (int  (*)(void *, const u_int8_t *, u_int16_t)) AES_CBC_MAC_Update,
	.Final = (void (*)(u_int8_t *, void *)) AES_CBC_MAC_Final,
};
struct auth_hash auth_hash_ccm_cbc_mac_192 = {
	.type = CRYPTO_AES_CCM_CBC_MAC,
	.name = "CBC-CCM-AES-192",
	.keysize = AES_192_CBC_MAC_KEY_LEN,
	.hashsize = AES_CBC_MAC_HASH_LEN,
	.ctxsize = sizeof(struct aes_cbc_mac_ctx),
	.blocksize = CCM_CBC_BLOCK_LEN,
	.Init = (void (*)(void *)) AES_CBC_MAC_Init,
	.Setkey =
	    (void (*)(void *, const u_int8_t *, u_int16_t)) AES_CBC_MAC_Setkey,
	.Reinit =
	    (void (*)(void *, const u_int8_t *, u_int16_t)) AES_CBC_MAC_Reinit,
	.Update =
	    (int  (*)(void *, const u_int8_t *, u_int16_t)) AES_CBC_MAC_Update,
	.Final = (void (*)(u_int8_t *, void *)) AES_CBC_MAC_Final,
};
struct auth_hash auth_hash_ccm_cbc_mac_256 = {
	.type = CRYPTO_AES_CCM_CBC_MAC,
	.name = "CBC-CCM-AES-256",
	.keysize = AES_256_CBC_MAC_KEY_LEN,
	.hashsize = AES_CBC_MAC_HASH_LEN,
	.ctxsize = sizeof(struct aes_cbc_mac_ctx),
	.blocksize = CCM_CBC_BLOCK_LEN,
	.Init = (void (*)(void *)) AES_CBC_MAC_Init,
	.Setkey =
	    (void (*)(void *, const u_int8_t *, u_int16_t)) AES_CBC_MAC_Setkey,
	.Reinit =
	    (void (*)(void *, const u_int8_t *, u_int16_t)) AES_CBC_MAC_Reinit,
	.Update =
	    (int  (*)(void *, const u_int8_t *, u_int16_t)) AES_CBC_MAC_Update,
	.Final = (void (*)(u_int8_t *, void *)) AES_CBC_MAC_Final,
};
