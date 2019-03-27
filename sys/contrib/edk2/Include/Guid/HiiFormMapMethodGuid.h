/** @file
  Guid used to identify HII FormMap configuration method.

  Copyright (c) 2009, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials                          
  are licensed and made available under the terms and conditions of the BSD License         
  which accompanies this distribution.  The full text of the license may be found at        
  http://opensource.org/licenses/bsd-license.php                                            

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

  @par Revision Reference:
  GUID defined in UEFI 2.2 spec.
**/

#ifndef __EFI_HII_FORMMAP_GUID_H__
#define __EFI_HII_FORMMAP_GUID_H__

#define EFI_HII_STANDARD_FORM_GUID \
  { 0x3bd2f4ec, 0xe524, 0x46e4, { 0xa9, 0xd8, 0x51, 0x1, 0x17, 0x42, 0x55, 0x62 } }

extern EFI_GUID gEfiHiiStandardFormGuid;

#endif
