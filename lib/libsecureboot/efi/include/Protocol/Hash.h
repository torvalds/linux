/** @file
  EFI_HASH_SERVICE_BINDING_PROTOCOL as defined in UEFI 2.0.
  EFI_HASH_PROTOCOL as defined in UEFI 2.0.
  The EFI Hash Service Binding Protocol is used to locate hashing services support
  provided by a driver and to create and destroy instances of the EFI Hash Protocol
  so that a multiple drivers can use the underlying hashing services.
Copyright (c) 2006 - 2018, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials are licensed and made available under
the terms and conditions of the BSD License that accompanies this distribution.
The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php.
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#ifndef __EFI_HASH_PROTOCOL_H__
#define __EFI_HASH_PROTOCOL_H__

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define CONST const

#define EFI_HASH_SERVICE_BINDING_PROTOCOL_GUID \
  { \
    0x42881c98, 0xa4f3, 0x44b0, {0xa3, 0x9d, 0xdf, 0xa1, 0x86, 0x67, 0xd8, 0xcd } \
  }

#define EFI_HASH_PROTOCOL_GUID \
  { \
    0xc5184932, 0xdba5, 0x46db, {0xa5, 0xba, 0xcc, 0x0b, 0xda, 0x9c, 0x14, 0x35 } \
  }

#define EFI_HASH_ALGORITHM_SHA1_GUID \
  { \
    0x2ae9d80f, 0x3fb2, 0x4095, {0xb7, 0xb1, 0xe9, 0x31, 0x57, 0xb9, 0x46, 0xb6 } \
  }

#define EFI_HASH_ALGORITHM_SHA224_GUID \
  { \
    0x8df01a06, 0x9bd5, 0x4bf7, {0xb0, 0x21, 0xdb, 0x4f, 0xd9, 0xcc, 0xf4, 0x5b } \
  }

#define EFI_HASH_ALGORITHM_SHA256_GUID \
  { \
    0x51aa59de, 0xfdf2, 0x4ea3, {0xbc, 0x63, 0x87, 0x5f, 0xb7, 0x84, 0x2e, 0xe9 } \
  }

#define EFI_HASH_ALGORITHM_SHA384_GUID \
  { \
    0xefa96432, 0xde33, 0x4dd2, {0xae, 0xe6, 0x32, 0x8c, 0x33, 0xdf, 0x77, 0x7a } \
  }

#define EFI_HASH_ALGORITHM_SHA512_GUID \
  { \
    0xcaa4381e, 0x750c, 0x4770, {0xb8, 0x70, 0x7a, 0x23, 0xb4, 0xe4, 0x21, 0x30 } \
  }

#define EFI_HASH_ALGORTIHM_MD5_GUID \
  { \
    0xaf7c79c, 0x65b5, 0x4319, {0xb0, 0xae, 0x44, 0xec, 0x48, 0x4e, 0x4a, 0xd7 } \
  }

#define EFI_HASH_ALGORITHM_SHA1_NOPAD_GUID \
  { \
    0x24c5dc2f, 0x53e2, 0x40ca, {0x9e, 0xd6, 0xa5, 0xd9, 0xa4, 0x9f, 0x46, 0x3b } \
  }

#define EFI_HASH_ALGORITHM_SHA256_NOPAD_GUID \
  { \
    0x8628752a, 0x6cb7, 0x4814, {0x96, 0xfc, 0x24, 0xa8, 0x15, 0xac, 0x22, 0x26 } \
  }

//
// Note: Use of the following algorithms with EFI_HASH_PROTOCOL is deprecated.
//       EFI_HASH_ALGORITHM_SHA1_GUID
//       EFI_HASH_ALGORITHM_SHA224_GUID
//       EFI_HASH_ALGORITHM_SHA256_GUID
//       EFI_HASH_ALGORITHM_SHA384_GUID
//       EFI_HASH_ALGORITHM_SHA512_GUID
//       EFI_HASH_ALGORTIHM_MD5_GUID
//

typedef struct _EFI_HASH_PROTOCOL EFI_HASH_PROTOCOL;

typedef UINT8  EFI_MD5_HASH[16];
typedef UINT8  EFI_SHA1_HASH[20];
typedef UINT8  EFI_SHA224_HASH[28];
typedef UINT8  EFI_SHA256_HASH[32];
typedef UINT8  EFI_SHA384_HASH[48];
typedef UINT8  EFI_SHA512_HASH[64];

typedef union {
  EFI_MD5_HASH     *Md5Hash;
  EFI_SHA1_HASH    *Sha1Hash;
  EFI_SHA224_HASH  *Sha224Hash;
  EFI_SHA256_HASH  *Sha256Hash;
  EFI_SHA384_HASH  *Sha384Hash;
  EFI_SHA512_HASH  *Sha512Hash;
} EFI_HASH_OUTPUT;

/**
  Returns the size of the hash which results from a specific algorithm.
  @param[in]  This                  Points to this instance of EFI_HASH_PROTOCOL.
  @param[in]  HashAlgorithm         Points to the EFI_GUID which identifies the algorithm to use.
  @param[out] HashSize              Holds the returned size of the algorithm's hash.
  @retval EFI_SUCCESS           Hash size returned successfully.
  @retval EFI_INVALID_PARAMETER HashSize is NULL or HashAlgorithm is NULL.
  @retval EFI_UNSUPPORTED       The algorithm specified by HashAlgorithm is not supported
                                by this driver.
**/
typedef
EFI_STATUS
(EFIAPI *EFI_HASH_GET_HASH_SIZE)(
  IN  CONST EFI_HASH_PROTOCOL     *This,
  IN  CONST EFI_GUID              *HashAlgorithm,
  OUT UINTN                       *HashSize
  );

