/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997        Gabor Kincses <gabor@acm.org>
 *               1997 - 2001 Brian Somers <brian@Awfulhak.org>
 *          based on work by Eric Rosenquist
 *                           Strata Software Limited.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <ctype.h>
#ifdef __FreeBSD__
#include <openssl/des.h>
#include <sha.h>
#else
#include <sys/types.h>
#include <stdlib.h>
#ifdef __NetBSD__
#include <openssl/des.h>
#else
#include <des.h>
#endif
#include <openssl/sha.h>
#endif
#include <md4.h>
#include <string.h>

#include "chap_ms.h"

/*
 * Documentation & specifications:
 *
 * MS-CHAP (CHAP80)	rfc2433
 * MS-CHAP-V2 (CHAP81)	rfc2759
 * MPPE key management	draft-ietf-pppext-mppe-keys-02.txt
 */

static char SHA1_Pad1[40] =
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static char SHA1_Pad2[40] =
  {0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2,
   0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2,
   0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2,
   0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2, 0xF2};

/* unused, for documentation only */
/* only NTResp is filled in for FreeBSD */
struct MS_ChapResponse {
    u_char LANManResp[24];
    u_char NTResp[24];
    u_char UseNT;	/* If 1, ignore the LANMan response field */
};

static u_char
Get7Bits(u_char *input, int startBit)
{
    register unsigned int	word;

    word  = (unsigned)input[startBit / 8] << 8;
    word |= (unsigned)input[startBit / 8 + 1];

    word >>= 15 - (startBit % 8 + 7);

    return word & 0xFE;
}

/* IN  56 bit DES key missing parity bits
   OUT 64 bit DES key with parity bits added */
static void
MakeKey(u_char *key, u_char *des_key)
{
    des_key[0] = Get7Bits(key,  0);
    des_key[1] = Get7Bits(key,  7);
    des_key[2] = Get7Bits(key, 14);
    des_key[3] = Get7Bits(key, 21);
    des_key[4] = Get7Bits(key, 28);
    des_key[5] = Get7Bits(key, 35);
    des_key[6] = Get7Bits(key, 42);
    des_key[7] = Get7Bits(key, 49);

    DES_set_odd_parity((DES_cblock *)des_key);
}

static void /* IN 8 octets IN 7 octest OUT 8 octets */
DesEncrypt(u_char *clear, u_char *key, u_char *cipher)
{
    DES_cblock		des_key;
    DES_key_schedule	key_schedule;

    MakeKey(key, des_key);
    DES_set_key(&des_key, &key_schedule);
    DES_ecb_encrypt((DES_cblock *)clear, (DES_cblock *)cipher, &key_schedule, 1);
}

static void      /* IN 8 octets      IN 16 octets     OUT 24 octets */
ChallengeResponse(u_char *challenge, u_char *pwHash, u_char *response)
{
    char    ZPasswordHash[21];

    memset(ZPasswordHash, '\0', sizeof ZPasswordHash);
    memcpy(ZPasswordHash, pwHash, 16);

    DesEncrypt(challenge, ZPasswordHash +  0, response + 0);
    DesEncrypt(challenge, ZPasswordHash +  7, response + 8);
    DesEncrypt(challenge, ZPasswordHash + 14, response + 16);
}

void
NtPasswordHash(char *key, int keylen, char *hash)
{
  MD4_CTX MD4context;

  MD4Init(&MD4context);
  MD4Update(&MD4context, key, keylen);
  MD4Final(hash, &MD4context);
}

void
HashNtPasswordHash(char *hash, char *hashhash)
{
  MD4_CTX MD4context;

  MD4Init(&MD4context);
  MD4Update(&MD4context, hash, 16);
  MD4Final(hashhash, &MD4context);
}

static void
ChallengeHash(char *PeerChallenge, char *AuthenticatorChallenge,
              char *UserName, char *Challenge)
{
  SHA_CTX Context;
  char Digest[SHA_DIGEST_LENGTH];
  char *Name;

  Name = strrchr(UserName, '\\');
  if(NULL == Name)
    Name = UserName;
  else
    Name++;

  SHA1_Init(&Context);

  SHA1_Update(&Context, PeerChallenge, 16);
  SHA1_Update(&Context, AuthenticatorChallenge, 16);
  SHA1_Update(&Context, Name, strlen(Name));

  SHA1_Final(Digest, &Context);
  memcpy(Challenge, Digest, 8);
}

void
GenerateNTResponse(char *AuthenticatorChallenge, char *PeerChallenge,
                   char *UserName, char *Password,
                   int PasswordLen, char *Response)
{
  char Challenge[8];
  char PasswordHash[16];

  ChallengeHash(PeerChallenge, AuthenticatorChallenge, UserName, Challenge);
  NtPasswordHash(Password, PasswordLen, PasswordHash);
  ChallengeResponse(Challenge, PasswordHash, Response);
}

