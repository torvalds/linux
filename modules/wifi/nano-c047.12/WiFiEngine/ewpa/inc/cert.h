/** @file cert.h
 *  @brief This header file contains data structures and function declarations of cert
 *
 *  Copyright (C) 2001-2008, Iwncomm Ltd.
 */

#ifndef _CERT_H_
#define _CERT_H_

#define V_X509_V1   0
#define V_X509_V2   1
#define V_X509_V3   2

#define V_ASN1_UNIVERSAL		0x00
#define	V_ASN1_APPLICATION		0x40
#define V_ASN1_CONTEXT_SPECIFIC		0x80
#define V_ASN1_PRIVATE			0xc0

#define V_ASN1_CONSTRUCTED		0x20
#define V_ASN1_PRIMITIVE_TAG		0x1f

#define V_ASN1_UNDEF			-1
#define V_ASN1_EOC			0
#define V_ASN1_BOOLEAN			1	/**/
#define V_ASN1_INTEGER			2
#define V_ASN1_BIT_STRING		3
#define V_ASN1_OCTET_STRING		4
#define V_ASN1_NULL			5
#define V_ASN1_OBJECT			6
#define V_ASN1_OBJECT_DESCRIPTOR	7
#define V_ASN1_EXTERNAL			8
#define V_ASN1_REAL			9
#define V_ASN1_ENUMERATED		10
#define V_ASN1_UTF8STRING		12
#define V_ASN1_SEQUENCE			16
#define V_ASN1_SET			17
#define V_ASN1_NUMERICSTRING		18	/**/
#define V_ASN1_PRINTABLESTRING		19
#define V_ASN1_T61STRING		20
#define V_ASN1_TELETEXSTRING		20	/* alias */
#define V_ASN1_VIDEOTEXSTRING		21	/**/
#define V_ASN1_IA5STRING		22
#define V_ASN1_UTCTIME			23
#define V_ASN1_GENERALIZEDTIME		24	/**/
#define V_ASN1_GRAPHICSTRING		25	/**/
#define V_ASN1_ISO64STRING		26	/**/
#define V_ASN1_VISIBLESTRING		26	/* alias */
#define V_ASN1_GENERALSTRING		27	/**/
#define V_ASN1_UNIVERSALSTRING		28	/**/
#define V_ASN1_BMPSTRING		30


#define WAPI_OID_NUMBER     1

#define MAX_BYTE_DATA_LEN  256
#define COMM_DATA_LEN          	2048


#define PACK_ERROR  0xffff



#define SIGN_LEN               		48  
#define PUBKEY_LEN             		48  
#define PUBKEY2_LEN             		49  
#define SECKEY_LEN             		24  
#define DIGEST_LEN             		32  
#define HMAC_LEN                        	20  
#define PRF_OUTKEY_LEN			48

#define X509_CERT_SIGN_LEN        57  

#define KD_HMAC_OUTKEY_LEN	96


#define IWN_FIELD_OFFSET(type, field)   ((int)(&((type *)0)->field))

/*ECDSA192+SHA256*/
#define WAPI_ECDSA_OID          "1.2.156.11235.1.1.1"
/*CURVE OID*/
#define WAPI_ECC_CURVE_OID      "1.2.156.11235.1.1.2.1"

#define ECDSA_ECDH_OID          "1.2.840.10045.2.1"


typedef struct _WOID 
{
    const char*     pszOIDName;
    unsigned short  usOIDLen;
    unsigned short  usParLen;
    unsigned short  bOID[MAX_BYTE_DATA_LEN];        
    unsigned short  bParameter[MAX_BYTE_DATA_LEN];  
} WOID, *PWOID;




#define X509_TIME_LEN       15
typedef struct _pov_x {
    //struct  
    //{
        unsigned long not_before;   
        unsigned long not_after;    
    //};
    //struct  
    //{
        unsigned long Length;
        unsigned char Xnot_before[X509_TIME_LEN + 1];    
        unsigned char Xnot_after[X509_TIME_LEN + 1];     
    //};
} pov_x_t;



