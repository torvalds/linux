/** @file cert.c
  * @brief This file contains functions for cert.
 *
 *  Copyright (C) 2001-2008, Iwncomm Ltd.
 */

//#include "include/common.h"
#include "includes.h"
#include "common.h"
#include "wapi.h"
#include "wapi_i.h"
#include "cert.h"
#include "ecc.h"

/* local funciton declare */
static int ParseLength(unsigned char **pBuf, unsigned char *pMax, unsigned long *pLen);
static int ParseSequence(unsigned char **pBuffer, unsigned char *pMax, int *pClass, int *pTag, unsigned long *pLength, unsigned char *pbIsConstruct);
static int ParseOID(unsigned char **pBuffer, unsigned char *pMax, unsigned char *pszString, unsigned long *pStrLen, unsigned long *pParLen);
static int ParseString(unsigned char **pBuffer, unsigned char *pMax, unsigned char *pszString, unsigned long *pStrLen);
static int ParseName(unsigned char **pBuffer, unsigned char *pMax, unsigned char *pszString, unsigned long *pStrLen);
static unsigned char hlpCheckOIDAndParam(unsigned char *pOID, unsigned long dwOIDLen, unsigned char* pParam, unsigned long dwParamLen, unsigned char bIsPubKey);
static int ParseValidity(unsigned char **pBuffer, unsigned char *pMax, pov_x_t* pValid);
static unsigned char GetBase64Value(unsigned char ch);

/*
 * defined in wapi.c
 * TODO: This should be moved to eloop structs
 */
extern struct wapi_cert_data wapi_cert;

static const WOID gWOID[WAPI_OID_NUMBER] = 
{
    /* OIDName          OIDLen      ParLen      OID     Parameter*/
    {WAPI_ECDSA_OID, 8, 11, {0x2A, 0x81, 0x1C, 0xD7, 0x63, 0x01, 0x01, 0x01}, {0x06, 0x09, 0x2A, 0x81, 0x1C, 0xD7, 0x63, 0x01, 0x01, 0x02, 0x01}}
};

static const WOID gPubKeyOID[WAPI_OID_NUMBER] = 
{
    {ECDSA_ECDH_OID, 7, 11, {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01}, {0x06, 0x09, 0x2A, 0x81, 0x1C, 0xD7, 0x63, 0x01, 0x01, 0x02, 0x01}}
};


static int ParseLength(unsigned char ** pBuffer, unsigned char* pMax, unsigned long* pLen)
{
    unsigned char *p = *pBuffer;
    unsigned long l = 0, n = 0;
    if (*p & 0x80)
    {
        n = *p++ & 0x7f;
        while (n-- && p < pMax)
        {
            l <<= 8;
            l |= *p++;
        }
    }
    else
    {
        l = *p++;
    }
    *pLen = l;
    *pBuffer = p;
    return 0;
}

static int ParseSequence(unsigned char **pBuffer, unsigned char *pMax, int *pClass, int *pTag, unsigned long *pLength, unsigned char *pbIsConstruct)
{
    unsigned char *p = *pBuffer;
    int c, t;
    unsigned char bCt;
    unsigned long tl = 0;
    c = *p & V_ASN1_PRIVATE;
    t = *p & V_ASN1_PRIMITIVE_TAG;
    bCt = *p & V_ASN1_CONSTRUCTED;
    p++;
	if (t == V_ASN1_PRIMITIVE_TAG) 
    {
        t = 0;
        do
        {
            t <<= 7;
            t |= *p & 0x7f;
        } while( (*(p++) & 0x80) && p < pMax);
    }
    if (ParseLength(&p, pMax, &tl))
    {
        return 1;
    }
    *pBuffer = p;
    if (pClass)
    {
        *pClass = c;
    }
    if (pTag)
    {
        *pTag = t;
    }
    if (pbIsConstruct)
    {
        *pbIsConstruct = bCt;
    }
    if (pLength)
    {
        *pLength = tl;
    }
    return 0;
}

static int ParseOID(unsigned char **pBuffer, unsigned char *pMax, unsigned char *pszString, unsigned long *pStrLen, unsigned long *pParLen)
{
    unsigned char *p = *pBuffer, *pbak;
    unsigned long len;
    if (*p != V_ASN1_OBJECT)
    {
        return 1;
    }
    p++;
	if (ParseLength(&p, pMax, &len))
    {
        return 1;
    }
    *pStrLen = len;
    pbak = p;
    if (pParLen)
    {
        p += len;
        if (*p == V_ASN1_NULL)
        {
            *pParLen = 2;
        }
        else
        {
            p++;
            if (ParseLength(&p, pMax, pParLen))
            {
                return 1;
            }
            *pParLen += (unsigned long)(p - pbak - len);
        }
    }
    if (pszString)
    {
        os_memcpy(pszString, pbak, len);
        *pBuffer = pbak + len;
    }
    return 0;
}

static int ParseString(unsigned char **pBuffer, unsigned char *pMax, unsigned char *pszString, unsigned long *pStrLen)
{
    unsigned char* p = *pBuffer;
    unsigned char type = *p++;
    unsigned long len;
    if (ParseLength(&p, pMax, &len))
    {
        return 1;
    }
    if (pszString == NULL || *pStrLen < len)
    {
        *pStrLen = len;
        return 0;
    }
    switch(type)
    {
    case V_ASN1_UTF8STRING:
        break;
    case V_ASN1_BMPSTRING:
        break;
    case V_ASN1_UNIVERSALSTRING:
        break;
    case V_ASN1_PRINTABLESTRING:
        break;
    case V_ASN1_BIT_STRING:
        break;
    case V_ASN1_OCTET_STRING:
        break;
    default:
        return 1;
    }
    os_memcpy(pszString, p, len);
    *pStrLen = len;
    *pBuffer = p+len;
    return 0;
}

static int ParseName(unsigned char **pBuffer, unsigned char *pMax, unsigned char *pszString, unsigned long *pStrLen)
{
    unsigned char* p = *pBuffer;
    unsigned long allLen;
    if (ParseSequence(&p, pMax, NULL, NULL, &allLen, NULL))
    {
        return 1;
    }
    p += allLen;
    allLen = (unsigned long)(p - *pBuffer);
    if (pszString)
    {
        os_memcpy(pszString, *pBuffer, allLen);
    }
    *pBuffer = p;
    *pStrLen = allLen;
    return 0;
}

