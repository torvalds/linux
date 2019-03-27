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
 */

/*
 * Routines to format EFI_DEVICE_PATHs from the UEFI standard. Much of
 * this file is taken from EDK2 and rototilled.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <efivar.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/endian.h>

#include "efi-osdep.h"

#include "uefi-dplib.h"

/* XXX maybe I should include the entire DevicePathUtiltiies.c and ifdef out what we don't use */

/*
 * Taken from MdePkg/Library/UefiDevicePathLib/DevicePathUtilities.c
 * hash a11928f3310518ab1c6fd34e8d0fdbb72de9602c 2017-Mar-01
 */

/** @file
  Device Path services. The thing to remember is device paths are built out of
  nodes. The device path is terminated by an end node that is length
  sizeof(EFI_DEVICE_PATH_PROTOCOL). That would be why there is sizeof(EFI_DEVICE_PATH_PROTOCOL)
  all over this file.

  The only place where multi-instance device paths are supported is in
  environment varibles. Multi-instance device paths should never be placed
  on a Handle.

  Copyright (c) 2006 - 2016, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php.

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

//
// Template for an end-of-device path node.
//
static CONST EFI_DEVICE_PATH_PROTOCOL  mUefiDevicePathLibEndDevicePath = {
  END_DEVICE_PATH_TYPE,
  END_ENTIRE_DEVICE_PATH_SUBTYPE,
  {
    END_DEVICE_PATH_LENGTH,
    0
  }
};


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
GetDevicePathSize (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  CONST EFI_DEVICE_PATH_PROTOCOL  *Start;

  if (DevicePath == NULL) {
    return 0;
  }

  if (!IsDevicePathValid (DevicePath, 0)) {
    return 0;
  }

  //
  // Search for the end of the device path structure
  //
  Start = DevicePath;
  while (!IsDevicePathEnd (DevicePath)) {
    DevicePath = NextDevicePathNode (DevicePath);
  }

  //
  // Compute the size and add back in the size of the end device path structure
  //
  return ((UINTN) DevicePath - (UINTN) Start) + DevicePathNodeLength (DevicePath);
}

/**
  Determine whether a given device path is valid.
  If DevicePath is NULL, then ASSERT().

  @param  DevicePath  A pointer to a device path data structure.
  @param  MaxSize     The maximum size of the device path data structure.

  @retval TRUE        DevicePath is valid.
  @retval FALSE       The length of any node in the DevicePath is less
                      than sizeof (EFI_DEVICE_PATH_PROTOCOL).
  @retval FALSE       If MaxSize is not zero, the size of the DevicePath
                      exceeds MaxSize.
  @retval FALSE       If PcdMaximumDevicePathNodeCount is not zero, the node
                      count of the DevicePath exceeds PcdMaximumDevicePathNodeCount.
**/
BOOLEAN
EFIAPI
IsDevicePathValid (
  IN CONST EFI_DEVICE_PATH_PROTOCOL *DevicePath,
  IN       UINTN                    MaxSize
  )
{
  UINTN Count;
  UINTN Size;
  UINTN NodeLength;

  ASSERT (DevicePath != NULL);

  if (MaxSize == 0) {
    MaxSize = MAX_UINTN;
  }

  //
  // Validate the input size big enough to touch the first node.
  //
  if (MaxSize < sizeof (EFI_DEVICE_PATH_PROTOCOL)) {
    return FALSE;
  }

  for (Count = 0, Size = 0; !IsDevicePathEnd (DevicePath); DevicePath = NextDevicePathNode (DevicePath)) {
    NodeLength = DevicePathNodeLength (DevicePath);
    if (NodeLength < sizeof (EFI_DEVICE_PATH_PROTOCOL)) {
      return FALSE;
    }

    if (NodeLength > MAX_UINTN - Size) {
      return FALSE;
    }
    Size += NodeLength;

    //
    // Validate next node before touch it.
    //
    if (Size > MaxSize - END_DEVICE_PATH_LENGTH ) {
      return FALSE;
    }

    if (PcdGet32 (PcdMaximumDevicePathNodeCount) > 0) {
      Count++;
      if (Count >= PcdGet32 (PcdMaximumDevicePathNodeCount)) {
        return FALSE;
      }
    }
  }

  //
  // Only return TRUE when the End Device Path node is valid.
  //
  return (BOOLEAN) (DevicePathNodeLength (DevicePath) == END_DEVICE_PATH_LENGTH);
}

/**
  Returns the Type field of a device path node.

  Returns the Type field of the device path node specified by Node.

  If Node is NULL, then ASSERT().

  @param  Node      A pointer to a device path node data structure.

  @return The Type field of the device path node specified by Node.

**/
UINT8
EFIAPI
DevicePathType (
  IN CONST VOID  *Node
  )
{
  ASSERT (Node != NULL);
  return ((const EFI_DEVICE_PATH_PROTOCOL *)(Node))->Type;
}