#ifndef __FreeBSD__
#define LENGTH 20
static char *
SHA1_End(SHA_CTX *ctx, char *buf)
{
    int i;
    unsigned char digest[LENGTH];
    static const char hex[]="0123456789abcdef";

    if (!buf)
        buf = malloc(2*LENGTH + 1);
    if (!buf)
        return 0;
    SHA1_Final(digest, ctx);
    for (i = 0; i < LENGTH; i++) {
        buf[i+i] = hex[digest[i] >> 4];
        buf[i+i+1] = hex[digest[i] & 0x0f];
    }
    buf[i+i] = '\0';
    return buf;
}
#endif

void
GenerateAuthenticatorResponse(char *Password, int PasswordLen,
                              char *NTResponse, char *PeerChallenge,
                              char *AuthenticatorChallenge, char *UserName,
                              char *AuthenticatorResponse)
{
  SHA_CTX Context;
  char PasswordHash[16];
  char PasswordHashHash[16];
  char Challenge[8];
  u_char Digest[SHA_DIGEST_LENGTH];
  int i;

      /*
       * "Magic" constants used in response generation
       */
  char Magic1[39] =
         {0x4D, 0x61, 0x67, 0x69, 0x63, 0x20, 0x73, 0x65, 0x72, 0x76,
          0x65, 0x72, 0x20, 0x74, 0x6F, 0x20, 0x63, 0x6C, 0x69, 0x65,
          0x6E, 0x74, 0x20, 0x73, 0x69, 0x67, 0x6E, 0x69, 0x6E, 0x67,
          0x20, 0x63, 0x6F, 0x6E, 0x73, 0x74, 0x61, 0x6E, 0x74};


  char Magic2[41] =
         {0x50, 0x61, 0x64, 0x20, 0x74, 0x6F, 0x20, 0x6D, 0x61, 0x6B,
          0x65, 0x20, 0x69, 0x74, 0x20, 0x64, 0x6F, 0x20, 0x6D, 0x6F,
          0x72, 0x65, 0x20, 0x74, 0x68, 0x61, 0x6E, 0x20, 0x6F, 0x6E,
          0x65, 0x20, 0x69, 0x74, 0x65, 0x72, 0x61, 0x74, 0x69, 0x6F,
          0x6E};
      /*
       * Hash the password with MD4
       */
  NtPasswordHash(Password, PasswordLen, PasswordHash);
      /*
       * Now hash the hash
       */
  HashNtPasswordHash(PasswordHash, PasswordHashHash);

  SHA1_Init(&Context);
  SHA1_Update(&Context, PasswordHashHash, 16);
  SHA1_Update(&Context, NTResponse, 24);
  SHA1_Update(&Context, Magic1, 39);
  SHA1_Final(Digest, &Context);
  ChallengeHash(PeerChallenge, AuthenticatorChallenge, UserName, Challenge);
  SHA1_Init(&Context);
  SHA1_Update(&Context, Digest, 20);
  SHA1_Update(&Context, Challenge, 8);
  SHA1_Update(&Context, Magic2, 41);

      /*
       * Encode the value of 'Digest' as "S=" followed by
       * 40 ASCII hexadecimal digits and return it in
       * AuthenticatorResponse.
       * For example,
       *   "S=0123456789ABCDEF0123456789ABCDEF01234567"
       */
  AuthenticatorResponse[0] = 'S';
  AuthenticatorResponse[1] = '=';
  SHA1_End(&Context, AuthenticatorResponse + 2);
  for (i=2; i<42; i++)
    AuthenticatorResponse[i] = toupper(AuthenticatorResponse[i]);

}

void
GetMasterKey(char *PasswordHashHash, char *NTResponse, char *MasterKey)
{
  char Digest[SHA_DIGEST_LENGTH];
  SHA_CTX Context;
  static char Magic1[27] =
      {0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x74,
       0x68, 0x65, 0x20, 0x4d, 0x50, 0x50, 0x45, 0x20, 0x4d,
       0x61, 0x73, 0x74, 0x65, 0x72, 0x20, 0x4b, 0x65, 0x79};

  SHA1_Init(&Context);
  SHA1_Update(&Context, PasswordHashHash, 16);
  SHA1_Update(&Context, NTResponse, 24);
  SHA1_Update(&Context, Magic1, 27);
  SHA1_Final(Digest, &Context);
  memcpy(MasterKey, Digest, 16);
}