typedef  struct  __private_key
{
unsigned char                  tVersion;
unsigned char                  lVersion;
unsigned char                  vVersion;
unsigned char                  verpad;

unsigned char                  tPrivateKey;                   
unsigned char                  lPrivateKey;                   
unsigned char                  prikeypad[2];
unsigned char                  vPrivateKey[MAX_BYTE_DATA_LEN];   

unsigned char                  tSPrivateKeyAlgorithm;
unsigned char                  lSPrivateKeyAlgorithm;
unsigned char                  tOID;                              
unsigned char                  lOID;                          
unsigned char                  vOID[MAX_BYTE_DATA_LEN];           

unsigned char                  tSPubkey;  
unsigned char                  lSPubkey;  
unsigned char                  tPubkey;    
unsigned char                  lPubkey;    
unsigned char                  vPubkey[MAX_BYTE_DATA_LEN];
}private_key;




short unpack_private_key(private_key *p_private_key, const void * buffer, short bufflen);

void *iwn_x509_get_pubkey(void *cert_st);

void *iwn_x509_get_subject_name(void *cert_st);

void *iwn_x509_get_serial_number(void *cert_st);

void *iwn_x509_get_issuer_name(void *cert_st);

int iwn_x509_get_sign(void *cert_st, unsigned char *out, int out_len);
int iwn_x509_get_sign_inlen(void *cert_st);

int Base64Dec(unsigned char *buf,const unsigned char*text,int size);

unsigned char *get_realinfo_from_cert(unsigned char *des, const unsigned char *src_cert, int len,
					const  char *start_flag,
					const  char *end_flag);





#define CERT_OBJ_NONE 0
#define  CERT_OBJ_X509 1

struct cert_obj_st_t{
	int cert_type;
	char *cert_name;
	void *asu_cert_st;
	tkey *asu_pubkey;
	void *user_cert_st;/*user certificate struct*/
	tkey *private_key;
	struct cert_bin_t  *cert_bin;/*user certificate file*/
	struct cert_bin_t  *asu_cert_bin;/*asu certificate file*/
	void* (*get_public_key)(void *cert_st);
	void* (*get_subject_name)(void *cert_st);
	void* (*get_issuer_name)(void *cert_st);
	void* (*get_serial_number)(void *cert_st);
	int (*verify_key)( const unsigned char *pub_s, int pub_sl,  const unsigned char *priv_s, int priv_sl);
	int (*sign)(const unsigned char *priv_s, int priv_sl, const unsigned char * in, int in_len, unsigned char *out);
	int (*verify)( const unsigned char *pub_s, int pub_sl, unsigned char *in ,  int in_len, unsigned char *sig,int sign_len);
};

struct asue_config
{
  char cert_name[256];
  char client_cert_name[256];
  unsigned short used_cert;
  unsigned short pad;
};

struct _asue_cert_info {
  struct cert_obj_st_t *asue_cert_obj;
  struct asue_config config;
};


void cert_obj_register(struct cert_obj_st_t *cert_obj);
void cert_obj_unregister(const struct cert_obj_st_t *cert_obj);

int get_x509_cert(struct cert_obj_st_t *cert_obj);
void x509_free_obj_data(struct cert_obj_st_t *cert_obj);
int init_cert(void);
int change_cert_format(const  char *cert_file,
		       const  char *client_file,
		       unsigned char *out_user,
		       int len_user,
		       unsigned char *out_asu,
		       int len_asu);

const struct cert_obj_st_t *get_cert_obj(unsigned short index);


#define PEM_STRING_X509_CERTS		"-----BEGIN CERTIFICATE-----"
#define PEM_STRING2_X509_CERTE		"-----END CERTIFICATE-----"
#define PEM_STRING_PRIKEYS			"-----BEGIN EC PRIVATE KEY-----"
#define PEM_STRING_PRIKEYE			"-----END EC PRIVATE KEY-----"

void X509_exit(void);
int X509_init(void);

typedef struct _wai_fixdata_id_cert
{
  u16     id_flag;
  u16     id_len;
  u8      id_data[1000];
} wai_fixdata_id_cert;

struct wapi_cert_data {
  struct _asue_cert_info  cert_info;
  wai_fixdata_id_cert          asue_id;
  int has_cert;
  unsigned char user[1024];
  int len_user; /* 1024?*/
  unsigned char asu[1024];
  int len_asu;
};

#endif  /* _CERT_H_ end */