static int ParseValidity(unsigned char **pBuffer, unsigned char *pMax, pov_x_t* pValid)
{
    unsigned char *p = *pBuffer;
    unsigned long len;

    if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
    {
        return 1;
    }
	if (*p != V_ASN1_UTCTIME && *p != V_ASN1_GENERALIZEDTIME)
	{
		return 1;
	}
    if (ParseSequence(&p, pMax, NULL, NULL, &len, NULL))
    {
        return 1;
    }

	if (pValid)
	{
	    os_memcpy(pValid->Xnot_before, p, len);
	}
    p += len;
    if (*p != V_ASN1_UTCTIME &&
        *p != V_ASN1_GENERALIZEDTIME)
    {
        return 1;
    }

    if (ParseSequence(&p, pMax, NULL, NULL, &len, NULL))
    {
        return 1;
    }
	if (pValid)
	{
		os_memcpy(pValid->Xnot_after, p, len);
		pValid->Length = len;
	}
    *pBuffer = p + len;
    return 0;
}

static unsigned char hlpCheckOIDAndParam(unsigned char *pOID, unsigned long dwOIDLen, unsigned char* pParam, unsigned long dwParamLen, unsigned char bIsPubKey)
{
    int i;
    const WOID* pWD = bIsPubKey ? &gPubKeyOID[0] : &gWOID[0];
    if (!pOID && !pParam)
    {
        return 1;
    }

    for(i = 0; i < WAPI_OID_NUMBER; i++)
    {
        if ( (pOID == NULL || (pWD[i].usOIDLen == dwOIDLen &&
                        os_memcmp(pWD[i].bOID, pOID, dwOIDLen) == 0) )
              &&
              (pParam == NULL || (pWD[i].usParLen == dwParamLen &&
                         os_memcmp(pWD[i].bParameter, pParam, dwParamLen) == 0) )
            )
        {
            return 1;
        }
    }
    return 0;
}

short unpack_private_key(private_key *p_private_key, const void * buffer, short bufflen)
{
	short  offset = 0;
	unsigned char  tTotal;
	unsigned char  lTotal;

	os_memcpy(&tTotal, (unsigned char *)buffer + offset, 1);
	offset ++;

	os_memcpy(&lTotal, (unsigned char *)buffer + offset, 1);
	offset ++;

	os_memcpy(&p_private_key->tVersion, (unsigned char *)buffer + offset, 1);
	offset ++;
	os_memcpy(&p_private_key->lVersion, (unsigned char *)buffer + offset, 1);
	offset ++;
	if (offset + p_private_key->lVersion > bufflen)
		return (short)PACK_ERROR;
	os_memcpy(&p_private_key->vVersion, (unsigned char *)buffer + offset, p_private_key->lVersion);
	offset += p_private_key->lVersion;

	os_memcpy(&p_private_key->tPrivateKey, (unsigned char *)buffer + offset, 1);
	offset ++;
	os_memcpy(&p_private_key->lPrivateKey, (unsigned char *)buffer + offset, 1);
	offset ++;
	if (offset + p_private_key->lPrivateKey > bufflen)
		return (short)PACK_ERROR;
	os_memcpy(&p_private_key->vPrivateKey, (unsigned char *)buffer + offset, p_private_key->lPrivateKey);
	offset += p_private_key->lPrivateKey;

	os_memcpy(&p_private_key->tSPrivateKeyAlgorithm, (unsigned char *)buffer + offset, 1);
	offset ++;
	os_memcpy(&p_private_key->lSPrivateKeyAlgorithm, (unsigned char *)buffer + offset, 1);
	offset ++;

	os_memcpy(&p_private_key->tOID, (unsigned char *)buffer + offset, 1);
	offset ++;
	os_memcpy(&p_private_key->lOID, (unsigned char *)buffer + offset, 1);
	offset ++;
	if (offset + p_private_key->lOID > bufflen)
		return (short)PACK_ERROR;
	os_memcpy(&p_private_key->vOID, (unsigned char *)buffer + offset, p_private_key->lOID);
	offset += p_private_key->lOID;

	os_memcpy(&p_private_key->tSPubkey, (unsigned char *)buffer + offset, 1);
	offset ++;
	os_memcpy(&p_private_key->lSPubkey, (unsigned char *)buffer + offset, 1);
	offset ++;

	os_memcpy(&p_private_key->tPubkey, (unsigned char *)buffer + offset, 1);
	offset ++;
	os_memcpy(&p_private_key->lPubkey, (unsigned char *)buffer + offset, 1);
	offset ++;
	if (offset + p_private_key->lPubkey > bufflen)
		return (short)PACK_ERROR;
	os_memcpy(&p_private_key->vPubkey, (unsigned char *)buffer + offset, p_private_key->lPubkey);
	offset += p_private_key->lPubkey;

    return offset;
}

int ParsePubKey(unsigned char **pBuffer, unsigned char *pMax, unsigned char *pPubKey, unsigned long *pLen)
{
#define TMP_BUF 100
    unsigned char* p = *pBuffer;
    unsigned long len = 0, ParLen;
    unsigned char pTmp[TMP_BUF] = {0};
    if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
    {
        return 1;
    }
    if (ParseSequence(&p, pMax, NULL, NULL, &len, NULL))
    {
        return 1;
    }
    len = TMP_BUF;
    if (ParseOID(&p, pMax, pTmp, &len, &ParLen))
    {
        return 1;
    }
    if (hlpCheckOIDAndParam(pTmp, len, p, ParLen, 1))
    {
        return 1;
    }
    p += ParLen;   
    if ((*p & V_ASN1_PRIMITIVE_TAG) != V_ASN1_BIT_STRING)
    {
        return 1;
    }
    if (ParseString(&p, pMax, NULL, &len) || len != PUBKEY_LEN + 1 + 1)
    {
        return 1;
    }
    if (pPubKey == NULL || *pLen < len - 1)
    {
        *pLen = len;
        return 0;
    }
    if (ParseString(&p, pMax, pTmp, &len))
    {
        return 1;
    }
    if (pTmp[1] != 0x04 || pTmp[0] != 0)
    {
        return 1;
    }
    /* copy unusedbits */
    os_memcpy(&pPubKey[0], &pTmp[1], PUBKEY2_LEN);
    *pBuffer = p;
    *pLen = len - 1;
    return 0;
}

