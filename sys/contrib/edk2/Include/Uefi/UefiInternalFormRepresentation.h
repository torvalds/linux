/** @file
  This file defines the encoding for the VFR (Visual Form Representation) language.
  IFR is primarily consumed by the EFI presentation engine, and produced by EFI
  internal application and drivers as well as all add-in card option-ROM drivers

Copyright (c) 2006 - 2016, Intel Corporation. All rights reserved.<BR>
(C) Copyright 2016 Hewlett Packard Enterprise Development LP<BR>
This program and the accompanying materials are licensed and made available under 
the terms and conditions of the BSD License that accompanies this distribution.  
The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php.                                          
    
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

  @par Revision Reference:
  These definitions are from UEFI 2.1 and 2.2.

**/

#ifndef __UEFI_INTERNAL_FORMREPRESENTATION_H__
#define __UEFI_INTERNAL_FORMREPRESENTATION_H__

#include <Guid/HiiFormMapMethodGuid.h>

///
/// The following types are currently defined:
///
typedef VOID*   EFI_HII_HANDLE;
typedef CHAR16* EFI_STRING;
typedef UINT16  EFI_IMAGE_ID;
typedef UINT16  EFI_QUESTION_ID;
typedef UINT16  EFI_STRING_ID;
typedef UINT16  EFI_FORM_ID;
typedef UINT16  EFI_VARSTORE_ID;
typedef UINT16  EFI_ANIMATION_ID;

typedef UINT16  EFI_DEFAULT_ID;

typedef UINT32  EFI_HII_FONT_STYLE;



#pragma pack(1)

//
// Definitions for Package Lists and Package Headers
// Section 27.3.1
//

///
/// The header found at the start of each package list.
///
typedef struct {
  EFI_GUID               PackageListGuid;
  UINT32                 PackageLength;
} EFI_HII_PACKAGE_LIST_HEADER;

///
/// The header found at the start of each package.
///
typedef struct {
  UINT32  Length:24;
  UINT32  Type:8;
  // UINT8  Data[...];
} EFI_HII_PACKAGE_HEADER;

//
// Value of HII package type
// 
#define EFI_HII_PACKAGE_TYPE_ALL             0x00
#define EFI_HII_PACKAGE_TYPE_GUID            0x01
#define EFI_HII_PACKAGE_FORMS                0x02
#define EFI_HII_PACKAGE_STRINGS              0x04
#define EFI_HII_PACKAGE_FONTS                0x05
#define EFI_HII_PACKAGE_IMAGES               0x06
#define EFI_HII_PACKAGE_SIMPLE_FONTS         0x07
#define EFI_HII_PACKAGE_DEVICE_PATH          0x08
#define EFI_HII_PACKAGE_KEYBOARD_LAYOUT      0x09
#define EFI_HII_PACKAGE_ANIMATIONS           0x0A
#define EFI_HII_PACKAGE_END                  0xDF
#define EFI_HII_PACKAGE_TYPE_SYSTEM_BEGIN    0xE0
#define EFI_HII_PACKAGE_TYPE_SYSTEM_END      0xFF

//
// Definitions for Simplified Font Package
//

///
/// Contents of EFI_NARROW_GLYPH.Attributes.
///@{
#define EFI_GLYPH_NON_SPACING                0x01
#define EFI_GLYPH_WIDE                       0x02
#define EFI_GLYPH_HEIGHT                     19
#define EFI_GLYPH_WIDTH                      8
///@}

///
/// The EFI_NARROW_GLYPH has a preferred dimension (w x h) of 8 x 19 pixels.
///
typedef struct {
  ///
  /// The Unicode representation of the glyph. The term weight is the 
  /// technical term for a character code.
  ///
  CHAR16                 UnicodeWeight;
  ///
  /// The data element containing the glyph definitions.
  ///
  UINT8                  Attributes;
  ///
  /// The column major glyph representation of the character. Bits 
  /// with values of one indicate that the corresponding pixel is to be
  /// on when normally displayed; those with zero are off.
  ///
  UINT8                  GlyphCol1[EFI_GLYPH_HEIGHT];
} EFI_NARROW_GLYPH;

///
/// The EFI_WIDE_GLYPH has a preferred dimension (w x h) of 16 x 19 pixels, which is large enough 
/// to accommodate logographic characters.
///
typedef struct {
  ///
  /// The Unicode representation of the glyph. The term weight is the 
  /// technical term for a character code.
  ///
  CHAR16                 UnicodeWeight;
  ///
  /// The data element containing the glyph definitions.
  ///
  UINT8                  Attributes;
  ///
  /// The column major glyph representation of the character. Bits 
  /// with values of one indicate that the corresponding pixel is to be 
  /// on when normally displayed; those with zero are off.
  ///
  UINT8                  GlyphCol1[EFI_GLYPH_HEIGHT];
  ///
  /// The column major glyph representation of the character. Bits 
  /// with values of one indicate that the corresponding pixel is to be 
  /// on when normally displayed; those with zero are off.
  ///
  UINT8                  GlyphCol2[EFI_GLYPH_HEIGHT];
  ///
  /// Ensures that sizeof (EFI_WIDE_GLYPH) is twice the 
  /// sizeof (EFI_NARROW_GLYPH). The contents of Pad must 
  /// be zero.
  ///
  UINT8                  Pad[3];
} EFI_WIDE_GLYPH;

///
/// A simplified font package consists of a font header
/// followed by a series of glyph structures.
///
typedef struct _EFI_HII_SIMPLE_FONT_PACKAGE_HDR {
  EFI_HII_PACKAGE_HEADER Header;
  UINT16                 NumberOfNarrowGlyphs;
  UINT16                 NumberOfWideGlyphs;
  // EFI_NARROW_GLYPH       NarrowGlyphs[];
  // EFI_WIDE_GLYPH         WideGlyphs[];
} EFI_HII_SIMPLE_FONT_PACKAGE_HDR;

//
// Definitions for Font Package
// Section 27.3.3
//

//
// Value for font style
//
#define EFI_HII_FONT_STYLE_NORMAL            0x00000000
#define EFI_HII_FONT_STYLE_BOLD              0x00000001
#define EFI_HII_FONT_STYLE_ITALIC            0x00000002
#define EFI_HII_FONT_STYLE_EMBOSS            0x00010000
#define EFI_HII_FONT_STYLE_OUTLINE           0x00020000
#define EFI_HII_FONT_STYLE_SHADOW            0x00040000
#define EFI_HII_FONT_STYLE_UNDERLINE         0x00080000
#define EFI_HII_FONT_STYLE_DBL_UNDER         0x00100000

typedef struct _EFI_HII_GLYPH_INFO {
  UINT16                 Width;
  UINT16                 Height;
  INT16                  OffsetX;
  INT16                  OffsetY;
  INT16                  AdvanceX;
} EFI_HII_GLYPH_INFO;

///
/// The fixed header consists of a standard record header,
/// then the character values in this section, the flags
/// (including the encoding method) and the offsets of the glyph
/// information, the glyph bitmaps and the character map.
///
typedef struct _EFI_HII_FONT_PACKAGE_HDR {
  EFI_HII_PACKAGE_HEADER Header;
  UINT32                 HdrSize;
  UINT32                 GlyphBlockOffset;
  EFI_HII_GLYPH_INFO     Cell;
  EFI_HII_FONT_STYLE     FontStyle;
  CHAR16                 FontFamily[1];
} EFI_HII_FONT_PACKAGE_HDR;

//
// Value of different glyph info block types
//
#define EFI_HII_GIBT_END                  0x00
#define EFI_HII_GIBT_GLYPH                0x10
#define EFI_HII_GIBT_GLYPHS               0x11
#define EFI_HII_GIBT_GLYPH_DEFAULT        0x12
#define EFI_HII_GIBT_GLYPHS_DEFAULT       0x13
#define EFI_HII_GIBT_GLYPH_VARIABILITY    0x14
#define EFI_HII_GIBT_DUPLICATE            0x20
#define EFI_HII_GIBT_SKIP2                0x21
#define EFI_HII_GIBT_SKIP1                0x22
#define EFI_HII_GIBT_DEFAULTS             0x23
#define EFI_HII_GIBT_EXT1                 0x30
#define EFI_HII_GIBT_EXT2                 0x31
#define EFI_HII_GIBT_EXT4                 0x32

typedef struct _EFI_HII_GLYPH_BLOCK {
  UINT8                  BlockType;
} EFI_HII_GLYPH_BLOCK;

//
// Definition of different glyph info block types
//

typedef struct _EFI_HII_GIBT_DEFAULTS_BLOCK {
  EFI_HII_GLYPH_BLOCK    Header;
  EFI_HII_GLYPH_INFO     Cell;
} EFI_HII_GIBT_DEFAULTS_BLOCK;

typedef struct _EFI_HII_GIBT_DUPLICATE_BLOCK {
  EFI_HII_GLYPH_BLOCK    Header;
  CHAR16                 CharValue;
} EFI_HII_GIBT_DUPLICATE_BLOCK;

typedef struct _EFI_GLYPH_GIBT_END_BLOCK {
  EFI_HII_GLYPH_BLOCK    Header;
} EFI_GLYPH_GIBT_END_BLOCK;

typedef struct _EFI_HII_GIBT_EXT1_BLOCK {
  EFI_HII_GLYPH_BLOCK    Header;
  UINT8                  BlockType2;
  UINT8                  Length;
} EFI_HII_GIBT_EXT1_BLOCK;

typedef struct _EFI_HII_GIBT_EXT2_BLOCK {
  EFI_HII_GLYPH_BLOCK    Header;
  UINT8                  BlockType2;
  UINT16                 Length;
} EFI_HII_GIBT_EXT2_BLOCK;

