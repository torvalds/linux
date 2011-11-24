/*
 *
 * Copyright (c) 2007-2008 Nanoradio AB. All rights reserved.
 * 
 */

/*
 * WPA Supplicant / Wi-Fi Simple Configuration 7C Proposal
 * Copyright (c) 2004-2005, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright (c) 2005 Intel Corporation. All rights reserved.
 * 
 * Contact Information: Harsha Hegde  <harsha.hegde@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README, README_WSC and COPYING for more details.
 *
 */

#include "includes.h"

#include "common.h"
#include "eap_i.h"
#include "eap_wsc.h"
#include "config_ssid.h"
#include "config.h"

#ifdef EAP_WSC

#ifdef USE_WPS

dhm_context dhm; /* Stores Diffie-Hellman Keys */
extern struct wpa_config *config;


#define WSC_ID_CREDENTIAL           0x100E
#define WSC_ID_KEY_WRAP_AUTH        0x101E
#define WSC_ID_MAC_ADDR             0x1020
#define WSC_ID_NW_INDEX             0x1026
#define WSC_ID_SSID                 0x1045
#define WSC_ID_AUTH_TYPE            0x1003
#define WSC_ID_ENCR_TYPE            0x100F
#define WSC_ID_NW_KEY_INDEX         0x1028
#define WSC_ID_NW_KEY               0x1027

static int find_and_memcpy(
      u8 *src, 
      size_t src_len, 
      u16 type, 
      u8 *dst, 
      size_t dst_len)
{
   m80211_tlv_t tlv;
   unsigned char *tlv_data = NULL;
   int err = 0;

   if(!m80211_tlv_find(src, src_len, type, &tlv, (void**)&tlv_data))
   {
      /* none found */
      err = -1;
      goto out;
   }

   if(tlv.len != dst_len)
   {
      /* incorrect len */
      err = -2;
      goto out;
   }

   os_memcpy(dst, tlv_data, dst_len);

   return 0;

out:

   wpa_printf(MSG_ERROR,"err parsing WPS tlv %04x: %d\n", type, err);
   wpa_hexdump_ascii(MSG_ERROR, "tlv_buf: ", src, src_len);
   return err;
}

static void add_tlv_8bit_value(unsigned char *buf, size_t *len, unsigned char value_id, unsigned char value) 
{
	/* TLV Type */
	*(buf + *len) = 0x10; /* 0x10 is the first byte of all WPS TLV Types */
	*len += 1;
	*(buf + *len) = value_id;
	*len += 1;
	/* TLV Length */
	*(buf + *len) = 0x00;
	*len += 1;
	*(buf + *len) = 0x01;
	*len +=1;
	/* TLV Data */
	*(buf + *len) = value;
	*len +=1;
}
	
static void add_tlv_16bit_value(unsigned char *buf, size_t *len, unsigned char value_id, unsigned char value) 
{
	/* TLV Type */
	*(buf + *len) = 0x10;
	*len += 1;
	*(buf + *len) = value_id;
	*len += 1;
	/* TLV Length */
	*(buf + *len) = 0x00;
	*len += 1;
	*(buf + *len) = 0x02;
	*len += 1;
	/* TLV Data */
	*(buf + *len) = 0x00;
	*len +=1;
	*(buf + *len) = value;
	*len +=1;
}

static void add_tlv_data(unsigned char *buf, size_t *len, unsigned char value_id, unsigned char *data, size_t data_len) 
{
	/* TLV Type */
	*(buf + *len) = 0x10;
	*len += 1;
	*(buf + *len) = value_id;
	*len += 1;
	/* TLV Length */
	*(buf + *len) = 0x00;
	*len += 1;
	*(buf + *len) = data_len;
	*len += 1;
	/* TLV Data */
	os_memcpy( (buf+*len), data, data_len);
	*len += data_len;
}

static uint32 BuildMessageM1(S_REGISTRATION_DATA *regInfo)
{
	uint8 primDevType[12] = { 0x10, 0x54, 0x00, 0x08, 
				  0x00, 0x01, 0x00, 0x50, 
				  0xf2, 0x04, 0x00, 0x01};
	uint8 osVersion[8] = {0x10, 0x2d, 0x00, 0x04, 
			      0x80, 0x00, 0x00, 0x00};

	uint8  *msg = regInfo->outMsg;
	uint32 *msg_len = &regInfo->outMsg_len;

	/* Enrollee nonce */
	os_get_random(regInfo->enrolleeNonce, SIZE_128_BITS);

	os_memset(&dhm, 0, sizeof(dhm_context));

	/* Extract the DH public key */
	GenerateDHKeyPair(&dhm, regInfo->pke);

	/*
	 *  Now start composing the message. 
	 */

	*msg_len = 0;

	/* Version */
	add_tlv_8bit_value(msg, msg_len, 0x4a, WSC_VERSION);

	/* Message */
	add_tlv_8bit_value(msg, msg_len, 0x22, WSC_ID_MESSAGE_M1);

	/* UUID */
	add_tlv_data(msg, msg_len, 0x47, regInfo->p_enrolleeInfo->uuid, SIZE_UUID);

	/* Mac Address */
	add_tlv_data(msg, msg_len, 0x20, regInfo->p_enrolleeInfo->macAddr, SIZE_MAC_ADDR);

	/* NONCE */
	add_tlv_data(msg, msg_len, 0x1a, regInfo->enrolleeNonce, SIZE_128_BITS);

	/* Public Key */
	add_tlv_data(msg, msg_len, 0x32, regInfo->pke, SIZE_PUB_KEY);

	/* Auth Type Flags */
	add_tlv_16bit_value(msg, msg_len, 0x04, 0x23);

	/* Encryption Type Flags */
	add_tlv_16bit_value(msg, msg_len, 0x10, 0x0D);

	/* Conn Type Flags */
	add_tlv_8bit_value(msg, msg_len, 0x0d, 0x01);

	/* Config Methods */
	add_tlv_16bit_value(msg, msg_len, 0x08, 0x08); 

	/* WPS State */
	add_tlv_8bit_value(msg, msg_len, 0x44, 0x01);
  
	/* Manufacturer */
	add_tlv_data(msg, msg_len, 0x21, (unsigned char *)regInfo->p_enrolleeInfo->manufacturer, 16);

	/* Model Name */
	add_tlv_data(msg, msg_len, 0x23, (unsigned char *)regInfo->p_enrolleeInfo->modelName, 9);

	/* Model Number */
	add_tlv_data(msg, msg_len, 0x24, (unsigned char *)regInfo->p_enrolleeInfo->modelNumber, 9);
	
	/* Serial Number */
	add_tlv_data(msg, msg_len, 0x42, (unsigned char *)regInfo->p_enrolleeInfo->serialNumber, 16); 

	/* Primary Device Type */
	os_memcpy(msg+*msg_len, primDevType, 12);
	*msg_len += 12;
  
	/* Device Name */
	add_tlv_data(msg, msg_len, 0x11, (unsigned char *)regInfo->p_enrolleeInfo->deviceName, 16);

	/* RF Band */
	add_tlv_8bit_value(msg, msg_len, 0x3c, 0x01);

	/* Assoc State */
	add_tlv_16bit_value(msg, msg_len, 0x02, 0x00);

	/* Device Pwd ID */
	if(config->ssid->use_wps == 1) {
		wpa_printf(MSG_DEBUG, "PBC Device Password ID will be used.\n");
		add_tlv_16bit_value(msg, msg_len, 0x12, 0x04);
	} else {
		wpa_printf(MSG_DEBUG, "PIN Device Password ID will be used.\n");
		add_tlv_16bit_value(msg, msg_len, 0x12, 0x00);
	}
  
	/* Config Error */
	add_tlv_16bit_value(msg, msg_len, 0x09, 0x00);
	
	/* OS Version */
	os_memcpy(msg+*msg_len, osVersion, 8);
	*msg_len += 8;

	return WSC_SUCCESS;
}

#define FIND_AND_MEMCPY_OR_EXIT(_id, _buf, _size) \
	s = find_and_memcpy(msg, msg_len, _id, _buf, _size); \
        if(s!=0) { \
           return WSC_ERR_SYSTEM; \
        }

static uint32 ProcessMessageM2D(S_REGISTRATION_DATA *regInfo, uint8 *msg, uint32 msg_len)
{
//	uint8       version, 
   uint8       msgType;
	uint8       enrolleeNonce[SIZE_128_BITS];
	uint8       registrarNonce[SIZE_128_BITS];
	int          s;

	wpa_printf(MSG_DEBUG, "ProcessMessageM2D: %d byte message\n", msg_len);
	
	/* First, deserialize (parse) the message. */
//	version = msg[4];
	msgType = msg[9];

	/*
	 *  First and foremost, check the version and message number.
	 *  Don't deserialize incompatible messages!
	 */
	if(WSC_ID_MESSAGE_M2D != msgType)
		return WSC_ERR_SYSTEM;

	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_ENROLLEE_NONCE, 
	      enrolleeNonce, SIZE_128_BITS);

	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_REGISTRAR_NONCE, 
	      registrarNonce, SIZE_128_BITS);
	
	/*
	 *   Now start processing the message
	 */

	/* confirm the enrollee nonce */
	if(os_memcmp((uint8*)regInfo->enrolleeNonce, enrolleeNonce, SIZE_128_BITS)) {
		wpa_printf(MSG_INFO,"ProcessMessageM2: Incorrect enrollee nonce\n");
		return WSC_ERR_SYSTEM;
	}
	
	/*
	 *  to verify the hmac, we need to process the nonces, generate
	 *  the DH secret, the KDK and finally the auth key
	 */
	os_memcpy(regInfo->registrarNonce, registrarNonce, SIZE_128_BITS);
	
	/* Store the received buffer */
	os_memcpy(regInfo->inMsg, msg, msg_len);
	regInfo->inMsg_len = msg_len;

	return WSC_SUCCESS;
}

