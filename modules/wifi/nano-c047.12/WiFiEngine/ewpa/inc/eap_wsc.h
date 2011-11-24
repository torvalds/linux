/****************************************/
/* Copyright (c) 2007-2008 Nanoradio AB */
/****************************************/

#ifndef EAP_WSC_H
#define EAP_WSC_H

#include "bignum.h"
#include "sha256.h"
#include "bignum_xyssl.h"
#include "dhm_xyssl.h"

#ifndef __rtke__
#define ATTRIBUTE_PACK
#pragma pack(push, 1)
#endif

#define ENROLLEE_ID_STRING        "WFA-SimpleConfig-Enrollee-1-0"

#define BUF_SIZE_64_BITS    8
#define BUF_SIZE_128_BITS   16
#define BUF_SIZE_160_BITS   20
#define BUF_SIZE_256_BITS   32
#define BUF_SIZE_512_BITS   64
#define BUF_SIZE_1024_BITS  128
#define BUF_SIZE_1536_BITS  192

#define PERSONALIZATION_STRING  "Wi-Fi Easy and Secure Key Derivation"
#define PRF_DIGEST_SIZE         BUF_SIZE_256_BITS
#define KDF_KEY_BITS            640

#define WSC_VERSION                 0x10

//Data element definitions
#define WSC_ID_VERSION              0x104A
#define WSC_ID_MSG_TYPE             0x1022
#define WSC_ID_UUID_E               0x1047
#define WSC_ID_UUID_R               0x1048
#define WSC_ID_MAC_ADDR             0x1020
#define WSC_ID_ENROLLEE_NONCE       0x101A
#define WSC_ID_REGISTRAR_NONCE      0x1039
#define WSC_ID_PUBLIC_KEY           0x1032
#define WSC_ID_AUTH_TYPE_FLAGS      0x1004
#define WSC_ID_ENCR_TYPE_FLAGS      0x1010
#define WSC_ID_CONN_TYPE_FLAGS      0x100D
#define WSC_ID_CONFIG_METHODS       0x1008
#define WSC_ID_SC_STATE             0x1044
#define WSC_ID_MANUFACTURER         0x1021
#define WSC_ID_MODEL_NAME           0x1023
#define WSC_ID_MODEL_NUMBER         0x1024
#define WSC_ID_SERIAL_NUM           0x1042
#define WSC_ID_PRIM_DEV_TYPE        0x1054
#define WSC_ID_DEVICE_NAME          0x1011
#define WSC_ID_RF_BAND              0x103C
#define WSC_ID_ASSOC_STATE          0x1002
#define WSC_ID_DEVICE_PWD_ID        0x1012
#define WSC_ID_CONFIG_ERROR         0x1009
#define WSC_ID_OS_VERSION           0x102D
#define WSC_ID_ENCR_SETTINGS        0x1018
#define WSC_ID_AUTHENTICATOR        0x1005
#define WSC_ID_E_HASH1              0x1014
#define WSC_ID_E_HASH2              0x1015
#define WSC_ID_R_HASH1              0x103D
#define WSC_ID_R_HASH2              0x103E

#define WSC_ID_CREDENTIAL           0x100E
#define WSC_ID_KEY_WRAP_AUTH        0x101E
#define WSC_ID_MAC_ADDR             0x1020
#define WSC_ID_NW_INDEX             0x1026
#define WSC_ID_SSID                 0x1045
#define WSC_ID_AUTH_TYPE            0x1003
#define WSC_ID_ENCR_TYPE            0x100F
#define WSC_ID_NW_KEY_INDEX         0x1028
#define WSC_ID_NW_KEY               0x1027

#define WSC_SUCCESS                 0