/**
  Creates a hash for the specified message text.
  @param[in]  This          Points to this instance of EFI_HASH_PROTOCOL.
  @param[in]  HashAlgorithm Points to the EFI_GUID which identifies the algorithm to use.
  @param[in]  Extend        Specifies whether to create a new hash (FALSE) or extend the specified
                            existing hash (TRUE).
  @param[in]  Message       Points to the start of the message.
  @param[in]  MessageSize   The size of Message, in bytes.
  @param[in,out]  Hash      On input, if Extend is TRUE, then this parameter holds a pointer
                            to a pointer to an array containing the hash to extend. If Extend
                            is FALSE, then this parameter holds a pointer to a pointer to a
                            caller-allocated array that will receive the result of the hash
                            computation. On output (regardless of the value of Extend), the
                            array will contain the result of the hash computation.
  @retval EFI_SUCCESS           Hash returned successfully.
  @retval EFI_INVALID_PARAMETER Message or Hash, HashAlgorithm is NULL or MessageSize is 0.
                                MessageSize is not an integer multiple of block size.
  @retval EFI_UNSUPPORTED       The algorithm specified by HashAlgorithm is not supported by this
                                 driver. Or, Extend is TRUE, and the algorithm doesn't support extending the hash.
**/
typedef
EFI_STATUS
(EFIAPI *EFI_HASH_HASH)(
  IN CONST EFI_HASH_PROTOCOL      *This,
  IN CONST EFI_GUID               *HashAlgorithm,
  IN BOOLEAN                      Extend,
  IN CONST UINT8                  *Message,
  IN UINT64                       MessageSize,
  IN OUT EFI_HASH_OUTPUT          *Hash
  );

///
/// This protocol allows creating a hash of an arbitrary message digest
/// using one or more hash algorithms.
///
struct _EFI_HASH_PROTOCOL {
  EFI_HASH_GET_HASH_SIZE          GetHashSize;
  EFI_HASH_HASH                   Hash;
};

extern EFI_GUID gEfiHashServiceBindingProtocolGuid;
extern EFI_GUID gEfiHashProtocolGuid;
extern EFI_GUID gEfiHashAlgorithmSha1Guid;
extern EFI_GUID gEfiHashAlgorithmSha224Guid;
extern EFI_GUID gEfiHashAlgorithmSha256Guid;
extern EFI_GUID gEfiHashAlgorithmSha384Guid;
extern EFI_GUID gEfiHashAlgorithmSha512Guid;
extern EFI_GUID gEfiHashAlgorithmMD5Guid;
extern EFI_GUID gEfiHashAlgorithmSha1NoPadGuid;
extern EFI_GUID gEfiHashAlgorithmSha256NoPadGuid;

#endif
