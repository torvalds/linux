//===- OutputSections.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_WASM_OUTPUT_SECTIONS_H
#define LLD_WASM_OUTPUT_SECTIONS_H

#include "InputChunks.h"
#include "WriterUtils.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/DenseMap.h"

namespace lld {

namespace wasm {
class OutputSection;
}
std::string toString(const wasm::OutputSection &section);

namespace wasm {

class OutputSegment;

class OutputSection {
public:
  OutputSection(uint32_t type, std::string name = "")
      : type(type), name(name) {}
  virtual ~OutputSection() = default;

  StringRef getSectionName() const;
  void setOffset(size_t newOffset) { offset = newOffset; }
  void createHeader(size_t bodySize);
  virtual bool isNeeded() const { return true; }
  virtual size_t getSize() const = 0;
  virtual size_t getOffset() { return offset; }
  virtual void writeTo(uint8_t *buf) = 0;
  virtual void finalizeContents() = 0;
  virtual uint32_t getNumRelocations() const { return 0; }
  virtual void writeRelocations(raw_ostream &os) const {}

  std::string header;
  uint32_t type;
  uint32_t sectionIndex = UINT32_MAX;
  std::string name;
  OutputSectionSymbol *sectionSym = nullptr;

protected:
  size_t offset = 0;
};

class CodeSection : public OutputSection {
public:
  explicit CodeSection(ArrayRef<InputFunction *> functions)
      : OutputSection(llvm::wasm::WASM_SEC_CODE), functions(functions) {}

  static bool classof(const OutputSection *sec) {
    return sec->type == llvm::wasm::WASM_SEC_CODE;
  }

  size_t getSize() const override { return header.size() + bodySize; }
  void writeTo(uint8_t *buf) override;
  uint32_t getNumRelocations() const override;
  void writeRelocations(raw_ostream &os) const override;
  bool isNeeded() const override { return functions.size() > 0; }
  void finalizeContents() override;

  ArrayRef<InputFunction *> functions;

protected:
  std::string codeSectionHeader;
  size_t bodySize = 0;
};

class DataSection : public OutputSection {
public:
  explicit DataSection(ArrayRef<OutputSegment *> segments)
      : OutputSection(llvm::wasm::WASM_SEC_DATA), segments(segments) {}

  static bool classof(const OutputSection *sec) {
    return sec->type == llvm::wasm::WASM_SEC_DATA;
  }

  size_t getSize() const override { return header.size() + bodySize; }
  void writeTo(uint8_t *buf) override;
  uint32_t getNumRelocations() const override;
  void writeRelocations(raw_ostream &os) const override;
  bool isNeeded() const override;
  void finalizeContents() override;

  ArrayRef<OutputSegment *> segments;

protected:
  std::string dataSectionHeader;
  size_t bodySize = 0;
};

// Represents a custom section in the output file.  Wasm custom sections are
// used for storing user-defined metadata.  Unlike the core sections types
// they are identified by their string name.
// The linker combines custom sections that have the same name by simply
// concatenating them.
// Note that some custom sections such as "name" and "linking" are handled
// separately and are instead synthesized by the linker.
class CustomSection : public OutputSection {
public:
  CustomSection(std::string name, ArrayRef<InputChunk *> inputSections)
      : OutputSection(llvm::wasm::WASM_SEC_CUSTOM, name),
        inputSections(inputSections) {}

  static bool classof(const OutputSection *sec) {
    return sec->type == llvm::wasm::WASM_SEC_CUSTOM;
  }

  size_t getSize() const override {
    return header.size() + nameData.size() + payloadSize;
  }
  void writeTo(uint8_t *buf) override;
  uint32_t getNumRelocations() const override;
  void writeRelocations(raw_ostream &os) const override;
  void finalizeContents() override;

protected:
  void finalizeInputSections();
  size_t payloadSize = 0;
  std::vector<InputChunk *> inputSections;
  std::string nameData;
};

} // namespace wasm
} // namespace lld

#endif // LLD_WASM_OUTPUT_SECTIONS_H