#define WSC_ERR_BASE(_CODE)         (__LINE__ | _CODE << 16)
#define WSC_ERR_OUTOFMEMORY         WSC_ERR_BASE(2)
#define WSC_ERR_SYSTEM              WSC_ERR_BASE(3)
#define WSC_ERR_NOT_INITIALIZED     WSC_ERR_BASE(4)
#define WSC_ERR_INVALID_PARAMETERS  WSC_ERR_BASE(5)
#define WSC_ERR_BUFFER_TOO_SMALL    WSC_ERR_BASE(6)
#define WSC_ERR_NOT_IMPLEMENTED     WSC_ERR_BASE(7)
#define WSC_ERR_ALREADY_INITIALIZED WSC_ERR_BASE(8)
#define WSC_ERR_GENERIC             WSC_ERR_BASE(9)
#define WSC_ERR_FILE_OPEN           WSC_ERR_BASE(10)
#define WSC_ERR_FILE_READ           WSC_ERR_BASE(11)
#define WSC_ERR_FILE_WRITE          WSC_ERR_BASE(12)

#define WSC_ID_MESSAGE_M1        0x04
#define WSC_ID_MESSAGE_M2        0x05
#define WSC_ID_MESSAGE_M2D       0x06
#define WSC_ID_MESSAGE_M3        0x07
#define WSC_ID_MESSAGE_M4        0x08
#define WSC_ID_MESSAGE_M5        0x09
#define WSC_ID_MESSAGE_M6        0x0A
#define WSC_ID_MESSAGE_M7        0x0B
#define WSC_ID_MESSAGE_M8        0x0C
#define WSC_ID_MESSAGE_ACK       0x0D
#define WSC_ID_MESSAGE_NACK      0x0E
#define WSC_ID_MESSAGE_DONE      0x0F

#define SIZE_1_BYTE         1
#define SIZE_2_BYTES        2
#define SIZE_4_BYTES        4
#define SIZE_6_BYTES        6
#define SIZE_8_BYTES        8
#define SIZE_16_BYTES       16
#define SIZE_20_BYTES       20
#define SIZE_32_BYTES       32
#define SIZE_64_BYTES       64
#define SIZE_80_BYTES       80
#define SIZE_128_BYTES      128
#define SIZE_192_BYTES      192

#define BUF_SIZE_1536_BITS  192

#define SIZE_64_BITS        8
#define SIZE_128_BITS       16
#define SIZE_160_BITS       20
#define SIZE_256_BITS       32

#define SIZE_ENCR_IV            SIZE_128_BITS
#define ENCR_DATA_BLOCK_SIZE    SIZE_128_BITS
#define SIZE_DATA_HASH          SIZE_160_BITS
#define SIZE_PUB_KEY_HASH       SIZE_160_BITS
#define SIZE_UUID               SIZE_16_BYTES
#define SIZE_MAC_ADDR           SIZE_6_BYTES
#define SIZE_PUB_KEY            SIZE_192_BYTES
#define SIZE_ENROLLEE_NONCE     SIZE_128_BITS

#ifndef __nucleus__
typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;

typedef char int8;
typedef short int16;
typedef int int32;
#endif

typedef enum {
	MSTART = 0,
	M1,
	M2,
	M2D,
	M3,
	M4,
	M5,
	M6,
	M7,
	M8,
	DONE,
	COMPLETE,
	ACK,
	MNONE = 99
} EMsg;

/* Declare TLV header as extern, since it will be defined elsewhere */
typedef struct {
	uint16    attributeType;
	uint16    dataLength;
} S_WSC_TLV_HEADER;

/* data structure to hold Enrollee and Registrar information */
typedef struct {
	uint8   version;
	uint8   connTypeFlags;
	uint8   scState;
	uint8   rfBand;

	char    deviceName[SIZE_32_BYTES];
	char    modelName[SIZE_32_BYTES];
	char    modelNumber[SIZE_32_BYTES];
	char    serialNumber[SIZE_32_BYTES];
	char    ssid[SIZE_32_BYTES];

	char    manufacturer[SIZE_64_BYTES];

	uint8   uuid[SIZE_16_BYTES];

	uint16  assocState;
	uint16  devPwdId;

	uint16  configError;
	uint16  primDeviceCategory;

	uint16  primDeviceSubCategory;
	uint16  authTypeFlags;

	uint16  encrTypeFlags;
	uint16  configMethods;

	uint32  primDeviceOui;
	uint32  osVersion;
	uint32  featureId;

	char    keyMgmt[SIZE_20_BYTES];

	uint8   macAddr[SIZE_6_BYTES];
	uint8   b_ap;
	uint8   unused[1];
}S_DEVICE_INFO;