static uint32 ProcessMessageM2(S_REGISTRATION_DATA *regInfo, uint8 *msg, uint32 msg_len)
{
//	uint8       version, 
   uint8       msgType;
	uint8       enrolleeNonce[SIZE_128_BITS];
	uint8       registrarNonce[SIZE_128_BITS];
	uint8       authenticator[SIZE_64_BITS];
	int         ret = 0;
	uint8       *secret;
	int         secretLen = SIZE_PUB_KEY;
	uint8       DHKey[SIZE_256_BITS];
	const u8    *addr[1];
	size_t      vlen[1];
	uint8       *kdkData;
	uint8       *kdk;
	uint8       pString[36]; /*= PERSONALIZATION_STRING;*/
	uint32      hmacDataLen;
	uint8       *hmacData;
	uint8       *keys;
	int          s;

	wpa_printf(MSG_DEBUG, "ProcessMessageM2: %d byte message\n", msg_len);
	
	/* First, deserialize (parse) the message. */
//	version = msg[4];
	msgType = msg[9];

	/*
	 *  First and foremost, check the version and message number.
	 *  Don't deserialize incompatible messages!
	 */
	if(WSC_ID_MESSAGE_M2 != msgType)
		return WSC_ERR_SYSTEM;

	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_ENROLLEE_NONCE, 
	      enrolleeNonce, SIZE_128_BITS); 

	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_REGISTRAR_NONCE, 
	      registrarNonce, SIZE_128_BITS);
	
	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_AUTHENTICATOR, 
	      authenticator, SIZE_64_BITS);
	
	/*
	 *   Now start processing the message
	 */

	/* confirm the enrollee nonce */
	if(os_memcmp((uint8*)regInfo->enrolleeNonce, enrolleeNonce, SIZE_128_BITS)) {
		wpa_printf(MSG_INFO,"ProcessMessageM2: Incorrect enrollee nonce\n");
		return WSC_ERR_SYSTEM;
	}
	
	/*
	 *  to verify the hmac, we need to process the nonces, generate
	 *  the DH secret, the KDK and finally the auth key
	 */
	os_memcpy(regInfo->registrarNonce, registrarNonce, SIZE_128_BITS);

	/*
	 *  read the registrar's public key
	 *  First store the raw public key (to be used for e/rhash computation)
	 */
	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_PUBLIC_KEY, 
              (uint8*)regInfo->pkr, SIZE_PUB_KEY);

	if( ( ret = mpi_read_binary( &dhm.GY, (uint8*)regInfo->pkr, SIZE_PUB_KEY ) ) != 0 ) {
		wpa_printf(MSG_INFO,"ProcessMessageM2: failed to load GY (peer key)\n");
		return WSC_ERR_SYSTEM;
	}

	/****** KDK generation *******/
	/* 1. generate the DH shared secret */
	secret = (uint8 *)os_malloc(SIZE_PUB_KEY);
	if(secret == NULL)
		return WSC_ERR_SYSTEM;
	if( (ret = dhm_calc_secret(&dhm, secret, &secretLen )) != 0) {
		wpa_printf(MSG_INFO, "ProcessMessageM2: failed to calculate secret key!(%d)\n", ret);
		os_free(secret);
		return WSC_ERR_SYSTEM;
	}

	/* 2. compute the DHKey based on the DH secret */
	addr[0] = secret;
	vlen[0] = secretLen;
	sha256_vector(1, addr, vlen, DHKey);
	os_free(secret);

	/* 3. Append the enrollee nonce(N1), enrollee mac and registrar nonce(N2) */
	kdkData = (uint8 *)os_malloc(2*SIZE_128_BITS + SIZE_MAC_ADDR);
	if(kdkData == NULL)
		return WSC_ERR_SYSTEM;
	os_memcpy(kdkData, (uint8*)regInfo->enrolleeNonce, SIZE_128_BITS);
	os_memcpy(kdkData+SIZE_128_BITS, (uint8*)regInfo->p_enrolleeInfo->macAddr, SIZE_MAC_ADDR);
	os_memcpy(kdkData+SIZE_MAC_ADDR+SIZE_128_BITS, (uint8*)regInfo->registrarNonce, SIZE_128_BITS);

	/* 4. now generate the KDK */
	kdk = (uint8 *)os_malloc(SIZE_256_BITS);
	if(kdk == NULL) {
		os_free(kdkData);
		return WSC_ERR_SYSTEM;
	}
	hmac_sha256(DHKey, SIZE_256_BITS, kdkData, 2*SIZE_128_BITS+SIZE_MAC_ADDR, kdk);
	os_free(kdkData);

	/****** KDK generation *******/
	
	/****** Derivation of AuthKey, KeyWrapKey and EMSK ******/
	/* 1. declare and initialize the appropriate buffers */
	os_memcpy(pString, (uint8 *)PERSONALIZATION_STRING, strlen(PERSONALIZATION_STRING));
	keys = (uint8 *)os_malloc(KDF_KEY_BITS/8);
	if(keys == NULL) {
		os_free(kdk);
		return WSC_ERR_SYSTEM;
	}
	/* 2. call the key derivation function */
	DeriveKey(kdk, pString, KDF_KEY_BITS, keys);
	os_free(kdk);

	/* 3. split the key into the component keys and store them */
	os_memcpy(regInfo->authKey, keys, SIZE_256_BITS);
	os_memcpy(regInfo->keyWrapKey, keys+SIZE_256_BITS, SIZE_128_BITS);
	os_memcpy(regInfo->emsk, keys+SIZE_256_BITS+SIZE_128_BITS, SIZE_256_BITS);
	os_free(keys);

	/****** Derivation of AuthKey, KeyWrapKey and EMSK ******/
	
	/****** HMAC validation ******/
	hmacDataLen = regInfo->outMsg_len + msg_len - 12;/*(sizeof(S_WSC_TLV_HEADER)+SIZE_64_BITS);*/
	
	hmacData = (uint8 *)os_malloc(hmacDataLen);
	if(hmacData == NULL)
		return WSC_ERR_SYSTEM;
	/* append the last message sent */
	os_memcpy(hmacData, (uint8*)regInfo->outMsg, regInfo->outMsg_len);

	/* append the current message. Don't append the last TLV (auth) */
	os_memcpy(hmacData+regInfo->outMsg_len, msg, msg_len-12);/* (sizeof(S_WSC_TLV_HEADER)+SIZE_64_BITS)); */


	if(!ValidateMac(hmacData, hmacDataLen, authenticator, regInfo->authKey)) {
		wpa_printf(MSG_INFO, "ProcessMessageM2: HMAC validation failed");
		os_free(hmacData);
		return WSC_ERR_SYSTEM;
	}
	os_free(hmacData);
	/****** HMAC validation ******/

	/* Store the received buffer */
	os_memcpy(regInfo->inMsg, msg, msg_len);
	regInfo->inMsg_len = msg_len;

	return WSC_SUCCESS;
}

static uint32 BuildMessageM3(S_REGISTRATION_DATA *regInfo)
{
//	uint8 message;
	uint8 hashBuf[SIZE_256_BITS];
	int pwdLen;
	uint8 *pwdPtr;
	uint8 *ehashBuf;
	int   ehashBuf_len = 2*SIZE_128_BITS+2*SIZE_PUB_KEY;
	uint8 *hmacData;
	uint8 hmac[SIZE_256_BITS];
	uint8  *msg = regInfo->outMsg;
	uint32 *msg_len = &regInfo->outMsg_len;

	ehashBuf = (uint8*)os_malloc(ehashBuf_len);
	if(ehashBuf==NULL)
		return WSC_ERR_SYSTEM;

	/* First, generate or gather the required data */
//	message = WSC_ID_MESSAGE_M3;

	/****** PSK1 and PSK2 generation ******/
	pwdPtr = regInfo->password;
	pwdLen = regInfo->password_len;

	/*
	 *  Hash 1st half of passwd. If it is an odd length, the extra byte
	 *  goes along with the first half
	 */
	hmac_sha256(regInfo->authKey, SIZE_256_BITS, pwdPtr, (pwdLen/2)+(pwdLen%2), hashBuf);

	/* copy first 128 bits into psk1; */
	os_memcpy((uint8*)regInfo->psk1, hashBuf, SIZE_128_BITS);
	
	/* Hash 2nd half of passwd */
	hmac_sha256((uint8*)regInfo->authKey, SIZE_256_BITS, pwdPtr+(pwdLen/2)+(pwdLen%2), pwdLen/2, hashBuf);

	/* copy first 128 bits into psk2; */
	os_memcpy((uint8*)regInfo->psk2, hashBuf, SIZE_128_BITS);
	/****** PSK1 and PSK2 generation ******/
	
	/****** EHash1 and EHash2 generation ******/
	os_get_random((uint8*)regInfo->es1, SIZE_128_BITS);
	os_get_random((uint8*)regInfo->es2, SIZE_128_BITS);
	
	os_memcpy(ehashBuf, (uint8*)regInfo->es1, SIZE_128_BITS);
	os_memcpy(ehashBuf+SIZE_128_BITS, (uint8*)regInfo->psk1, SIZE_128_BITS);
	os_memcpy(ehashBuf+2*SIZE_128_BITS, (uint8*)regInfo->pke, SIZE_PUB_KEY);
	os_memcpy(ehashBuf+2*SIZE_128_BITS+SIZE_PUB_KEY, (uint8*)regInfo->pkr, SIZE_PUB_KEY);
	
	hmac_sha256((uint8*)regInfo->authKey, SIZE_256_BITS, ehashBuf, 2*SIZE_128_BITS+2*SIZE_PUB_KEY, hashBuf);

	os_memcpy((uint8*)regInfo->eHash1, hashBuf, SIZE_256_BITS);

	os_memcpy(ehashBuf, (uint8*)regInfo->es2, SIZE_128_BITS);
	os_memcpy(ehashBuf+SIZE_128_BITS, (uint8*)regInfo->psk2, SIZE_128_BITS);
	os_memcpy(ehashBuf+2*SIZE_128_BITS, (uint8*)regInfo->pke, SIZE_PUB_KEY);
	os_memcpy(ehashBuf+2*SIZE_128_BITS+SIZE_PUB_KEY, (uint8*)regInfo->pkr, SIZE_PUB_KEY);
  
	hmac_sha256((uint8*)regInfo->authKey, SIZE_256_BITS, ehashBuf, 2*SIZE_128_BITS+2*SIZE_PUB_KEY, hashBuf);

	os_free(ehashBuf);

	os_memcpy((uint8*)regInfo->eHash2, hashBuf, SIZE_256_BITS);
	/****** EHash1 and EHash2 generation ******/

	/*
	 *   Now assemble the message
	 */
	*msg_len = 0;

	/* Version */
        add_tlv_8bit_value(msg, msg_len, 0x4a, WSC_VERSION);

        /* Message */
        add_tlv_8bit_value(msg, msg_len, 0x22, WSC_ID_MESSAGE_M3);
   
	/* Registrar Nonce */
	add_tlv_data(msg, msg_len, 0x39, regInfo->registrarNonce, SIZE_128_BITS);

	/* Hash 1 */
	add_tlv_data(msg, msg_len, 0x14, regInfo->eHash1, SIZE_256_BITS);

	/* Hash 2 */
	add_tlv_data(msg, msg_len, 0x15, regInfo->eHash2, SIZE_256_BITS);

	/* No vendor extension */

	/* Calculate the hmac */
	hmacData = (uint8 *)os_malloc(regInfo->inMsg_len+*msg_len);
	if(hmacData == NULL)
		return WSC_ERR_SYSTEM;
	os_memcpy(hmacData, (uint8*)regInfo->inMsg, regInfo->inMsg_len);
	os_memcpy(hmacData+regInfo->inMsg_len, msg, *msg_len);

	hmac_sha256((uint8*)regInfo->authKey, SIZE_256_BITS, hmacData, regInfo->inMsg_len+*msg_len, hmac);
	os_free(hmacData);

	/* Authenticator */
	*(uint16*)(msg+*msg_len) = bswap_16(WSC_ID_AUTHENTICATOR);
	*msg_len += 2;
	*(uint16*)(msg+*msg_len) = bswap_16(SIZE_64_BITS);
	*msg_len +=2;
	os_memcpy(msg+*msg_len, hmac, SIZE_64_BITS);
	*msg_len += SIZE_64_BITS;

	wpa_printf(MSG_DEBUG, "BuildMessageM3: %d bytes\n", *msg_len);
  
	return WSC_SUCCESS;
}