/**
  Returns the SubType field of a device path node.

  Returns the SubType field of the device path node specified by Node.

  If Node is NULL, then ASSERT().

  @param  Node      A pointer to a device path node data structure.

  @return The SubType field of the device path node specified by Node.

**/
UINT8
EFIAPI
DevicePathSubType (
  IN CONST VOID  *Node
  )
{
  ASSERT (Node != NULL);
  return ((const EFI_DEVICE_PATH_PROTOCOL *)(Node))->SubType;
}

/**
  Returns the 16-bit Length field of a device path node.

  Returns the 16-bit Length field of the device path node specified by Node.
  Node is not required to be aligned on a 16-bit boundary, so it is recommended
  that a function such as ReadUnaligned16() be used to extract the contents of
  the Length field.

  If Node is NULL, then ASSERT().

  @param  Node      A pointer to a device path node data structure.

  @return The 16-bit Length field of the device path node specified by Node.

**/
UINTN
EFIAPI
DevicePathNodeLength (
  IN CONST VOID  *Node
  )
{
  ASSERT (Node != NULL);
  return ((const EFI_DEVICE_PATH_PROTOCOL *)Node)->Length[0] |
      (((const EFI_DEVICE_PATH_PROTOCOL *)Node)->Length[1] << 8);
}

/**
  Returns a pointer to the next node in a device path.

  Returns a pointer to the device path node that follows the device path node
  specified by Node.

  If Node is NULL, then ASSERT().

  @param  Node      A pointer to a device path node data structure.

  @return a pointer to the device path node that follows the device path node
  specified by Node.

**/
EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
NextDevicePathNode (
  IN CONST VOID  *Node
  )
{
  ASSERT (Node != NULL);
  return ((EFI_DEVICE_PATH_PROTOCOL *)(__DECONST(UINT8 *, Node) + DevicePathNodeLength(Node)));
}

/**
  Determines if a device path node is an end node of a device path.
  This includes nodes that are the end of a device path instance and nodes that
  are the end of an entire device path.

  Determines if the device path node specified by Node is an end node of a device path.
  This includes nodes that are the end of a device path instance and nodes that are the
  end of an entire device path.  If Node represents an end node of a device path,
  then TRUE is returned.  Otherwise, FALSE is returned.

  If Node is NULL, then ASSERT().

  @param  Node      A pointer to a device path node data structure.

  @retval TRUE      The device path node specified by Node is an end node of a
                    device path.
  @retval FALSE     The device path node specified by Node is not an end node of
                    a device path.

**/
BOOLEAN
EFIAPI
IsDevicePathEndType (
  IN CONST VOID  *Node
  )
{
  ASSERT (Node != NULL);
  return (BOOLEAN) (DevicePathType (Node) == END_DEVICE_PATH_TYPE);
}

/**
  Determines if a device path node is an end node of an entire device path.

  Determines if a device path node specified by Node is an end node of an entire
  device path. If Node represents the end of an entire device path, then TRUE is
  returned.  Otherwise, FALSE is returned.

  If Node is NULL, then ASSERT().

  @param  Node      A pointer to a device path node data structure.

  @retval TRUE      The device path node specified by Node is the end of an entire
                    device path.
  @retval FALSE     The device path node specified by Node is not the end of an
                    entire device path.

**/
BOOLEAN
EFIAPI
IsDevicePathEnd (
  IN CONST VOID  *Node
  )
{
  ASSERT (Node != NULL);
  return (BOOLEAN) (IsDevicePathEndType (Node) && DevicePathSubType(Node) == END_ENTIRE_DEVICE_PATH_SUBTYPE);
}

/**
  Fills in all the fields of a device path node that is the end of an entire device path.

  Fills in all the fields of a device path node specified by Node so Node represents
  the end of an entire device path.  The Type field of Node is set to
  END_DEVICE_PATH_TYPE, the SubType field of Node is set to
  END_ENTIRE_DEVICE_PATH_SUBTYPE, and the Length field of Node is set to
  END_DEVICE_PATH_LENGTH.  Node is not required to be aligned on a 16-bit boundary,
  so it is recommended that a function such as WriteUnaligned16() be used to set
  the contents of the Length field.

  If Node is NULL, then ASSERT().

  @param  Node      A pointer to a device path node data structure.

**/
VOID
EFIAPI
SetDevicePathEndNode (
  OUT VOID  *Node
  )
{
  ASSERT (Node != NULL);
  memcpy (Node, &mUefiDevicePathLibEndDevicePath, sizeof (mUefiDevicePathLibEndDevicePath));
}

