//===-- CSKYELFStreamer.h - CSKY ELF Target Streamer -----------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_CSKY_CSKYELFSTREAMER_H
#define LLVM_LIB_TARGET_CSKY_CSKYELFSTREAMER_H

#include "CSKYTargetStreamer.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCObjectWriter.h"

namespace llvm {

class CSKYTargetELFStreamer : public CSKYTargetStreamer {
private:
  enum class AttributeType { Hidden, Numeric, Text, NumericAndText };

  struct AttributeItem {
    AttributeType Type;
    unsigned Tag;
    unsigned IntValue;
    std::string StringValue;
  };

  StringRef CurrentVendor;
  SmallVector<AttributeItem, 64> Contents;

  MCSection *AttributeSection = nullptr;

  AttributeItem *getAttributeItem(unsigned Attribute) {
    for (size_t i = 0; i < Contents.size(); ++i)
      if (Contents[i].Tag == Attribute)
        return &Contents[i];
    return nullptr;
  }

  void setAttributeItem(unsigned Attribute, unsigned Value,
                        bool OverwriteExisting) {
    // Look for existing attribute item.
    if (AttributeItem *Item = getAttributeItem(Attribute)) {
      if (!OverwriteExisting)
        return;
      Item->Type = AttributeType::Numeric;
      Item->IntValue = Value;
      return;
    }

    // Create new attribute item.
    Contents.push_back({AttributeType::Numeric, Attribute, Value, ""});
  }

  void setAttributeItem(unsigned Attribute, StringRef Value,
                        bool OverwriteExisting) {
    // Look for existing attribute item.
    if (AttributeItem *Item = getAttributeItem(Attribute)) {
      if (!OverwriteExisting)
        return;
      Item->Type = AttributeType::Text;
      Item->StringValue = std::string(Value);
      return;
    }

    // Create new attribute item.
    Contents.push_back({AttributeType::Text, Attribute, 0, std::string(Value)});
  }

  void setAttributeItems(unsigned Attribute, unsigned IntValue,
                         StringRef StringValue, bool OverwriteExisting) {
    // Look for existing attribute item.
    if (AttributeItem *Item = getAttributeItem(Attribute)) {
      if (!OverwriteExisting)
        return;
      Item->Type = AttributeType::NumericAndText;
      Item->IntValue = IntValue;
      Item->StringValue = std::string(StringValue);
      return;
    }

    // Create new attribute item.
    Contents.push_back({AttributeType::NumericAndText, Attribute, IntValue,
                        std::string(StringValue)});
  }

  void emitAttribute(unsigned Attribute, unsigned Value) override;
  void emitTextAttribute(unsigned Attribute, StringRef String) override;
  void finishAttributeSection() override;
  size_t calculateContentSize() const;

  void emitTargetAttributes(const MCSubtargetInfo &STI) override;

public:
  MCELFStreamer &getStreamer();
  CSKYTargetELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI);
};

class CSKYELFStreamer : public MCELFStreamer {
  void EmitMappingSymbol(StringRef Name);

public:
  friend class CSKYTargetELFStreamer;

  enum ElfMappingSymbol { EMS_None, EMS_Text, EMS_Data };

  ElfMappingSymbol State;

  CSKYELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                  std::unique_ptr<MCObjectWriter> OW,
                  std::unique_ptr<MCCodeEmitter> Emitter)
      : MCELFStreamer(Context, std::move(TAB), std::move(OW),
                      std::move(Emitter)),
        State(EMS_None) {}

  ~CSKYELFStreamer() override = default;

  void emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                SMLoc Loc) override {
    EmitMappingSymbol("$d");
    MCObjectStreamer::emitFill(NumBytes, FillValue, Loc);
  }
  void emitBytes(StringRef Data) override {
    EmitMappingSymbol("$d");
    MCELFStreamer::emitBytes(Data);
  }
  void emitInstruction(const MCInst &Inst,
                       const MCSubtargetInfo &STI) override {
    EmitMappingSymbol("$t");
    MCELFStreamer::emitInstruction(Inst, STI);
  }
  void emitValueImpl(const MCExpr *Value, unsigned Size, SMLoc Loc) override {
    EmitMappingSymbol("$d");
    MCELFStreamer::emitValueImpl(Value, Size, Loc);
  }
  void reset() override {
    State = EMS_None;
    MCELFStreamer::reset();
  }
};

} // namespace llvm
#endif