typedef struct _EFI_HII_GIBT_EXT4_BLOCK {
  EFI_HII_GLYPH_BLOCK    Header;
  UINT8                  BlockType2;
  UINT32                 Length;
} EFI_HII_GIBT_EXT4_BLOCK;

typedef struct _EFI_HII_GIBT_GLYPH_BLOCK {
  EFI_HII_GLYPH_BLOCK    Header;
  EFI_HII_GLYPH_INFO     Cell;
  UINT8                  BitmapData[1];
} EFI_HII_GIBT_GLYPH_BLOCK;

typedef struct _EFI_HII_GIBT_GLYPHS_BLOCK {
  EFI_HII_GLYPH_BLOCK    Header;
  EFI_HII_GLYPH_INFO     Cell;
  UINT16                 Count;  
  UINT8                  BitmapData[1];
} EFI_HII_GIBT_GLYPHS_BLOCK;

typedef struct _EFI_HII_GIBT_GLYPH_DEFAULT_BLOCK {
  EFI_HII_GLYPH_BLOCK    Header;
  UINT8                  BitmapData[1];
} EFI_HII_GIBT_GLYPH_DEFAULT_BLOCK;

typedef struct _EFI_HII_GIBT_GLYPHS_DEFAULT_BLOCK {
  EFI_HII_GLYPH_BLOCK    Header;
  UINT16                 Count;
  UINT8                  BitmapData[1];
} EFI_HII_GIBT_GLYPHS_DEFAULT_BLOCK;

typedef struct _EFI_HII_GIBT_VARIABILITY_BLOCK {
  EFI_HII_GLYPH_BLOCK    Header;
  EFI_HII_GLYPH_INFO     Cell;
  UINT8                  GlyphPackInBits;
  UINT8                  BitmapData [1];
} EFI_HII_GIBT_VARIABILITY_BLOCK;

typedef struct _EFI_HII_GIBT_SKIP1_BLOCK {
  EFI_HII_GLYPH_BLOCK    Header;
  UINT8                  SkipCount;
} EFI_HII_GIBT_SKIP1_BLOCK;

typedef struct _EFI_HII_GIBT_SKIP2_BLOCK {
  EFI_HII_GLYPH_BLOCK    Header;
  UINT16                 SkipCount;
} EFI_HII_GIBT_SKIP2_BLOCK;

//
// Definitions for Device Path Package
// Section 27.3.4
//

///
/// The device path package is used to carry a device path
/// associated with the package list.
///
typedef struct _EFI_HII_DEVICE_PATH_PACKAGE_HDR {
  EFI_HII_PACKAGE_HEADER   Header;
  // EFI_DEVICE_PATH_PROTOCOL DevicePath[];
} EFI_HII_DEVICE_PATH_PACKAGE_HDR;

//
// Definitions for GUID Package
// Section 27.3.5
//

///
/// The GUID package is used to carry data where the format is defined by a GUID.
///
typedef struct _EFI_HII_GUID_PACKAGE_HDR {
  EFI_HII_PACKAGE_HEADER  Header;
  EFI_GUID                Guid;
  // Data per GUID definition may follow
} EFI_HII_GUID_PACKAGE_HDR;

//
// Definitions for String Package
// Section 27.3.6
//

#define UEFI_CONFIG_LANG   "x-UEFI"
#define UEFI_CONFIG_LANG_2 "x-i-UEFI"

///
/// The fixed header consists of a standard record header and then the string identifiers
/// contained in this section and the offsets of the string and language information.
///
typedef struct _EFI_HII_STRING_PACKAGE_HDR {
  EFI_HII_PACKAGE_HEADER  Header;
  UINT32                  HdrSize;
  UINT32                  StringInfoOffset;
  CHAR16                  LanguageWindow[16];
  EFI_STRING_ID           LanguageName;
  CHAR8                   Language[1];
} EFI_HII_STRING_PACKAGE_HDR;

typedef struct {
  UINT8                   BlockType;
} EFI_HII_STRING_BLOCK;

//
// Value of different string information block types
//
#define EFI_HII_SIBT_END                     0x00
#define EFI_HII_SIBT_STRING_SCSU             0x10
#define EFI_HII_SIBT_STRING_SCSU_FONT        0x11
#define EFI_HII_SIBT_STRINGS_SCSU            0x12
#define EFI_HII_SIBT_STRINGS_SCSU_FONT       0x13
#define EFI_HII_SIBT_STRING_UCS2             0x14
#define EFI_HII_SIBT_STRING_UCS2_FONT        0x15
#define EFI_HII_SIBT_STRINGS_UCS2            0x16
#define EFI_HII_SIBT_STRINGS_UCS2_FONT       0x17
#define EFI_HII_SIBT_DUPLICATE               0x20
#define EFI_HII_SIBT_SKIP2                   0x21
#define EFI_HII_SIBT_SKIP1                   0x22
#define EFI_HII_SIBT_EXT1                    0x30
#define EFI_HII_SIBT_EXT2                    0x31
#define EFI_HII_SIBT_EXT4                    0x32
#define EFI_HII_SIBT_FONT                    0x40

//
// Definition of different string information block types
//

typedef struct _EFI_HII_SIBT_DUPLICATE_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
  EFI_STRING_ID           StringId;
} EFI_HII_SIBT_DUPLICATE_BLOCK;

typedef struct _EFI_HII_SIBT_END_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
} EFI_HII_SIBT_END_BLOCK;

typedef struct _EFI_HII_SIBT_EXT1_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
  UINT8                   BlockType2;
  UINT8                   Length;
} EFI_HII_SIBT_EXT1_BLOCK;

typedef struct _EFI_HII_SIBT_EXT2_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
  UINT8                   BlockType2;
  UINT16                  Length;
} EFI_HII_SIBT_EXT2_BLOCK;

typedef struct _EFI_HII_SIBT_EXT4_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
  UINT8                   BlockType2;
  UINT32                  Length;
} EFI_HII_SIBT_EXT4_BLOCK;

typedef struct _EFI_HII_SIBT_FONT_BLOCK {
  EFI_HII_SIBT_EXT2_BLOCK Header;
  UINT8                   FontId;
  UINT16                  FontSize;
  EFI_HII_FONT_STYLE      FontStyle;
  CHAR16                  FontName[1];
} EFI_HII_SIBT_FONT_BLOCK;

typedef struct _EFI_HII_SIBT_SKIP1_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
  UINT8                   SkipCount;
} EFI_HII_SIBT_SKIP1_BLOCK;

typedef struct _EFI_HII_SIBT_SKIP2_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
  UINT16                  SkipCount;
} EFI_HII_SIBT_SKIP2_BLOCK;

typedef struct _EFI_HII_SIBT_STRING_SCSU_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
  UINT8                   StringText[1];
} EFI_HII_SIBT_STRING_SCSU_BLOCK;

typedef struct _EFI_HII_SIBT_STRING_SCSU_FONT_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
  UINT8                   FontIdentifier;
  UINT8                   StringText[1];
} EFI_HII_SIBT_STRING_SCSU_FONT_BLOCK;

typedef struct _EFI_HII_SIBT_STRINGS_SCSU_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
  UINT16                  StringCount;
  UINT8                   StringText[1];
} EFI_HII_SIBT_STRINGS_SCSU_BLOCK;

typedef struct _EFI_HII_SIBT_STRINGS_SCSU_FONT_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
  UINT8                   FontIdentifier;
  UINT16                  StringCount;
  UINT8                   StringText[1];
} EFI_HII_SIBT_STRINGS_SCSU_FONT_BLOCK;

typedef struct _EFI_HII_SIBT_STRING_UCS2_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
  CHAR16                  StringText[1];
} EFI_HII_SIBT_STRING_UCS2_BLOCK;

typedef struct _EFI_HII_SIBT_STRING_UCS2_FONT_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
  UINT8                   FontIdentifier;
  CHAR16                  StringText[1];
} EFI_HII_SIBT_STRING_UCS2_FONT_BLOCK;

typedef struct _EFI_HII_SIBT_STRINGS_UCS2_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
  UINT16                  StringCount;
  CHAR16                  StringText[1];
} EFI_HII_SIBT_STRINGS_UCS2_BLOCK;

typedef struct _EFI_HII_SIBT_STRINGS_UCS2_FONT_BLOCK {
  EFI_HII_STRING_BLOCK    Header;
  UINT8                   FontIdentifier;
  UINT16                  StringCount;
  CHAR16                  StringText[1];
} EFI_HII_SIBT_STRINGS_UCS2_FONT_BLOCK;

//
// Definitions for Image Package
// Section 27.3.7
//

typedef struct _EFI_HII_IMAGE_PACKAGE_HDR {
  EFI_HII_PACKAGE_HEADER  Header;
  UINT32                  ImageInfoOffset;
  UINT32                  PaletteInfoOffset;
} EFI_HII_IMAGE_PACKAGE_HDR;

typedef struct _EFI_HII_IMAGE_BLOCK {
  UINT8                   BlockType;
} EFI_HII_IMAGE_BLOCK;

//
// Value of different image information block types
//
#define EFI_HII_IIBT_END               0x00
#define EFI_HII_IIBT_IMAGE_1BIT        0x10
#define EFI_HII_IIBT_IMAGE_1BIT_TRANS  0x11
#define EFI_HII_IIBT_IMAGE_4BIT        0x12
#define EFI_HII_IIBT_IMAGE_4BIT_TRANS  0x13
#define EFI_HII_IIBT_IMAGE_8BIT        0x14
#define EFI_HII_IIBT_IMAGE_8BIT_TRANS  0x15
#define EFI_HII_IIBT_IMAGE_24BIT       0x16
#define EFI_HII_IIBT_IMAGE_24BIT_TRANS 0x17
#define EFI_HII_IIBT_IMAGE_JPEG        0x18
#define EFI_HII_IIBT_IMAGE_PNG         0x19
#define EFI_HII_IIBT_DUPLICATE         0x20
#define EFI_HII_IIBT_SKIP2             0x21
#define EFI_HII_IIBT_SKIP1             0x22
#define EFI_HII_IIBT_EXT1              0x30
#define EFI_HII_IIBT_EXT2              0x31
#define EFI_HII_IIBT_EXT4              0x32

