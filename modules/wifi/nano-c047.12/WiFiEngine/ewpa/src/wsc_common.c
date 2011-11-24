/****************************************/
/* Copyright (c) 2007-2008 Nanoradio AB */
/****************************************/


#include "includes.h"
#include "common.h"
#include "eap_wsc.h"
#include "crypto.h"


#ifdef EAP_WSC

#ifndef __rtke__
#define ATTRIBUTE_PACK
#pragma pack(push, 1)
#endif


static unsigned char DH_P_VALUE[BUF_SIZE_1536_BITS] =
{
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68, 0xC2, 0x34,
	0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
	0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74,
	0x02, 0x0B, 0xBE, 0xA6, 0x3B, 0x13, 0x9B, 0x22,
	0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
	0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B,
	0x30, 0x2B, 0x0A, 0x6D, 0xF2, 0x5F, 0x14, 0x37,
	0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
	0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6,
	0xF4, 0x4C, 0x42, 0xE9, 0xA6, 0x37, 0xED, 0x6B,
	0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED,
	0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5,
	0xAE, 0x9F, 0x24, 0x11, 0x7C, 0x4B, 0x1F, 0xE6,
	0x49, 0x28, 0x66, 0x51, 0xEC, 0xE4, 0x5B, 0x3D,
	0xC2, 0x00, 0x7C, 0xB8, 0xA1, 0x63, 0xBF, 0x05,
	0x98, 0xDA, 0x48, 0x36, 0x1C, 0x55, 0xD3, 0x9A,
	0x69, 0x16, 0x3F, 0xA8, 0xFD, 0x24, 0xCF, 0x5F,
	0x83, 0x65, 0x5D, 0x23, 0xDC, 0xA3, 0xAD, 0x96,
	0x1C, 0x62, 0xF3, 0x56, 0x20, 0x85, 0x52, 0xBB,
	0x9E, 0xD5, 0x29, 0x07, 0x70, 0x96, 0x96, 0x6D,
	0x67, 0x0C, 0x35, 0x4E, 0x4A, 0xBC, 0x98, 0x04,
	0xF1, 0x74, 0x6C, 0x08, 0xCA, 0x23, 0x73, 0x27,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
static uint32 DH_G_VALUE = 2;


#ifndef __rtke__
#pragma pack(pop)
#endif

static int dhm_rand( void *rng_d )
{
	return os_random();
}

uint32 GenerateDHKeyPair(dhm_context *dhm, uint8 *pubKey)
{
	uint32 g;

	/*mpi_self_test(1);*/
	
	/* 1. load the value of P */
	if( mpi_read_binary(&dhm->P, DH_P_VALUE, BUF_SIZE_1536_BITS) != 0 ) {
		wpa_printf(MSG_INFO, "GenerateKeyPair: failed to load P\n"); 
		return WSC_ERR_SYSTEM;
	}	

	/* 2. load the value of G */
	g = htonl(DH_G_VALUE);
   
	if( mpi_read_binary(&dhm->G, (uint8 *)&g, 4) != 0 ) {
		wpa_printf(MSG_INFO, "GenerateKeyPair: failed to load G\n");
		return WSC_ERR_SYSTEM;
	}

	/* 3. generate the DH key */
	dhm->len = 192;/*( mpi_msb( &dhm->P ) + 7 ) >> 3;*/

	if( dhm_make_public(dhm, 16, pubKey, dhm->len, dhm_rand, NULL ) != 0) {
		wpa_printf(MSG_INFO, "GenerateKeyPair: failed to load generate public key\n"); 
		return WSC_ERR_SYSTEM;
	}

	return WSC_SUCCESS;
}


void DeriveKey(uint8 *KDK,
	       uint8 *prsnlString,
	       uint32 keyBits,
	       uint8 *key)
{
	uint32 i=0, iterations = 0;
	uint8 inPtr[2*SIZE_4_BYTES+36];/*strlen(PERSONALIZATION_STRING)];*/
	uint8 output[96];
	uint32 output_len = 0;
	uint8 hmac[SIZE_256_BITS];
	uint32 hmacLen = 0, offset;
	uint32 temp;
	
	wpa_printf(MSG_DEBUG, "DeriveKey: Deriving a key of %d bits\n", keyBits);
	
	iterations = 3;/* ((keyBits/8) + PRF_DIGEST_SIZE - 1)/PRF_DIGEST_SIZE;*/
	
	/*
	 *  Prepare the input buffer. During the iterations, we need only replace the
	 *  value of i at the start of the buffer.
	 */
	temp = htonl(i);
	os_memcpy(inPtr, (uint8 *)&temp, SIZE_4_BYTES);
	os_memcpy(inPtr+SIZE_4_BYTES, prsnlString, 36);/*strlen(PERSONALIZATION_STRING));*/
	temp = htonl(keyBits);
	os_memcpy(inPtr+SIZE_4_BYTES+36/*strlen(PERSONALIZATION_STRING)*/, (uint8 *)&temp, SIZE_4_BYTES);
	
	offset = 0;
	for(i = 0; i < iterations; i++) {
		/* Set the current value of i at the start of the input buffer */
		*(uint32 *)inPtr = htonl(i+1); /*i should start at 1 */
		
		hmac_sha256(KDK, SIZE_256_BITS, inPtr, 2*SIZE_4_BYTES+36/*strlen(PERSONALIZATION_STRING)*/, hmac);
		hmacLen = 32;
		
		os_memcpy(output+offset, hmac, hmacLen);
		offset+=hmacLen;
		/*os_memcpy(*key+hmacLen, hmac, hmacLen);*/
		output_len += hmacLen;
	}

	/* Sanity check */
	if(keyBits/8 > output_len) {
		wpa_printf(MSG_INFO, "DeriveKey: Key derivation generated less bits than asked\n");
	}

	/*  We now have at least the number of key bits requested.
	 *  Return only the number of bits asked for. Discard the excess.
	 */
	os_memcpy(key, output, keyBits/8);
}

uint8 ValidateMac(uint8 *data,
		  uint32 data_len, 
		  uint8 *hmac, 
		  uint8 *key)
{
	uint8 *dataMac;

	dataMac = (uint8 *)os_malloc(BUF_SIZE_256_BITS);
	if(dataMac == NULL)
		return 0;

	/* First calculate the hmac of the data */
	hmac_sha256(key, SIZE_256_BITS, data, data_len, dataMac);

	/* next, compare it against the received hmac */
	wpa_printf(MSG_DEBUG, "ValidateMac: Verifying the first 64 bits of the generated HMAC\n");
	
	if(os_memcmp(dataMac, hmac, SIZE_64_BITS) != 0) {
		wpa_printf(MSG_INFO, "ValidateMac: HMAC results don't match\n");
		os_free(dataMac);
		return 0; /* 0 : Failure*/
	}
	os_free(dataMac);

	return 1; /*1: Success*/
}

/**
 * Decrypt WPS data
 *
 * out_len:   <= encr_len or 0
 */
uint8* wps_decrypt(
    uint8 *encr,
    uint32 encr_len,
    uint8 *iv,
    uint8 *keywrapkey,
    uint32 *out_len)
{
	struct crypto_cipher *ctx = NULL;
	const size_t block_size = 16;
	uint8 *out = NULL;
	u8 *in;
	int in_len;
	size_t i;
	const u8 *pos;
	u8 pad;

	/* AES-128-CBC */
	if (encr == NULL || encr_len < 2 * block_size || encr_len % block_size)
	{
		wpa_printf(MSG_DEBUG, "WPS: No Encrypted Settings received");
		*out_len = 0;
		return NULL;
	}

	in = encr + block_size; /* first block is the iv */
	in_len = encr_len - block_size;

	out = (uint8 *)os_malloc(in_len);
	if (out == NULL)
	{
		*out_len = 0;
		return NULL;
	}

	//wpa_hexdump(MSG_MSGDUMP, "WPS: Encrypted Settings", encr, encr_len);

	ctx = crypto_cipher_init(CRYPTO_CIPHER_ALG_AES, iv, keywrapkey, block_size);
	crypto_cipher_decrypt(ctx, in, out, in_len);
	crypto_cipher_deinit(ctx);

	//wpa_hexdump(MSG_MSGDUMP, "WPS: Decrypted Settings", out, in_len);

	pos = out + in_len - 1;
	pad = *pos;
	if (pad > in_len) {
		wpa_printf(MSG_DEBUG, "WPS: Invalid PKCS#5 v2.0 pad value");
		os_free(out);
		*out_len = 0;
		return NULL;
	}

	for (i = 0; i < pad; i++) {
		if (*pos-- != pad) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid PKCS#5 v2.0 pad "
			           "string");
			os_free(out);
			*out_len = 0;
			return NULL;
		}
	}

	*out_len = in_len -pad;

	return out;
}