static unsigned char GetBase64Value(unsigned char ch)
{
    if ((ch >= 'A') && (ch <= 'Z')) 
        return ch - 'A'; 
    if ((ch >= 'a') && (ch <= 'z')) 
        return ch - 'a' + 26; 
    if ((ch >= '0') && (ch <= '9')) 
        return ch - '0' + 52; 
    switch (ch) 
	{ 
    case '+': 
        return 62; 
    case '/': 
        return 63; 
    case '=': /* base64 padding */ 
        return 0; 
    default: 
        return 0; 
	} 
}


int Base64Dec(unsigned char *buf,const unsigned char*text,int size)
{
	unsigned char chunk[4];
	int parsenum=0;
	unsigned char* p = buf;

	if(size%4)
		return -1;

	while(size>0)
	{
		chunk[0] = GetBase64Value(text[0]); 
		chunk[1] = GetBase64Value(text[1]); 
		chunk[2] = GetBase64Value(text[2]); 
		chunk[3] = GetBase64Value(text[3]); 
		
		*buf++ = (chunk[0] << 2) | (chunk[1] >> 4); 
		*buf++ = (chunk[1] << 4) | (chunk[2] >> 2); 
		*buf++ = (chunk[2] << 6) | (chunk[3]);
		
		text+=4;
		size-=4;
		parsenum+=3;
	}

	if (0x30 == p[0])
	{
		if (0x82 == p[1])
		{
			parsenum = (p[2]<<8) + p[3] + 4;
		}
		else
		{
			parsenum = p[1] + 2;
		}
	}

	return parsenum;
}

/*ret:0-char of base64,1-\n or \r,-1-unknow char*/
static int getchartype_base64(unsigned char b)
{
	if (	(b>='A'&&b<='Z')
		||	(b>='a'&&b<='z')
		||	(b>='0'&&b<='9')
		||	'+'==b || '/'==b || '='==b)
	{
		return 0;
	}
	else if ('\r'==b || '\n'==b)
	{
		return 1;
	}
	return -1;
}

/*find mark in src*/
static const unsigned char* findmark_mem(const unsigned char* src, int lsrc, const  char* mark, int lmark)
{
	const unsigned char* p = src;
	const unsigned char* pe = src+lsrc;
	if (NULL==src || NULL==mark || lsrc<0 || lmark<0 || lsrc<lmark)
	{
		return NULL;
	}
	pe -= lmark;
	for (; p<=pe; p++)
	{
		if (0 == os_memcmp(p, mark, lmark))
		{
			return p;
		}
	}
	return NULL;
}

/* ---------------------------------------------------------------------------------------
 * [Name]      get_prikey_from_cert
 * [Function]   get information from cert buffer with flag
 * [Input]       const unsigned char *src_cert
 *                  const unsigned char *start_flag
 *                  const unsigned char *end_flag
 * [Output]     int * len 
 * [Return]     unsigned char * 
 *                            NULL  fail
                               !0     success (private key value)                                   
 * [Limitation] NULL
 * ---------------------------------------------------------------------------------------
 */
unsigned char *get_realinfo_from_cert(unsigned char *des, const unsigned char *src_cert, int len, const  char *start_flag, const  char *end_flag)
{
	const unsigned char *p = src_cert;
	const unsigned char *ps  = NULL;
	const unsigned char *pe  = NULL;
	int l0;
	int l1;
	int c = 0;

	if (src_cert == NULL || start_flag == NULL || end_flag == NULL)
	{
		return NULL;
	}
	l0 = strlen((const char*)start_flag);
	l1 = strlen((const char*)end_flag);
	

	ps = findmark_mem(p, len, start_flag, l0);
	pe = findmark_mem(p, len, end_flag, l1);
	if (NULL==ps || NULL==pe || ps>=pe)
	{
	  return NULL;
	}
	
	for (p=ps+l0; p<pe; p++)
	{
		int t = getchartype_base64(*p);
		if (0 == t)
		{
			des[c++] = *p;
		}
	}
	
	return des;
}

void *iwn_x509_get_pubkey(void *cert_st)
{
    unsigned long tmp;
    unsigned long parLen = 0;
    unsigned char tmpOID[100];
    unsigned char *p = NULL;
    unsigned char *pMax = NULL;
	unsigned char *pBAK = NULL;
	tkey *ret = NULL;
	int fail_flag = 0;

    if (cert_st == NULL)
    {
		return (void *)ret;
    }

    p = (unsigned char *)((cert_id  *)cert_st)->data;
    if (p == NULL)
	return (void *)ret;
    pMax = p + ((cert_id  *)cert_st)->length;

    ret = get_buffer(sizeof(tkey));
	if (ret == NULL)
	{
		return (void *)ret;
	}

    do
    {
        /* tbsCertificate */
        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
		fail_flag = 1;
            break;
        }

        /* version */
        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
			fail_flag = 1;
            break;
        }

        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
			fail_flag = 1;
            break;
        }

        if (ParseSequence(&p, pMax, NULL, NULL, &tmp, NULL))
        {
			fail_flag = 1;
            break;
        }

        if (*p != V_X509_V3)    /* only support V3 */
        {
			fail_flag = 1;
            break;
        }
        p += tmp;
        /* sn */
        {

            pBAK = p;
            if (ParseSequence(&p, pMax, NULL, NULL, &tmp, NULL))
            {
				fail_flag = 1;
                break;
            }


            p += tmp;
            if ((unsigned long)(p - pBAK) > 0xff)
            {
				fail_flag = 1;
                break;
            }

        }

        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
			fail_flag = 1;
            break;
        }

        tmp = sizeof(tmpOID);
        if (ParseOID(&p, pMax, tmpOID, &tmp, &parLen))
        {
			fail_flag = 1;
            break;
        }

        if (p + parLen > pMax)
        {
			fail_flag = 1;
            break;
        }

		if (hlpCheckOIDAndParam(tmpOID, tmp, NULL, 0, 0))
        {
			fail_flag = 1;
            break;
        }

        p += parLen;
        /* Issuer */
        if (ParseName(&p, pMax, NULL, &tmp))
        {
			fail_flag = 1;
            break;
        }

        /* validity */
        if (ParseValidity(&p, pMax, NULL))
        {
			fail_flag = 1;
            break;
        }

        /* subject */
        if (ParseName(&p, pMax, NULL, &tmp))
        {
			fail_flag = 1;
            break;
        }

        /* pubkey info */
        tmp = sizeof(ret->data);
        if (ParsePubKey(&p, pMax, ret->data, &tmp))
        {
			wpa_printf(MSG_ERROR,  "iwn_x509_get_pubkey: '%s', '%d' ", __FILE__, __LINE__);
			fail_flag = 1;
            break;
        }
        ret->length = (unsigned short)tmp;

		wpa_printf(MSG_DEBUG, "iwn_x509_get_pubkey: tmp = '%d', '%s', '%d' ", tmp, __FILE__, __LINE__);
			wpa_hexdump(MSG_DEBUG, "get_x509_cert value", ret->data, ret->length);
    } while(0);

	if (fail_flag)
	{
		ret = free_buffer(ret, sizeof(tkey));
	}
    return (void *)ret;
}