//
// Definition of different image information block types
//

typedef struct _EFI_HII_IIBT_END_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
} EFI_HII_IIBT_END_BLOCK;

typedef struct _EFI_HII_IIBT_EXT1_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
  UINT8                        BlockType2;
  UINT8                        Length;
} EFI_HII_IIBT_EXT1_BLOCK;

typedef struct _EFI_HII_IIBT_EXT2_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
  UINT8                        BlockType2;
  UINT16                       Length;
} EFI_HII_IIBT_EXT2_BLOCK;

typedef struct _EFI_HII_IIBT_EXT4_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
  UINT8                        BlockType2;
  UINT32                       Length;
} EFI_HII_IIBT_EXT4_BLOCK;

typedef struct _EFI_HII_IIBT_IMAGE_1BIT_BASE {
  UINT16                       Width;
  UINT16                       Height;
  UINT8                        Data[1];
} EFI_HII_IIBT_IMAGE_1BIT_BASE;

typedef struct _EFI_HII_IIBT_IMAGE_1BIT_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
  UINT8                        PaletteIndex;
  EFI_HII_IIBT_IMAGE_1BIT_BASE Bitmap;
} EFI_HII_IIBT_IMAGE_1BIT_BLOCK;

typedef struct _EFI_HII_IIBT_IMAGE_1BIT_TRANS_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
  UINT8                        PaletteIndex;
  EFI_HII_IIBT_IMAGE_1BIT_BASE Bitmap;
} EFI_HII_IIBT_IMAGE_1BIT_TRANS_BLOCK;

typedef struct _EFI_HII_RGB_PIXEL {
  UINT8                        b;
  UINT8                        g;
  UINT8                        r;
} EFI_HII_RGB_PIXEL;

typedef struct _EFI_HII_IIBT_IMAGE_24BIT_BASE {
  UINT16                       Width;
  UINT16                       Height;
  EFI_HII_RGB_PIXEL            Bitmap[1];
} EFI_HII_IIBT_IMAGE_24BIT_BASE;

typedef struct _EFI_HII_IIBT_IMAGE_24BIT_BLOCK {
  EFI_HII_IMAGE_BLOCK           Header;
  EFI_HII_IIBT_IMAGE_24BIT_BASE Bitmap;
} EFI_HII_IIBT_IMAGE_24BIT_BLOCK;

typedef struct _EFI_HII_IIBT_IMAGE_24BIT_TRANS_BLOCK {
  EFI_HII_IMAGE_BLOCK           Header;
  EFI_HII_IIBT_IMAGE_24BIT_BASE Bitmap;
} EFI_HII_IIBT_IMAGE_24BIT_TRANS_BLOCK;

typedef struct _EFI_HII_IIBT_IMAGE_4BIT_BASE {
  UINT16                       Width;
  UINT16                       Height;
  UINT8                        Data[1];
} EFI_HII_IIBT_IMAGE_4BIT_BASE;

typedef struct _EFI_HII_IIBT_IMAGE_4BIT_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
  UINT8                        PaletteIndex;
  EFI_HII_IIBT_IMAGE_4BIT_BASE Bitmap;
} EFI_HII_IIBT_IMAGE_4BIT_BLOCK;

typedef struct _EFI_HII_IIBT_IMAGE_4BIT_TRANS_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
  UINT8                        PaletteIndex;
  EFI_HII_IIBT_IMAGE_4BIT_BASE Bitmap;
} EFI_HII_IIBT_IMAGE_4BIT_TRANS_BLOCK;

typedef struct _EFI_HII_IIBT_IMAGE_8BIT_BASE {
  UINT16                       Width;
  UINT16                       Height;
  UINT8                        Data[1];
} EFI_HII_IIBT_IMAGE_8BIT_BASE;

typedef struct _EFI_HII_IIBT_IMAGE_8BIT_PALETTE_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
  UINT8                        PaletteIndex;
  EFI_HII_IIBT_IMAGE_8BIT_BASE Bitmap;
} EFI_HII_IIBT_IMAGE_8BIT_BLOCK;

typedef struct _EFI_HII_IIBT_IMAGE_8BIT_TRANS_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
  UINT8                        PaletteIndex;
  EFI_HII_IIBT_IMAGE_8BIT_BASE Bitmap;
} EFI_HII_IIBT_IMAGE_8BIT_TRAN_BLOCK;

typedef struct _EFI_HII_IIBT_DUPLICATE_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
  EFI_IMAGE_ID                 ImageId;
} EFI_HII_IIBT_DUPLICATE_BLOCK;

typedef struct _EFI_HII_IIBT_JPEG_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
  UINT32                       Size;
  UINT8                        Data[1];
} EFI_HII_IIBT_JPEG_BLOCK;

typedef struct _EFI_HII_IIBT_PNG_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
  UINT32                       Size;
  UINT8                        Data[1];
} EFI_HII_IIBT_PNG_BLOCK;

typedef struct _EFI_HII_IIBT_SKIP1_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
  UINT8                        SkipCount;
} EFI_HII_IIBT_SKIP1_BLOCK;

typedef struct _EFI_HII_IIBT_SKIP2_BLOCK {
  EFI_HII_IMAGE_BLOCK          Header;
  UINT16                       SkipCount;
} EFI_HII_IIBT_SKIP2_BLOCK;

//
// Definitions for Palette Information
//

typedef struct _EFI_HII_IMAGE_PALETTE_INFO_HEADER {
  UINT16                       PaletteCount;
} EFI_HII_IMAGE_PALETTE_INFO_HEADER;

typedef struct _EFI_HII_IMAGE_PALETTE_INFO {
  UINT16                       PaletteSize;
  EFI_HII_RGB_PIXEL            PaletteValue[1];
} EFI_HII_IMAGE_PALETTE_INFO;

//
// Definitions for Forms Package
// Section 27.3.8
//

///
/// The Form package is used to carry form-based encoding data.
///
typedef struct _EFI_HII_FORM_PACKAGE_HDR {
  EFI_HII_PACKAGE_HEADER       Header;
  // EFI_IFR_OP_HEADER         OpCodeHeader;
  // More op-codes follow
} EFI_HII_FORM_PACKAGE_HDR;

typedef struct {
  UINT8 Hour;
  UINT8 Minute;
  UINT8 Second;
} EFI_HII_TIME;

typedef struct {
  UINT16 Year;
  UINT8  Month;
  UINT8  Day;
} EFI_HII_DATE;

typedef struct {
  EFI_QUESTION_ID QuestionId;
  EFI_FORM_ID     FormId;
  EFI_GUID        FormSetGuid;
  EFI_STRING_ID   DevicePath;
} EFI_HII_REF;

typedef union {
  UINT8           u8;
  UINT16          u16;
  UINT32          u32;
  UINT64          u64;
  BOOLEAN         b;
  EFI_HII_TIME    time;
  EFI_HII_DATE    date;
  EFI_STRING_ID   string; ///< EFI_IFR_TYPE_STRING, EFI_IFR_TYPE_ACTION
  EFI_HII_REF     ref;    ///< EFI_IFR_TYPE_REF
  // UINT8 buffer[];      ///< EFI_IFR_TYPE_BUFFER
} EFI_IFR_TYPE_VALUE;

