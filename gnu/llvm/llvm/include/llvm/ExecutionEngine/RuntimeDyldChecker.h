//===---- RuntimeDyldChecker.h - RuntimeDyld tester framework -----*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_RUNTIMEDYLDCHECKER_H
#define LLVM_EXECUTIONENGINE_RUNTIMEDYLDCHECKER_H

#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/Support/Endian.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace llvm {

class StringRef;
class MCDisassembler;
class MemoryBuffer;
class MCInstPrinter;
class RuntimeDyld;
class RuntimeDyldCheckerImpl;
class raw_ostream;

/// Holds target-specific properties for a symbol.
using TargetFlagsType = uint8_t;

/// RuntimeDyld invariant checker for verifying that RuntimeDyld has
///        correctly applied relocations.
///
/// The RuntimeDyldChecker class evaluates expressions against an attached
/// RuntimeDyld instance to verify that relocations have been applied
/// correctly.
///
/// The expression language supports basic pointer arithmetic and bit-masking,
/// and has limited disassembler integration for accessing instruction
/// operands and the next PC (program counter) address for each instruction.
///
/// The language syntax is:
///
/// check = expr '=' expr
///
/// expr = binary_expr
///      | sliceable_expr
///
/// sliceable_expr = '*{' number '}' load_addr_expr [slice]
///                | '(' expr ')' [slice]
///                | ident_expr [slice]
///                | number [slice]
///
/// slice = '[' high-bit-index ':' low-bit-index ']'
///
/// load_addr_expr = symbol
///                | '(' symbol '+' number ')'
///                | '(' symbol '-' number ')'
///
/// ident_expr = 'decode_operand' '(' symbol ',' operand-index ')'
///            | 'next_pc'        '(' symbol ')'
///            | 'stub_addr' '(' stub-container-name ',' symbol ')'
///            | 'got_addr' '(' stub-container-name ',' symbol ')'
///            | 'section_addr' '(' stub-container-name ',' symbol ')'
///            | symbol
///
/// binary_expr = expr '+' expr
///             | expr '-' expr
///             | expr '&' expr
///             | expr '|' expr
///             | expr '<<' expr
///             | expr '>>' expr
///
class RuntimeDyldChecker {
public:
  class MemoryRegionInfo {
  public:
    MemoryRegionInfo() = default;

    /// Constructor for symbols/sections with content and TargetFlag.
    MemoryRegionInfo(ArrayRef<char> Content, JITTargetAddress TargetAddress,
                     TargetFlagsType TargetFlags)
        : ContentPtr(Content.data()), Size(Content.size()),
          TargetAddress(TargetAddress), TargetFlags(TargetFlags) {}

    /// Constructor for zero-fill symbols/sections.
    MemoryRegionInfo(uint64_t Size, JITTargetAddress TargetAddress)
        : Size(Size), TargetAddress(TargetAddress) {}

    /// Returns true if this is a zero-fill symbol/section.
    bool isZeroFill() const {
      assert(Size && "setContent/setZeroFill must be called first");
      return !ContentPtr;
    }

    /// Set the content for this memory region.
    void setContent(ArrayRef<char> Content) {
      assert(!ContentPtr && !Size && "Content/zero-fill already set");
      ContentPtr = Content.data();
      Size = Content.size();
    }

    /// Set a zero-fill length for this memory region.
    void setZeroFill(uint64_t Size) {
      assert(!ContentPtr && !this->Size && "Content/zero-fill already set");
      this->Size = Size;
    }

    /// Returns the content for this section if there is any.
    ArrayRef<char> getContent() const {
      assert(!isZeroFill() && "Can't get content for a zero-fill section");
      return {ContentPtr, static_cast<size_t>(Size)};
    }

    /// Returns the zero-fill length for this section.
    uint64_t getZeroFillLength() const {
      assert(isZeroFill() && "Can't get zero-fill length for content section");
      return Size;
    }

    /// Set the target address for this region.
    void setTargetAddress(JITTargetAddress TargetAddress) {
      assert(!this->TargetAddress && "TargetAddress already set");
      this->TargetAddress = TargetAddress;
    }

    /// Return the target address for this region.
    JITTargetAddress getTargetAddress() const { return TargetAddress; }

    /// Get the target flags for this Symbol.
    TargetFlagsType getTargetFlags() const { return TargetFlags; }

    /// Set the target flags for this Symbol.
    void setTargetFlags(TargetFlagsType Flags) {
      assert(Flags <= 1 && "Add more bits to store more than one flag");
      TargetFlags = Flags;
    }

  private:
    const char *ContentPtr = nullptr;
    uint64_t Size = 0;
    JITTargetAddress TargetAddress = 0;
    TargetFlagsType TargetFlags = 0;
  };

  using IsSymbolValidFunction = std::function<bool(StringRef Symbol)>;
  using GetSymbolInfoFunction =
      std::function<Expected<MemoryRegionInfo>(StringRef SymbolName)>;
  using GetSectionInfoFunction = std::function<Expected<MemoryRegionInfo>(
      StringRef FileName, StringRef SectionName)>;
  using GetStubInfoFunction = std::function<Expected<MemoryRegionInfo>(
      StringRef StubContainer, StringRef TargetName, StringRef StubKindFilter)>;
  using GetGOTInfoFunction = std::function<Expected<MemoryRegionInfo>(
      StringRef GOTContainer, StringRef TargetName)>;

  RuntimeDyldChecker(IsSymbolValidFunction IsSymbolValid,
                     GetSymbolInfoFunction GetSymbolInfo,
                     GetSectionInfoFunction GetSectionInfo,
                     GetStubInfoFunction GetStubInfo,
                     GetGOTInfoFunction GetGOTInfo, llvm::endianness Endianness,
                     Triple TT, StringRef CPU, SubtargetFeatures TF,
                     raw_ostream &ErrStream);
  ~RuntimeDyldChecker();

  /// Check a single expression against the attached RuntimeDyld
  ///        instance.
  bool check(StringRef CheckExpr) const;

  /// Scan the given memory buffer for lines beginning with the string
  ///        in RulePrefix. The remainder of the line is passed to the check
  ///        method to be evaluated as an expression.
  bool checkAllRulesInBuffer(StringRef RulePrefix, MemoryBuffer *MemBuf) const;

  /// Returns the address of the requested section (or an error message
  ///        in the second element of the pair if the address cannot be found).
  ///
  /// if 'LocalAddress' is true, this returns the address of the section
  /// within the linker's memory. If 'LocalAddress' is false it returns the
  /// address within the target process (i.e. the load address).
  std::pair<uint64_t, std::string> getSectionAddr(StringRef FileName,
                                                  StringRef SectionName,
                                                  bool LocalAddress);

  /// If there is a section at the given local address, return its load
  /// address, otherwise return std::nullopt.
  std::optional<uint64_t> getSectionLoadAddress(void *LocalAddress) const;

private:
  std::unique_ptr<RuntimeDyldCheckerImpl> Impl;
};

} // end namespace llvm

#endif