void *iwn_x509_get_subject_name(void *cert_st)
{
	unsigned long tmp;
	unsigned long parLen = 0;
	unsigned char tmpOID[100];
	unsigned char *p = NULL;
	unsigned char *pMax = NULL;
	unsigned char *pBAK = NULL;
	byte_data *ret = NULL;
	int fail_flag  = 0;

	if (cert_st == NULL)
	{
		return (void *)ret;
	}

	p = (unsigned char *)((cert_id  *)cert_st)->data;
	if (p == NULL)
		return (void *)ret;
	pMax = p + ((cert_id  *)cert_st)->length;

	ret = get_buffer(sizeof(byte_data));
	if (ret == NULL)
	{
		return (void *)ret;
	}
  
    do
    {
        /* tbsCertificate */
        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
			fail_flag = 1;
            break;
        }
        /* version */
        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
			fail_flag = 1;
            break;
        }
        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
			fail_flag = 1;
            break;
        }
        if (ParseSequence(&p, pMax, NULL, NULL, &tmp, NULL))
        {
			fail_flag = 1;
            break;
        }
        if (*p != V_X509_V3)    /* only support V3 */
        {
			fail_flag = 1;
            break;
        }
        p += tmp;
        /* SN */
        {
            pBAK = p;
            if (ParseSequence(&p, pMax, NULL, NULL, &tmp, NULL))
            {
		fail_flag = 1;
		break;
            }
            p += tmp;
            if ((unsigned long)(p - pBAK) > 0xff)
            {
		fail_flag = 1;
		break;
            }
        }

        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
		fail_flag = 1;
		break;
        }
        tmp = sizeof(tmpOID);
        if (ParseOID(&p, pMax, tmpOID, &tmp, &parLen))
        {
		fail_flag = 1;
		break;
        }
        if (p + parLen > pMax)
        {
		fail_flag = 1;
		break;
        }
	if (hlpCheckOIDAndParam(tmpOID, tmp, NULL, 0, 0))
        {
		fail_flag = 1;
		break;
        }

        p += parLen;
        /* Issuer */
        tmp = sizeof(ret->data);
        if (ParseName(&p, pMax, NULL, &tmp))
        {
		fail_flag = 1;
		break;
        }
        /* validity */
        if (ParseValidity(&p, pMax, NULL))
        {
		fail_flag = 1;
		break;
        }
        /* subject */
        tmp = sizeof(ret->data);
        if (ParseName(&p, pMax, ret->data, &tmp))
        {
		fail_flag = 1;
		break;
        }
        ret->length = (unsigned char)tmp;
        
    } while(0);

	if (fail_flag)
	{
		ret = free_buffer(ret, sizeof(byte_data));
	}
    return (void *)ret;

}

void *iwn_x509_get_serial_number(void *cert_st)
{
    unsigned long tmp;
    unsigned char *p = NULL;
    unsigned char *pMax = NULL;
	unsigned char *pBAK = NULL;
	byte_data *ret = NULL;
	int fail_flag  = 0;

    if (cert_st == NULL)
    {
		return (void *)ret;
    }

    p = (unsigned char *)((cert_id  *)cert_st)->data;
    if (p == NULL)
	return (void *)ret;
    pMax = p + ((cert_id  *)cert_st)->length;

    ret = get_buffer(sizeof(byte_data));
	if (ret == NULL)
	{
		return (void *)ret;
	}
  
    do
    {
        /* tbsCertificate */
        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
			fail_flag = 1;
            break;
        }
        /* version */
        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
			fail_flag = 1;
            break;
        }
        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
			fail_flag = 1;
            break;
        }
        if (ParseSequence(&p, pMax, NULL, NULL, &tmp, NULL))
        {
			fail_flag = 1;
            break;
        }
        if (*p != V_X509_V3)    /* only support V3 */
        {
			fail_flag = 1;
            break;
        }
        p += tmp;
        {
            pBAK = p;
            if (ParseSequence(&p, pMax, NULL, NULL, &tmp, NULL))
            {
				fail_flag = 1;
                break;
            }
            p += tmp;
            if ((unsigned long)(p - pBAK) > 0xff)
            {
				fail_flag = 1;
                break;
            }
            os_memcpy(ret->data, pBAK, p - pBAK);
            ret->length = (unsigned char)(p - pBAK);
        }
    } while(0);

	if (fail_flag)
	{
		ret = free_buffer(ret, sizeof(byte_data));
	}
    return (void *)ret;
}

