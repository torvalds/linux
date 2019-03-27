/** @file
  Image signature database are defined for the signed image validation.
  Copyright (c) 2009 - 2018, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php
  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
  @par Revision Reference:
  GUIDs defined in UEFI 2.5 spec.
**/

#ifndef __IMAGE_AUTHTICATION_H__
#define __IMAGE_AUTHTICATION_H__

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <Guid/GlobalVariable.h>
#include <Protocol/Hash.h>

#define EFI_IMAGE_SECURITY_DATABASE_GUID \
  { \
    0xd719b2cb, 0x3d3a, 0x4596, { 0xa3, 0xbc, 0xda, 0xd0, 0xe, 0x67, 0x65, 0x6f } \
  }

///
/// Varialbe name with guid EFI_IMAGE_SECURITY_DATABASE_GUID
/// for the authorized signature database.
///
#define EFI_IMAGE_SECURITY_DATABASE       L"db"
///
/// Varialbe name with guid EFI_IMAGE_SECURITY_DATABASE_GUID
/// for the forbidden signature database.
///
#define EFI_IMAGE_SECURITY_DATABASE1      L"dbx"
///
/// Variable name with guid EFI_IMAGE_SECURITY_DATABASE_GUID
/// for the timestamp signature database.
///
#define EFI_IMAGE_SECURITY_DATABASE2      L"dbt"

#define SECURE_BOOT_MODE_ENABLE           1
#define SECURE_BOOT_MODE_DISABLE          0

#define SETUP_MODE                        1
#define USER_MODE                         0

//***********************************************************************
// Signature Database
//***********************************************************************
///
/// The format of a signature database.
///
#pragma pack(1)

typedef struct {
  ///
  /// An identifier which identifies the agent which added the signature to the list.
  ///
  EFI_GUID          SignatureOwner;
  ///
  /// The format of the signature is defined by the SignatureType.
  ///
  UINT8             SignatureData[1];
} EFI_SIGNATURE_DATA;

typedef struct {
  ///
  /// Type of the signature. GUID signature types are defined in below.
  ///
  EFI_GUID            SignatureType;
  ///
  /// Total size of the signature list, including this header.
  ///
  UINT32              SignatureListSize;
  ///
  /// Size of the signature header which precedes the array of signatures.
  ///
  UINT32              SignatureHeaderSize;
  ///
  /// Size of each signature.
  ///
  UINT32              SignatureSize;
  ///
  /// Header before the array of signatures. The format of this header is specified
  /// by the SignatureType.
  /// UINT8           SignatureHeader[SignatureHeaderSize];
  ///
  /// An array of signatures. Each signature is SignatureSize bytes in length.
  /// EFI_SIGNATURE_DATA Signatures[][SignatureSize];
  ///
} EFI_SIGNATURE_LIST;

typedef struct {
  ///
  /// The SHA256 hash of an X.509 certificate's To-Be-Signed contents.
  ///
  EFI_SHA256_HASH     ToBeSignedHash;
  ///
  /// The time that the certificate shall be considered to be revoked.
  ///
  EFI_TIME            TimeOfRevocation;
} EFI_CERT_X509_SHA256;

typedef struct {
  ///
  /// The SHA384 hash of an X.509 certificate's To-Be-Signed contents.
  ///
  EFI_SHA384_HASH     ToBeSignedHash;
  ///
  /// The time that the certificate shall be considered to be revoked.
  ///
  EFI_TIME            TimeOfRevocation;
} EFI_CERT_X509_SHA384;

typedef struct {
  ///
  /// The SHA512 hash of an X.509 certificate's To-Be-Signed contents.
  ///
  EFI_SHA512_HASH     ToBeSignedHash;
  ///
  /// The time that the certificate shall be considered to be revoked.
  ///
  EFI_TIME            TimeOfRevocation;
} EFI_CERT_X509_SHA512;

#pragma pack()

///
/// This identifies a signature containing a SHA-256 hash. The SignatureHeader size shall
/// always be 0. The SignatureSize shall always be 16 (size of SignatureOwner component) +
/// 32 bytes.
///
#define EFI_CERT_SHA256_GUID \
  { \
    0xc1c41626, 0x504c, 0x4092, {0xac, 0xa9, 0x41, 0xf9, 0x36, 0x93, 0x43, 0x28} \
  }

///
/// This identifies a signature containing an RSA-2048 key. The key (only the modulus
/// since the public key exponent is known to be 0x10001) shall be stored in big-endian
/// order.
/// The SignatureHeader size shall always be 0. The SignatureSize shall always be 16 (size
/// of SignatureOwner component) + 256 bytes.
///
#define EFI_CERT_RSA2048_GUID \
  { \
    0x3c5766e8, 0x269c, 0x4e34, {0xaa, 0x14, 0xed, 0x77, 0x6e, 0x85, 0xb3, 0xb6} \
  }