/**
 *
 * M4 Encrypted Settings (page 57)
 * 1. R-SNonce1
 * 2. <other...> Multiple attributes are permitted .
 * 3. Key Wrap Authenticator
 *
 * Ex.
 *
 * WPS: Decrypted Settings - hexdump(len=48):   
 * 10 3f 00 10 61 66 2b 49 49 f8 b0 e2 70 57 78 02 7a e7 ad fe // R-SNonce1 4+16 bytes
 * 10 1e 00 08 32 48 6b 1c 34 b0 e0 41                         // Key Wrap Authenticator 4+8 bytes
 * 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10             // 16 bytes padding to fill block (0x10 = 16)
 *
 */
static uint32 ProcessMessageM4(S_REGISTRATION_DATA *regInfo, uint8 *msg, uint32 msg_len)
{
//	uint8       version;
   uint8       msgType;
	uint8       enrolleeNonce[SIZE_128_BITS];
	uint8       rHash1[SIZE_256_BITS];
	uint8       rHash2[SIZE_256_BITS];
	uint8       authenticator[SIZE_64_BITS];
	uint8       *iv;
	uint32      hmacDataLen;
	uint8       *hmacData;
	uint8       cipherText[64];
	uint8       plainText[sizeof(cipherText)];
	uint32      plainTextLen = 0;
	uint8       rNonce[16];
	uint8       keyWrapAuth[8];
	uint8       dataMac[BUF_SIZE_256_BITS];
	uint8       enc_offset = 20;
	uint8       *rhashBuf;
	uint8       *hashBuf;
	int         s;
	uint8* decrypted = NULL;

	wpa_printf(MSG_DEBUG, "ProcessMessageM4: %d byte message\n",msg_len);

	/* First, deserialize (parse) the message. */
//	version = msg[4];
	msgType = msg[9];

	/*
	 *   First and foremost, check the version and message number.
	 *   Don't deserialize incompatible messages!
	 */
	if(WSC_ID_MESSAGE_M4 != msgType)
		return WSC_ERR_SYSTEM;

	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_ENROLLEE_NONCE, 
	      enrolleeNonce, SIZE_128_BITS);

	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_R_HASH1,
	      rHash1, SIZE_256_BITS);
	
	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_R_HASH2, 
	      rHash2, SIZE_256_BITS);

	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_AUTHENTICATOR,
	      authenticator, SIZE_64_BITS);

	/* is the cipher text always of a fix size? */
	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_ENCR_SETTINGS,
	      cipherText, SIZE_128_BITS+48);

	/* the first 16 bytes are the iv */
	iv = &cipherText[0];

	/*
	 *  Now start validating the message
	 */

	/* confirm the enrollee nonce */
	if(os_memcmp((uint8*)regInfo->enrolleeNonce,
		  enrolleeNonce,
		  SIZE_128_BITS)) {
		wpa_printf(MSG_INFO, "ProcessMessageM4: Incorrect enrollee nonce\n");
		return WSC_ERR_SYSTEM;
	}


	/****** HMAC validation ******/
	hmacDataLen = regInfo->outMsg_len+msg_len-12;/*(sizeof(S_WSC_TLV_HEADER)+SIZE_64_BITS);*/

	hmacData = (uint8 *)os_malloc(hmacDataLen);
	if(hmacData == NULL)
		return WSC_ERR_SYSTEM;
	/* append the last message sent */
	os_memcpy(hmacData, (uint8*)regInfo->outMsg, regInfo->outMsg_len);
	/* append the current message. Don't append the last TLV (auth) */
	os_memcpy(hmacData+regInfo->outMsg_len, msg, msg_len-12);/*(sizeof(S_WSC_TLV_HEADER)+SIZE_64_BITS));*/

	if(!ValidateMac(hmacData, hmacDataLen, authenticator, (uint8*)regInfo->authKey)) {
		wpa_printf(MSG_INFO, "ProcessMessageM4: HMAC validation failed\n");
		os_free(hmacData);
		return WSC_ERR_SYSTEM;
	}
	os_free(hmacData);
	/****** HMAC validation ******/

	/* Now copy the relevant data */
	os_memcpy((uint8*)regInfo->rHash1, rHash1, SIZE_256_BITS);
	os_memcpy((uint8*)regInfo->rHash2, rHash2, SIZE_256_BITS);

	/****** extract encrypted settings ******/
	decrypted = wps_decrypt(cipherText,
		64,/*cipherText length*/
		iv,
		(uint8*)regInfo->keyWrapKey,
		&plainTextLen);
	if(decrypted == NULL || plainTextLen > sizeof(plainText))
	{
		wpa_printf(MSG_ERROR, "failed to decrypt M4 %d > %d\n", plainTextLen, sizeof(plainText));
		if(decrypted != NULL) os_free(decrypted);
		return WSC_ERR_SYSTEM;
	}
	os_memcpy(plainText, decrypted, plainTextLen);
	os_free(decrypted);
   
	/*
	 *  parse the plainText
	 */
	os_memcpy(rNonce, (plainText+4), 16);
	os_memcpy(keyWrapAuth, (plainText+24), 8);

	/* validate the mac */

	/* calculate the hmac of the data (data only, not the last auth TLV) */
	hmac_sha256(regInfo->authKey, SIZE_256_BITS, plainText, enc_offset, dataMac);

	/* compare it against the received hmac */
	if(os_memcmp(dataMac, keyWrapAuth, SIZE_64_BITS) != 0) {
		wpa_printf(MSG_INFO, "ProcessMessageM4: HMAC results don't match\n");
		return WSC_ERR_SYSTEM;
	}
	/****** extract encrypted settings ******/
	
	/****** RHash1 validation ******/
	/* 1. Save RS1 */
	os_memcpy((uint8*)regInfo->rs1, rNonce, 16);
	
	/* 2. prepare the buffer */
	rhashBuf = (uint8 *)os_malloc(2*SIZE_128_BITS+2*SIZE_PUB_KEY);
	if(rhashBuf == NULL)
		return WSC_ERR_SYSTEM;
	os_memcpy(rhashBuf, (uint8*)regInfo->rs1, SIZE_128_BITS);
	os_memcpy(rhashBuf+SIZE_128_BITS, (uint8*)regInfo->psk1, SIZE_128_BITS);  
	os_memcpy(rhashBuf+2*SIZE_128_BITS, (uint8*)regInfo->pke, SIZE_PUB_KEY);
	os_memcpy(rhashBuf+2*SIZE_128_BITS+SIZE_PUB_KEY, (uint8*)regInfo->pkr, SIZE_PUB_KEY);

	/* 3. generate the mac */
	hashBuf = (uint8 *)os_malloc(SIZE_256_BITS);
	if(hashBuf == NULL)
	{
		os_free(rhashBuf);
		return WSC_ERR_SYSTEM;
	}
	hmac_sha256((uint8*)regInfo->authKey, SIZE_256_BITS, rhashBuf, 2*SIZE_128_BITS+2*SIZE_PUB_KEY, hashBuf);
	os_free(rhashBuf);
	
	/* 4. compare the mac to rhash1 */
	if(os_memcmp((uint8*)regInfo->rHash1, hashBuf, SIZE_256_BITS)) {
		wpa_printf(MSG_INFO, "ProcessMessageM4: RS1 hash doesn't match RHash1\n");
		os_free(hashBuf);
		return WSC_ERR_SYSTEM;
	}
	os_free(hashBuf);
	/* 5. Instead of steps 3 & 4, we could have called ValidateMac */
	/****** RHash1 validation ******/
	
	/* Store the received buffer */
	regInfo->inMsg_len = msg_len;
	os_memcpy(regInfo->inMsg, msg, msg_len);

	return WSC_SUCCESS;
}