void *iwn_x509_get_issuer_name(void *cert_st)
{
    unsigned long tmp;
    unsigned long parLen = 0;
    unsigned char tmpOID[100];
    unsigned char *p       = NULL;
    unsigned char *pMax = NULL;
    unsigned char *pBAK = NULL;
    byte_data *ret          = NULL;
    int fail_flag  = 0;

    if (cert_st == NULL)
    {
		return (void *)ret;
    }

    p = (unsigned char *)((cert_id  *)cert_st)->data;
    if (p == NULL)
	return (void *)ret;
    pMax = p + ((cert_id  *)cert_st)->length;
	
    ret = get_buffer(sizeof(byte_data));
	if (ret == NULL)
	{
		return (void *)ret;
	}
    do
    {
        /* tbsCertificate */
        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
			fail_flag = 1;
            break;
        }
        /* version */
        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
			fail_flag = 1;
            break;
        }
        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
			fail_flag = 1;
            break;
        }
        if (ParseSequence(&p, pMax, NULL, NULL, &tmp, NULL))
        {
			fail_flag = 1;
            break;
        }
        if (*p != V_X509_V3)    /* only support V3 */
        {
			fail_flag = 1;
            break;
        }
        p += tmp;
        /* SN */
        {
            pBAK = p;
            if (ParseSequence(&p, pMax, NULL, NULL, &tmp, NULL))
            {
				fail_flag = 1;
                break;
            }
            p += tmp;
            if ((unsigned long)(p - pBAK) > 0xff)
            {
				fail_flag = 1;
                break;
            }
        }

		/* signature algorithm */
        if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
        {
			fail_flag = 1;
            break;
        }
        tmp = sizeof(tmpOID);
        if (ParseOID(&p, pMax, tmpOID, &tmp, &parLen))
        {
			fail_flag = 1;
            break;
        }
        if (p + parLen > pMax)
        {
			fail_flag = 1;
            break;
        }
		if (hlpCheckOIDAndParam(tmpOID, tmp, NULL, 0, 0))
        {
			fail_flag = 1;
            break;
        }

        p += parLen;
        /* Issuer */
        tmp = sizeof(ret->data);
        if (ParseName(&p, pMax, ret->data, &tmp))
        {
			fail_flag = 1;
            break;
        }
        ret->length = (unsigned char)tmp;
        
    } while(0);

	if (fail_flag)
	{
		ret = free_buffer(ret, sizeof(byte_data));
	}
    return (void *)ret;
}



int iwn_x509_get_sign(void *cert_st, unsigned char *out, int out_len)
{
       unsigned char *p       = NULL;
	unsigned char *pMax = NULL;
       unsigned long tmp = 0;
	int tmp_len = 0;
	int ret = -1;	

       if (cert_st == NULL ||out == NULL || out_len < SIGN_LEN)
       {
		return ret;
       }

	p = (unsigned char *)((cert_id  *)cert_st)->data;
	if (p == NULL)
		return ret;
	pMax = p + ((cert_id  *)cert_st)->length;
 
	do
	{
		/* tbsCertificate */
	       if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
	       {
	            break;
	       }
	       if (ParseSequence(&p, pMax, NULL, NULL, &tmp, NULL))
	       {
	            break;
	       }
		/* skip the cert main informations */
		p += tmp;
		tmp = 0;

	       if (ParseSequence(&p, pMax, NULL, NULL, &tmp, NULL))
	       {
	            break;
	       }
		/* skip the sign arithmetic */
		p += tmp;
		tmp = 0;

		/* parse sign value -------start--------*/
	       if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
	       {
	            break;
	       }

		/* skip the compress flag */
		p++;

	       if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
	       {
	            break;
	       }

		/* first parts */
	       if (ParseSequence(&p, pMax, NULL, NULL, &tmp, NULL))
	       {
	            break;
	       }

		if (tmp > 0x18 && *p == 0x00)
		{
			/* skip 0x00 */
			p++;
			tmp_len = (int)tmp - 1;
			os_memcpy(out, p, tmp_len);
		}
		else
		{
			if (tmp == 0x17)
			{	
				p--;
				tmp_len = (int)tmp + 1;
				os_memcpy(out, p, tmp_len);
				out[0] = 0x00;
			}
			else
			{
				tmp_len = (int)tmp;
				os_memcpy(out, p, tmp_len);
			}
		}
		p += tmp_len;
		tmp = 0;

		/* second parts */
	       if (ParseSequence(&p, pMax, NULL, NULL, &tmp, NULL))
	       {
	            break;
	       }

		if (tmp > 0x18 && *p == 0x00)
		{
			/* skip 0x00 */
			p++;
			os_memcpy(out+tmp_len, p, tmp - 1);
		}
		else
		{
			if (tmp == 0x17)
			{	
				p--;
				os_memcpy(out+tmp_len, p, tmp + 1);
				out[tmp_len] = 0x00;
			}
			else
			{
				os_memcpy(out+tmp_len, p, tmp_len);
			}
		}
		/* parse sign value -------end--------*/

		/* sccess return */
		ret = 0;

	}while(0);

	wpa_hexdump(MSG_DEBUG, "iwn_x509_get_sign", out , 48);
	return ret;
}

int iwn_x509_get_sign_inlen(void *cert_st)
{
       unsigned char *p       = NULL;
	unsigned char *pMax = NULL;
	unsigned char *pBAK = NULL;
       unsigned long tmp;
	int ret_len = 0;	

       if (cert_st == NULL)
       {
		return ret_len;
       }

	p = (unsigned char *)((cert_id  *)cert_st)->data;
	if (p == NULL)
		return ret_len;
	pMax = p + ((cert_id  *)cert_st)->length;

	do
	{
		/* tbsCertificate */
	       if (ParseSequence(&p, pMax, NULL, NULL, NULL, NULL))
	       {
	            break;
	       }
		pBAK = p;	
	       if (ParseSequence(&p, pMax, NULL, NULL, &tmp, NULL))
	       {
	            break;
	       }
		/* sign(input) availability length */
		ret_len = ((int)tmp + (p - pBAK));
	}while(0);

	wpa_printf(MSG_DEBUG,"iwn_x509_get_sign_inlen: '%d'", ret_len);
	return ret_len;
}








/*----------orgin in cert_info.c-----------------*/

#define NUM_OF_CERTS 3
static  const struct cert_obj_st_t  *cert_objs[NUM_OF_CERTS];