/**
 * Ecrypt WPS data
 *
 * out_len: will be plain_len+block_size(16) or less if successfull, otherwise 0
 *
 */
uint8* wps_encrypt(
    uint8 *plain,
    uint32 plain_len,
    uint8 *iv,
    uint8 *keywrapkey,
    uint32 *cipherTextLen)
{
	const size_t block_size = 16;
	struct crypto_cipher *ctx = NULL;
	size_t pad_len = 0;
	uint8 *out = NULL;
	int out_len;
	uint8 *in = NULL;
	int in_len;

	/* PKCS#5 v2.0 pad */
	pad_len = block_size - plain_len % block_size;
	in_len = plain_len + pad_len;
	if (pad_len==0)
	{
		in = plain;
	} else {
		in = (uint8 *)os_malloc(in_len);
		os_memcpy(in, plain, plain_len);
		os_memset(in+plain_len, pad_len, pad_len);
	}

	out_len = in_len;
	out = (uint8 *)os_malloc(out_len);

	if (out == NULL) {
		goto bail;
	}
	os_memset(out, 0, out_len);

	/* Generate a random iv */
	os_get_random(iv, block_size);

	/* Now encrypt the plaintext and mac using the encryption key and IV. */
	ctx = crypto_cipher_init(CRYPTO_CIPHER_ALG_AES, iv, keywrapkey, block_size);
	if (ctx == NULL) {
		os_free(out);
		out = NULL;
		goto bail;
	}
	crypto_cipher_encrypt(ctx, in, out, out_len);

	*cipherTextLen = out_len;

	/* fall-through */
bail:
	if (ctx) crypto_cipher_deinit(ctx);
	if (pad_len > 0 && in) os_free(in);
	return out;
}

#endif //EAP_WSC

/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */
