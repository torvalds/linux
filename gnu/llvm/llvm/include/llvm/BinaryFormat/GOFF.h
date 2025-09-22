//===-- llvm/BinaryFormat/GOFF.h - GOFF definitions --------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header contains common, non-processor-specific data structures and
// constants for the GOFF file format.
//
// GOFF specifics can be found in MVS Program Management: Advanced Facilities.
// See
// https://www.ibm.com/docs/en/zos/3.1.0?topic=facilities-generalized-object-file-format-goff
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_GOFF_H
#define LLVM_BINARYFORMAT_GOFF_H

#include "llvm/Support/DataTypes.h"

namespace llvm {

namespace GOFF {

/// \brief Length of the parts of a physical GOFF record.
constexpr uint8_t RecordLength = 80;
constexpr uint8_t RecordPrefixLength = 3;
constexpr uint8_t PayloadLength = 77;
constexpr uint8_t RecordContentLength = RecordLength - RecordPrefixLength;

/// \brief Maximum data length before starting a new card for RLD and TXT data.
///
/// The maximum number of bytes that can be included in an RLD or TXT record and
/// their continuations is a SIGNED 16 bit int despite what the spec says. The
/// number of bytes we allow ourselves to attach to a card is thus arbitrarily
/// limited to 32K-1 bytes.
constexpr uint16_t MaxDataLength = 32 * 1024 - 1;

/// \brief Prefix byte on every record. This indicates GOFF format.
constexpr uint8_t PTVPrefix = 0x03;

enum RecordType : uint8_t {
  RT_ESD = 0,
  RT_TXT = 1,
  RT_RLD = 2,
  RT_LEN = 3,
  RT_END = 4,
  RT_HDR = 15,
};

enum ESDSymbolType : uint8_t {
  ESD_ST_SectionDefinition = 0,
  ESD_ST_ElementDefinition = 1,
  ESD_ST_LabelDefinition = 2,
  ESD_ST_PartReference = 3,
  ESD_ST_ExternalReference = 4,
};

enum ESDNameSpaceId : uint8_t {
  ESD_NS_ProgramManagementBinder = 0,
  ESD_NS_NormalName = 1,
  ESD_NS_PseudoRegister = 2,
  ESD_NS_Parts = 3
};

enum ESDReserveQwords : uint8_t {
  ESD_RQ_0 = 0,
  ESD_RQ_1 = 1,
  ESD_RQ_2 = 2,
  ESD_RQ_3 = 3
};

enum ESDAmode : uint8_t {
  ESD_AMODE_None = 0,
  ESD_AMODE_24 = 1,
  ESD_AMODE_31 = 2,
  ESD_AMODE_ANY = 3,
  ESD_AMODE_64 = 4,
  ESD_AMODE_MIN = 16,
};

enum ESDRmode : uint8_t {
  ESD_RMODE_None = 0,
  ESD_RMODE_24 = 1,
  ESD_RMODE_31 = 3,
  ESD_RMODE_64 = 4,
};

enum ESDTextStyle : uint8_t {
  ESD_TS_ByteOriented = 0,
  ESD_TS_Structured = 1,
  ESD_TS_Unstructured = 2,
};

enum ESDBindingAlgorithm : uint8_t {
  ESD_BA_Concatenate = 0,
  ESD_BA_Merge = 1,
};

enum ESDTaskingBehavior : uint8_t {
  ESD_TA_Unspecified = 0,
  ESD_TA_NonReus = 1,
  ESD_TA_Reus = 2,
  ESD_TA_Rent = 3,
};

enum ESDExecutable : uint8_t {
  ESD_EXE_Unspecified = 0,
  ESD_EXE_DATA = 1,
  ESD_EXE_CODE = 2,
};

enum ESDDuplicateSymbolSeverity : uint8_t {
  ESD_DSS_NoWarning = 0,
  ESD_DSS_Warning = 1,
  ESD_DSS_Error = 2,
  ESD_DSS_Reserved = 3,
};

enum ESDBindingStrength : uint8_t {
  ESD_BST_Strong = 0,
  ESD_BST_Weak = 1,
};

enum ESDLoadingBehavior : uint8_t {
  ESD_LB_Initial = 0,
  ESD_LB_Deferred = 1,
  ESD_LB_NoLoad = 2,
  ESD_LB_Reserved = 3,
};

enum ESDBindingScope : uint8_t {
  ESD_BSC_Unspecified = 0,
  ESD_BSC_Section = 1,
  ESD_BSC_Module = 2,
  ESD_BSC_Library = 3,
  ESD_BSC_ImportExport = 4,
};

enum ESDLinkageType : uint8_t { ESD_LT_OS = 0, ESD_LT_XPLink = 1 };

enum ESDAlignment : uint8_t {
  ESD_ALIGN_Byte = 0,
  ESD_ALIGN_Halfword = 1,
  ESD_ALIGN_Fullword = 2,
  ESD_ALIGN_Doubleword = 3,
  ESD_ALIGN_Quadword = 4,
  ESD_ALIGN_32byte = 5,
  ESD_ALIGN_64byte = 6,
  ESD_ALIGN_128byte = 7,
  ESD_ALIGN_256byte = 8,
  ESD_ALIGN_512byte = 9,
  ESD_ALIGN_1024byte = 10,
  ESD_ALIGN_2Kpage = 11,
  ESD_ALIGN_4Kpage = 12,
};

enum ENDEntryPointRequest : uint8_t {
  END_EPR_None = 0,
  END_EPR_EsdidOffset = 1,
  END_EPR_ExternalName = 2,
  END_EPR_Reserved = 3,
};

// \brief Subsections of the primary C_CODE section in the object file.
enum SubsectionKind : uint8_t {
  SK_PPA1 = 2,
  SK_PPA2 = 4,
};
} // end namespace GOFF

} // end namespace llvm

#endif // LLVM_BINARYFORMAT_GOFF_H