void cert_obj_register( struct cert_obj_st_t *cert_obj)
{
  if(cert_obj->cert_type >= NUM_OF_CERTS)
	{
		wpa_printf(MSG_DEBUG,"%s: certificate %s has an invalid cert_index %u\n",
			__func__, cert_obj->cert_name,  cert_obj->cert_type);
		return;
	}


	if (((cert_objs[cert_obj->cert_type]) )!= NULL && ((cert_objs[cert_obj->cert_type]) != cert_obj))
	{
		wpa_printf(MSG_DEBUG,"%s: certificate object %s registered with a different template\n",
			__func__, cert_obj->cert_name);
		return;
	}

	cert_obj->cert_bin= (struct cert_bin_t  *)get_buffer(sizeof(struct cert_bin_t));
	if(cert_obj->cert_bin == NULL)
	{
		return ;
	}
	cert_obj->asu_cert_bin= (struct cert_bin_t  *)get_buffer(sizeof(struct cert_bin_t));
	if(cert_obj->asu_cert_bin == NULL)
	{
		return ;
	}
	
	cert_objs[cert_obj->cert_type] = cert_obj;
	wapi_cert.cert_info.config.used_cert = (unsigned short)cert_obj->cert_type;
	return;
}


void cert_obj_unregister(const struct cert_obj_st_t *cert_obj)
{
	int index = cert_obj->cert_type;
	
	/*struct cert_obj_st_t *obj = NULL;*/
	if(cert_obj->cert_type > 3)
	{
		wpa_printf(MSG_ERROR,"%s: certificate %s has an invalid cert_index %u\n",
			__func__, cert_obj->cert_name,  cert_obj->cert_type);
		return;
	}

	if ((cert_objs[cert_obj->cert_type] != NULL) && (cert_objs[cert_obj->cert_type] != cert_obj))
	{
		wpa_printf(MSG_ERROR,"cert_obj address 	%p\n", cert_obj);
		wpa_printf(MSG_ERROR,"obj address 		%p\n", cert_objs[cert_obj->cert_type]);
		wpa_printf(MSG_ERROR,"%s: certificate object %s registered with a different template\n",
			__func__, cert_obj->cert_name);
		return;
	}
	free_buffer((cert_objs[cert_obj->cert_type]->cert_bin), sizeof(struct cert_bin_t));
	free_buffer((cert_objs[cert_obj->cert_type]->asu_cert_bin), sizeof(struct cert_bin_t));

	cert_objs[index] = NULL;
}


const struct cert_obj_st_t *get_cert_obj(unsigned short index)
{
	if(index < 3)
	{
		return cert_objs[index];
	}
	else
		return NULL;
}


int x509_verify_cert(struct cert_obj_st_t *cert_obj)
{
	tkey *asu_pkey = cert_obj->asu_pubkey;
	unsigned char asu_sign_value[SIGN_LEN+1];
	unsigned char user_sign_value[SIGN_LEN+1];
	int    ret= -1;

	cert_id *asu_cert  = (cert_id *)(cert_obj->asu_cert_st);
	cert_id *user_cert = (cert_id *)(cert_obj->user_cert_st);

	os_memset(asu_sign_value, 0, sizeof(asu_sign_value));
	if (iwn_x509_get_sign(asu_cert, asu_sign_value, sizeof(asu_sign_value)) < 0)
		return ret;

	os_memset(user_sign_value, 0, sizeof(user_sign_value));
	if (iwn_x509_get_sign(user_cert, user_sign_value, sizeof(user_sign_value)) < 0)
		return ret;

	if ((*cert_obj->verify)(asu_pkey->data, asu_pkey->length, asu_cert->data+4, iwn_x509_get_sign_inlen(asu_cert), 
		asu_sign_value, SIGN_LEN) <= 0)
	{
		wpa_printf(MSG_ERROR,"in %s X509_verify(asu_cert) failure\n", __func__);
		goto err;
	}
	else if ((*cert_obj->verify)(asu_pkey->data, asu_pkey->length, user_cert->data+4, iwn_x509_get_sign_inlen(user_cert),
		user_sign_value, SIGN_LEN) <= 0)
	{
		wpa_printf(MSG_ERROR,"in %s X509_verify(user_cert) failure\n", __func__);
		goto err;
	}
	ret = 0;
	
err:
	return ret;
}

void x509_free_obj_data(struct cert_obj_st_t *cert_obj)
{	
	if(cert_obj->asu_cert_st != NULL)
	{
		wpa_printf(MSG_DEBUG,"in %s:%d free asu_cert_st\n", __func__, __LINE__);
		free_buffer(cert_obj->asu_cert_st, 0);
		cert_obj->asu_cert_st = NULL;
	}
	if(cert_obj->user_cert_st != NULL)
	{
		wpa_printf(MSG_DEBUG,"in %s:%d free user_cert_st\n", __func__, __LINE__);
		free_buffer(cert_obj->user_cert_st, 0);
		cert_obj->user_cert_st = NULL;
	}
	if(cert_obj->asu_pubkey != NULL)
	{
		wpa_printf(MSG_DEBUG,"in %s:%d free asu_pubkey\n", __func__, __LINE__);
		free_buffer(cert_obj->asu_pubkey, sizeof(cert_obj->asu_pubkey));
		cert_obj->asu_pubkey = NULL;
	}
	if(cert_obj->private_key != NULL)
	{
		wpa_printf(MSG_DEBUG,"in %s:%d free private_key\n", __func__, __LINE__);
		free_buffer(cert_obj->private_key, sizeof(cert_obj->private_key));
		cert_obj->private_key = NULL;
	}
	if(cert_obj->cert_bin->data != NULL)
	{
		wpa_printf(MSG_DEBUG,"in %s:%d free cert_bin->data\n", __func__, __LINE__);
		os_memset(cert_obj->cert_bin->data, 0,  cert_obj->cert_bin->length);
		free_buffer(cert_obj->cert_bin->data, 0);
		cert_obj->cert_bin->data = NULL;
	}
}

int get_x509_cert(struct cert_obj_st_t *cert_obj)
{
       	unsigned char *p  = NULL;
	static unsigned char buffer[2048]={0,};
	static unsigned char tmp[2048]={0,};
	static private_key    prikey;
	//const CNTAP_PARA *cert_info = (const CNTAP_PARA *)param;
	int ret =  0;
	int len = 0;

	wpa_printf(MSG_DEBUG, "HELLLOOOOO\n");

	if (cert_obj == NULL)
	{
		ret = -1;
		goto error;
	}

	wpa_printf(MSG_DEBUG,  "get_x509_cert: '%s', '%d' ", __FILE__, __LINE__);

       /* parse the private key start */
       os_memset(tmp, 0, sizeof(tmp));
	p = get_realinfo_from_cert(tmp, wapi_cert.user, strlen((char *)wapi_cert.user), PEM_STRING_PRIKEYS,  PEM_STRING_PRIKEYE);
	if (p == NULL )
	{
		wpa_printf(MSG_DEBUG,"get prikey error. \n");
		ret = -1;
		goto error;
	}

       len = strlen((char *)tmp);
	os_memset(buffer, 0, sizeof(buffer));
	/* decode base64 */
	if ((len = Base64Dec(buffer, tmp, len)) < 0)
	{
		wpa_printf(MSG_DEBUG,"Base64 decode prikey error\n");
		ret = -1;
		goto error;
	}

	/* parse der */
	unpack_private_key(&prikey, buffer, (short)len);

	cert_obj->private_key = get_buffer(sizeof(tkey));
	if (cert_obj->private_key == NULL)
	{
		wpa_printf(MSG_DEBUG,"malloc prikey fail \n");
		ret = -1;
		goto error;
	}
	os_memcpy(cert_obj->private_key->data, prikey.vPrivateKey, prikey.lPrivateKey);
	cert_obj->private_key->length = prikey.lPrivateKey;
       /* parse the private key end */


	/* parse the user cert start */
	os_memset(tmp, 0, sizeof(tmp));
	p = get_realinfo_from_cert(tmp, wapi_cert.user, strlen((char *)wapi_cert.user), PEM_STRING_X509_CERTS,  PEM_STRING2_X509_CERTE);
	if (p == NULL )
	{
		wpa_printf(MSG_DEBUG,"get user cert error. \n");
		ret = -1;
		goto error;
	}

	len = strlen((char *)tmp);
	os_memset(buffer, 0, sizeof(buffer));
	/* decode base64 */
	if ((len = Base64Dec(buffer, tmp, len)) < 0)
	{
		wpa_printf(MSG_DEBUG,"Base64 decode user cert error\n");
		ret = -1;
		goto error;
	}

	wpa_printf(MSG_DEBUG,"user cert infor(user_cert)  '%d'\n", len);
	wpa_hexdump(MSG_DEBUG, "user cert infor", buffer , len);

       /* get buffer */
	cert_obj->user_cert_st = get_buffer(sizeof(cert_id));
	cert_obj->cert_bin->data = get_buffer(len + 1);
	if (cert_obj->user_cert_st == NULL
		|| cert_obj->cert_bin->data == NULL)
	{
		wpa_printf(MSG_DEBUG,"malloc user cert error. \n");
		ret = -1;
		goto error;
	}

       /* save information */
	os_memcpy(((cert_id *)(cert_obj->user_cert_st))->data, buffer, len);
	((cert_id *)(cert_obj->user_cert_st))->length = (unsigned short)len;		
	os_memcpy(cert_obj->cert_bin->data, buffer, len);
	cert_obj->cert_bin->length = (unsigned short)len;
	/* parse the user cert end */            


	/* parse the asu cert start */
       os_memset(tmp, 0, sizeof(tmp));
	p = get_realinfo_from_cert(tmp, wapi_cert.asu, strlen((char *)wapi_cert.asu), PEM_STRING_X509_CERTS,  PEM_STRING2_X509_CERTE);
	if (p == NULL )
	{
		wpa_printf(MSG_DEBUG,"get asu cert error. \n");
		ret = -1;
		goto error;	
	}

	len = strlen((char *)tmp);
	os_memset(buffer, 0, sizeof(buffer));
	/* decode base64 */
	if ((len = Base64Dec(buffer, tmp, len)) < 0)
	{
		wpa_printf(MSG_DEBUG,"Base64 decode asu cert error\n");
		ret = -1;
		goto error;
	}

	wpa_printf(MSG_DEBUG,"asu cert infor(asu_cert)  '%d'\n", len);
	wpa_hexdump(MSG_DEBUG, "asu cert infor", buffer , len);

	cert_obj->asu_cert_st = get_buffer(sizeof(cert_id));
	if (cert_obj->asu_cert_st == NULL)
	{
		wpa_printf(MSG_DEBUG,"malloc asu cert error. \n");
		ret = -1;
		goto error;
	}

	os_memcpy(((cert_id *)(cert_obj->asu_cert_st))->data, buffer, len);
	((cert_id *)(cert_obj->asu_cert_st))->length = (unsigned short)len;
	

	/* get asu public key */
	cert_obj->asu_pubkey = (*cert_obj->get_public_key)(cert_obj->asu_cert_st);
	if (cert_obj->asu_pubkey == NULL)
	{
		wpa_printf(MSG_DEBUG,"get asu public key fial. \n");
		ret = -1;
		goto error;
	}	
	/* parse the asu cert end */

	/* Verify the public key and private key  */
	wpa_printf(MSG_DEBUG,  "get_x509_cert public:  '%d' ", prikey.lPubkey);
	wpa_printf(MSG_DEBUG,  "get_x509_cert private: '%d' ", prikey.lPrivateKey);

	/* "1" skip one invalid byte */
	if (!(*cert_obj->verify_key)(prikey.vPubkey+1, prikey.lPubkey-1, prikey.vPrivateKey, prikey.lPrivateKey))
	{
		wpa_printf(MSG_DEBUG,"verify_key the public_private_key fail. \n");
		ret = -1;
		goto error;
	}

    /* Verify the asu and user cert */
	ret = x509_verify_cert(cert_obj);
	if(ret != 0)
	{
		ret = -1;
		goto error;
	}
	ret = 0;

error:	
	wpa_printf(MSG_DEBUG,"get_x509_cert over\n");
	return ret;
}

static int load_x509(const struct cert_obj_st_t *cert_obj)
{
	return  get_x509_cert((struct cert_obj_st_t *)cert_obj);
}