static uint32 BuildMessageM5(S_REGISTRATION_DATA *regInfo)
{
//	uint8 message;
	uint32 cipherTextLen = 0;
	uint8 TempEncData[20];
	uint8 esNonceHdr[4] = {0x10, 0x16, 0x00, 0x10};
	uint8 hmac[SIZE_256_BITS];
	uint8 encData[48];
	uint8 authenticatorHdr[4] = {0x10, 0x1E, 0x00, 0x08};
	uint8 cipherText[48], iv[16];
	uint8 encrSettings[256];
	uint32 hmacDataLen;
	uint8 *hmacData;
	uint8 hmac1[SIZE_256_BITS];
	uint8  *msg = regInfo->outMsg;
	uint32 *msg_len = &regInfo->outMsg_len;
	uint8* encrypted = NULL;
	
	/* First, generate or gather the required data*/
//	message = WSC_ID_MESSAGE_M5;

	/* encrypted settings. */
	os_memcpy(TempEncData, esNonceHdr, 4);
	os_memcpy(TempEncData+4, (uint8*)regInfo->es1, SIZE_128_BITS);

	/* calculate the hmac and append the TLV to the buffer */
	hmac_sha256((uint8*)regInfo->authKey, SIZE_256_BITS, TempEncData, 20, hmac);
	
	os_memcpy(encData, TempEncData, 20);
	os_memcpy(encData+20, authenticatorHdr, 4);
	os_memcpy(encData+20+4, hmac, 8);

	encrypted = wps_encrypt(
		encData,
		32,
		iv,
		(uint8*)regInfo->keyWrapKey,
		&cipherTextLen);
	if(encrypted == NULL || cipherTextLen > sizeof(cipherText))
	{
		wpa_printf(MSG_ERROR, "failed to encrypt M5 %d > %d\n", cipherTextLen, sizeof(cipherText));
		if(encrypted != NULL) os_free(encrypted);
		return WSC_ERR_SYSTEM;
	}
	os_memcpy(cipherText, encrypted, cipherTextLen);
	os_free(encrypted);

	/*
	 *  Now assemble the message
	 */

	*msg_len = 0;

	/* Version */
        add_tlv_8bit_value(msg, msg_len, 0x4a, WSC_VERSION);

        /* Message */
        add_tlv_8bit_value(msg, msg_len, 0x22, WSC_ID_MESSAGE_M5);

	/* Registrar Nonce */
	add_tlv_data(msg, msg_len, 0x39, regInfo->registrarNonce, SIZE_128_BITS);

	os_memcpy(encrSettings, iv, 16);
	os_memcpy(encrSettings + 16, cipherText, cipherTextLen);

	/* Encryption Settings */
	add_tlv_data(msg, msg_len, 0x18, encrSettings, SIZE_128_BITS+cipherTextLen);

	/* No vendor extension */

	/* Calculate the hmac */
	hmacDataLen = regInfo->inMsg_len + *msg_len;

	hmacData = (uint8 *)os_malloc(hmacDataLen);
	if(hmacData == NULL)
		return WSC_ERR_SYSTEM;
	os_memcpy(hmacData, (uint8*)regInfo->inMsg, regInfo->inMsg_len);
	os_memcpy(hmacData+regInfo->inMsg_len, msg, *msg_len);
  
	hmac_sha256((uint8*)regInfo->authKey, SIZE_256_BITS, hmacData, hmacDataLen, hmac1);
	os_free(hmacData);

	/* Authenticator */
	add_tlv_data(msg, msg_len, 0x05, hmac1, SIZE_64_BITS);

	wpa_printf(MSG_DEBUG, "BuildMessageM5: %d bytes\n", *msg_len);
  
	return WSC_SUCCESS;
}

static uint32 ProcessMessageM6(S_REGISTRATION_DATA *regInfo, uint8 *msg, uint32 msg_len)
{
//	uint8 version;
	uint8 msgType;
	uint8 enrolleeNonce[SIZE_128_BITS];
	uint8 *iv;
	uint8 authenticator[SIZE_64_BITS];
	uint8 *hmacData;
	uint8 cipherText[64];
	uint32 hmacDataLen;
	//uint8 plainText[sizeof(cipherText) + 16];
	//uint32 plainTextLen = 0;
	int   s;

	wpa_printf(MSG_DEBUG, "ProcessMessageM6: %d byte message\n",msg_len);	

	if(WSC_IN_MSG_LEN < msg_len)
		return WSC_ERR_SYSTEM;

	if(WSC_IN_MSG_LEN < regInfo->outMsg_len)
		return WSC_ERR_SYSTEM;

	/* First, deserialize (parse) the message. */
//	version = msg[4];
	msgType = msg[9];
	
	/* 
	 *  First and foremost, check the version and message number.
	 *  Don't deserialize incompatible messages!
	 */
	if(WSC_ID_MESSAGE_M6 != msgType)
		return WSC_ERR_SYSTEM;
  
	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_ENROLLEE_NONCE,
	      enrolleeNonce, SIZE_128_BITS);

	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_AUTHENTICATOR,
	      authenticator, SIZE_64_BITS);

	/* is the cipher text always of a fix size? */
	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_ENCR_SETTINGS,
	      cipherText, SIZE_128_BITS+48);

	/* the first 16 bytes are the iv */
	iv = &cipherText[0];

	/* 
	 *  Now start validating the message
	 */

	/* confirm the enrollee nonce */
	if(os_memcmp((uint8*)regInfo->enrolleeNonce,
		  enrolleeNonce,
		  SIZE_128_BITS))
	{
		wpa_printf(MSG_INFO, "ProcessMessageM6: Incorrect enrollee nonce\n");
		return WSC_ERR_SYSTEM;
	}
	
	/****** HMAC validation ******/
	hmacDataLen = regInfo->outMsg_len+msg_len-12;/*(sizeof(S_WSC_TLV_HEADER)+SIZE_64_BITS);*/

	hmacData = (uint8 *)os_malloc(hmacDataLen);
	if(hmacData == NULL)
		return WSC_ERR_SYSTEM;
	/* append the last message sent */
	os_memcpy(hmacData, (uint8*)regInfo->outMsg, regInfo->outMsg_len);
	/* append the current message. Don't append the last TLV (auth) */
	os_memcpy(hmacData+regInfo->outMsg_len, msg, msg_len-12);//(sizeof(S_WSC_TLV_HEADER)+SIZE_64_BITS));
	
	if(!ValidateMac(
	             hmacData,
	             hmacDataLen,
	             authenticator,
	             (uint8*)regInfo->authKey))
	{
		wpa_printf(MSG_INFO, "ProcessMessageM6: HMAC validation failed\n");
		os_free(hmacData);
		return WSC_ERR_SYSTEM;
	}
	os_free(hmacData);

#if 0
	/* regInfo->rs2 is never used so skip this step */
	wps_decrypt(cipherText, 
		    sizeof(cipherText),
		    iv,
		    regInfo->keyWrapKey,
		    plainText,
		    &plainTextLen);
	DE_ASSERT(plainTextLen <= sizeof(plainText));

	/* rNonce */
	os_memcpy(regInfo->rs2,
              &plainText[16+4],
              sizeof(regInfo->rs2));
#endif

	os_memcpy(regInfo->inMsg,
              msg,
              msg_len);

	regInfo->inMsg_len = msg_len;

	return WSC_SUCCESS;
}

/*
 *
 * M7
 * 1. E-SNonce2
 * 2. Identity Proof
 * 3. <other...> Multiple attributes are permitted
 * 4. Key Wrap Authenticator
 *
 * AP settings in M7
 *
 * 1. E-SNonce2
 * 2. SSID
 * 3. MAC Address - AP’s BSSID
 * 4. Authentication Type
 * 5. Encryption Type
 * 6. Network Key Index - If omitted, the Network Key Index defaults to 1.
 * 7. Network Key - Multiple instances of Network Key and its preceding Network Key Index may be included.
 * 8. <other...> Multiple attributes are permitted 
 * 9. Key Wrap Authenticator
 *
 */
static uint32 BuildMessageM7(S_REGISTRATION_DATA *regInfo)
{
//	uint8    message;
	uint32   cipherTextLen = 0;
	uint8    hmac[SIZE_256_BITS];
	uint8    esBuf[48];
	uint8    TempEsBuf[20];
	uint8    esNonce2Hdr[4] = {0x10, 0x17, 0x00, 0x10};
	uint8    authenticatorHdr[4] = {0x10, 0x1E, 0x00, 0x08};
	uint32   hmacDataLen;
	uint8    *hmacData;
	uint8    cipherText[48], iv[16];
	uint8    *encrSettings;
	int      encrSettingsLen = 256;
	uint8    hmac1[SIZE_256_BITS];
	uint8    *msg = regInfo->outMsg;
	uint32   *msg_len = &regInfo->outMsg_len;
	uint8* encrypted = NULL;

	encrSettings = os_malloc(encrSettingsLen);
	if(encrSettings==NULL)
		return WSC_ERR_SYSTEM;
	
	/* First, generate or gather the required data */
//	message = WSC_ID_MESSAGE_M7;
  
	/* encrypted settings. */
	os_memcpy(TempEsBuf, esNonce2Hdr, 4);
	os_memcpy(TempEsBuf+4, regInfo->es2, SIZE_128_BITS);
	
	/* calculate the hmac and append the TLV to the buffer */
	hmac_sha256(regInfo->authKey, SIZE_256_BITS, TempEsBuf, 20, hmac);

	os_memcpy(esBuf, TempEsBuf, 20);
	os_memcpy(esBuf+20, authenticatorHdr, 4);
	os_memcpy(esBuf+20+4, hmac, 8);

	/*regInfo->staEncrSettings = (void *)esBuf; */
  
	encrypted = wps_encrypt(
		esBuf,
		32,
		iv,
		(uint8*)regInfo->keyWrapKey,
		&cipherTextLen);
	if(encrypted == NULL || cipherTextLen > sizeof(cipherText))
	{
		wpa_printf(MSG_ERROR, "failed to encrypt M7 %d > %d\n", cipherTextLen, sizeof(cipherText));
		if(encrypted != NULL) os_free(encrypted);
		return WSC_ERR_SYSTEM;
	}
	os_memcpy(cipherText, encrypted, cipherTextLen);
	os_free(encrypted);
	
	/*
	 *  Now assemble the message
	 */

	*msg_len = 0;

	/* Version */
        add_tlv_8bit_value(msg, msg_len, 0x4a, WSC_VERSION);

        /*  Message */
        add_tlv_8bit_value(msg, msg_len, 0x22, WSC_ID_MESSAGE_M7);

	/* Registrar Nonce */
	add_tlv_data(msg, msg_len, 0x39, regInfo->registrarNonce, SIZE_128_BITS);

	os_memcpy(encrSettings, iv, 16);
	os_memcpy(encrSettings + 16, cipherText, cipherTextLen);
  
	/* Encryption Settings */
	add_tlv_data(msg, msg_len, 0x18, encrSettings, SIZE_128_BITS+cipherTextLen);

	os_free(encrSettings);

	/* No vendor extension */
  
	/* Calculate the hmac */
	hmacDataLen = regInfo->inMsg_len + (*msg_len);
	
	hmacData = (uint8 *)os_malloc(hmacDataLen);
	if(hmacData == NULL)
		return WSC_ERR_SYSTEM;
	os_memcpy(hmacData, (uint8*)regInfo->inMsg, regInfo->inMsg_len);
	os_memcpy(hmacData+regInfo->inMsg_len, msg, *msg_len);
  
	hmac_sha256((uint8*)regInfo->authKey, SIZE_256_BITS, hmacData, hmacDataLen, hmac1);
	os_free(hmacData);
       
	/* Authenticator */
	add_tlv_data(msg, msg_len, 0x05, hmac1, SIZE_64_BITS);

	wpa_printf(MSG_DEBUG, "BuildMessageM7: %d bytes\n", *msg_len);
  
	return WSC_SUCCESS;
}

/**
 *
 * Attributes in Encrypted Settings of M2, M8 if Enrollee is AP (page 59)
 *
 * Network Index - This attribute is only used if the Enrollee is an AP and
 *                 the Registrar wants to configure settings for a 
 *                 non-default network interface. If omitted, the Network 
 *                 Index defaults to 1.
 * SSID
 * Authentication Type
 * Encryption Type 
 * Network Key Index - If omitted, the Network Key Index defaults to 1
 * Network Key -    Multiple instances of Network Key and its preceding Network
 *                  Key Index may be included.
 * MAC Address
 * New Password
 * Device Password ID - Required if New Password is included
 * <other...> -    Multiple attributes are permitted
 * Key Wrap Authenticator
 *
 *
 * Ex: Decrypted data from M8:  - hexdump_ascii(len=132):
 *
 * 10 0e 00 74          // Credential: 0x74 = 116
 *    10 26 00 01 01    // Network Index
 *    10 45 00 20       // SSID: 0x20 = 32
 *       57 50 53 4d 41 58 53 53 49 44 30 31 32 33 34 35 
 *       36 37 38 39 30 31 32 33 34 35 36 37 38 39 30 31 // WPSMAXSSID0123456789012345678901
 *    10 03 00 02 00 20 // Authentication Type
 *    10 0f 00 02 00 08 // Encryption Type 
 *    10 28 00 01 01    // Network Key Index
 *    10 27 00 20       // Network Key
 *       57 50 53 4d 41 58 50 53 4b 30 31 32 33 34 35 36 
 *       37 38 39 30 31 32 33 34 35 36 37 38 39 30 31 32 // WPSMAXPSK01234567890123456789012
 *    10 20 00 06 8a ba 5e 5d 59 fd       // MAC Address
 *    10 1e 00 08 32 0c 52 f9 bd 23 17 f5 // Key Wrap Authenticator
 *    10 1e 00 08 b0 75 ac d1 f2 73 9b 6f // Key Wrap Authenticator
 */
static uint32 ProcessMessageM8(S_REGISTRATION_DATA *regInfo, uint8 *msg, uint32 msg_len, uint8 *encrSettings, uint32 *encrSettingsLen)
{
//	uint8       version;
   uint8       msgType;
	uint8       enrolleeNonce[SIZE_128_BITS];
	uint8       *iv;
	uint8       authenticator[SIZE_64_BITS];
	uint32      hmacDataLen;
	uint8       *hmacData;
#define WSC_ENC_SETTINGS_SIZE 384
	uint8       cipherText[WSC_ENC_SETTINGS_SIZE];
	uint32      cipherTextLen;
	uint8       plainText[sizeof(cipherText)];
	uint32      plainTextLen = 0;
	unsigned char *tlv_data = NULL;
	m80211_tlv_t tlv;
	int s;
	uint8 multiple_creds = 0;
	uint8 first_cred_len;
	uint8* decrypted = NULL;

	wpa_printf(MSG_DEBUG, "ProcessMessageM8: %d byte message\n",msg_len);
	
	/* First, deserialize (parse) the message. */
//	version = msg[4];
	msgType = msg[9];
  
	/* First and foremost, check the version and message number. */
	/* Don't deserialize incompatible messages! */
	if(WSC_ID_MESSAGE_M8 != msgType)
		return WSC_ERR_SYSTEM;

	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_ENROLLEE_NONCE,
	      enrolleeNonce, SIZE_128_BITS);

	FIND_AND_MEMCPY_OR_EXIT(WSC_ID_AUTHENTICATOR,
	      authenticator, SIZE_64_BITS); 
	
	s = m80211_tlv_find(msg, msg_len, WSC_ID_ENCR_SETTINGS, &tlv, (void**)&tlv_data); 
	if( s == FALSE || tlv.len > sizeof(cipherText) || tlv.len < SIZE_128_BITS)
	{
		/* none found or incorrect len */ 
		wpa_printf(MSG_ERROR,"err parsing %04x: %d %d\n", 
				WSC_ID_ENCR_SETTINGS, s, tlv.len); 
		wpa_hexdump_ascii(MSG_ERROR, "tlv_buf: ", msg, msg_len); 
		return WSC_ERR_SYSTEM;
	}

	cipherTextLen = tlv.len;
	os_memcpy(cipherText, tlv_data, cipherTextLen);
	/* the first 16 bytes are the iv */
	iv = &cipherText[0];

	/* 
	 * Now start validating the message
	 */
	
	/* confirm the enrollee nonce */
	if(os_memcmp((uint8*)regInfo->enrolleeNonce,
		  enrolleeNonce,
		  SIZE_128_BITS)) {
		wpa_printf(MSG_INFO, "ProcessMessageM8: Incorrect enrollee nonce\n");
		return WSC_ERR_SYSTEM;
	}

	/****** HMAC validation ******/
	hmacDataLen = regInfo->outMsg_len+msg_len-12;/*(sizeof(S_WSC_TLV_HEADER)+SIZE_64_BITS); */

	hmacData = (uint8 *)os_malloc(hmacDataLen);
	if(hmacData == NULL)
		return WSC_ERR_SYSTEM;
	/* append the last message sent */
	os_memcpy(hmacData, (uint8*)regInfo->outMsg, regInfo->outMsg_len);
	/* append the current message. Don't append the last TLV (auth) */
	os_memcpy(hmacData+regInfo->outMsg_len, msg, msg_len-12);/*(sizeof(S_WSC_TLV_HEADER)+SIZE_64_BITS));*/
	
	if(!ValidateMac(hmacData, hmacDataLen, authenticator, (uint8*)regInfo->authKey)) {
		wpa_printf(MSG_INFO, "ProcessMessageM8: HMAC validation failed\n");
		os_free(hmacData);
		return WSC_ERR_SYSTEM;
	}
	os_free(hmacData);
	/****** HMAC validation ******/
	
	/****** extract encrypted settings ******/
	
	decrypted = wps_decrypt(cipherText,
		cipherTextLen,
		iv,
		(uint8*)regInfo->keyWrapKey,
		&plainTextLen);
	if(decrypted == NULL || plainTextLen > sizeof(plainText))
	{
		wpa_printf(MSG_ERROR, "failed to decrypt M8 %d > %d\n", plainTextLen, sizeof(plainText));
		if(decrypted != NULL) os_free(decrypted);
		return WSC_ERR_SYSTEM;
	}
	os_memcpy(plainText, decrypted, plainTextLen);
	os_free(decrypted);

	wpa_hexdump_ascii(MSG_DEBUG, "Decrypted data from M8: ", plainText, plainTextLen);
	
	first_cred_len = *(uint8 *)(plainText+3);
	if( *(uint8 *)(plainText+4+first_cred_len+1) == 0x0e) {
		wpa_printf(MSG_DEBUG, "Multiple credentials found in M8 message");
		multiple_creds = 1;
	}
		
	/*
	 *  store the connection credentials for WPA-PSK
	 */
	if(multiple_creds) {
		*encrSettingsLen = *(uint8 *)(plainText+4+first_cred_len+3) + 4;
		os_memcpy(encrSettings, plainText+4+first_cred_len, *encrSettingsLen);
	} else {
	*encrSettingsLen = *(uint8 *)(plainText+3) + 4;
	os_memcpy(encrSettings, plainText, *encrSettingsLen);
	}
	
	return WSC_SUCCESS;
}

static uint32 BuildMessageDone(S_REGISTRATION_DATA *regInfo)
{
	uint8  *msg = regInfo->outMsg;
	uint32 *msg_len = &regInfo->outMsg_len;

	/*
	 *  Assemble the message
	 */

	*msg_len = 0;

	/* Version */
	add_tlv_8bit_value(msg, msg_len, 0x4a, WSC_VERSION);

	/* Message */
	add_tlv_8bit_value(msg, msg_len, 0x22, WSC_ID_MESSAGE_DONE);

	/* Enrollee Nonce */
	add_tlv_data(msg, msg_len, 0x1a, regInfo->enrolleeNonce, SIZE_128_BITS);

	/* Registrar Nonce */
	add_tlv_data(msg, msg_len, 0x39, regInfo->registrarNonce, SIZE_128_BITS);

	wpa_printf(MSG_DEBUG, "BuildMessageDone: %d bytes\n", *msg_len);
	
	return WSC_SUCCESS;
}

static uint32 BuildMessageAck(S_REGISTRATION_DATA *regInfo)
{
	uint8  *msg = regInfo->outMsg;
	uint32 *msg_len = &regInfo->outMsg_len;

	/*
	 *  Assemble the message
	 */

	*msg_len = 0;

	/* Version */
	add_tlv_8bit_value(msg, msg_len, 0x4a, WSC_VERSION);

	/* Message */
	add_tlv_8bit_value(msg, msg_len, 0x22, WSC_ID_MESSAGE_ACK);

	/* Enrollee Nonce */
	add_tlv_data(msg, msg_len, 0x1a, regInfo->enrolleeNonce, SIZE_128_BITS);

	/* Registrar Nonce */
	add_tlv_data(msg, msg_len, 0x39, regInfo->registrarNonce, SIZE_128_BITS);

	wpa_printf(MSG_DEBUG, "BuildMessageAck: %d bytes\n", *msg_len);
	
	return WSC_SUCCESS;
}

static uint32 ProcessMessageAck(S_REGISTRATION_DATA *regInfo, uint8 *msg, uint32 msg_len)
{
//	uint8       version;
   uint8       msgType;
	uint8       enrolleeNonce[SIZE_128_BITS];
	uint8       registrarNonce[SIZE_128_BITS];
	
	wpa_printf(MSG_DEBUG, "ProcessMessageAck: %d byte message\n",msg_len);
  
	/* First, deserialize (parse) the message. */
//	version = msg[4];
	msgType = msg[9];

	/*
	 *  First and foremost, check the version and message number.
	 *  Don't deserialize incompatible messages!
	 */
	if(WSC_ID_MESSAGE_ACK != msgType)
		return WSC_ERR_SYSTEM;

	os_memcpy(enrolleeNonce, (uint8 *)(msg+14), SIZE_128_BITS);
	os_memcpy(registrarNonce, (uint8 *)(msg+14+4+SIZE_128_BITS), SIZE_128_BITS);

	/* confirm the enrollee nonce */
	if(os_memcmp((uint8*)regInfo->enrolleeNonce,
		  enrolleeNonce,
		  SIZE_128_BITS)) {
		wpa_printf(MSG_INFO, "ProcessMessageAck: Incorrect enrollee nonce\n");
		return WSC_ERR_SYSTEM;
	}

	/* confirm the registrar nonce */
	if(os_memcmp((uint8*)regInfo->registrarNonce,
		     registrarNonce,
		     SIZE_128_BITS)) {
		wpa_printf(MSG_INFO, "ProcessMessageAck: Incorrect enrollee nonce\n");
		return WSC_ERR_SYSTEM;
	}

	return WSC_SUCCESS;
}