/**
  Sets the length, in bytes, of a device path node.

  Sets the length of the device path node specified by Node to the value specified
  by NodeLength.  NodeLength is returned.  Node is not required to be aligned on
  a 16-bit boundary, so it is recommended that a function such as WriteUnaligned16()
  be used to set the contents of the Length field.

  If Node is NULL, then ASSERT().
  If NodeLength >= SIZE_64KB, then ASSERT().
  If NodeLength < sizeof (EFI_DEVICE_PATH_PROTOCOL), then ASSERT().

  @param  Node      A pointer to a device path node data structure.
  @param  Length    The length, in bytes, of the device path node.

  @return Length

**/
UINT16
EFIAPI
SetDevicePathNodeLength (
  IN OUT VOID  *Node,
  IN UINTN     Length
  )
{
  ASSERT (Node != NULL);
  ASSERT ((Length >= sizeof (EFI_DEVICE_PATH_PROTOCOL)) && (Length < SIZE_64KB));
//  return WriteUnaligned16 ((UINT16 *)&((EFI_DEVICE_PATH_PROTOCOL *)(Node))->Length[0], (UINT16)(Length));
  le16enc(&((EFI_DEVICE_PATH_PROTOCOL *)(Node))->Length[0], (UINT16)(Length));
  return Length;
}

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
CreateDeviceNode (
  IN UINT8                           NodeType,
  IN UINT8                           NodeSubType,
  IN UINT16                          NodeLength
  )
{
  EFI_DEVICE_PATH_PROTOCOL      *DevicePath;

  if (NodeLength < sizeof (EFI_DEVICE_PATH_PROTOCOL)) {
    //
    // NodeLength is less than the size of the header.
    //
    return NULL;
  }

  DevicePath = AllocateZeroPool (NodeLength);
  if (DevicePath != NULL) {
     DevicePath->Type    = NodeType;
     DevicePath->SubType = NodeSubType;
     SetDevicePathNodeLength (DevicePath, NodeLength);
  }

  return DevicePath;
}

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
DuplicateDevicePath (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  UINTN                     Size;

  //
  // Compute the size
  //
  Size = GetDevicePathSize (DevicePath);
  if (Size == 0) {
    return NULL;
  }

  //
  // Allocate space for duplicate device path
  //

  return AllocateCopyPool (Size, DevicePath);
}

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
AppendDevicePath (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *FirstDevicePath,  OPTIONAL
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *SecondDevicePath  OPTIONAL
  )
{
  UINTN                     Size;
  UINTN                     Size1;
  UINTN                     Size2;
  EFI_DEVICE_PATH_PROTOCOL  *NewDevicePath;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath2;

  //
  // If there's only 1 path, just duplicate it.
  //
  if (FirstDevicePath == NULL) {
    return DuplicateDevicePath ((SecondDevicePath != NULL) ? SecondDevicePath : &mUefiDevicePathLibEndDevicePath);
  }

  if (SecondDevicePath == NULL) {
    return DuplicateDevicePath (FirstDevicePath);
  }

  if (!IsDevicePathValid (FirstDevicePath, 0) || !IsDevicePathValid (SecondDevicePath, 0)) {
    return NULL;
  }

  //
  // Allocate space for the combined device path. It only has one end node of
  // length EFI_DEVICE_PATH_PROTOCOL.
  //
  Size1         = GetDevicePathSize (FirstDevicePath);
  Size2         = GetDevicePathSize (SecondDevicePath);
  Size          = Size1 + Size2 - END_DEVICE_PATH_LENGTH;

  NewDevicePath = AllocatePool (Size);

  if (NewDevicePath != NULL) {
    NewDevicePath = CopyMem (NewDevicePath, FirstDevicePath, Size1);
    //
    // Over write FirstDevicePath EndNode and do the copy
    //
    DevicePath2 = (EFI_DEVICE_PATH_PROTOCOL *) ((CHAR8 *) NewDevicePath +
                  (Size1 - END_DEVICE_PATH_LENGTH));
    CopyMem (DevicePath2, SecondDevicePath, Size2);
  }

  return NewDevicePath;
}

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
AppendDevicePathNode (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath,     OPTIONAL
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePathNode  OPTIONAL
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *TempDevicePath;
  EFI_DEVICE_PATH_PROTOCOL  *NextNode;
  EFI_DEVICE_PATH_PROTOCOL  *NewDevicePath;
  UINTN                     NodeLength;

  if (DevicePathNode == NULL) {
    return DuplicateDevicePath ((DevicePath != NULL) ? DevicePath : &mUefiDevicePathLibEndDevicePath);
  }
  //
  // Build a Node that has a terminator on it
  //
  NodeLength = DevicePathNodeLength (DevicePathNode);

  TempDevicePath = AllocatePool (NodeLength + END_DEVICE_PATH_LENGTH);
  if (TempDevicePath == NULL) {
    return NULL;
  }
  TempDevicePath = CopyMem (TempDevicePath, DevicePathNode, NodeLength);
  //
  // Add and end device path node to convert Node to device path
  //
  NextNode = NextDevicePathNode (TempDevicePath);
  SetDevicePathEndNode (NextNode);
  //
  // Append device paths
  //
  NewDevicePath = AppendDevicePath (DevicePath, TempDevicePath);

  FreePool (TempDevicePath);

  return NewDevicePath;
}