int init_cert(void)
{
	int ret =  -1;
	unsigned short  index = wapi_cert.cert_info.config.used_cert;    

	x509_free_obj_data((struct cert_obj_st_t *)cert_objs[index]);
	ret = load_x509(cert_objs[index]);

	/* success */
	if(ret == 0)
	{
		wapi_cert.cert_info.asue_cert_obj = (struct cert_obj_st_t *)cert_objs[index];
		wpa_printf(MSG_DEBUG, "WAPI: in %s:%d,length=%d", __func__, __LINE__,wapi_cert.cert_info.asue_cert_obj->cert_bin->length);

		wapi_fixdata_id_by_ident(wapi_cert.cert_info.asue_cert_obj->user_cert_st, 
                                 (wai_fixdata_id*)&(wapi_cert.asue_id), 
                                 wapi_cert.cert_info.config.used_cert);

		wapi_cert.has_cert = 1;
	}
	return ret;
}

int cleanup_cert(void)
{
	int ret =  -1;
	unsigned short  index =  wapi_cert.cert_info.config.used_cert;    
	x509_free_obj_data((struct cert_obj_st_t *)cert_objs[index]);
	return ret;
}


/*------------orgin in x509_cert.c-------------------*/

/* ---------------------------------------------------------------------------------------
 * [Name]      x509_ecc_verify
 * [Function]   verify the sign information with the public key
 * [Input]       const unsigned char *pub_s
 *                  int pub_sl,
 *                  ......
 * [Output]     NULL
 * [Return]     int
 *                              1   success
 *                              0   fail
 * [Limitation] NULL
 * ---------------------------------------------------------------------------------------
 */
int   x509_ecc_verify(const unsigned char *pub_s, int pub_sl, unsigned char *in ,  int in_len, unsigned char *sign,int sign_len)
{
  int ret = 0;
  
  if (pub_s == NULL || pub_sl <= 0 || in == NULL || in_len <= 0 || sign == NULL || sign_len <= 0)
    {
      return ret;
    }
  else
    {
      ret = ecc192_verify(pub_s, in, in_len, sign, sign_len);
    }

  if (ret <= 0)
    ret = 0;
  else
    ret = 1;

  return ret;
}


/* ---------------------------------------------------------------------------------------
 * [Name]      x509_ecc_sign
 * [Function]   sign with the private key
 * [Input]       const unsigned char *priv_s
 *                  int priv_sl,
 *                  const unsigned char *int,
 *                  int in_len
 * [Output]     unsigned char *out 
 * [Return]     int
 *                              > 0   success
 *                              0      fail
 * [Limitation] NULL
 * ---------------------------------------------------------------------------------------
 */
int x509_ecc_sign(const unsigned char *priv_s, int priv_sl, const unsigned char * in, int in_len, unsigned char *out)
{
  priv_sl = priv_sl;/*disable warnning*/
  if (priv_s == NULL || in == NULL || in_len <= 0 || out == NULL)
    {
      return 0;
    }
  else
    {
      return ecc192_sign(priv_s, in, in_len, out);
    }
}



/* ---------------------------------------------------------------------------------------
 * [Name]      x509_ecc_verify_key
 * [Function]   verify the public key and the private key
 * [Input]       const unsigned char *pub_s
 *                  int pub_sl,
 *                  const unsigned char *priv_s
 *                  int priv_sl
 * [Output]     NULL
 * [Return]     int
 *                              1   success
 *                              0   fail
 * [Limitation] NULL
 * ---------------------------------------------------------------------------------------
 */
int x509_ecc_verify_key(const unsigned char *pub_s, int pub_sl, const unsigned char *priv_s, int priv_sl)
{

#define EC962_SIGN_LEN 48
  unsigned char data[] = "123456abcd";
  
  unsigned char sign[EC962_SIGN_LEN+1];
  int ret = 0;

  if (priv_s == NULL || pub_sl <= 0  || priv_s == NULL || priv_sl <= 0)
    {
      return 0;
    }
 
  os_memset(sign, 0, sizeof(sign));
  ret = ecc192_sign(priv_s, data, strlen((char*)data), sign);
  if (ret != EC962_SIGN_LEN)
    {
      printf("ecc192_sign call fail \n");
      ret = 0;
      return ret;
    }

  ret = ecc192_verify(pub_s, data, strlen((char*)data), sign, EC962_SIGN_LEN);
  if (ret <= 0)
    {
      printf("ecc192_verify call fail \n");
      ret = 0;
    }

  return ret;

}

static void *X509_wapi_get_subject_name(void *cert_st)
{
	return iwn_x509_get_subject_name(cert_st);
}

static void *X509_wapi_get_issuer_name(void *cert_st)
{
	return iwn_x509_get_issuer_name(cert_st);
}

static void *X509_wapi_get_serial_number(void *cert_st)
{
	return iwn_x509_get_serial_number(cert_st);

}

static void *X509_wapi_get_pubkey(void *cert_st)
{
	return iwn_x509_get_pubkey(cert_st);
}

static struct cert_obj_st_t cert_obj_x509;

static void init_struct_x509(void)
{
	cert_obj_x509.cert_type		= CERT_OBJ_X509;
	cert_obj_x509.cert_name		= "x509v3";
	cert_obj_x509.asu_cert_st	= NULL;
	cert_obj_x509.asu_pubkey	= NULL;
	cert_obj_x509.user_cert_st	= NULL;
	cert_obj_x509.private_key	= NULL;
	cert_obj_x509.cert_bin		= NULL;
	
	cert_obj_x509.get_public_key	= X509_wapi_get_pubkey;
	cert_obj_x509.get_subject_name	= X509_wapi_get_subject_name;
	cert_obj_x509.get_issuer_name	= X509_wapi_get_issuer_name;
	cert_obj_x509.get_serial_number	= X509_wapi_get_serial_number;
	cert_obj_x509.verify_key = x509_ecc_verify_key;
	cert_obj_x509.sign	= x509_ecc_sign;
	cert_obj_x509.verify = x509_ecc_verify;
}

int X509_init(void)
{
	init_struct_x509();
	cert_obj_register(&cert_obj_x509);
	return 0;
}
void X509_exit(void)
{
	cert_obj_unregister(&cert_obj_x509);
}