static char iface_addr[M80211_ADDRESS_SIZE];

static void cleanup_wcs_registration_data(S_REGISTRATION_DATA *data)
{
   if (data)
   {

#define E(_name) if (data->_name) { os_free(data->_name); }

      E(password)
      E(emsk)          
      E(keyWrapKey)    
      E(authKey)       
      E(inMsg)
      E(outMsg)        
      E(p_enrolleeInfo)

#undef E

      os_free(data);
   }
}

static void * eap_wsc_init(struct eap_sm *sm)
{
	S_REGISTRATION_DATA *data;
	S_DEVICE_INFO *deviceInfo;

	/* TODO: Pass these values through a configuration file or something like that */
	uint8 uuid[16] = {0x22, 0x21, 0x02, 0x03, 0x04, 0x05,
			  0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 
			  0x0C, 0x0D, 0x0E, 0x0F};
	uint8  macAddr[6];/* = {0x02, 0x00, 0x00, 0x00, 0x00, 0x6a}; */

	uint16 authTypeFlags = 0x0001;
	uint16 encrTypeFlags = 0x0004;
	uint8  connTypeFlags = 0x01;
	uint16 configMethods = 0x0001;
	uint8  scState = 0x01;
	char manufacturer[16] = WSC_PLATFORM_MANUFACTURER ;
	char modelName[9]     = WSC_PLATFORM_MODULE;
	char deviceName[16]   = WSC_DEVICE_NAME;
	/* Model Number : 1234 */
	char modelNumber[9] = {0x31, 0x32, 0x33, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00};/*, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00};*/
	/* Serial Number : 5678 */
	char serialNumber[16] ={0x35, 0x36, 0x37, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00};/*, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00};*/
	uint16 assocState = 0x0000;
	uint16 devPwdId = 0x0004;
	uint16 primDeviceCategory = 0x0001;
	uint32 primDeviceOui = 0x0050F204;
	uint16 primDeviceSubCategory = 0x0001;
	uint8 rfBand = 0x01;
	uint16 configError = 0;
	uint32 osVersion = 0x80000000;
	uint8 pbc_password[8] = {0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30};
	size_t password_len, pbc_password_len = 8;
	/*struct wpa_ssid *config; */
	const u8 *password;
	int status;
	int byte_count = sizeof(iface_addr);

	wpa_printf(MSG_DEBUG, " *** EAP-WSC Init ***\n");

	/*config = eap_get_config(sm);*/

	status = WiFiEngine_GetMACAddress(iface_addr, &byte_count);
        if(status != WIFI_ENGINE_SUCCESS)
                return NULL;

	os_memcpy(macAddr, iface_addr, 6);

	data = os_malloc(sizeof(*data));
	if (data == NULL)
	{
		return NULL;
	}
	os_memset(data, 0x00, sizeof(*data));

#define E(_name,_size) \
	data->_name = os_malloc(_size); \
        if (data->_name == NULL) { \
		cleanup_wcs_registration_data(data); \
		return NULL; \
	}
	
	E(p_enrolleeInfo, sizeof(S_DEVICE_INFO))
	E(outMsg,         WSC_OUT_MSG_LEN)
	E(inMsg,          WSC_IN_MSG_LEN)
	E(authKey,        SIZE_256_BITS)
	E(keyWrapKey,     SIZE_128_BITS)
	E(emsk,           SIZE_256_BITS)
	E(password,       16)

#undef E

	if(config->ssid->use_wps == 1) {
		wpa_printf(MSG_DEBUG, "EAP-WSC: PBC will be used.\n");
		/* only for PBC the password value is zero (8 bytes) */
		data->password_len = pbc_password_len;
		os_memcpy(data->password, (u8 *)pbc_password, pbc_password_len);
	} else if(config->ssid->use_wps == 2) {
		wpa_printf(MSG_DEBUG, "EAP-WSC: Pin Config Method will be used.\n");
		password = eap_get_config_password(sm, &password_len);
		data->password_len = password_len;
		os_memcpy(data->password, (u8 *)password, password_len);
		if (password == NULL) {
			wpa_printf(MSG_INFO, "EAP-WSC: Pin Method selected bug password not configured! Aborting...");
			cleanup_wcs_registration_data(data);
			return NULL;
		}
	} else {
		wpa_printf(MSG_INFO, "EAP-WSC: Invalid use_wps value (%d). Aborting...\n", config->ssid->use_wps);
		cleanup_wcs_registration_data(data);
		return NULL;
	}

	data->e_lastMsgRecd = MNONE;
	data->e_lastMsgSent = MNONE;
	data->staEncrSettings = NULL;
	data->apEncrSettings = NULL;
	
	deviceInfo = data->p_enrolleeInfo;

	os_memcpy((uint8*)deviceInfo->uuid, uuid, 16);
	os_memcpy((uint8*)deviceInfo->macAddr, macAddr, 6);
	deviceInfo->authTypeFlags = authTypeFlags;
	deviceInfo->encrTypeFlags = encrTypeFlags;
	deviceInfo->connTypeFlags = connTypeFlags;
	deviceInfo->configMethods = configMethods;
	deviceInfo->scState = scState;
	
	os_memcpy((uint8*)deviceInfo->manufacturer, manufacturer, 16);
	os_memcpy((uint8*)deviceInfo->modelName, modelName, 9);
	os_memcpy((uint8*)deviceInfo->modelNumber, modelNumber, 9);
	os_memcpy((uint8*)deviceInfo->serialNumber, serialNumber, 16);
	os_memcpy((uint8*)deviceInfo->deviceName, deviceName, 16);
  
	deviceInfo->primDeviceCategory = primDeviceCategory;
	deviceInfo->primDeviceOui = primDeviceOui;
	deviceInfo->primDeviceSubCategory = primDeviceSubCategory;
	deviceInfo->rfBand = rfBand;
	deviceInfo->assocState = assocState;
	deviceInfo->devPwdId = devPwdId;
	deviceInfo->configError = configError;
	deviceInfo->osVersion = osVersion;
	
	return data;
}


static void eap_wsc_deinit(struct eap_sm *sm, void *priv)
{
	S_REGISTRATION_DATA *data = priv;

	wpa_printf(MSG_DEBUG, "** EAP-WSC De-Init **\n");

   cleanup_wcs_registration_data(data);
}

/************ WPS CREDENTALS START **************/

static struct wps_creds_ {
   unsigned char *tlvs;
   size_t tlvs_len;
} wps_creds = {NULL,0};

void wps_reset_credentials(void)
{
   if(wps_creds.tlvs)
   {
      os_free(wps_creds.tlvs);
      wps_creds.tlvs = NULL;
      wps_creds.tlvs_len = 0;
   }
   os_memset(&wps_creds,0,sizeof(wps_creds));
}


void wps_set_credentials_tlvs(u8 *buf,size_t len)
{
   wps_reset_credentials();

   wps_creds.tlvs = os_malloc(len);
   if(!wps_creds.tlvs)
      return;

   os_memcpy(wps_creds.tlvs, buf, len);
   wps_creds.tlvs_len = len;

   wpa_hexdump_ascii(MSG_INFO,
         " new tlv creds: ", buf, len);
}


/*
 * auth_modes according to WPS
 *
 * 0x0001: No encryption or Open WEP encryption
 * 0x0002: WPA-PSK
 * 0x0004: Shared WEP
 * 0x0008: WPA
 * 0x0010: WPA2
 * 0x0020: WPA2-PSK
 * 0x0022: WPA-PSK & WPA2-PSK
 */
int wps_get_auth_mode(int *auth_type)
{
   m80211_tlv_t tlv;
   unsigned char *tlv_data = NULL;
   u16 t;

   if(wps_creds.tlvs==NULL)
      return -1;

   if(!m80211_tlv_find(wps_creds.tlvs, wps_creds.tlvs_len, 
            WSC_ID_AUTH_TYPE,
            &tlv, (void**)&tlv_data))
   {
      return -2;
   }

   /* tlv's can not be assumed to be word aligned */
   os_memcpy(&t,tlv_data,2);
   *auth_type = ntohs(t);

   return tlv.len;
}


/* 
 * 0x01: No encryption
 * 0x02: WEP encryption
 * 0x04: TKIP
 * 0x08: AES-CCMP
 * 0x0C: AES-CCMP + TKIP
 */
int wps_get_encr_type(int *encr_type)
{
   m80211_tlv_t tlv;
   unsigned char *tlv_data = NULL;
   u16 t;

   if(wps_creds.tlvs==NULL)
      return -1;

   if(!m80211_tlv_find(wps_creds.tlvs, wps_creds.tlvs_len, 
            WSC_ID_ENCR_TYPE,
            &tlv, (void**)&tlv_data))
   {
      return -2;
   }

   /* tlv's can not be assumed to be word aligned */
   os_memcpy(&t,tlv_data,2);
   *encr_type = ntohs(t);

   return tlv.len;
}

/**
 * SSID is not a string, use wps_get_ssid_as_string() if you need it 
 * to be a string but it then needs to be at least 32+1=33 bytes long.
 */
int wps_get_ssid(char *dst, size_t size)
{
   m80211_tlv_t tlv;
   unsigned char *tlv_data = NULL;

   if(wps_creds.tlvs==NULL)
      return -1;

   if(!m80211_tlv_find(wps_creds.tlvs, wps_creds.tlvs_len, 
            WSC_ID_SSID,
            &tlv, (void**)&tlv_data))
   {
      return -2;
   }

   if(size<tlv.len)
      return -3;

   os_memcpy(dst, tlv_data, tlv.len); 

   return tlv.len;
}

/**
 * Will return number of bytes in the SSID, not the length of the string
 *
 * return: strlen(dst) - 1
 */
int wps_get_ssid_as_string(char *dst, size_t size)
{
   os_memset(dst, 0, size);
   return wps_get_ssid(dst, size - 1);
}

static int hex2num(char c)
{
   if (c >= '0' && c <= '9')
      return c - '0';
   if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
   if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
   return -1;
}

static int hex2byte(const char *hex)
{
        int a, b;
        a = hex2num(*hex++);
        if (a < 0)
                return -1;
        b = hex2num(*hex++);
        if (b < 0)
                return -1;
        return (a << 4) | b;
}