/*
 *  data structure to store info about a particular instance
 *  of the Registration protocol
 */
#define WSC_IN_MSG_LEN 768
#define WSC_OUT_MSG_LEN 768
typedef struct {
	/* 
	 * pointers and uint32_t first to ensure 
	 * alignment and to make it easier to debug 
	 */
	uint8       *password;
	uint32      password_len;

	uint8       *authKey;
	uint32      authKey_len;
	
	uint8       *keyWrapKey;
	uint32      keyWrapKey_len;

	uint8       *emsk;
	uint32      emsk_len;

	uint8       *x509csr;
	uint32      x509csr_len;
	
	uint8       *x509Cert;
	uint32      x509Cert_len;

	uint8       *inMsg; /* A recd msg will be stored here */
	uint32      inMsg_len;

	uint8       *outMsg; /* Contains msg to be transmitted */
	uint32      outMsg_len;

	void        *staEncrSettings; /* to be sent in M2/M8 by reg & M7 by enrollee */

	void        *apEncrSettings;

	/* enrollee endpoint - filled in by the Registrar, NULL for Enrollee */
	S_DEVICE_INFO    *p_enrolleeInfo;

	/* Registrar endpoint - filled in by the Enrollee, NULL for Registrar */
	S_DEVICE_INFO    *p_registrarInfo;

	/* 20*4 = 80 bytes this far */

	/* byte buffers here, all 4 byte aligned */

	uint8       enrolleeNonce[SIZE_128_BITS];

	uint8       registrarNonce[SIZE_128_BITS];

	uint8       pke[SIZE_PUB_KEY]; /* enrollee's raw pub key */
	uint8       pkr[SIZE_PUB_KEY]; /* registrar's raw pub key */

	uint8       psk1[SIZE_128_BITS];
	uint8       psk2[SIZE_128_BITS];

	uint8       eHash1[SIZE_256_BITS];
	uint8       eHash2[SIZE_256_BITS];
	uint8       es1[SIZE_128_BITS];
	uint8       es2[SIZE_128_BITS];
	
	uint8       rHash1[SIZE_256_BITS];
	uint8       rHash2[SIZE_256_BITS];
	uint8       rs1[SIZE_128_BITS];

#if 0
	/* is never used ? */
	uint8       rs2[SIZE_128_BITS];
#endif

	/* enum and u16 */

	EMsg        e_lastMsgRecd;
	EMsg        e_lastMsgSent;

	uint16      enrolleePwdId;

	/* for alignment */
	uint16      ignore_padding;

	/* Diffie Hellman parameters */
	dhm_context dhm;

}S_REGISTRATION_DATA;


uint32 GenerateDHKeyPair(dhm_context *dhm, uint8 *pubKey);

void DeriveKey(uint8 *KDK,
               uint8 *prsnlString,
               uint32 keyBits,
               uint8 *key);

uint8 ValidateMac(uint8 *data, uint32 data_len, uint8 *hmac, uint8 *key);

uint8* wps_decrypt(
    uint8 *encr,
    uint32 encr_len,
    uint8 *iv,
    uint8 *keywrapkey,
    uint32 *out_len);

uint8* wps_encrypt(
    uint8 *plain,
    uint32 plain_len,
    uint8 *iv,
    uint8 *keywrapkey,
    uint32 *cipherTextLen);

#ifndef __rtke__
#pragma pack(pop)
#endif

#endif /*EAP_WSC_H */

/* Local Variables:    */
/* c-basic-offset: 8   */
/* indent-tabs-mode: t */
/* End:                */
