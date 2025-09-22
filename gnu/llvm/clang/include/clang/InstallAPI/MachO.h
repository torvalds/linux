//===- InstallAPI/MachO.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Imports and forward declarations for llvm::MachO types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INSTALLAPI_MACHO_H
#define LLVM_CLANG_INSTALLAPI_MACHO_H

#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/InterfaceFile.h"
#include "llvm/TextAPI/PackedVersion.h"
#include "llvm/TextAPI/Platform.h"
#include "llvm/TextAPI/RecordVisitor.h"
#include "llvm/TextAPI/Symbol.h"
#include "llvm/TextAPI/Target.h"
#include "llvm/TextAPI/TextAPIWriter.h"
#include "llvm/TextAPI/Utils.h"

using AliasMap = llvm::MachO::AliasMap;
using Architecture = llvm::MachO::Architecture;
using ArchitectureSet = llvm::MachO::ArchitectureSet;
using SymbolFlags = llvm::MachO::SymbolFlags;
using RecordLinkage = llvm::MachO::RecordLinkage;
using Record = llvm::MachO::Record;
using EncodeKind = llvm::MachO::EncodeKind;
using GlobalRecord = llvm::MachO::GlobalRecord;
using InterfaceFile = llvm::MachO::InterfaceFile;
using ObjCContainerRecord = llvm::MachO::ObjCContainerRecord;
using ObjCInterfaceRecord = llvm::MachO::ObjCInterfaceRecord;
using ObjCCategoryRecord = llvm::MachO::ObjCCategoryRecord;
using ObjCIVarRecord = llvm::MachO::ObjCIVarRecord;
using ObjCIFSymbolKind = llvm::MachO::ObjCIFSymbolKind;
using Records = llvm::MachO::Records;
using RecordLoc = llvm::MachO::RecordLoc;
using RecordsSlice = llvm::MachO::RecordsSlice;
using BinaryAttrs = llvm::MachO::RecordsSlice::BinaryAttrs;
using SymbolSet = llvm::MachO::SymbolSet;
using SimpleSymbol = llvm::MachO::SimpleSymbol;
using FileType = llvm::MachO::FileType;
using PackedVersion = llvm::MachO::PackedVersion;
using PathSeq = llvm::MachO::PathSeq;
using PlatformType = llvm::MachO::PlatformType;
using PathToPlatformSeq = llvm::MachO::PathToPlatformSeq;
using Target = llvm::MachO::Target;
using TargetList = llvm::MachO::TargetList;

#endif // LLVM_CLANG_INSTALLAPI_MACHO_H