static int wep_hex2bytes(
      char *dst,
      const char *hex, 
      int hex_len)
{
   char tmp[3];
   int i;

   if(! (hex_len == 10 || hex_len == 26) )
      return -1;

   for(i=0; i < hex_len/2; i++)
   {
      int c;
      snprintf(tmp,sizeof(tmp),"%c%c",
            hex[2*i+0],
            hex[2*i+1]);
      c = hex2byte(tmp);
      if(c<0)
         return -1;

      dst[i] = c;
   }
   return 0;
}


/*
 * key_len:
 *
 *    WEP:
 * ---------
 *  5: ASCII
 * 13: ASCII
 * 10: HEX
 * 26: HEX
 *
 * non-WEP
 * ---------
 * 64:   WPA-PSK HEX
 * 8-63: WPA-PSK ASCII
 *
 */
int wps_get_key(char *dst, size_t size)
{
   m80211_tlv_t tlv;
   char *tlv_data = NULL;
   int encr_type = 1; // No encryption

   if(wps_creds.tlvs==NULL)
      return -1;

   if(!m80211_tlv_find(wps_creds.tlvs, wps_creds.tlvs_len, 
            WSC_ID_NW_KEY,
            &tlv, (void**)&tlv_data))
   {
      return -2;
   }

   wps_get_encr_type(&encr_type);
   /* WEP ASCII */
   if(encr_type == 2 && tlv.len == 10)
   {
      if(size<5)
         return -3;

      if( 0 > wep_hex2bytes(dst,tlv_data,tlv.len) )
         return -4;

      return 5;
   }

   /* WEP ASCII */
   if(encr_type == 2 && tlv.len == 26)
   {
      if(size<13)
         return -5;

      if( 0 > wep_hex2bytes(dst,tlv_data,tlv.len) )
         return -6;

      return 13;
   }

   if(size<tlv.len)
      return -7;
#if 0
   if(tlv.len > 8 && tlv.len < 64 &&
      tlv_data[tlv.len - 1] == 0) {
      /*
       * A deployed external registrar is known to encode ASCII
       * passphrases incorrectly. Remove the extra NULL termination
       * to fix the encoding.
       */
        wpa_printf(MSG_DEBUG, "WPS: Workaround - remove NULL "
                   "termination from ASCII passphrase");
        tlv.len--;
   }
#endif
   
   os_memset(dst, 0, size);
   os_memcpy(dst, tlv_data, tlv.len); 

   return tlv.len;
}


int wps_get_wep_key_index(int *index)
{
   m80211_tlv_t tlv;
   unsigned char *tlv_data = NULL;

   if(wps_creds.tlvs==NULL)
      return -1;

   if(!m80211_tlv_find(wps_creds.tlvs, wps_creds.tlvs_len,
            WSC_ID_NW_KEY_INDEX,
            &tlv, (void**)&tlv_data))
   {
      return -2;
   }

   *index = *tlv_data;

   return tlv.len;
}

/**
 * validate the tlv credentials so that everything needed is present 
 *
 */
int wps_validate_credentials(u8 *buf, size_t len)
{
   size_t tlv_off = 0;
   m80211_tlv_t tlv;
   void *tlv_data = NULL;

   char v_ssid = 0;
   char v_key = 0;
   char v_enc = 0;

   wpa_hexdump_ascii(MSG_ERROR, "validating tlv:", buf, len);

   while (m80211_tlv_pars_next(
            buf,
            len,
            &tlv_off,
            &tlv,
            &tlv_data))
   {
      switch(tlv.type)
      {
         case WSC_ID_SSID:
            if(tlv.len>0) v_ssid = 1;
            break;

         case WSC_ID_AUTH_TYPE:
            {
               uint16 t;

               os_memcpy(&t,tlv_data,2);

               /* 0x0001: No encryption or Open WEP encryption */
               if(t!=htons(0x0001)) {
                  // require a key
                  v_enc = 1;
               }
            }
            break;

         case WSC_ID_NW_KEY: 
            if(tlv.len>0) v_key = 1;
            break;

         default:
            /* ignore */
            break;
      }
   }

   /* open system */
   if(v_ssid && v_enc==0)
      return TRUE;

   if(v_ssid && v_enc && v_key)
      return TRUE;

   return FALSE;
}

/************ WPS CREDENTALS END **************/



#if 0
/**
 * wps_parseConnCredentials - parse the connection credentials
 * @wpa_s: Pointer to wpa_supplicant data
 * @buf: Input buffer
 * @len: Input length
 * Returns: 0 on success, <0 on failure
 *
 * This function parses the connection credentials that we got from
 * the EAP-WSC M8 message and copies them to the wpa_s structure.
 *
 */
static int wps_parseConnCredentials(u8 *encrSettings, size_t encrSettingsLen)
{
        struct wpa_ssid *ssid_conf = config->ssid;
        int    offset = 0;
        u8    auth_type = 0;
        u8    encr_type = 0;
        u8    wep_key_index = 0;
        size_t passphrase_len;
	char str[128];
	u8 lssid[33];
	char lpsk[65];


        ssid_conf->id = 0;
        wpa_config_set_network_defaults(ssid_conf);
	ssid_conf->use_wps = 2;

        /*                                                                                                                                                           
	 *  Parse encrSettings and store the values to the wpa_ssid context of wpa_supplicant                                                                             
	 */                                                                                                           
               
	/* SSID */
        offset = 0;
        offset = wps_findTlv((u8 *)encrSettings, encrSettingsLen, WSC_ID_SSID);
        if(!offset)
                return -1;

        ssid_conf->ssid_len = *(u8 *)(encrSettings+offset+3);

	os_memcpy(lssid, (u8 *)(encrSettings+offset+4), ssid_conf->ssid_len);
	lssid[*(encrSettings+offset+3)] = '\0';
	DE_SNPRINTF(str, sizeof(str), "\"%s\"", lssid);
        if(wpa_config_set(ssid_conf, "ssid", str, 1) != 0) {
                return -1;
        }

        /* Key Mgmt, Proto */
        offset = 0;
        offset = wps_findTlv((u8 *)encrSettings, encrSettingsLen, WSC_ID_AUTH_TYPE);
        if(!offset)
                return -1;

        printf("auth_type: 0x%x \n", *(u8 *)(encrSettings+offset+4+1));
        auth_type = *(u8 *)(encrSettings+offset+4+1);

        switch(auth_type) {
        case 0x01: /* No encryption or Open WEP encryption */
                ssid_conf->key_mgmt = WPA_KEY_MGMT_NONE;
                break;
        case 0x02: /* WPA-PSK */                                                                                                                            
                ssid_conf->key_mgmt = WPA_KEY_MGMT_PSK;
                ssid_conf->proto = WPA_PROTO_WPA;
                break;
        case 0x04: /* Shared WEP */
                ssid_conf->key_mgmt = WPA_KEY_MGMT_NONE;
                ssid_conf->auth_alg = WPA_AUTH_ALG_SHARED;
                break;
        case 0x08: /* WPA */
                ssid_conf->key_mgmt = WPA_KEY_MGMT_IEEE8021X;
                ssid_conf->proto = WPA_PROTO_WPA;
                break;
        case 0x10: /* WPA2 */
                ssid_conf->key_mgmt = WPA_KEY_MGMT_IEEE8021X;
                ssid_conf->proto = WPA_PROTO_RSN;
                break;
        case 0x20: /* WPA2-PSK */
                ssid_conf->key_mgmt = WPA_KEY_MGMT_PSK;
                ssid_conf->proto = WPA_PROTO_RSN;
                break;
	case 0x22: /* WPA-PSK & WPA2-PSK */
                ssid_conf->key_mgmt = WPA_KEY_MGMT_PSK;
                ssid_conf->proto = WPA_PROTO_RSN;
                break;
        default:
                wpa_printf(MSG_INFO, "wps_parseConnCredentials: Invalid Auth Type received!\n");
        }

	/* Pairwise Cipher */
        offset = 0;
        offset = wps_findTlv((u8 *)encrSettings, encrSettingsLen, WSC_ID_ENCR_TYPE);
        if(!offset)
                return -1;

        printf("encr_type: 0x%x \n", *(u8 *)(encrSettings+offset+4+1));
        encr_type = *(u8 *)(encrSettings+offset+4+1);

        switch(encr_type) {
        case 0x01: /* No encryption */
        case 0x02: /* WEP encryption */                                                                                                                                                           
                break;
        case 0x04: /* TKIP */
		ssid_conf->pairwise_cipher = WPA_CIPHER_TKIP;
                ssid_conf->group_cipher = WPA_CIPHER_TKIP;
                break;
        case 0x08: /* AES-CCMP */
		ssid_conf->pairwise_cipher = WPA_CIPHER_CCMP;
                ssid_conf->group_cipher = WPA_CIPHER_CCMP;
                break;
	case 0x0C: /* AES-CCMP + TKIP */
                ssid_conf->pairwise_cipher = WPA_CIPHER_TKIP;
                ssid_conf->group_cipher = WPA_CIPHER_TKIP;
                break;
        default:
                wpa_printf(MSG_INFO, "wps_parseConnCredentials: Invalid Encryption Type received!\n");
        }

	/* Check if the key is a WEP key or PSK. */
        if(encr_type == 0x02) { /* WEP key */
                wpa_printf(MSG_DEBUG, "wps_parseConnCredentials: WEP encryption...\n");
                offset = 0;
                offset = wps_findTlv((u8 *)encrSettings, encrSettingsLen, WSC_ID_NW_KEY_INDEX);
                if(!offset)
                        return -1;

                wep_key_index = *(u8 *)(encrSettings+offset+4);
                ssid_conf->wep_tx_keyidx = wep_key_index -1; /*zero based index*/                                                                                               

                offset = 0;
                offset = wps_findTlv((u8 *)encrSettings, encrSettingsLen, WSC_ID_NW_KEY);
                if(!offset)
                        return -1;

                os_memcpy(ssid_conf->wep_key+(MAX_WEP_KEY_LEN*ssid_conf->wep_tx_keyidx),
                       (u8 *)(encrSettings+offset+4),
                       *(u8 *)(encrSettings+offset+3));
                ssid_conf->wep_key_len[ssid_conf->wep_tx_keyidx] = *(u8 *)(encrSettings+offset+3);
        }
        else if ( (auth_type == 0x02) || (auth_type == 0x20)|| (auth_type == 0x22)) {
                wpa_printf(MSG_INFO, "wps_parseConnCredentials: PSK will be used.\n");
                offset = 0;
                offset = wps_findTlv((u8 *)encrSettings, encrSettingsLen, WSC_ID_NW_KEY);
                if(!offset)
                        return -1;

                passphrase_len = *(encrSettings+offset+3);
		os_memcpy(lpsk, (u8 *)(encrSettings+offset+4), passphrase_len);
		lpsk[passphrase_len]='\0';
		
		if(passphrase_len == 64) {
			if(wpa_config_set(ssid_conf, "psk", lpsk, 1) != 0) {
				return -1;                      
			}                                       
		} else {                                        
			DE_SNPRINTF(str, sizeof(str), "\"%s\"", lpsk);
			if(wpa_config_set(ssid_conf, "psk", str, 1) != 0) {
				return -1;
			}
		}

		os_memcpy(WPSEncryptionKey, 
			  lpsk, 
			  passphrase_len + 1);

		/* wpa_config_update_psk(ssid_conf); */
	} 
	/*eapol_sm_notify_config(wpa_s->eapol, NULL, NULL);*/
	return 0;
}
#endif