///
/// This identifies a signature containing a RSA-2048 signature of a SHA-256 hash.  The
/// SignatureHeader size shall always be 0. The SignatureSize shall always be 16 (size of
/// SignatureOwner component) + 256 bytes.
///
#define EFI_CERT_RSA2048_SHA256_GUID \
  { \
    0xe2b36190, 0x879b, 0x4a3d, {0xad, 0x8d, 0xf2, 0xe7, 0xbb, 0xa3, 0x27, 0x84} \
  }

///
/// This identifies a signature containing a SHA-1 hash.  The SignatureSize shall always
/// be 16 (size of SignatureOwner component) + 20 bytes.
///
#define EFI_CERT_SHA1_GUID \
  { \
    0x826ca512, 0xcf10, 0x4ac9, {0xb1, 0x87, 0xbe, 0x1, 0x49, 0x66, 0x31, 0xbd} \
  }

///
/// TThis identifies a signature containing a RSA-2048 signature of a SHA-1 hash.  The
/// SignatureHeader size shall always be 0. The SignatureSize shall always be 16 (size of
/// SignatureOwner component) + 256 bytes.
///
#define EFI_CERT_RSA2048_SHA1_GUID \
  { \
    0x67f8444f, 0x8743, 0x48f1, {0xa3, 0x28, 0x1e, 0xaa, 0xb8, 0x73, 0x60, 0x80} \
  }

///
/// This identifies a signature based on an X.509 certificate. If the signature is an X.509
/// certificate then verification of the signature of an image should validate the public
/// key certificate in the image using certificate path verification, up to this X.509
/// certificate as a trusted root.  The SignatureHeader size shall always be 0. The
/// SignatureSize may vary but shall always be 16 (size of the SignatureOwner component) +
/// the size of the certificate itself.
/// Note: This means that each certificate will normally be in a separate EFI_SIGNATURE_LIST.
///
#define EFI_CERT_X509_GUID \
  { \
    0xa5c059a1, 0x94e4, 0x4aa7, {0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72} \
  }

///
/// This identifies a signature containing a SHA-224 hash. The SignatureHeader size shall
/// always be 0. The SignatureSize shall always be 16 (size of SignatureOwner component) +
/// 28 bytes.
///
#define EFI_CERT_SHA224_GUID \
  { \
    0xb6e5233, 0xa65c, 0x44c9, {0x94, 0x7, 0xd9, 0xab, 0x83, 0xbf, 0xc8, 0xbd} \
  }

///
/// This identifies a signature containing a SHA-384 hash. The SignatureHeader size shall
/// always be 0. The SignatureSize shall always be 16 (size of SignatureOwner component) +
/// 48 bytes.
///
#define EFI_CERT_SHA384_GUID \
  { \
    0xff3e5307, 0x9fd0, 0x48c9, {0x85, 0xf1, 0x8a, 0xd5, 0x6c, 0x70, 0x1e, 0x1} \
  }

///
/// This identifies a signature containing a SHA-512 hash. The SignatureHeader size shall
/// always be 0. The SignatureSize shall always be 16 (size of SignatureOwner component) +
/// 64 bytes.
///
#define EFI_CERT_SHA512_GUID \
  { \
    0x93e0fae, 0xa6c4, 0x4f50, {0x9f, 0x1b, 0xd4, 0x1e, 0x2b, 0x89, 0xc1, 0x9a} \
  }

///
/// This identifies a signature containing the SHA256 hash of an X.509 certificate's
/// To-Be-Signed contents, and a time of revocation. The SignatureHeader size shall
/// always be 0. The SignatureSize shall always be 16 (size of the SignatureOwner component)
/// + 48 bytes for an EFI_CERT_X509_SHA256 structure. If the TimeOfRevocation is non-zero,
/// the certificate should be considered to be revoked from that time and onwards, and
/// otherwise the certificate shall be considered to always be revoked.
///
#define EFI_CERT_X509_SHA256_GUID \
  { \
    0x3bd2a492, 0x96c0, 0x4079, {0xb4, 0x20, 0xfc, 0xf9, 0x8e, 0xf1, 0x03, 0xed } \
  }

///
/// This identifies a signature containing the SHA384 hash of an X.509 certificate's
/// To-Be-Signed contents, and a time of revocation. The SignatureHeader size shall
/// always be 0. The SignatureSize shall always be 16 (size of the SignatureOwner component)
/// + 64 bytes for an EFI_CERT_X509_SHA384 structure. If the TimeOfRevocation is non-zero,
/// the certificate should be considered to be revoked from that time and onwards, and
/// otherwise the certificate shall be considered to always be revoked.
///
#define EFI_CERT_X509_SHA384_GUID \
  { \
    0x7076876e, 0x80c2, 0x4ee6, {0xaa, 0xd2, 0x28, 0xb3, 0x49, 0xa6, 0x86, 0x5b } \
  }

