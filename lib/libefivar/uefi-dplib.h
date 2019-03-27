/*-
 * Copyright (c) 2017 Netflix, Inc.
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

/*
 * Taken from MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.h
 * hash a11928f3310518ab1c6fd34e8d0fdbb72de9602c 2017-Mar-01
 */

/** @file
  Definition for Device Path library.

Copyright (c) 2013 - 2015, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef _UEFI_DEVICE_PATH_LIB_H_
#define _UEFI_DEVICE_PATH_LIB_H_
#include <Uefi.h>
#include <Protocol/DevicePathUtilities.h>
#include <Protocol/DebugPort.h>
#include <Protocol/DevicePathToText.h>
#include <Protocol/DevicePathFromText.h>
#include <Guid/PcAnsi.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DevicePathLib.h>
#include <Library/PcdLib.h>
#include <IndustryStandard/Bluetooth.h>

#define IS_COMMA(a)                ((a) == ',')
#define IS_HYPHEN(a)               ((a) == '-')
#define IS_DOT(a)                  ((a) == '.')
#define IS_LEFT_PARENTH(a)         ((a) == '(')
#define IS_RIGHT_PARENTH(a)        ((a) == ')')
#define IS_SLASH(a)                ((a) == '/')
#define IS_NULL(a)                 ((a) == '\0')


//
// Private Data structure
//
typedef struct {
  char  *Str;
  UINTN   Count;
  UINTN   Capacity;
} POOL_PRINT;

typedef
EFI_DEVICE_PATH_PROTOCOL  *
(*DEVICE_PATH_FROM_TEXT) (
  IN char *Str
  );

typedef
VOID
(*DEVICE_PATH_TO_TEXT) (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevicePath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  );

typedef struct {
  UINT8                Type;
  UINT8                SubType;
  DEVICE_PATH_TO_TEXT  Function;
} DEVICE_PATH_TO_TEXT_TABLE;

typedef struct {
  UINT8                Type;
  const char          *Text;
} DEVICE_PATH_TO_TEXT_GENERIC_TABLE;

typedef struct {
  const char                *DevicePathNodeText;
  DEVICE_PATH_FROM_TEXT     Function;
} DEVICE_PATH_FROM_TEXT_TABLE;

typedef struct {
  BOOLEAN ClassExist;
  UINT8   Class;
  BOOLEAN SubClassExist;
  UINT8   SubClass;
} USB_CLASS_TEXT;

#define USB_CLASS_AUDIO            1
#define USB_CLASS_CDCCONTROL       2
#define USB_CLASS_HID              3
#define USB_CLASS_IMAGE            6
#define USB_CLASS_PRINTER          7
#define USB_CLASS_MASS_STORAGE     8
#define USB_CLASS_HUB              9
#define USB_CLASS_CDCDATA          10
#define USB_CLASS_SMART_CARD       11
#define USB_CLASS_VIDEO            14
#define USB_CLASS_DIAGNOSTIC       220
#define USB_CLASS_WIRELESS         224

#define USB_CLASS_RESERVE          254
#define USB_SUBCLASS_FW_UPDATE     1
#define USB_SUBCLASS_IRDA_BRIDGE   2
#define USB_SUBCLASS_TEST          3

#define RFC_1700_UDP_PROTOCOL      17
#define RFC_1700_TCP_PROTOCOL      6

#pragma pack(1)

typedef struct {
  EFI_DEVICE_PATH_PROTOCOL  Header;
  EFI_GUID                  Guid;
  UINT8                     VendorDefinedData[1];
} VENDOR_DEFINED_HARDWARE_DEVICE_PATH;

typedef struct {
  EFI_DEVICE_PATH_PROTOCOL  Header;
  EFI_GUID                  Guid;
  UINT8                     VendorDefinedData[1];
} VENDOR_DEFINED_MESSAGING_DEVICE_PATH;

typedef struct {
  EFI_DEVICE_PATH_PROTOCOL  Header;
  EFI_GUID                  Guid;
  UINT8                     VendorDefinedData[1];
} VENDOR_DEFINED_MEDIA_DEVICE_PATH;

typedef struct {
  EFI_DEVICE_PATH_PROTOCOL  Header;
  UINT32                    Hid;
  UINT32                    Uid;
  UINT32                    Cid;
  CHAR8                     HidUidCidStr[3];
} ACPI_EXTENDED_HID_DEVICE_PATH_WITH_STR;

typedef struct {
  EFI_DEVICE_PATH_PROTOCOL  Header;
  UINT16                    NetworkProtocol;
  UINT16                    LoginOption;
  UINT64                    Lun;
  UINT16                    TargetPortalGroupTag;
  CHAR8                     TargetName[1];
} ISCSI_DEVICE_PATH_WITH_NAME;

typedef struct {
  EFI_DEVICE_PATH_PROTOCOL  Header;
  EFI_GUID                  Guid;
  UINT8                     VendorDefinedData[1];
} VENDOR_DEVICE_PATH_WITH_DATA;

#pragma pack()

#ifdef FreeBSD		/* Remove these on FreeBSD */

/**
  Returns the size of a device path in bytes.

  This function returns the size, in bytes, of the device path data structure
  specified by DevicePath including the end of device path node.
  If DevicePath is NULL or invalid, then 0 is returned.

  @param  DevicePath  A pointer to a device path data structure.

  @retval 0           If DevicePath is NULL or invalid.
  @retval Others      The size of a device path in bytes.

**/
UINTN
EFIAPI
UefiDevicePathLibGetDevicePathSize (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  );

/**
  Creates a new copy of an existing device path.

  This function allocates space for a new copy of the device path specified by DevicePath.
  If DevicePath is NULL, then NULL is returned.  If the memory is successfully
  allocated, then the contents of DevicePath are copied to the newly allocated
  buffer, and a pointer to that buffer is returned.  Otherwise, NULL is returned.
  The memory for the new device path is allocated from EFI boot services memory.
  It is the responsibility of the caller to free the memory allocated.

  @param  DevicePath    A pointer to a device path data structure.

  @retval NULL          DevicePath is NULL or invalid.
  @retval Others        A pointer to the duplicated device path.

**/
EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
UefiDevicePathLibDuplicateDevicePath (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  );

/**
  Creates a new device path by appending a second device path to a first device path.

  This function creates a new device path by appending a copy of SecondDevicePath
  to a copy of FirstDevicePath in a newly allocated buffer.  Only the end-of-device-path
  device node from SecondDevicePath is retained. The newly created device path is
  returned. If FirstDevicePath is NULL, then it is ignored, and a duplicate of
  SecondDevicePath is returned.  If SecondDevicePath is NULL, then it is ignored,
  and a duplicate of FirstDevicePath is returned. If both FirstDevicePath and
  SecondDevicePath are NULL, then a copy of an end-of-device-path is returned.

  If there is not enough memory for the newly allocated buffer, then NULL is returned.
  The memory for the new device path is allocated from EFI boot services memory.
  It is the responsibility of the caller to free the memory allocated.

  @param  FirstDevicePath            A pointer to a device path data structure.
  @param  SecondDevicePath           A pointer to a device path data structure.

  @retval NULL      If there is not enough memory for the newly allocated buffer.
  @retval NULL      If FirstDevicePath or SecondDevicePath is invalid.
  @retval Others    A pointer to the new device path if success.
                    Or a copy an end-of-device-path if both FirstDevicePath and SecondDevicePath are NULL.

**/
EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
UefiDevicePathLibAppendDevicePath (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *FirstDevicePath,  OPTIONAL
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *SecondDevicePath  OPTIONAL
  );

/**
  Creates a new path by appending the device node to the device path.

  This function creates a new device path by appending a copy of the device node
  specified by DevicePathNode to a copy of the device path specified by DevicePath
  in an allocated buffer. The end-of-device-path device node is moved after the
  end of the appended device node.
  If DevicePathNode is NULL then a copy of DevicePath is returned.
  If DevicePath is NULL then a copy of DevicePathNode, followed by an end-of-device
  path device node is returned.
  If both DevicePathNode and DevicePath are NULL then a copy of an end-of-device-path
  device node is returned.
  If there is not enough memory to allocate space for the new device path, then
  NULL is returned.
  The memory is allocated from EFI boot services memory. It is the responsibility
  of the caller to free the memory allocated.

  @param  DevicePath                 A pointer to a device path data structure.
  @param  DevicePathNode             A pointer to a single device path node.

  @retval NULL      If there is not enough memory for the new device path.
  @retval Others    A pointer to the new device path if success.
                    A copy of DevicePathNode followed by an end-of-device-path node
                    if both FirstDevicePath and SecondDevicePath are NULL.
                    A copy of an end-of-device-path node if both FirstDevicePath
                    and SecondDevicePath are NULL.

**/
EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
UefiDevicePathLibAppendDevicePathNode (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath,     OPTIONAL
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePathNode  OPTIONAL
  );

/**
  Creates a new device path by appending the specified device path instance to the specified device
  path.

  This function creates a new device path by appending a copy of the device path
  instance specified by DevicePathInstance to a copy of the device path specified
  by DevicePath in a allocated buffer.
  The end-of-device-path device node is moved after the end of the appended device
  path instance and a new end-of-device-path-instance node is inserted between.
  If DevicePath is NULL, then a copy if DevicePathInstance is returned.
  If DevicePathInstance is NULL, then NULL is returned.
  If DevicePath or DevicePathInstance is invalid, then NULL is returned.
  If there is not enough memory to allocate space for the new device path, then
  NULL is returned.
  The memory is allocated from EFI boot services memory. It is the responsibility
  of the caller to free the memory allocated.

  @param  DevicePath                 A pointer to a device path data structure.
  @param  DevicePathInstance         A pointer to a device path instance.

  @return A pointer to the new device path.

**/
EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
UefiDevicePathLibAppendDevicePathInstance (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath,        OPTIONAL
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePathInstance OPTIONAL
  );

/**
  Creates a copy of the current device path instance and returns a pointer to the next device path
  instance.

  This function creates a copy of the current device path instance. It also updates
  DevicePath to point to the next device path instance in the device path (or NULL
  if no more) and updates Size to hold the size of the device path instance copy.
  If DevicePath is NULL, then NULL is returned.
  If DevicePath points to a invalid device path, then NULL is returned.
  If there is not enough memory to allocate space for the new device path, then
  NULL is returned.
  The memory is allocated from EFI boot services memory. It is the responsibility
  of the caller to free the memory allocated.
  If Size is NULL, then ASSERT().

  @param  DevicePath                 On input, this holds the pointer to the current
                                     device path instance. On output, this holds
                                     the pointer to the next device path instance
                                     or NULL if there are no more device path
                                     instances in the device path pointer to a
                                     device path data structure.
  @param  Size                       On output, this holds the size of the device
                                     path instance, in bytes or zero, if DevicePath
                                     is NULL.

  @return A pointer to the current device path instance.

**/
EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
UefiDevicePathLibGetNextDevicePathInstance (
  IN OUT EFI_DEVICE_PATH_PROTOCOL    **DevicePath,
  OUT UINTN                          *Size
  );

/**
  Creates a device node.

  This function creates a new device node in a newly allocated buffer of size
  NodeLength and initializes the device path node header with NodeType and NodeSubType.
  The new device path node is returned.
  If NodeLength is smaller than a device path header, then NULL is returned.
  If there is not enough memory to allocate space for the new device path, then
  NULL is returned.
  The memory is allocated from EFI boot services memory. It is the responsibility
  of the caller to free the memory allocated.

  @param  NodeType                   The device node type for the new device node.
  @param  NodeSubType                The device node sub-type for the new device node.
  @param  NodeLength                 The length of the new device node.

  @return The new device path.

**/
EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
UefiDevicePathLibCreateDeviceNode (
  IN UINT8                           NodeType,
  IN UINT8                           NodeSubType,
  IN UINT16                          NodeLength
  );

/**
  Determines if a device path is single or multi-instance.

  This function returns TRUE if the device path specified by DevicePath is
  multi-instance.
  Otherwise, FALSE is returned.
  If DevicePath is NULL or invalid, then FALSE is returned.

  @param  DevicePath                 A pointer to a device path data structure.

  @retval  TRUE                      DevicePath is multi-instance.
  @retval  FALSE                     DevicePath is not multi-instance, or DevicePath
                                     is NULL or invalid.

**/
BOOLEAN
EFIAPI
UefiDevicePathLibIsDevicePathMultiInstance (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  );


/**
  Converts a device path to its text representation.

  @param DevicePath      A Pointer to the device to be converted.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

  @return A pointer to the allocated text representation of the device path or
          NULL if DeviceNode is NULL or there was insufficient memory.

**/
CHAR16 *
EFIAPI
UefiDevicePathLibConvertDevicePathToText (
  IN CONST EFI_DEVICE_PATH_PROTOCOL   *DevicePath,
  IN BOOLEAN                          DisplayOnly,
  IN BOOLEAN                          AllowShortcuts
  );

/**
  Converts a device node to its string representation.

  @param DeviceNode        A Pointer to the device node to be converted.
  @param DisplayOnly       If DisplayOnly is TRUE, then the shorter text representation
                           of the display node is used, where applicable. If DisplayOnly
                           is FALSE, then the longer text representation of the display node
                           is used.
  @param AllowShortcuts    If AllowShortcuts is TRUE, then the shortcut forms of text
                           representation for a device node can be used, where applicable.

  @return A pointer to the allocated text representation of the device node or NULL if DeviceNode
          is NULL or there was insufficient memory.

**/
CHAR16 *
EFIAPI
UefiDevicePathLibConvertDeviceNodeToText (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DeviceNode,
  IN BOOLEAN                         DisplayOnly,
  IN BOOLEAN                         AllowShortcuts
  );

/**
  Convert text to the binary representation of a device node.

  @param TextDeviceNode  TextDeviceNode points to the text representation of a device
                         node. Conversion starts with the first character and continues
                         until the first non-device node character.

  @return A pointer to the EFI device node or NULL if TextDeviceNode is NULL or there was
          insufficient memory or text unsupported.

**/
EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
UefiDevicePathLibConvertTextToDeviceNode (
  IN CONST CHAR16 *TextDeviceNode
  );

/**
  Convert text to the binary representation of a device path.


  @param TextDevicePath  TextDevicePath points to the text representation of a device
                         path. Conversion starts with the first character and continues
                         until the first non-device node character.

  @return A pointer to the allocated device path or NULL if TextDeviceNode is NULL or
          there was insufficient memory.

**/
EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
UefiDevicePathLibConvertTextToDevicePath (
  IN CONST CHAR16 *TextDevicePath
  );
#else

/*
 * Small FreeBSD shim layer. Fast and lose hacks to make this code work with FreeBSD.
 */

#include <ctype.h>

#define _PCD_GET_MODE_32_PcdMaximumDevicePathNodeCount 1000
#define MAX_UINTN UINTPTR_MAX

#define AllocatePool(x) malloc(x)
#define AllocateZeroPool(x) calloc(1,x)
#define AsciiStrLen(s) strlen(s)
#define CopyGuid(dst, src) memcpy(dst, src, sizeof(uuid_t))
#define CopyMem(d, s, l) memcpy(d, s, l)
#define FreePool(x) free(x)
#define LShiftU64(x, s) ((x) << s)
#define ReadUnaligned64(x)    le64dec(x)
#define ReallocatePool(old, new, ptr) realloc(ptr, new)
/*
 * Quirky StrCmp returns 0 if equal, 1 if not. This is what the code
 * expects, though that expectation is likely a bug (it casts the
 * return value. EDK2's StrCmp returns values just like C's strcmp,
 * but the parse code casts this to an UINTN, which is bogus. This
 * definition papers over that bogusness to do the right thing.  If
 * iSCSI protocol string processing is ever fixed, we can remove this
 * bletcherous kludge.
 */