//
// IFR Opcodes
//
#define EFI_IFR_FORM_OP                0x01
#define EFI_IFR_SUBTITLE_OP            0x02
#define EFI_IFR_TEXT_OP                0x03
#define EFI_IFR_IMAGE_OP               0x04
#define EFI_IFR_ONE_OF_OP              0x05
#define EFI_IFR_CHECKBOX_OP            0x06
#define EFI_IFR_NUMERIC_OP             0x07
#define EFI_IFR_PASSWORD_OP            0x08
#define EFI_IFR_ONE_OF_OPTION_OP       0x09
#define EFI_IFR_SUPPRESS_IF_OP         0x0A
#define EFI_IFR_LOCKED_OP              0x0B
#define EFI_IFR_ACTION_OP              0x0C
#define EFI_IFR_RESET_BUTTON_OP        0x0D
#define EFI_IFR_FORM_SET_OP            0x0E
#define EFI_IFR_REF_OP                 0x0F
#define EFI_IFR_NO_SUBMIT_IF_OP        0x10
#define EFI_IFR_INCONSISTENT_IF_OP     0x11
#define EFI_IFR_EQ_ID_VAL_OP           0x12
#define EFI_IFR_EQ_ID_ID_OP            0x13
#define EFI_IFR_EQ_ID_VAL_LIST_OP      0x14
#define EFI_IFR_AND_OP                 0x15
#define EFI_IFR_OR_OP                  0x16
#define EFI_IFR_NOT_OP                 0x17
#define EFI_IFR_RULE_OP                0x18
#define EFI_IFR_GRAY_OUT_IF_OP         0x19
#define EFI_IFR_DATE_OP                0x1A
#define EFI_IFR_TIME_OP                0x1B
#define EFI_IFR_STRING_OP              0x1C
#define EFI_IFR_REFRESH_OP             0x1D
#define EFI_IFR_DISABLE_IF_OP          0x1E
#define EFI_IFR_ANIMATION_OP           0x1F
#define EFI_IFR_TO_LOWER_OP            0x20
#define EFI_IFR_TO_UPPER_OP            0x21
#define EFI_IFR_MAP_OP                 0x22
#define EFI_IFR_ORDERED_LIST_OP        0x23
#define EFI_IFR_VARSTORE_OP            0x24
#define EFI_IFR_VARSTORE_NAME_VALUE_OP 0x25
#define EFI_IFR_VARSTORE_EFI_OP        0x26
#define EFI_IFR_VARSTORE_DEVICE_OP     0x27
#define EFI_IFR_VERSION_OP             0x28
#define EFI_IFR_END_OP                 0x29
#define EFI_IFR_MATCH_OP               0x2A
#define EFI_IFR_GET_OP                 0x2B
#define EFI_IFR_SET_OP                 0x2C
#define EFI_IFR_READ_OP                0x2D
#define EFI_IFR_WRITE_OP               0x2E
#define EFI_IFR_EQUAL_OP               0x2F
#define EFI_IFR_NOT_EQUAL_OP           0x30
#define EFI_IFR_GREATER_THAN_OP        0x31
#define EFI_IFR_GREATER_EQUAL_OP       0x32
#define EFI_IFR_LESS_THAN_OP           0x33
#define EFI_IFR_LESS_EQUAL_OP          0x34
#define EFI_IFR_BITWISE_AND_OP         0x35
#define EFI_IFR_BITWISE_OR_OP          0x36
#define EFI_IFR_BITWISE_NOT_OP         0x37
#define EFI_IFR_SHIFT_LEFT_OP          0x38
#define EFI_IFR_SHIFT_RIGHT_OP         0x39
#define EFI_IFR_ADD_OP                 0x3A
#define EFI_IFR_SUBTRACT_OP            0x3B
#define EFI_IFR_MULTIPLY_OP            0x3C
#define EFI_IFR_DIVIDE_OP              0x3D
#define EFI_IFR_MODULO_OP              0x3E
#define EFI_IFR_RULE_REF_OP            0x3F
#define EFI_IFR_QUESTION_REF1_OP       0x40
#define EFI_IFR_QUESTION_REF2_OP       0x41
#define EFI_IFR_UINT8_OP               0x42
#define EFI_IFR_UINT16_OP              0x43
#define EFI_IFR_UINT32_OP              0x44
#define EFI_IFR_UINT64_OP              0x45
#define EFI_IFR_TRUE_OP                0x46
#define EFI_IFR_FALSE_OP               0x47
#define EFI_IFR_TO_UINT_OP             0x48
#define EFI_IFR_TO_STRING_OP           0x49
#define EFI_IFR_TO_BOOLEAN_OP          0x4A
#define EFI_IFR_MID_OP                 0x4B
#define EFI_IFR_FIND_OP                0x4C
#define EFI_IFR_TOKEN_OP               0x4D
#define EFI_IFR_STRING_REF1_OP         0x4E
#define EFI_IFR_STRING_REF2_OP         0x4F
#define EFI_IFR_CONDITIONAL_OP         0x50
#define EFI_IFR_QUESTION_REF3_OP       0x51
#define EFI_IFR_ZERO_OP                0x52
#define EFI_IFR_ONE_OP                 0x53
#define EFI_IFR_ONES_OP                0x54
#define EFI_IFR_UNDEFINED_OP           0x55
#define EFI_IFR_LENGTH_OP              0x56
#define EFI_IFR_DUP_OP                 0x57
#define EFI_IFR_THIS_OP                0x58
#define EFI_IFR_SPAN_OP                0x59
#define EFI_IFR_VALUE_OP               0x5A
#define EFI_IFR_DEFAULT_OP             0x5B
#define EFI_IFR_DEFAULTSTORE_OP        0x5C
#define EFI_IFR_FORM_MAP_OP            0x5D
#define EFI_IFR_CATENATE_OP            0x5E
#define EFI_IFR_GUID_OP                0x5F
#define EFI_IFR_SECURITY_OP            0x60
#define EFI_IFR_MODAL_TAG_OP           0x61
#define EFI_IFR_REFRESH_ID_OP          0x62
#define EFI_IFR_WARNING_IF_OP          0x63
#define EFI_IFR_MATCH2_OP              0x64

//
// Definitions of IFR Standard Headers
// Section 27.3.8.2
//

typedef struct _EFI_IFR_OP_HEADER {
  UINT8                    OpCode;
  UINT8                    Length:7;
  UINT8                    Scope:1;
} EFI_IFR_OP_HEADER;

typedef struct _EFI_IFR_STATEMENT_HEADER {
  EFI_STRING_ID            Prompt;
  EFI_STRING_ID            Help;
} EFI_IFR_STATEMENT_HEADER;

typedef struct _EFI_IFR_QUESTION_HEADER {
  EFI_IFR_STATEMENT_HEADER Header;
  EFI_QUESTION_ID          QuestionId;
  EFI_VARSTORE_ID          VarStoreId;
  union {
    EFI_STRING_ID          VarName;
    UINT16                 VarOffset;
  }                        VarStoreInfo;
  UINT8                    Flags;
} EFI_IFR_QUESTION_HEADER;

//
// Flag values of EFI_IFR_QUESTION_HEADER
//
#define EFI_IFR_FLAG_READ_ONLY          0x01
#define EFI_IFR_FLAG_CALLBACK           0x04
#define EFI_IFR_FLAG_RESET_REQUIRED     0x10
#define EFI_IFR_FLAG_RECONNECT_REQUIRED 0x40
#define EFI_IFR_FLAG_OPTIONS_ONLY       0x80

//
// Definition for Opcode Reference
// Section 27.3.8.3
//
typedef struct _EFI_IFR_DEFAULTSTORE {
  EFI_IFR_OP_HEADER        Header;
  EFI_STRING_ID            DefaultName;
  UINT16                   DefaultId;
} EFI_IFR_DEFAULTSTORE;

//
// Default Identifier of default store 
//
#define EFI_HII_DEFAULT_CLASS_STANDARD       0x0000
#define EFI_HII_DEFAULT_CLASS_MANUFACTURING  0x0001
#define EFI_HII_DEFAULT_CLASS_SAFE           0x0002
#define EFI_HII_DEFAULT_CLASS_PLATFORM_BEGIN 0x4000
#define EFI_HII_DEFAULT_CLASS_PLATFORM_END   0x7fff
#define EFI_HII_DEFAULT_CLASS_HARDWARE_BEGIN 0x8000
#define EFI_HII_DEFAULT_CLASS_HARDWARE_END   0xbfff
#define EFI_HII_DEFAULT_CLASS_FIRMWARE_BEGIN 0xc000
#define EFI_HII_DEFAULT_CLASS_FIRMWARE_END   0xffff

typedef struct _EFI_IFR_VARSTORE {
  EFI_IFR_OP_HEADER        Header;
  EFI_GUID                 Guid;
  EFI_VARSTORE_ID          VarStoreId;
  UINT16                   Size;
  UINT8                    Name[1];
} EFI_IFR_VARSTORE;

typedef struct _EFI_IFR_VARSTORE_EFI {
  EFI_IFR_OP_HEADER        Header;
  EFI_VARSTORE_ID          VarStoreId;
  EFI_GUID                 Guid;
  UINT32                   Attributes;
  UINT16                   Size;
  UINT8                    Name[1];
} EFI_IFR_VARSTORE_EFI;

typedef struct _EFI_IFR_VARSTORE_NAME_VALUE {
  EFI_IFR_OP_HEADER        Header;
  EFI_VARSTORE_ID          VarStoreId;
  EFI_GUID                 Guid;
} EFI_IFR_VARSTORE_NAME_VALUE;

typedef struct _EFI_IFR_FORM_SET {
  EFI_IFR_OP_HEADER        Header;
  EFI_GUID                 Guid;
  EFI_STRING_ID            FormSetTitle;
  EFI_STRING_ID            Help;
  UINT8                    Flags;
  // EFI_GUID              ClassGuid[];
} EFI_IFR_FORM_SET;