// TODO: FIXME (terminate cm session properly)
#define MESSAGE_ERROR_CHECK   if(WSC_SUCCESS != err) { \
	                              wpa_printf(MSG_ERROR, "### %s:%d: ERROR while building/processing a WSC message. Error Code : %d. Aborting...", __func__,__LINE__, err); \
                                      ret->methodState = METHOD_DONE; \
	                              ret->decision = DECISION_FAIL; \
	                              ret->allowNotifications = FALSE; \
                                      config->ssid->use_wps = 0; \
                                      WiFiEngine_Deauthenticate(); \
	                              return NULL; \
                              }


static u8 * eap_wsc_process(struct eap_sm *sm, void *priv,
			    struct eap_method_ret *ret,
			    const u8 *reqData, size_t reqDataLen,
			    size_t *respDataLen)
{
	const struct eap_hdr *req;
	uint32 err;
	u8 eap_wsc_header[14];
	u8 *resp;
	int ack_msg_sent = 0;
	/* Consider replacing these headers with a more detailed WPS Header.*/
	u8 eap_wsc_header_M1[10]   = {0xFE, 0x00, 0x37, 0x2A, 0x00, 0x00, 0x00, 0x01, 0x04, 0x00};
	u8 eap_wsc_header_M3[10]   = {0xFE, 0x00, 0x37, 0x2A, 0x00, 0x00, 0x00, 0x01, 0x04, 0x00};
	u8 eap_wsc_header_M5[10]   = {0xFE, 0x00, 0x37, 0x2A, 0x00, 0x00, 0x00, 0x01, 0x04, 0x00};
	u8 eap_wsc_header_M7[10]   = {0xFE, 0x00, 0x37, 0x2A, 0x00, 0x00, 0x00, 0x01, 0x04, 0x00};
	u8 eap_wsc_header_DONE[10] = {0xFE, 0x00, 0x37, 0x2A, 0x00, 0x00, 0x00, 0x01, 0x05, 0x00};
	u8 eap_wsc_header_ACK[10]  = {0xFE, 0x00, 0x37, 0x2A, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00};
	u8 *encrSettings;
	uint32 encrSettingsLen = 0;
	S_REGISTRATION_DATA *wscRegData = priv;
	u8 msgType = 0;

	req = (const struct eap_hdr *) reqData;
	
	wpa_printf(MSG_INFO, "EAP-WSC : Received packet(len=%lu) ",
		   (unsigned long) reqDataLen);

	if(ntohs(req->length) != reqDataLen) {
		wpa_printf(MSG_INFO, "EAP-WSC: Pkt length in pkt(%d) differs from"
			   " supplied (%d)\n", ntohs(req->length), reqDataLen);
		ret->ignore = TRUE;
		return NULL;
	}

	/* Get the message type from the WSC_ID_MSG_TYPE tlv */
	if(reqDataLen > 23) {
		wpa_printf(MSG_DEBUG, "Message Type : 0x%x\n", reqData[23]);
		msgType = reqData[23];
	}

	eap_wsc_header[0] = 0x02; /* EAP-Response */
	eap_wsc_header[1] = sm->reqId; /* Identifier */
	
	/* Check the last message that we sent */
	switch(wscRegData->e_lastMsgSent) {
	case MNONE:
		/*mpi_self_test(1);*/
		if (WSC_ID_MESSAGE_M2 == msgType) { /* It may be possible if reconnection took place */
			err = ProcessMessageM2(wscRegData, (u8 *)(reqData+14), reqDataLen-14);
			MESSAGE_ERROR_CHECK
			wscRegData->e_lastMsgSent = M2;
			err = BuildMessageM3(wscRegData);
			MESSAGE_ERROR_CHECK
			os_memcpy(eap_wsc_header+4, eap_wsc_header_M3, 10);
			wscRegData->e_lastMsgSent = M3;
			ack_msg_sent = 0;
		} else {
			err = BuildMessageM1(wscRegData);
			MESSAGE_ERROR_CHECK
			os_memcpy(eap_wsc_header+4, eap_wsc_header_M1, 10);
			wscRegData->e_lastMsgSent = M1;
		}
		break;
	case M1:
		if (WSC_ID_MESSAGE_M2D == msgType) {
			err = ProcessMessageM2D(wscRegData, (u8 *)(reqData+14), reqDataLen-14);
			MESSAGE_ERROR_CHECK
			wscRegData->e_lastMsgSent = M2D;
			err = BuildMessageAck(wscRegData);
			MESSAGE_ERROR_CHECK
			os_memcpy(eap_wsc_header+4, eap_wsc_header_ACK, 10);
			wscRegData->e_lastMsgSent = M1;
			ack_msg_sent = 1;
		} else {
			err = ProcessMessageM2(wscRegData, (u8 *)(reqData+14), reqDataLen-14);
			MESSAGE_ERROR_CHECK
			wscRegData->e_lastMsgSent = M2;
			err = BuildMessageM3(wscRegData);
			MESSAGE_ERROR_CHECK
			os_memcpy(eap_wsc_header+4, eap_wsc_header_M3, 10);
			wscRegData->e_lastMsgSent = M3;
			ack_msg_sent = 0;
		}
		break;
	case M3:
		err = ProcessMessageM4(wscRegData, (u8 *)(reqData+14), reqDataLen-14);
		MESSAGE_ERROR_CHECK
		wscRegData->e_lastMsgSent = M4;
		err = BuildMessageM5(wscRegData);
		MESSAGE_ERROR_CHECK
		os_memcpy(eap_wsc_header+4, eap_wsc_header_M5, 10);
		wscRegData->e_lastMsgSent = M5;
		break;
	case M5:
		err = ProcessMessageM6(wscRegData, (u8 *)(reqData+14), reqDataLen-14);
		MESSAGE_ERROR_CHECK
		wscRegData->e_lastMsgSent = M6;
		err = BuildMessageM7(wscRegData);
		MESSAGE_ERROR_CHECK
		os_memcpy(eap_wsc_header+4, eap_wsc_header_M7, 10);
		wscRegData->e_lastMsgSent = M7;
		break;
	case M7:
		encrSettings = os_malloc(WSC_ENC_SETTINGS_SIZE);
		if (encrSettings == NULL)
			return NULL;
		err = ProcessMessageM8(wscRegData, (u8 *)(reqData+14), reqDataLen-14, encrSettings, &encrSettingsLen);
		if(WSC_SUCCESS != err) 
         os_free(encrSettings);
		MESSAGE_ERROR_CHECK
         
		wscRegData->e_lastMsgSent = M8;
      
		err = BuildMessageDone(wscRegData);
		if(WSC_SUCCESS != err) 
         os_free(encrSettings);
		MESSAGE_ERROR_CHECK

		os_memcpy(eap_wsc_header+4, eap_wsc_header_DONE, 10);
		wscRegData->e_lastMsgSent = DONE;
		if(FALSE == wps_validate_credentials(encrSettings+4, *(u8 *)(encrSettings+3)))
		{ 
			os_free(encrSettings);
			return NULL;
		}
		wps_set_credentials_tlvs(encrSettings+4, *(u8 *)(encrSettings+3));
		os_free(encrSettings);
		break;
	case DONE:
		err = ProcessMessageAck(wscRegData, (u8 *)(reqData+14), reqDataLen-14);
		MESSAGE_ERROR_CHECK
		wscRegData->e_lastMsgSent = COMPLETE;
		break;
   case ACK:
     	wpa_printf(MSG_INFO, "EAP-WSC: lastMsgSent: ACK.\n");
     	break;
	default:
		wpa_printf(MSG_INFO, "EAP-WSC: lastMsgSent is not valid!\n");
	}

	resp = os_malloc(wscRegData->outMsg_len + 14);
        if (resp == NULL)
                return NULL;

	*(u16 *)(eap_wsc_header+2) = bswap_16(wscRegData->outMsg_len + 14);
	os_memcpy(resp, eap_wsc_header, 14);
	os_memcpy(resp+14, wscRegData->outMsg, wscRegData->outMsg_len);
	
	*respDataLen = wscRegData->outMsg_len + 14;
	ret->ignore = FALSE;
	ret->decision = DECISION_COND_SUCC;
	ret->allowNotifications = FALSE;

	/* Check if we're done */
	if (wscRegData->e_lastMsgSent == DONE) {
		wpa_printf(MSG_INFO, "@@@ WPS Protocol Completed Successfuly! @@@\n");
		ret->methodState = METHOD_DONE;
	} else {
		wpa_printf(MSG_DEBUG, "Always setting it to METHOD_CONT\n");
		ret->methodState = METHOD_CONT;
	}

	if (ack_msg_sent == 1) {
		wpa_printf(MSG_INFO, "@@@ Sent Ack @@@\n");
		ret->methodState = METHOD_DONE;
		WiFiEngine_Deauthenticate();
#if 0
		wps_scan(15); /* This should be necessary when 5.1.3/5.1.5 is done automatically */
#endif
	}
	
	return resp;
}

int eap_peer_wsc_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_WSC, "WSC");
	if (eap == NULL)
		return -1;
	
	eap->init = eap_wsc_init;
	eap->deinit = eap_wsc_deinit;
	eap->process = eap_wsc_process;
	
	ret = eap_peer_method_register(eap);
	if (ret)
		eap_peer_method_free(eap);
	return ret;
}

#endif //USE_WPS
#endif //EAP_WSC

/* Local Variables: */
/* c-basic-offset: 8 */
/* indent-tabs-mode: t */
/* End: */