void
GetAsymetricStartKey(char *MasterKey, char *SessionKey, int SessionKeyLength,
                     int IsSend, int IsServer)
{
  char Digest[SHA_DIGEST_LENGTH];
  SHA_CTX Context;
  char *s;

  static char Magic2[84] =
      {0x4f, 0x6e, 0x20, 0x74, 0x68, 0x65, 0x20, 0x63, 0x6c, 0x69,
       0x65, 0x6e, 0x74, 0x20, 0x73, 0x69, 0x64, 0x65, 0x2c, 0x20,
       0x74, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x74, 0x68,
       0x65, 0x20, 0x73, 0x65, 0x6e, 0x64, 0x20, 0x6b, 0x65, 0x79,
       0x3b, 0x20, 0x6f, 0x6e, 0x20, 0x74, 0x68, 0x65, 0x20, 0x73,
       0x65, 0x72, 0x76, 0x65, 0x72, 0x20, 0x73, 0x69, 0x64, 0x65,
       0x2c, 0x20, 0x69, 0x74, 0x20, 0x69, 0x73, 0x20, 0x74, 0x68,
       0x65, 0x20, 0x72, 0x65, 0x63, 0x65, 0x69, 0x76, 0x65, 0x20,
       0x6b, 0x65, 0x79, 0x2e};

  static char Magic3[84] =
      {0x4f, 0x6e, 0x20, 0x74, 0x68, 0x65, 0x20, 0x63, 0x6c, 0x69,
       0x65, 0x6e, 0x74, 0x20, 0x73, 0x69, 0x64, 0x65, 0x2c, 0x20,
       0x74, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x74, 0x68,
       0x65, 0x20, 0x72, 0x65, 0x63, 0x65, 0x69, 0x76, 0x65, 0x20,
       0x6b, 0x65, 0x79, 0x3b, 0x20, 0x6f, 0x6e, 0x20, 0x74, 0x68,
       0x65, 0x20, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x20, 0x73,
       0x69, 0x64, 0x65, 0x2c, 0x20, 0x69, 0x74, 0x20, 0x69, 0x73,
       0x20, 0x74, 0x68, 0x65, 0x20, 0x73, 0x65, 0x6e, 0x64, 0x20,
       0x6b, 0x65, 0x79, 0x2e};

  if (IsSend) {
     if (IsServer) {
        s = Magic3;
     } else {
        s = Magic2;
     }
  } else {
     if (IsServer) {
        s = Magic2;
     } else {
        s = Magic3;
     }
  }

  SHA1_Init(&Context);
  SHA1_Update(&Context, MasterKey, 16);
  SHA1_Update(&Context, SHA1_Pad1, 40);
  SHA1_Update(&Context, s, 84);
  SHA1_Update(&Context, SHA1_Pad2, 40);
  SHA1_Final(Digest, &Context);

  memcpy(SessionKey, Digest, SessionKeyLength);
}

void
GetNewKeyFromSHA(char *StartKey, char *SessionKey, long SessionKeyLength,
                 char *InterimKey)
{
  SHA_CTX Context;
  char Digest[SHA_DIGEST_LENGTH];

  SHA1_Init(&Context);
  SHA1_Update(&Context, StartKey, SessionKeyLength);
  SHA1_Update(&Context, SHA1_Pad1, 40);
  SHA1_Update(&Context, SessionKey, SessionKeyLength);
  SHA1_Update(&Context, SHA1_Pad2, 40);
  SHA1_Final(Digest, &Context);

  memcpy(InterimKey, Digest, SessionKeyLength);
}

#if 0
static void
Get_Key(char *InitialSessionKey, char *CurrentSessionKey,
        int LengthOfDesiredKey)
{
  SHA_CTX Context;
  char Digest[SHA_DIGEST_LENGTH];

  SHA1_Init(&Context);
  SHA1_Update(&Context, InitialSessionKey, LengthOfDesiredKey);
  SHA1_Update(&Context, SHA1_Pad1, 40);
  SHA1_Update(&Context, CurrentSessionKey, LengthOfDesiredKey);
  SHA1_Update(&Context, SHA1_Pad2, 40);
  SHA1_Final(Digest, &Context);

  memcpy(CurrentSessionKey, Digest, LengthOfDesiredKey);
}
#endif

/* passwordHash 16-bytes MD4 hashed password
   challenge    8-bytes peer CHAP challenge
   since passwordHash is in a 24-byte buffer, response is written in there */
void
mschap_NT(char *passwordHash, char *challenge)
{
    u_char response[24];

    ChallengeResponse(challenge, passwordHash, response);
    memcpy(passwordHash, response, 24);
    passwordHash[24] = 1;		/* NT-style response */
}

void
mschap_LANMan(char *digest, char *challenge, char *secret)
{
  static u_char salt[] = "KGS!@#$%";	/* RASAPI32.dll */
  char SECRET[14], *ptr, *end;
  u_char hash[16];

  end = SECRET + sizeof SECRET;
  for (ptr = SECRET; *secret && ptr < end; ptr++, secret++)
    *ptr = toupper(*secret);
  if (ptr < end)
    memset(ptr, '\0', end - ptr);

  DesEncrypt(salt, SECRET, hash);
  DesEncrypt(salt, SECRET + 7, hash + 8);

  ChallengeResponse(challenge, hash, digest);
}