#define StrCmp(a, b) (strcmp(a, b) != 0)
#define StrCpyS(d, l, s) strcpy(d, s)
#define StrHexToUint64(x) strtoll(x, NULL, 16)
#define StrHexToUintn(x) strtoll(x, NULL, 16)
#define StrLen(x) strlen(x)
#define StrSize(x) (strlen(x) + 1)
#define StrnCatS(d, l, s, len) strncat(d, s, len)
#define StrnCmp(a, b, n) strncmp(a, b, n)
#define StrnLenS(str, max) strlen(str)
#define Strtoi(x) strtol(x, NULL, 0)
#define Strtoi64(x, y) *(long long *)y = strtoll(x, NULL, 0)
#define SwapBytes64(u64) bswap64(u64)
#define UnicodeStrToAsciiStrS(src, dest, len) strlcpy(dest, src, len)
#define ZeroMem(p,l) memset(p, 0, l)

#undef ASSERT
#define ASSERT(x)

/*
 * Define AllocateCopyPool and others so that we "forget" about the
 * previous non-static deifnition since we want these to be static
 * inlines.
 */
#define AllocateCopyPool AllocateCopyPoolFreeBSD
#define CompareGuid CompareGuidFreeBSD
#define StrHexToBytes StrHexToBytesFreeBSD
#define StrToGuid StrToGuidFreeBSD
#define WriteUnaligned64 WriteUnaligned64FreeBSD

static inline void *
AllocateCopyPool(size_t l, const void *p)
{
	void *rv;
		
	rv = malloc(l);
	if (rv == NULL)
		return NULL;
	memcpy(rv, p, l);
	return (rv);
}

static inline BOOLEAN
CompareGuid (const GUID *g1, const GUID *g2)
{
	uint32_t ignored_status;

	return (uuid_compare((const uuid_t *)g1, (const uuid_t *)g2,
	    &ignored_status) == 0);
}

static inline int
StrHexToBytes(const char *str, size_t len, uint8_t *buf, size_t buflen)
{
	size_t i;
	char hex[3];

	/*
	 * Sanity check preconditions.
	 */
	if (buflen != len / 2 || (len % 2) == 1)
		return 1;
	for (i = 0; i < len; i += 2) {
		if (!isxdigit(str[i]) || !isxdigit(str[i + 1]))
			return 1;
		hex[0] = str[i];
		hex[1] = str[i + 1];
		hex[2] = '\0';
		buf[i / 2] = strtol(hex, NULL, 16);
	}
	return 0;
}

static inline void
StrToGuid(const char *str, GUID *guid)
{
	uint32_t status;

	uuid_from_string(str, (uuid_t *)guid, &status);
}

static inline void
WriteUnaligned64(void *ptr, uint64_t val)
{
	memcpy(ptr, &val, sizeof(val));
}

/*
 * Hack to allow converting %g to %s in printfs. Hack because
 * it's single entry, uses a static buffer, etc. Sufficient for
 * the day for this file though. IF you ever have to convert
 * two %g's in one format, punt. Did I mention this was super lame.
 * Not to mention it's name.... Also, the error GUID is horrific.
 */
static inline const char *
guid_str(const GUID *g)
{
	static char buf[36 + 1];
	char *str = NULL;
	int32_t ignored_status;

	uuid_to_string((const uuid_t *)g, &str, &ignored_status);
	if (str != NULL)
		strlcpy(buf, str, sizeof(buf));
	else
		strlcpy(buf, "groot-cannot-decode-guid-groot-smash",
		    sizeof(buf)); /* ^^^^^^^ 36 characters ^^^^^^^ */
	free(str);
	return buf;
}
#define G(x) guid_str((const GUID *)(const void *)x)
#endif

#undef GLOBAL_REMOVE_IF_UNREFERENCED
#define GLOBAL_REMOVE_IF_UNREFERENCED static

#endif