typedef struct _EFI_IFR_END {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_END;

typedef struct _EFI_IFR_FORM {
  EFI_IFR_OP_HEADER        Header;
  UINT16                   FormId;
  EFI_STRING_ID            FormTitle;
} EFI_IFR_FORM;

typedef struct _EFI_IFR_IMAGE {
  EFI_IFR_OP_HEADER        Header;
  EFI_IMAGE_ID             Id;
} EFI_IFR_IMAGE;

typedef struct _EFI_IFR_MODAL_TAG {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_MODAL_TAG;

typedef struct _EFI_IFR_LOCKED {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_LOCKED;

typedef struct _EFI_IFR_RULE {
  EFI_IFR_OP_HEADER        Header;
  UINT8                    RuleId;
} EFI_IFR_RULE;

typedef struct _EFI_IFR_DEFAULT {
  EFI_IFR_OP_HEADER        Header;
  UINT16                   DefaultId;
  UINT8                    Type;
  EFI_IFR_TYPE_VALUE       Value;
} EFI_IFR_DEFAULT;

typedef struct _EFI_IFR_DEFAULT_2 {
  EFI_IFR_OP_HEADER        Header;
  UINT16                   DefaultId;
  UINT8                    Type;
} EFI_IFR_DEFAULT_2;

typedef struct _EFI_IFR_VALUE {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_VALUE;

typedef struct _EFI_IFR_SUBTITLE {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_STATEMENT_HEADER Statement;
  UINT8                    Flags;
} EFI_IFR_SUBTITLE;

#define EFI_IFR_FLAGS_HORIZONTAL       0x01

typedef struct _EFI_IFR_CHECKBOX {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_QUESTION_HEADER  Question;
  UINT8                    Flags;
} EFI_IFR_CHECKBOX;

#define EFI_IFR_CHECKBOX_DEFAULT       0x01
#define EFI_IFR_CHECKBOX_DEFAULT_MFG   0x02

typedef struct _EFI_IFR_TEXT {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_STATEMENT_HEADER Statement;
  EFI_STRING_ID            TextTwo;
} EFI_IFR_TEXT;

typedef struct _EFI_IFR_REF {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_QUESTION_HEADER  Question;
  EFI_FORM_ID              FormId;
} EFI_IFR_REF;

typedef struct _EFI_IFR_REF2 {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_QUESTION_HEADER  Question;
  EFI_FORM_ID              FormId;
  EFI_QUESTION_ID          QuestionId;
} EFI_IFR_REF2;

typedef struct _EFI_IFR_REF3 {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_QUESTION_HEADER  Question;
  EFI_FORM_ID              FormId;
  EFI_QUESTION_ID          QuestionId;
  EFI_GUID                 FormSetId;
} EFI_IFR_REF3;

typedef struct _EFI_IFR_REF4 {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_QUESTION_HEADER  Question;
  EFI_FORM_ID              FormId;
  EFI_QUESTION_ID          QuestionId;
  EFI_GUID                 FormSetId;
  EFI_STRING_ID            DevicePath;
} EFI_IFR_REF4;

typedef struct _EFI_IFR_REF5 {
  EFI_IFR_OP_HEADER Header;
  EFI_IFR_QUESTION_HEADER Question;
} EFI_IFR_REF5;

typedef struct _EFI_IFR_RESET_BUTTON {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_STATEMENT_HEADER Statement;
  EFI_DEFAULT_ID           DefaultId;
} EFI_IFR_RESET_BUTTON;

typedef struct _EFI_IFR_ACTION {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_QUESTION_HEADER  Question;
  EFI_STRING_ID            QuestionConfig;
} EFI_IFR_ACTION;

typedef struct _EFI_IFR_ACTION_1 {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_QUESTION_HEADER  Question;
} EFI_IFR_ACTION_1;

typedef struct _EFI_IFR_DATE {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_QUESTION_HEADER  Question;
  UINT8                    Flags;
} EFI_IFR_DATE;

//
// Flags that describe the behavior of the question.
//
#define EFI_QF_DATE_YEAR_SUPPRESS      0x01
#define EFI_QF_DATE_MONTH_SUPPRESS     0x02
#define EFI_QF_DATE_DAY_SUPPRESS       0x04

#define EFI_QF_DATE_STORAGE            0x30
#define     QF_DATE_STORAGE_NORMAL     0x00
#define     QF_DATE_STORAGE_TIME       0x10
#define     QF_DATE_STORAGE_WAKEUP     0x20

typedef union {
  struct {
    UINT8 MinValue;
    UINT8 MaxValue;
    UINT8 Step;
  } u8;
  struct {
    UINT16 MinValue;
    UINT16 MaxValue;
    UINT16 Step;
  } u16;
  struct {
    UINT32 MinValue;
    UINT32 MaxValue;
    UINT32 Step;
  } u32;
  struct {
    UINT64 MinValue;
    UINT64 MaxValue;
    UINT64 Step;
  } u64;
} MINMAXSTEP_DATA;

typedef struct _EFI_IFR_NUMERIC {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_QUESTION_HEADER  Question;
  UINT8                    Flags;
  MINMAXSTEP_DATA          data;
} EFI_IFR_NUMERIC;

//
// Flags related to the numeric question
//
#define EFI_IFR_NUMERIC_SIZE           0x03
#define   EFI_IFR_NUMERIC_SIZE_1       0x00
#define   EFI_IFR_NUMERIC_SIZE_2       0x01
#define   EFI_IFR_NUMERIC_SIZE_4       0x02
#define   EFI_IFR_NUMERIC_SIZE_8       0x03

#define EFI_IFR_DISPLAY                0x30
#define   EFI_IFR_DISPLAY_INT_DEC      0x00
#define   EFI_IFR_DISPLAY_UINT_DEC     0x10
#define   EFI_IFR_DISPLAY_UINT_HEX     0x20

typedef struct _EFI_IFR_ONE_OF {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_QUESTION_HEADER  Question;
  UINT8                    Flags;
  MINMAXSTEP_DATA          data;
} EFI_IFR_ONE_OF;

typedef struct _EFI_IFR_STRING {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_QUESTION_HEADER  Question;
  UINT8                    MinSize;
  UINT8                    MaxSize;
  UINT8                    Flags;
} EFI_IFR_STRING;

#define EFI_IFR_STRING_MULTI_LINE      0x01

typedef struct _EFI_IFR_PASSWORD {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_QUESTION_HEADER  Question;
  UINT16                   MinSize;
  UINT16                   MaxSize;
} EFI_IFR_PASSWORD;

typedef struct _EFI_IFR_ORDERED_LIST {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_QUESTION_HEADER  Question;
  UINT8                    MaxContainers;
  UINT8                    Flags;
} EFI_IFR_ORDERED_LIST;

#define EFI_IFR_UNIQUE_SET             0x01
#define EFI_IFR_NO_EMPTY_SET           0x02

typedef struct _EFI_IFR_TIME {
  EFI_IFR_OP_HEADER        Header;
  EFI_IFR_QUESTION_HEADER  Question;
  UINT8                    Flags;
} EFI_IFR_TIME;

//
// A bit-mask that determines which unique settings are active for this opcode.
//
#define QF_TIME_HOUR_SUPPRESS          0x01
#define QF_TIME_MINUTE_SUPPRESS        0x02
#define QF_TIME_SECOND_SUPPRESS        0x04

#define QF_TIME_STORAGE                0x30
#define   QF_TIME_STORAGE_NORMAL       0x00
#define   QF_TIME_STORAGE_TIME         0x10
#define   QF_TIME_STORAGE_WAKEUP       0x20

typedef struct _EFI_IFR_DISABLE_IF {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_DISABLE_IF;

typedef struct _EFI_IFR_SUPPRESS_IF {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_SUPPRESS_IF;

typedef struct _EFI_IFR_GRAY_OUT_IF {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_GRAY_OUT_IF;

typedef struct _EFI_IFR_INCONSISTENT_IF {
  EFI_IFR_OP_HEADER        Header;
  EFI_STRING_ID            Error;
} EFI_IFR_INCONSISTENT_IF;

typedef struct _EFI_IFR_NO_SUBMIT_IF {
  EFI_IFR_OP_HEADER        Header;
  EFI_STRING_ID            Error;
} EFI_IFR_NO_SUBMIT_IF;

typedef struct _EFI_IFR_WARNING_IF {
  EFI_IFR_OP_HEADER        Header;
  EFI_STRING_ID            Warning;
  UINT8                    TimeOut;
} EFI_IFR_WARNING_IF;

typedef struct _EFI_IFR_REFRESH {
  EFI_IFR_OP_HEADER        Header;
  UINT8                    RefreshInterval;
} EFI_IFR_REFRESH;

typedef struct _EFI_IFR_VARSTORE_DEVICE {
  EFI_IFR_OP_HEADER        Header;
  EFI_STRING_ID            DevicePath;
} EFI_IFR_VARSTORE_DEVICE;

typedef struct _EFI_IFR_ONE_OF_OPTION {
  EFI_IFR_OP_HEADER        Header;
  EFI_STRING_ID            Option;
  UINT8                    Flags;
  UINT8                    Type;
  EFI_IFR_TYPE_VALUE       Value;
} EFI_IFR_ONE_OF_OPTION;

//
// Types of the option's value.
//
#define EFI_IFR_TYPE_NUM_SIZE_8        0x00
#define EFI_IFR_TYPE_NUM_SIZE_16       0x01
#define EFI_IFR_TYPE_NUM_SIZE_32       0x02
#define EFI_IFR_TYPE_NUM_SIZE_64       0x03
#define EFI_IFR_TYPE_BOOLEAN           0x04
#define EFI_IFR_TYPE_TIME              0x05
#define EFI_IFR_TYPE_DATE              0x06
#define EFI_IFR_TYPE_STRING            0x07
#define EFI_IFR_TYPE_OTHER             0x08
#define EFI_IFR_TYPE_UNDEFINED         0x09
#define EFI_IFR_TYPE_ACTION            0x0A
#define EFI_IFR_TYPE_BUFFER            0x0B
#define EFI_IFR_TYPE_REF               0x0C

#define EFI_IFR_OPTION_DEFAULT         0x10
#define EFI_IFR_OPTION_DEFAULT_MFG     0x20

typedef struct _EFI_IFR_GUID {
  EFI_IFR_OP_HEADER        Header;
  EFI_GUID                 Guid;
  //Optional Data Follows
} EFI_IFR_GUID;

typedef struct _EFI_IFR_REFRESH_ID {
  EFI_IFR_OP_HEADER Header;
  EFI_GUID          RefreshEventGroupId;
} EFI_IFR_REFRESH_ID;

typedef struct _EFI_IFR_DUP {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_DUP;

typedef struct _EFI_IFR_EQ_ID_ID {
  EFI_IFR_OP_HEADER        Header;
  EFI_QUESTION_ID          QuestionId1;
  EFI_QUESTION_ID          QuestionId2;
} EFI_IFR_EQ_ID_ID;

typedef struct _EFI_IFR_EQ_ID_VAL {
  EFI_IFR_OP_HEADER        Header;
  EFI_QUESTION_ID          QuestionId;
  UINT16                   Value;
} EFI_IFR_EQ_ID_VAL;

typedef struct _EFI_IFR_EQ_ID_VAL_LIST {
  EFI_IFR_OP_HEADER        Header;
  EFI_QUESTION_ID          QuestionId;
  UINT16                   ListLength;
  UINT16                   ValueList[1];
} EFI_IFR_EQ_ID_VAL_LIST;

typedef struct _EFI_IFR_UINT8 {
  EFI_IFR_OP_HEADER        Header;
  UINT8 Value;
} EFI_IFR_UINT8;

typedef struct _EFI_IFR_UINT16 {
  EFI_IFR_OP_HEADER        Header;
  UINT16                   Value;
} EFI_IFR_UINT16;

typedef struct _EFI_IFR_UINT32 {
  EFI_IFR_OP_HEADER        Header;
  UINT32                   Value;
} EFI_IFR_UINT32;

typedef struct _EFI_IFR_UINT64 {
  EFI_IFR_OP_HEADER        Header;
  UINT64 Value;
} EFI_IFR_UINT64;

typedef struct _EFI_IFR_QUESTION_REF1 {
  EFI_IFR_OP_HEADER        Header;
  EFI_QUESTION_ID          QuestionId;
} EFI_IFR_QUESTION_REF1;

typedef struct _EFI_IFR_QUESTION_REF2 {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_QUESTION_REF2;

typedef struct _EFI_IFR_QUESTION_REF3 {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_QUESTION_REF3;

typedef struct _EFI_IFR_QUESTION_REF3_2 {
  EFI_IFR_OP_HEADER        Header;
  EFI_STRING_ID            DevicePath;
} EFI_IFR_QUESTION_REF3_2;

typedef struct _EFI_IFR_QUESTION_REF3_3 {
  EFI_IFR_OP_HEADER        Header;
  EFI_STRING_ID            DevicePath;
  EFI_GUID                 Guid;
} EFI_IFR_QUESTION_REF3_3;

typedef struct _EFI_IFR_RULE_REF {
  EFI_IFR_OP_HEADER        Header;
  UINT8                    RuleId;
} EFI_IFR_RULE_REF;

typedef struct _EFI_IFR_STRING_REF1 {
  EFI_IFR_OP_HEADER        Header;
  EFI_STRING_ID            StringId;
} EFI_IFR_STRING_REF1;

typedef struct _EFI_IFR_STRING_REF2 {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_STRING_REF2;

typedef struct _EFI_IFR_THIS {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_THIS;

typedef struct _EFI_IFR_TRUE {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_TRUE;

typedef struct _EFI_IFR_FALSE {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_FALSE;

typedef struct _EFI_IFR_ONE {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_ONE;

typedef struct _EFI_IFR_ONES {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_ONES;

typedef struct _EFI_IFR_ZERO {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_ZERO;

typedef struct _EFI_IFR_UNDEFINED {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_UNDEFINED;

typedef struct _EFI_IFR_VERSION {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_VERSION;

typedef struct _EFI_IFR_LENGTH {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_LENGTH;

typedef struct _EFI_IFR_NOT {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_NOT;

typedef struct _EFI_IFR_BITWISE_NOT {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_BITWISE_NOT;

typedef struct _EFI_IFR_TO_BOOLEAN {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_TO_BOOLEAN;

///
/// For EFI_IFR_TO_STRING, when converting from
/// unsigned integers, these flags control the format:
/// 0 = unsigned decimal.
/// 1 = signed decimal.
/// 2 = hexadecimal (lower-case alpha).
/// 3 = hexadecimal (upper-case alpha).
///@{
#define EFI_IFR_STRING_UNSIGNED_DEC      0
#define EFI_IFR_STRING_SIGNED_DEC        1
#define EFI_IFR_STRING_LOWERCASE_HEX     2
#define EFI_IFR_STRING_UPPERCASE_HEX     3
///@}

///
/// When converting from a buffer, these flags control the format:
/// 0 = ASCII.
/// 8 = Unicode.
///@{
#define EFI_IFR_STRING_ASCII             0
#define EFI_IFR_STRING_UNICODE           8
///@}

typedef struct _EFI_IFR_TO_STRING {
  EFI_IFR_OP_HEADER        Header;
  UINT8                    Format;
} EFI_IFR_TO_STRING;

typedef struct _EFI_IFR_TO_UINT {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_TO_UINT;

typedef struct _EFI_IFR_TO_UPPER {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_TO_UPPER;

typedef struct _EFI_IFR_TO_LOWER {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_TO_LOWER;

typedef struct _EFI_IFR_ADD {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_ADD;

typedef struct _EFI_IFR_AND {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_AND;

typedef struct _EFI_IFR_BITWISE_AND {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_BITWISE_AND;

typedef struct _EFI_IFR_BITWISE_OR {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_BITWISE_OR;

typedef struct _EFI_IFR_CATENATE {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_CATENATE;

typedef struct _EFI_IFR_DIVIDE {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_DIVIDE;

typedef struct _EFI_IFR_EQUAL {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_EQUAL;

typedef struct _EFI_IFR_GREATER_EQUAL {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_GREATER_EQUAL;

typedef struct _EFI_IFR_GREATER_THAN {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_GREATER_THAN;

typedef struct _EFI_IFR_LESS_EQUAL {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_LESS_EQUAL;

typedef struct _EFI_IFR_LESS_THAN {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_LESS_THAN;

typedef struct _EFI_IFR_MATCH {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_MATCH;

typedef struct _EFI_IFR_MATCH2 {
  EFI_IFR_OP_HEADER        Header;
  EFI_GUID                 SyntaxType;
} EFI_IFR_MATCH2;

typedef struct _EFI_IFR_MULTIPLY {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_MULTIPLY;

typedef struct _EFI_IFR_MODULO {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_MODULO;

typedef struct _EFI_IFR_NOT_EQUAL {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_NOT_EQUAL;

typedef struct _EFI_IFR_OR {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_OR;

typedef struct _EFI_IFR_SHIFT_LEFT {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_SHIFT_LEFT;

typedef struct _EFI_IFR_SHIFT_RIGHT {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_SHIFT_RIGHT;

typedef struct _EFI_IFR_SUBTRACT {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_SUBTRACT;

typedef struct _EFI_IFR_CONDITIONAL {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_CONDITIONAL;

//
// Flags governing the matching criteria of EFI_IFR_FIND
//
#define EFI_IFR_FF_CASE_SENSITIVE    0x00
#define EFI_IFR_FF_CASE_INSENSITIVE  0x01

typedef struct _EFI_IFR_FIND {
  EFI_IFR_OP_HEADER        Header;
  UINT8                    Format;
} EFI_IFR_FIND;

typedef struct _EFI_IFR_MID {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_MID;

typedef struct _EFI_IFR_TOKEN {
  EFI_IFR_OP_HEADER        Header;
} EFI_IFR_TOKEN;

//
// Flags specifying whether to find the first matching string
// or the first non-matching string.
//
#define EFI_IFR_FLAGS_FIRST_MATCHING     0x00
#define EFI_IFR_FLAGS_FIRST_NON_MATCHING 0x01

typedef struct _EFI_IFR_SPAN {
  EFI_IFR_OP_HEADER        Header;
  UINT8                    Flags;
} EFI_IFR_SPAN;

typedef struct _EFI_IFR_SECURITY {
  ///
  /// Standard opcode header, where Header.Op = EFI_IFR_SECURITY_OP.
  ///
  EFI_IFR_OP_HEADER        Header;
  ///
  /// Security permission level.
  ///
  EFI_GUID                 Permissions;
} EFI_IFR_SECURITY;

typedef struct _EFI_IFR_FORM_MAP_METHOD {
  ///
  /// The string identifier which provides the human-readable name of 
  /// the configuration method for this standards map form.
  ///
  EFI_STRING_ID            MethodTitle;
  ///
  /// Identifier which uniquely specifies the configuration methods 
  /// associated with this standards map form.
  ///
  EFI_GUID                 MethodIdentifier;
} EFI_IFR_FORM_MAP_METHOD;

typedef struct _EFI_IFR_FORM_MAP {
  ///
  /// The sequence that defines the type of opcode as well as the length 
  /// of the opcode being defined. Header.OpCode = EFI_IFR_FORM_MAP_OP. 
  ///
  EFI_IFR_OP_HEADER        Header;
  ///
  /// The unique identifier for this particular form.
  ///
  EFI_FORM_ID              FormId;
  ///
  /// One or more configuration method's name and unique identifier.
  ///
  // EFI_IFR_FORM_MAP_METHOD  Methods[];
} EFI_IFR_FORM_MAP;

typedef struct _EFI_IFR_SET {
  ///
  /// The sequence that defines the type of opcode as well as the length 
  /// of the opcode being defined. Header.OpCode = EFI_IFR_SET_OP. 
  ///
  EFI_IFR_OP_HEADER  Header;
  ///
  /// Specifies the identifier of a previously declared variable store to 
  /// use when storing the question's value. 
  ///
  EFI_VARSTORE_ID    VarStoreId;
  union {
    ///
    /// A 16-bit Buffer Storage offset.
    ///
    EFI_STRING_ID    VarName;
    ///
    /// A Name Value or EFI Variable name (VarName).
    ///
    UINT16           VarOffset;
  }                  VarStoreInfo;
  ///
  /// Specifies the type used for storage. 
  ///
  UINT8              VarStoreType;
} EFI_IFR_SET;

typedef struct _EFI_IFR_GET {
  ///
  /// The sequence that defines the type of opcode as well as the length 
  /// of the opcode being defined. Header.OpCode = EFI_IFR_GET_OP. 
  ///
  EFI_IFR_OP_HEADER  Header;
  ///
  /// Specifies the identifier of a previously declared variable store to 
  /// use when retrieving the value. 
  ///
  EFI_VARSTORE_ID    VarStoreId;
  union {
    ///
    /// A 16-bit Buffer Storage offset.
    ///
    EFI_STRING_ID    VarName;
    ///
    /// A Name Value or EFI Variable name (VarName).
    ///
    UINT16           VarOffset;
  }                  VarStoreInfo;
  ///
  /// Specifies the type used for storage. 
  ///
  UINT8              VarStoreType;
} EFI_IFR_GET;

typedef struct _EFI_IFR_READ {
  EFI_IFR_OP_HEADER       Header;
} EFI_IFR_READ;

typedef struct _EFI_IFR_WRITE {
  EFI_IFR_OP_HEADER      Header;
} EFI_IFR_WRITE;

typedef struct _EFI_IFR_MAP {
  EFI_IFR_OP_HEADER      Header;
} EFI_IFR_MAP;
//
// Definitions for Keyboard Package
// Releated definitions are in Section of EFI_HII_DATABASE_PROTOCOL
//

///
/// Each enumeration values maps a physical key on a keyboard.
///
typedef enum {    
  EfiKeyLCtrl,
  EfiKeyA0, 
  EfiKeyLAlt,
  EfiKeySpaceBar,
  EfiKeyA2,
  EfiKeyA3,
  EfiKeyA4,
  EfiKeyRCtrl,
  EfiKeyLeftArrow,
  EfiKeyDownArrow,
  EfiKeyRightArrow,
  EfiKeyZero,
  EfiKeyPeriod,
  EfiKeyEnter,
  EfiKeyLShift,
  EfiKeyB0,
  EfiKeyB1,
  EfiKeyB2,
  EfiKeyB3,
  EfiKeyB4,
  EfiKeyB5,
  EfiKeyB6,
  EfiKeyB7,
  EfiKeyB8,
  EfiKeyB9,
  EfiKeyB10,
  EfiKeyRShift,
  EfiKeyUpArrow,
  EfiKeyOne,
  EfiKeyTwo,
  EfiKeyThree,
  EfiKeyCapsLock,
  EfiKeyC1,
  EfiKeyC2,
  EfiKeyC3,
  EfiKeyC4,
  EfiKeyC5,
  EfiKeyC6,
  EfiKeyC7,
  EfiKeyC8,
  EfiKeyC9,
  EfiKeyC10,
  EfiKeyC11,
  EfiKeyC12,
  EfiKeyFour,
  EfiKeyFive,
  EfiKeySix,
  EfiKeyPlus,
  EfiKeyTab,
  EfiKeyD1,
  EfiKeyD2,
  EfiKeyD3,
  EfiKeyD4,
  EfiKeyD5,
  EfiKeyD6,
  EfiKeyD7,
  EfiKeyD8,
  EfiKeyD9,
  EfiKeyD10,
  EfiKeyD11,
  EfiKeyD12,
  EfiKeyD13,
  EfiKeyDel,
  EfiKeyEnd,
  EfiKeyPgDn,
  EfiKeySeven,
  EfiKeyEight,
  EfiKeyNine,
  EfiKeyE0,
  EfiKeyE1,
  EfiKeyE2,
  EfiKeyE3,
  EfiKeyE4,
  EfiKeyE5,
  EfiKeyE6,
  EfiKeyE7,
  EfiKeyE8,
  EfiKeyE9,
  EfiKeyE10,
  EfiKeyE11,
  EfiKeyE12,
  EfiKeyBackSpace,
  EfiKeyIns,
  EfiKeyHome,
  EfiKeyPgUp,
  EfiKeyNLck,
  EfiKeySlash,
  EfiKeyAsterisk,
  EfiKeyMinus,
  EfiKeyEsc,
  EfiKeyF1,
  EfiKeyF2,
  EfiKeyF3,
  EfiKeyF4,
  EfiKeyF5,
  EfiKeyF6,
  EfiKeyF7,
  EfiKeyF8,
  EfiKeyF9,
  EfiKeyF10,
  EfiKeyF11,
  EfiKeyF12,
  EfiKeyPrint,
  EfiKeySLck,
  EfiKeyPause
} EFI_KEY;

typedef struct {
  ///
  /// Used to describe a physical key on a keyboard.
  ///
  EFI_KEY                 Key;
  ///
  /// Unicode character code for the Key.
  ///
  CHAR16                  Unicode;
  ///
  /// Unicode character code for the key with the shift key being held down.
  ///
  CHAR16                  ShiftedUnicode;
  ///
  /// Unicode character code for the key with the Alt-GR being held down.
  ///
  CHAR16                  AltGrUnicode;
  ///
  /// Unicode character code for the key with the Alt-GR and shift keys being held down.
  ///
  CHAR16                  ShiftedAltGrUnicode;
  ///
  /// Modifier keys are defined to allow for special functionality that is not necessarily 
  /// accomplished by a printable character. Many of these modifier keys are flags to toggle 
  /// certain state bits on and off inside of a keyboard driver.
  ///
  UINT16                  Modifier;
  UINT16                  AffectedAttribute;
} EFI_KEY_DESCRIPTOR;

///
/// A key which is affected by all the standard shift modifiers.  
/// Most keys would be expected to have this bit active.
///
#define EFI_AFFECTED_BY_STANDARD_SHIFT       0x0001

///
/// This key is affected by the caps lock so that if a keyboard driver
/// would need to disambiguate between a key which had a "1" defined
/// versus an "a" character.  Having this bit turned on would tell
/// the keyboard driver to use the appropriate shifted state or not.
///
#define EFI_AFFECTED_BY_CAPS_LOCK            0x0002

///
/// Similar to the case of CAPS lock, if this bit is active, the key
/// is affected by the num lock being turned on.
///
#define EFI_AFFECTED_BY_NUM_LOCK             0x0004

typedef struct {
  UINT16                  LayoutLength;
  EFI_GUID                Guid;
  UINT32                  LayoutDescriptorStringOffset;
  UINT8                   DescriptorCount;
  // EFI_KEY_DESCRIPTOR    Descriptors[];
} EFI_HII_KEYBOARD_LAYOUT;

typedef struct {
  EFI_HII_PACKAGE_HEADER  Header;
  UINT16                  LayoutCount;
  // EFI_HII_KEYBOARD_LAYOUT Layout[];
} EFI_HII_KEYBOARD_PACKAGE_HDR;

//
// Modifier values
//
#define EFI_NULL_MODIFIER                0x0000
#define EFI_LEFT_CONTROL_MODIFIER        0x0001
#define EFI_RIGHT_CONTROL_MODIFIER       0x0002
#define EFI_LEFT_ALT_MODIFIER            0x0003
#define EFI_RIGHT_ALT_MODIFIER           0x0004
#define EFI_ALT_GR_MODIFIER              0x0005
#define EFI_INSERT_MODIFIER              0x0006
#define EFI_DELETE_MODIFIER              0x0007
#define EFI_PAGE_DOWN_MODIFIER           0x0008
#define EFI_PAGE_UP_MODIFIER             0x0009
#define EFI_HOME_MODIFIER                0x000A
#define EFI_END_MODIFIER                 0x000B
#define EFI_LEFT_SHIFT_MODIFIER          0x000C
#define EFI_RIGHT_SHIFT_MODIFIER         0x000D
#define EFI_CAPS_LOCK_MODIFIER           0x000E
#define EFI_NUM_LOCK_MODIFIER            0x000F
#define EFI_LEFT_ARROW_MODIFIER          0x0010
#define EFI_RIGHT_ARROW_MODIFIER         0x0011
#define EFI_DOWN_ARROW_MODIFIER          0x0012
#define EFI_UP_ARROW_MODIFIER            0x0013
#define EFI_NS_KEY_MODIFIER              0x0014
#define EFI_NS_KEY_DEPENDENCY_MODIFIER   0x0015
#define EFI_FUNCTION_KEY_ONE_MODIFIER    0x0016
#define EFI_FUNCTION_KEY_TWO_MODIFIER    0x0017
#define EFI_FUNCTION_KEY_THREE_MODIFIER  0x0018
#define EFI_FUNCTION_KEY_FOUR_MODIFIER   0x0019
#define EFI_FUNCTION_KEY_FIVE_MODIFIER   0x001A
#define EFI_FUNCTION_KEY_SIX_MODIFIER    0x001B
#define EFI_FUNCTION_KEY_SEVEN_MODIFIER  0x001C
#define EFI_FUNCTION_KEY_EIGHT_MODIFIER  0x001D
#define EFI_FUNCTION_KEY_NINE_MODIFIER   0x001E
#define EFI_FUNCTION_KEY_TEN_MODIFIER    0x001F
#define EFI_FUNCTION_KEY_ELEVEN_MODIFIER 0x0020
#define EFI_FUNCTION_KEY_TWELVE_MODIFIER 0x0021

//
// Keys that have multiple control functions based on modifier
// settings are handled in the keyboard driver implementation.
// For instance, PRINT_KEY might have a modifier held down and
// is still a nonprinting character, but might have an alternate
// control function like SYSREQUEST
//
#define EFI_PRINT_MODIFIER               0x0022
#define EFI_SYS_REQUEST_MODIFIER         0x0023
#define EFI_SCROLL_LOCK_MODIFIER         0x0024
#define EFI_PAUSE_MODIFIER               0x0025
#define EFI_BREAK_MODIFIER               0x0026

#define EFI_LEFT_LOGO_MODIFIER           0x0027
#define EFI_RIGHT_LOGO_MODIFIER          0x0028
#define EFI_MENU_MODIFIER                0x0029

///
/// Animation IFR opcode
///
typedef struct _EFI_IFR_ANIMATION {
  ///
  /// Standard opcode header, where Header.OpCode is 
  /// EFI_IFR_ANIMATION_OP.
  ///
  EFI_IFR_OP_HEADER        Header;
  ///
  /// Animation identifier in the HII database.
  ///
  EFI_ANIMATION_ID         Id;
} EFI_IFR_ANIMATION;

///
/// HII animation package header.
///
typedef struct _EFI_HII_ANIMATION_PACKAGE_HDR {
  ///
  /// Standard package header, where Header.Type = EFI_HII_PACKAGE_ANIMATIONS.
  ///
  EFI_HII_PACKAGE_HEADER  Header;
  ///
  /// Offset, relative to this header, of the animation information. If 
  /// this is zero, then there are no animation sequences in the package.
  ///
  UINT32                  AnimationInfoOffset;
} EFI_HII_ANIMATION_PACKAGE_HDR;

///
/// Animation information is encoded as a series of blocks,
/// with each block prefixed by a single byte header EFI_HII_ANIMATION_BLOCK.
///
typedef struct _EFI_HII_ANIMATION_BLOCK {
  UINT8  BlockType;
  //UINT8  BlockBody[];
} EFI_HII_ANIMATION_BLOCK;

///
/// Animation block types.
///
#define EFI_HII_AIBT_END                 0x00
#define EFI_HII_AIBT_OVERLAY_IMAGES      0x10
#define EFI_HII_AIBT_CLEAR_IMAGES        0x11
#define EFI_HII_AIBT_RESTORE_SCRN        0x12
#define EFI_HII_AIBT_OVERLAY_IMAGES_LOOP 0x18
#define EFI_HII_AIBT_CLEAR_IMAGES_LOOP   0x19
#define EFI_HII_AIBT_RESTORE_SCRN_LOOP   0x1A
#define EFI_HII_AIBT_DUPLICATE           0x20
#define EFI_HII_AIBT_SKIP2               0x21
#define EFI_HII_AIBT_SKIP1               0x22
#define EFI_HII_AIBT_EXT1                0x30
#define EFI_HII_AIBT_EXT2                0x31
#define EFI_HII_AIBT_EXT4                0x32

///
/// Extended block headers used for variable sized animation records
/// which need an explicit length.
///

typedef struct _EFI_HII_AIBT_EXT1_BLOCK  {
  ///
  /// Standard animation header, where Header.BlockType = EFI_HII_AIBT_EXT1.
  ///
  EFI_HII_ANIMATION_BLOCK  Header;
  ///
  /// The block type.
  ///
  UINT8                    BlockType2;
  ///
  /// Size of the animation block, in bytes, including the animation block header.
  ///
  UINT8                    Length;
} EFI_HII_AIBT_EXT1_BLOCK;

typedef struct _EFI_HII_AIBT_EXT2_BLOCK {
  ///
  /// Standard animation header, where Header.BlockType = EFI_HII_AIBT_EXT2.
  ///
  EFI_HII_ANIMATION_BLOCK  Header;
  ///
  /// The block type
  ///
  UINT8                    BlockType2;
  ///
  /// Size of the animation block, in bytes, including the animation block header.
  ///
  UINT16                   Length;
} EFI_HII_AIBT_EXT2_BLOCK;

typedef struct _EFI_HII_AIBT_EXT4_BLOCK {
  ///
  /// Standard animation header, where Header.BlockType = EFI_HII_AIBT_EXT4.
  ///
  EFI_HII_ANIMATION_BLOCK  Header;
  ///
  /// The block type
  ///
  UINT8                    BlockType2;
  ///
  /// Size of the animation block, in bytes, including the animation block header.
  ///
  UINT32                   Length;
} EFI_HII_AIBT_EXT4_BLOCK;

typedef struct _EFI_HII_ANIMATION_CELL {
  ///
  /// The X offset from the upper left hand corner of the logical 
  /// window to position the indexed image.
  ///
  UINT16                    OffsetX;
  ///
  /// The Y offset from the upper left hand corner of the logical 
  /// window to position the indexed image.
  ///
  UINT16                    OffsetY;
  ///
  /// The image to display at the specified offset from the upper left 
  /// hand corner of the logical window.
  ///
  EFI_IMAGE_ID              ImageId;
  ///
  /// The number of milliseconds to delay after displaying the indexed 
  /// image and before continuing on to the next linked image.  If value 
  /// is zero, no delay.
  ///
  UINT16                    Delay;
} EFI_HII_ANIMATION_CELL;

///
/// An animation block to describe an animation sequence that does not cycle, and
/// where one image is simply displayed over the previous image.
///
typedef struct _EFI_HII_AIBT_OVERLAY_IMAGES_BLOCK {
  ///
  /// This is image that is to be reference by the image protocols, if the 
  /// animation function is not supported or disabled. This image can 
  /// be one particular image from the animation sequence (if any one 
  /// of the animation frames has a complete image) or an alternate 
  /// image that can be displayed alone. If the value is zero, no image 
  /// is displayed.
  ///
  EFI_IMAGE_ID            DftImageId;
  ///
  /// The overall width of the set of images (logical window width).
  ///
  UINT16                  Width;
  ///
  /// The overall height of the set of images (logical window height).
  ///
  UINT16                  Height;
  ///
  /// The number of EFI_HII_ANIMATION_CELL contained in the 
  /// animation sequence.
  ///
  UINT16                  CellCount;
  ///
  /// An array of CellCount animation cells.
  ///
  EFI_HII_ANIMATION_CELL  AnimationCell[1];
} EFI_HII_AIBT_OVERLAY_IMAGES_BLOCK;

///
/// An animation block to describe an animation sequence that does not cycle,
/// and where the logical window is cleared to the specified color before 
/// the next image is displayed.
///
typedef struct _EFI_HII_AIBT_CLEAR_IMAGES_BLOCK {
  ///
  /// This is image that is to be reference by the image protocols, if the 
  /// animation function is not supported or disabled. This image can 
  /// be one particular image from the animation sequence (if any one 
  /// of the animation frames has a complete image) or an alternate 
  /// image that can be displayed alone. If the value is zero, no image 
  /// is displayed.
  ///
  EFI_IMAGE_ID       DftImageId;
  ///
  /// The overall width of the set of images (logical window width).
  ///
  UINT16             Width;
  ///
  /// The overall height of the set of images (logical window height).
  ///
  UINT16             Height;
  ///
  /// The number of EFI_HII_ANIMATION_CELL contained in the 
  /// animation sequence.
  ///
  UINT16             CellCount;
  ///
  /// The color to clear the logical window to before displaying the 
  /// indexed image.
  ///
  EFI_HII_RGB_PIXEL  BackgndColor;
  ///
  /// An array of CellCount animation cells.
  ///
  EFI_HII_ANIMATION_CELL AnimationCell[1];
} EFI_HII_AIBT_CLEAR_IMAGES_BLOCK;

///
/// An animation block to describe an animation sequence that does not cycle,
/// and where the screen is restored to the original state before the next 
/// image is displayed.
///
typedef struct _EFI_HII_AIBT_RESTORE_SCRN_BLOCK {
  ///
  /// This is image that is to be reference by the image protocols, if the 
  /// animation function is not supported or disabled. This image can 
  /// be one particular image from the animation sequence (if any one 
  /// of the animation frames has a complete image) or an alternate 
  /// image that can be displayed alone. If the value is zero, no image 
  /// is displayed.
  ///
  EFI_IMAGE_ID            DftImageId;
  ///
  /// The overall width of the set of images (logical window width).
  ///
  UINT16                  Width;
  ///
  /// The overall height of the set of images (logical window height).
  ///
  UINT16                  Height;
  ///
  /// The number of EFI_HII_ANIMATION_CELL contained in the 
  /// animation sequence.
  ///
  UINT16                  CellCount;
  ///
  /// An array of CellCount animation cells.
  ///
  EFI_HII_ANIMATION_CELL  AnimationCell[1];
} EFI_HII_AIBT_RESTORE_SCRN_BLOCK;

///
/// An animation block to describe an animation sequence that continuously cycles,
/// and where one image is simply displayed over the previous image.
///
typedef EFI_HII_AIBT_OVERLAY_IMAGES_BLOCK  EFI_HII_AIBT_OVERLAY_IMAGES_LOOP_BLOCK;

///
/// An animation block to describe an animation sequence that continuously cycles,
/// and where the logical window is cleared to the specified color before 
/// the next image is displayed.
///
typedef EFI_HII_AIBT_CLEAR_IMAGES_BLOCK    EFI_HII_AIBT_CLEAR_IMAGES_LOOP_BLOCK;

///
/// An animation block to describe an animation sequence that continuously cycles,
/// and where the screen is restored to the original state before 
/// the next image is displayed.
///
typedef EFI_HII_AIBT_RESTORE_SCRN_BLOCK    EFI_HII_AIBT_RESTORE_SCRN_LOOP_BLOCK;

///
/// Assigns a new character value to a previously defined animation sequence.
///
typedef struct _EFI_HII_AIBT_DUPLICATE_BLOCK {
  ///
  /// The previously defined animation ID with the exact same 
  /// animation information.
  ///
  EFI_ANIMATION_ID  AnimationId;
} EFI_HII_AIBT_DUPLICATE_BLOCK;

///
/// Skips animation IDs.
///
typedef struct _EFI_HII_AIBT_SKIP1_BLOCK {
  ///
  /// The unsigned 8-bit value to add to AnimationIdCurrent.
  ///
  UINT8  SkipCount;
} EFI_HII_AIBT_SKIP1_BLOCK;

///
/// Skips animation IDs.
///
typedef struct _EFI_HII_AIBT_SKIP2_BLOCK {
  ///
  /// The unsigned 16-bit value to add to AnimationIdCurrent.
  ///
  UINT16  SkipCount;
} EFI_HII_AIBT_SKIP2_BLOCK;

#pragma pack()



///
/// References to string tokens must use this macro to enable scanning for
/// token usages.
///
///
/// STRING_TOKEN is not defined in UEFI specification. But it is placed 
/// here for the easy access by C files and VFR source files.
///
#define STRING_TOKEN(t) t

///
/// IMAGE_TOKEN is not defined in UEFI specification. But it is placed
/// here for the easy access by C files and VFR source files.
///
#define IMAGE_TOKEN(t) t

#endif
