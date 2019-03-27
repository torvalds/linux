/** @file
  EFI_DEVICE_PATH_FROM_TEXT_PROTOCOL as defined in UEFI 2.0.  
  This protocol provides service to convert text to device paths and device nodes.

  Copyright (c) 2006 - 2008, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials                          
  are licensed and made available under the terms and conditions of the BSD License         
  which accompanies this distribution.  The full text of the license may be found at        
  http://opensource.org/licenses/bsd-license.php                                            

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

**/

#ifndef __DEVICE_PATH_FROM_TEXT_PROTOCOL_H__
#define __DEVICE_PATH_FROM_TEXT_PROTOCOL_H__

///
/// Device Path From Text protocol
///
#define EFI_DEVICE_PATH_FROM_TEXT_PROTOCOL_GUID \
  { \
    0x5c99a21, 0xc70f, 0x4ad2, {0x8a, 0x5f, 0x35, 0xdf, 0x33, 0x43, 0xf5, 0x1e  } \
  }

/**
  Convert text to the binary representation of a device node.

  @param  TextDeviceNode TextDeviceNode points to the text representation of a device
                         node. Conversion starts with the first character and continues
                         until the first non-device node character.

  @retval a_pointer      Pointer to the EFI device node.
  @retval NULL           if TextDeviceNode is NULL or there was insufficient memory.

**/
typedef
EFI_DEVICE_PATH_PROTOCOL*
(EFIAPI *EFI_DEVICE_PATH_FROM_TEXT_NODE)(
  IN CONST CHAR16                 *TextDeviceNode
  );    
  

/**
  Convert text to the binary representation of a device node.

  @param  TextDeviceNode TextDevicePath points to the text representation of a device
                         path. Conversion starts with the first character and continues
                         until the first non-device path character.

  @retval a_pointer      Pointer to the allocated device path.
  @retval NULL           if TextDeviceNode is NULL or there was insufficient memory.

**/
typedef
EFI_DEVICE_PATH_PROTOCOL*
(EFIAPI *EFI_DEVICE_PATH_FROM_TEXT_PATH)(
  IN CONST CHAR16                 *TextDevicePath
  );  

///
/// This protocol converts text to device paths and device nodes.
///
typedef struct {
  EFI_DEVICE_PATH_FROM_TEXT_NODE  ConvertTextToDeviceNode;
  EFI_DEVICE_PATH_FROM_TEXT_PATH  ConvertTextToDevicePath;
} EFI_DEVICE_PATH_FROM_TEXT_PROTOCOL;

extern EFI_GUID gEfiDevicePathFromTextProtocolGuid;

#endif