///
/// This identifies a signature containing the SHA512 hash of an X.509 certificate's
/// To-Be-Signed contents, and a time of revocation. The SignatureHeader size shall
/// always be 0. The SignatureSize shall always be 16 (size of the SignatureOwner component)
/// + 80 bytes for an EFI_CERT_X509_SHA512 structure. If the TimeOfRevocation is non-zero,
/// the certificate should be considered to be revoked from that time and onwards, and
/// otherwise the certificate shall be considered to always be revoked.
///
#define EFI_CERT_X509_SHA512_GUID \
  { \
    0x446dbf63, 0x2502, 0x4cda, {0xbc, 0xfa, 0x24, 0x65, 0xd2, 0xb0, 0xfe, 0x9d } \
  }

///
/// This identifies a signature containing a DER-encoded PKCS #7 version 1.5 [RFC2315]
/// SignedData value.
///
#define EFI_CERT_TYPE_PKCS7_GUID \
  { \
    0x4aafd29d, 0x68df, 0x49ee, {0x8a, 0xa9, 0x34, 0x7d, 0x37, 0x56, 0x65, 0xa7} \
  }

//***********************************************************************
// Image Execution Information Table Definition
//***********************************************************************
typedef UINT32 EFI_IMAGE_EXECUTION_ACTION;

#define EFI_IMAGE_EXECUTION_AUTHENTICATION      0x00000007
#define EFI_IMAGE_EXECUTION_AUTH_UNTESTED       0x00000000
#define EFI_IMAGE_EXECUTION_AUTH_SIG_FAILED     0x00000001
#define EFI_IMAGE_EXECUTION_AUTH_SIG_PASSED     0x00000002
#define EFI_IMAGE_EXECUTION_AUTH_SIG_NOT_FOUND  0x00000003
#define EFI_IMAGE_EXECUTION_AUTH_SIG_FOUND      0x00000004
#define EFI_IMAGE_EXECUTION_POLICY_FAILED       0x00000005
#define EFI_IMAGE_EXECUTION_INITIALIZED         0x00000008

//
// EFI_IMAGE_EXECUTION_INFO is added to EFI System Configuration Table
// and assigned the GUID EFI_IMAGE_SECURITY_DATABASE_GUID.
//
typedef struct {
  ///
  /// Describes the action taken by the firmware regarding this image.
  ///
  EFI_IMAGE_EXECUTION_ACTION    Action;
  ///
  /// Size of all of the entire structure.
  ///
  UINT32                        InfoSize;
  ///
  /// If this image was a UEFI device driver (for option ROM, for example) this is the
  /// null-terminated, user-friendly name for the device. If the image was for an application,
  /// then this is the name of the application. If this cannot be determined, then a simple
  /// NULL character should be put in this position.
  /// CHAR16                    Name[];
  ///

  ///
  /// For device drivers, this is the device path of the device for which this device driver
  /// was intended. In some cases, the driver itself may be stored as part of the system
  /// firmware, but this field should record the device's path, not the firmware path. For
  /// applications, this is the device path of the application. If this cannot be determined,
  /// a simple end-of-path device node should be put in this position.
  /// EFI_DEVICE_PATH_PROTOCOL  DevicePath;
  ///

  ///
  /// Zero or more image signatures. If the image contained no signatures,
  /// then this field is empty.
  /// EFI_SIGNATURE_LIST            Signature;
  ///
} EFI_IMAGE_EXECUTION_INFO;


typedef struct {
  ///
  /// Number of EFI_IMAGE_EXECUTION_INFO structures.
  ///
  UINTN                     NumberOfImages;
  ///
  /// Number of image instances of EFI_IMAGE_EXECUTION_INFO structures.
  ///
  // EFI_IMAGE_EXECUTION_INFO  InformationInfo[]
} EFI_IMAGE_EXECUTION_INFO_TABLE;

extern EFI_GUID gEfiImageSecurityDatabaseGuid;
extern EFI_GUID gEfiCertSha256Guid;
extern EFI_GUID gEfiCertRsa2048Guid;
extern EFI_GUID gEfiCertRsa2048Sha256Guid;
extern EFI_GUID gEfiCertSha1Guid;
extern EFI_GUID gEfiCertRsa2048Sha1Guid;
extern EFI_GUID gEfiCertX509Guid;
extern EFI_GUID gEfiCertSha224Guid;
extern EFI_GUID gEfiCertSha384Guid;
extern EFI_GUID gEfiCertSha512Guid;
extern EFI_GUID gEfiCertX509Sha256Guid;
extern EFI_GUID gEfiCertX509Sha384Guid;
extern EFI_GUID gEfiCertX509Sha512Guid;
extern EFI_GUID gEfiCertPkcs7Guid;

#endif
