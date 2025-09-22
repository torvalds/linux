//===-- AMDGPUPALMetadata.cpp - Accumulate and print AMDGPU PAL metadata  -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
///
/// This class has methods called by AMDGPUAsmPrinter to accumulate and print
/// the PAL metadata.
//
//===----------------------------------------------------------------------===//
//

#include "AMDGPUPALMetadata.h"
#include "AMDGPUPTNote.h"
#include "SIDefines.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/Support/AMDGPUMetadata.h"
#include "llvm/Support/EndianStream.h"

using namespace llvm;
using namespace llvm::AMDGPU;

// Read the PAL metadata from IR metadata, where it was put by the frontend.
void AMDGPUPALMetadata::readFromIR(Module &M) {
  auto NamedMD = M.getNamedMetadata("amdgpu.pal.metadata.msgpack");
  if (NamedMD && NamedMD->getNumOperands()) {
    // This is the new msgpack format for metadata. It is a NamedMD containing
    // an MDTuple containing an MDString containing the msgpack data.
    BlobType = ELF::NT_AMDGPU_METADATA;
    auto MDN = dyn_cast<MDTuple>(NamedMD->getOperand(0));
    if (MDN && MDN->getNumOperands()) {
      if (auto MDS = dyn_cast<MDString>(MDN->getOperand(0)))
        setFromMsgPackBlob(MDS->getString());
    }
    return;
  }
  BlobType = ELF::NT_AMD_PAL_METADATA;
  NamedMD = M.getNamedMetadata("amdgpu.pal.metadata");
  if (!NamedMD || !NamedMD->getNumOperands()) {
    // Emit msgpack metadata by default
    BlobType = ELF::NT_AMDGPU_METADATA;
    return;
  }
  // This is the old reg=value pair format for metadata. It is a NamedMD
  // containing an MDTuple containing a number of MDNodes each of which is an
  // integer value, and each two integer values forms a key=value pair that we
  // store as Registers[key]=value in the map.
  auto Tuple = dyn_cast<MDTuple>(NamedMD->getOperand(0));
  if (!Tuple)
    return;
  for (unsigned I = 0, E = Tuple->getNumOperands() & -2; I != E; I += 2) {
    auto Key = mdconst::dyn_extract<ConstantInt>(Tuple->getOperand(I));
    auto Val = mdconst::dyn_extract<ConstantInt>(Tuple->getOperand(I + 1));
    if (!Key || !Val)
      continue;
    setRegister(Key->getZExtValue(), Val->getZExtValue());
  }
}

// Set PAL metadata from a binary blob from the applicable .note record.
// Returns false if bad format.  Blob must remain valid for the lifetime of the
// Metadata.
bool AMDGPUPALMetadata::setFromBlob(unsigned Type, StringRef Blob) {
  BlobType = Type;
  if (Type == ELF::NT_AMD_PAL_METADATA)
    return setFromLegacyBlob(Blob);
  return setFromMsgPackBlob(Blob);
}

// Set PAL metadata from legacy (array of key=value pairs) blob.
bool AMDGPUPALMetadata::setFromLegacyBlob(StringRef Blob) {
  auto Data = reinterpret_cast<const uint32_t *>(Blob.data());
  for (unsigned I = 0; I != Blob.size() / sizeof(uint32_t) / 2; ++I)
    setRegister(Data[I * 2], Data[I * 2 + 1]);
  return true;
}

// Set PAL metadata from msgpack blob.
bool AMDGPUPALMetadata::setFromMsgPackBlob(StringRef Blob) {
  return MsgPackDoc.readFromBlob(Blob, /*Multi=*/false);
}

// Given the calling convention, calculate the register number for rsrc1. In
// principle the register number could change in future hardware, but we know
// it is the same for gfx6-9 (except that LS and ES don't exist on gfx9), so
// we can use fixed values.
static unsigned getRsrc1Reg(CallingConv::ID CC) {
  switch (CC) {
  default:
    return PALMD::R_2E12_COMPUTE_PGM_RSRC1;
  case CallingConv::AMDGPU_LS:
    return PALMD::R_2D4A_SPI_SHADER_PGM_RSRC1_LS;
  case CallingConv::AMDGPU_HS:
    return PALMD::R_2D0A_SPI_SHADER_PGM_RSRC1_HS;
  case CallingConv::AMDGPU_ES:
    return PALMD::R_2CCA_SPI_SHADER_PGM_RSRC1_ES;
  case CallingConv::AMDGPU_GS:
    return PALMD::R_2C8A_SPI_SHADER_PGM_RSRC1_GS;
  case CallingConv::AMDGPU_VS:
    return PALMD::R_2C4A_SPI_SHADER_PGM_RSRC1_VS;
  case CallingConv::AMDGPU_PS:
    return PALMD::R_2C0A_SPI_SHADER_PGM_RSRC1_PS;
  }
}

// Calculate the PAL metadata key for *S_SCRATCH_SIZE. It can be used
// with a constant offset to access any non-register shader-specific PAL
// metadata key.
static unsigned getScratchSizeKey(CallingConv::ID CC) {
  switch (CC) {
  case CallingConv::AMDGPU_PS:
    return PALMD::Key::PS_SCRATCH_SIZE;
  case CallingConv::AMDGPU_VS:
    return PALMD::Key::VS_SCRATCH_SIZE;
  case CallingConv::AMDGPU_GS:
    return PALMD::Key::GS_SCRATCH_SIZE;
  case CallingConv::AMDGPU_ES:
    return PALMD::Key::ES_SCRATCH_SIZE;
  case CallingConv::AMDGPU_HS:
    return PALMD::Key::HS_SCRATCH_SIZE;
  case CallingConv::AMDGPU_LS:
    return PALMD::Key::LS_SCRATCH_SIZE;
  default:
    return PALMD::Key::CS_SCRATCH_SIZE;
  }
}

// Set the rsrc1 register in the metadata for a particular shader stage.
// In fact this ORs the value into any previous setting of the register.
void AMDGPUPALMetadata::setRsrc1(CallingConv::ID CC, unsigned Val) {
  setRegister(getRsrc1Reg(CC), Val);
}

void AMDGPUPALMetadata::setRsrc1(CallingConv::ID CC, const MCExpr *Val,
                                 MCContext &Ctx) {
  setRegister(getRsrc1Reg(CC), Val, Ctx);
}

// Set the rsrc2 register in the metadata for a particular shader stage.
// In fact this ORs the value into any previous setting of the register.
void AMDGPUPALMetadata::setRsrc2(CallingConv::ID CC, unsigned Val) {
  setRegister(getRsrc1Reg(CC) + 1, Val);
}

void AMDGPUPALMetadata::setRsrc2(CallingConv::ID CC, const MCExpr *Val,
                                 MCContext &Ctx) {
  setRegister(getRsrc1Reg(CC) + 1, Val, Ctx);
}

// Set the SPI_PS_INPUT_ENA register in the metadata.
// In fact this ORs the value into any previous setting of the register.
void AMDGPUPALMetadata::setSpiPsInputEna(unsigned Val) {
  setRegister(PALMD::R_A1B3_SPI_PS_INPUT_ENA, Val);
}

// Set the SPI_PS_INPUT_ADDR register in the metadata.
// In fact this ORs the value into any previous setting of the register.
void AMDGPUPALMetadata::setSpiPsInputAddr(unsigned Val) {
  setRegister(PALMD::R_A1B4_SPI_PS_INPUT_ADDR, Val);
}

// Get a register from the metadata, or 0 if not currently set.
unsigned AMDGPUPALMetadata::getRegister(unsigned Reg) {
  auto Regs = getRegisters();
  auto It = Regs.find(MsgPackDoc.getNode(Reg));
  if (It == Regs.end())
    return 0;
  auto N = It->second;
  if (N.getKind() != msgpack::Type::UInt)
    return 0;
  return N.getUInt();
}

// Set a register in the metadata.
// In fact this ORs the value into any previous setting of the register.
void AMDGPUPALMetadata::setRegister(unsigned Reg, unsigned Val) {
  if (!isLegacy()) {
    // In the new MsgPack format, ignore register numbered >= 0x10000000. It
    // is a PAL ABI pseudo-register in the old non-MsgPack format.
    if (Reg >= 0x10000000)
      return;
  }
  auto &N = getRegisters()[MsgPackDoc.getNode(Reg)];
  if (N.getKind() == msgpack::Type::UInt)
    Val |= N.getUInt();
  N = N.getDocument()->getNode(Val);
}

// Set a register in the metadata.
// In fact this ORs the value into any previous setting of the register.
void AMDGPUPALMetadata::setRegister(unsigned Reg, const MCExpr *Val,
                                    MCContext &Ctx) {
  if (!isLegacy()) {
    // In the new MsgPack format, ignore register numbered >= 0x10000000. It
    // is a PAL ABI pseudo-register in the old non-MsgPack format.
    if (Reg >= 0x10000000)
      return;
  }
  auto &N = getRegisters()[MsgPackDoc.getNode(Reg)];
  auto ExprIt = REM.find(Reg);

  if (ExprIt != REM.end()) {
    Val = MCBinaryExpr::createOr(Val, ExprIt->getSecond(), Ctx);
    // This conditional may be redundant most of the time, but the alternate
    // setRegister(unsigned, unsigned) could've been called while the
    // conditional returns true (i.e., Reg exists in REM).
    if (N.getKind() == msgpack::Type::UInt) {
      const MCExpr *NExpr = MCConstantExpr::create(N.getUInt(), Ctx);
      Val = MCBinaryExpr::createOr(Val, NExpr, Ctx);
    }
    ExprIt->getSecond() = Val;
  } else if (N.getKind() == msgpack::Type::UInt) {
    const MCExpr *NExpr = MCConstantExpr::create(N.getUInt(), Ctx);
    Val = MCBinaryExpr::createOr(Val, NExpr, Ctx);
    int64_t Unused;
    if (!Val->evaluateAsAbsolute(Unused))
      REM[Reg] = Val;
    (void)Unused;
  }
  DelayedExprs.assignDocNode(N, msgpack::Type::UInt, Val);
}

// Set the entry point name for one shader.
void AMDGPUPALMetadata::setEntryPoint(unsigned CC, StringRef Name) {
  if (isLegacy())
    return;
  // Msgpack format.
  getHwStage(CC)[".entry_point"] = MsgPackDoc.getNode(Name, /*Copy=*/true);
}

// Set the number of used vgprs in the metadata. This is an optional
// advisory record for logging etc; wave dispatch actually uses the rsrc1
// register for the shader stage to determine the number of vgprs to
// allocate.
void AMDGPUPALMetadata::setNumUsedVgprs(CallingConv::ID CC, unsigned Val) {
  if (isLegacy()) {
    // Old non-msgpack format.
    unsigned NumUsedVgprsKey = getScratchSizeKey(CC) +
                               PALMD::Key::VS_NUM_USED_VGPRS -
                               PALMD::Key::VS_SCRATCH_SIZE;
    setRegister(NumUsedVgprsKey, Val);
    return;
  }
  // Msgpack format.
  getHwStage(CC)[".vgpr_count"] = MsgPackDoc.getNode(Val);
}

void AMDGPUPALMetadata::setNumUsedVgprs(CallingConv::ID CC, const MCExpr *Val,
                                        MCContext &Ctx) {
  if (isLegacy()) {
    // Old non-msgpack format.
    unsigned NumUsedVgprsKey = getScratchSizeKey(CC) +
                               PALMD::Key::VS_NUM_USED_VGPRS -
                               PALMD::Key::VS_SCRATCH_SIZE;
    setRegister(NumUsedVgprsKey, Val, Ctx);
    return;
  }
  // Msgpack format.
  setHwStage(CC, ".vgpr_count", msgpack::Type::UInt, Val);
}

// Set the number of used agprs in the metadata.
void AMDGPUPALMetadata::setNumUsedAgprs(CallingConv::ID CC, unsigned Val) {
  getHwStage(CC)[".agpr_count"] = Val;
}

void AMDGPUPALMetadata::setNumUsedAgprs(unsigned CC, const MCExpr *Val) {
  setHwStage(CC, ".agpr_count", msgpack::Type::UInt, Val);
}

// Set the number of used sgprs in the metadata. This is an optional advisory
// record for logging etc; wave dispatch actually uses the rsrc1 register for
// the shader stage to determine the number of sgprs to allocate.
void AMDGPUPALMetadata::setNumUsedSgprs(CallingConv::ID CC, unsigned Val) {
  if (isLegacy()) {
    // Old non-msgpack format.
    unsigned NumUsedSgprsKey = getScratchSizeKey(CC) +
                               PALMD::Key::VS_NUM_USED_SGPRS -
                               PALMD::Key::VS_SCRATCH_SIZE;
    setRegister(NumUsedSgprsKey, Val);
    return;
  }
  // Msgpack format.
  getHwStage(CC)[".sgpr_count"] = MsgPackDoc.getNode(Val);
}

void AMDGPUPALMetadata::setNumUsedSgprs(unsigned CC, const MCExpr *Val,
                                        MCContext &Ctx) {
  if (isLegacy()) {
    // Old non-msgpack format.
    unsigned NumUsedSgprsKey = getScratchSizeKey(CC) +
                               PALMD::Key::VS_NUM_USED_SGPRS -
                               PALMD::Key::VS_SCRATCH_SIZE;
    setRegister(NumUsedSgprsKey, Val, Ctx);
    return;
  }
  // Msgpack format.
  setHwStage(CC, ".sgpr_count", msgpack::Type::UInt, Val);
}

// Set the scratch size in the metadata.
void AMDGPUPALMetadata::setScratchSize(CallingConv::ID CC, unsigned Val) {
  if (isLegacy()) {
    // Old non-msgpack format.
    setRegister(getScratchSizeKey(CC), Val);
    return;
  }
  // Msgpack format.
  getHwStage(CC)[".scratch_memory_size"] = MsgPackDoc.getNode(Val);
}

void AMDGPUPALMetadata::setScratchSize(unsigned CC, const MCExpr *Val,
                                       MCContext &Ctx) {
  if (isLegacy()) {
    // Old non-msgpack format.
    setRegister(getScratchSizeKey(CC), Val, Ctx);
    return;
  }
  // Msgpack format.
  setHwStage(CC, ".scratch_memory_size", msgpack::Type::UInt, Val);
}

// Set the stack frame size of a function in the metadata.
void AMDGPUPALMetadata::setFunctionScratchSize(StringRef FnName, unsigned Val) {
  auto Node = getShaderFunction(FnName);
  Node[".stack_frame_size_in_bytes"] = MsgPackDoc.getNode(Val);
  Node[".backend_stack_size"] = MsgPackDoc.getNode(Val);
}

// Set the amount of LDS used in bytes in the metadata.
void AMDGPUPALMetadata::setFunctionLdsSize(StringRef FnName, unsigned Val) {
  auto Node = getShaderFunction(FnName);
  Node[".lds_size"] = MsgPackDoc.getNode(Val);
}

// Set the number of used vgprs in the metadata.
void AMDGPUPALMetadata::setFunctionNumUsedVgprs(StringRef FnName,
                                                unsigned Val) {
  auto Node = getShaderFunction(FnName);
  Node[".vgpr_count"] = MsgPackDoc.getNode(Val);
}

void AMDGPUPALMetadata::setFunctionNumUsedVgprs(StringRef FnName,
                                                const MCExpr *Val) {
  auto Node = getShaderFunction(FnName);
  DelayedExprs.assignDocNode(Node[".vgpr_count"], msgpack::Type::UInt, Val);
}

// Set the number of used vgprs in the metadata.
void AMDGPUPALMetadata::setFunctionNumUsedSgprs(StringRef FnName,
                                                unsigned Val) {
  auto Node = getShaderFunction(FnName);
  Node[".sgpr_count"] = MsgPackDoc.getNode(Val);
}

void AMDGPUPALMetadata::setFunctionNumUsedSgprs(StringRef FnName,
                                                const MCExpr *Val) {
  auto Node = getShaderFunction(FnName);
  DelayedExprs.assignDocNode(Node[".sgpr_count"], msgpack::Type::UInt, Val);
}

// Set the hardware register bit in PAL metadata to enable wave32 on the
// shader of the given calling convention.
void AMDGPUPALMetadata::setWave32(unsigned CC) {
  switch (CC) {
  case CallingConv::AMDGPU_HS:
    setRegister(PALMD::R_A2D5_VGT_SHADER_STAGES_EN, S_028B54_HS_W32_EN(1));
    break;
  case CallingConv::AMDGPU_GS:
    setRegister(PALMD::R_A2D5_VGT_SHADER_STAGES_EN, S_028B54_GS_W32_EN(1));
    break;
  case CallingConv::AMDGPU_VS:
    setRegister(PALMD::R_A2D5_VGT_SHADER_STAGES_EN, S_028B54_VS_W32_EN(1));
    break;
  case CallingConv::AMDGPU_PS:
    setRegister(PALMD::R_A1B6_SPI_PS_IN_CONTROL, S_0286D8_PS_W32_EN(1));
    break;
  case CallingConv::AMDGPU_CS:
    setRegister(PALMD::R_2E00_COMPUTE_DISPATCH_INITIATOR,
                S_00B800_CS_W32_EN(1));
    break;
  }
}

// Convert a register number to name, for display by toString().
// Returns nullptr if none.
static const char *getRegisterName(unsigned RegNum) {
  // Table of registers.
  static const struct RegInfo {
    unsigned Num;
    const char *Name;
  } RegInfoTable[] = {
      // Registers that code generation sets/modifies metadata for.
      {PALMD::R_2C4A_SPI_SHADER_PGM_RSRC1_VS, "SPI_SHADER_PGM_RSRC1_VS"},
      {PALMD::R_2C4A_SPI_SHADER_PGM_RSRC1_VS + 1, "SPI_SHADER_PGM_RSRC2_VS"},
      {PALMD::R_2D4A_SPI_SHADER_PGM_RSRC1_LS, "SPI_SHADER_PGM_RSRC1_LS"},
      {PALMD::R_2D4A_SPI_SHADER_PGM_RSRC1_LS + 1, "SPI_SHADER_PGM_RSRC2_LS"},
      {PALMD::R_2D0A_SPI_SHADER_PGM_RSRC1_HS, "SPI_SHADER_PGM_RSRC1_HS"},
      {PALMD::R_2D0A_SPI_SHADER_PGM_RSRC1_HS + 1, "SPI_SHADER_PGM_RSRC2_HS"},
      {PALMD::R_2CCA_SPI_SHADER_PGM_RSRC1_ES, "SPI_SHADER_PGM_RSRC1_ES"},
      {PALMD::R_2CCA_SPI_SHADER_PGM_RSRC1_ES + 1, "SPI_SHADER_PGM_RSRC2_ES"},
      {PALMD::R_2C8A_SPI_SHADER_PGM_RSRC1_GS, "SPI_SHADER_PGM_RSRC1_GS"},
      {PALMD::R_2C8A_SPI_SHADER_PGM_RSRC1_GS + 1, "SPI_SHADER_PGM_RSRC2_GS"},
      {PALMD::R_2E00_COMPUTE_DISPATCH_INITIATOR, "COMPUTE_DISPATCH_INITIATOR"},
      {PALMD::R_2E12_COMPUTE_PGM_RSRC1, "COMPUTE_PGM_RSRC1"},
      {PALMD::R_2E12_COMPUTE_PGM_RSRC1 + 1, "COMPUTE_PGM_RSRC2"},
      {PALMD::R_2C0A_SPI_SHADER_PGM_RSRC1_PS, "SPI_SHADER_PGM_RSRC1_PS"},
      {PALMD::R_2C0A_SPI_SHADER_PGM_RSRC1_PS + 1, "SPI_SHADER_PGM_RSRC2_PS"},
      {PALMD::R_A1B3_SPI_PS_INPUT_ENA, "SPI_PS_INPUT_ENA"},
      {PALMD::R_A1B4_SPI_PS_INPUT_ADDR, "SPI_PS_INPUT_ADDR"},
      {PALMD::R_A1B6_SPI_PS_IN_CONTROL, "SPI_PS_IN_CONTROL"},
      {PALMD::R_A2D5_VGT_SHADER_STAGES_EN, "VGT_SHADER_STAGES_EN"},

      // Registers not known to code generation.
      {0x2c07, "SPI_SHADER_PGM_RSRC3_PS"},
      {0x2c46, "SPI_SHADER_PGM_RSRC3_VS"},
      {0x2c87, "SPI_SHADER_PGM_RSRC3_GS"},
      {0x2cc7, "SPI_SHADER_PGM_RSRC3_ES"},
      {0x2d07, "SPI_SHADER_PGM_RSRC3_HS"},
      {0x2d47, "SPI_SHADER_PGM_RSRC3_LS"},

      {0xa1c3, "SPI_SHADER_POS_FORMAT"},
      {0xa1b1, "SPI_VS_OUT_CONFIG"},
      {0xa207, "PA_CL_VS_OUT_CNTL"},
      {0xa204, "PA_CL_CLIP_CNTL"},
      {0xa206, "PA_CL_VTE_CNTL"},
      {0xa2f9, "PA_SU_VTX_CNTL"},
      {0xa293, "PA_SC_MODE_CNTL_1"},
      {0xa2a1, "VGT_PRIMITIVEID_EN"},
      {0x2c81, "SPI_SHADER_PGM_RSRC4_GS"},
      {0x2e18, "COMPUTE_TMPRING_SIZE"},
      {0xa1b5, "SPI_INTERP_CONTROL_0"},
      {0xa1ba, "SPI_TMPRING_SIZE"},
      {0xa1c4, "SPI_SHADER_Z_FORMAT"},
      {0xa1c5, "SPI_SHADER_COL_FORMAT"},
      {0xa203, "DB_SHADER_CONTROL"},
      {0xa08f, "CB_SHADER_MASK"},
      {0xa191, "SPI_PS_INPUT_CNTL_0"},
      {0xa192, "SPI_PS_INPUT_CNTL_1"},
      {0xa193, "SPI_PS_INPUT_CNTL_2"},
      {0xa194, "SPI_PS_INPUT_CNTL_3"},
      {0xa195, "SPI_PS_INPUT_CNTL_4"},
      {0xa196, "SPI_PS_INPUT_CNTL_5"},
      {0xa197, "SPI_PS_INPUT_CNTL_6"},
      {0xa198, "SPI_PS_INPUT_CNTL_7"},
      {0xa199, "SPI_PS_INPUT_CNTL_8"},
      {0xa19a, "SPI_PS_INPUT_CNTL_9"},
      {0xa19b, "SPI_PS_INPUT_CNTL_10"},
      {0xa19c, "SPI_PS_INPUT_CNTL_11"},
      {0xa19d, "SPI_PS_INPUT_CNTL_12"},
      {0xa19e, "SPI_PS_INPUT_CNTL_13"},
      {0xa19f, "SPI_PS_INPUT_CNTL_14"},
      {0xa1a0, "SPI_PS_INPUT_CNTL_15"},
      {0xa1a1, "SPI_PS_INPUT_CNTL_16"},
      {0xa1a2, "SPI_PS_INPUT_CNTL_17"},
      {0xa1a3, "SPI_PS_INPUT_CNTL_18"},
      {0xa1a4, "SPI_PS_INPUT_CNTL_19"},
      {0xa1a5, "SPI_PS_INPUT_CNTL_20"},
      {0xa1a6, "SPI_PS_INPUT_CNTL_21"},
      {0xa1a7, "SPI_PS_INPUT_CNTL_22"},
      {0xa1a8, "SPI_PS_INPUT_CNTL_23"},
      {0xa1a9, "SPI_PS_INPUT_CNTL_24"},
      {0xa1aa, "SPI_PS_INPUT_CNTL_25"},
      {0xa1ab, "SPI_PS_INPUT_CNTL_26"},
      {0xa1ac, "SPI_PS_INPUT_CNTL_27"},
      {0xa1ad, "SPI_PS_INPUT_CNTL_28"},
      {0xa1ae, "SPI_PS_INPUT_CNTL_29"},
      {0xa1af, "SPI_PS_INPUT_CNTL_30"},
      {0xa1b0, "SPI_PS_INPUT_CNTL_31"},

      {0xa2ce, "VGT_GS_MAX_VERT_OUT"},
      {0xa2ab, "VGT_ESGS_RING_ITEMSIZE"},
      {0xa290, "VGT_GS_MODE"},
      {0xa291, "VGT_GS_ONCHIP_CNTL"},
      {0xa2d7, "VGT_GS_VERT_ITEMSIZE"},
      {0xa2d8, "VGT_GS_VERT_ITEMSIZE_1"},
      {0xa2d9, "VGT_GS_VERT_ITEMSIZE_2"},
      {0xa2da, "VGT_GS_VERT_ITEMSIZE_3"},
      {0xa298, "VGT_GSVS_RING_OFFSET_1"},
      {0xa299, "VGT_GSVS_RING_OFFSET_2"},
      {0xa29a, "VGT_GSVS_RING_OFFSET_3"},

      {0xa2e4, "VGT_GS_INSTANCE_CNT"},
      {0xa297, "VGT_GS_PER_VS"},
      {0xa29b, "VGT_GS_OUT_PRIM_TYPE"},
      {0xa2ac, "VGT_GSVS_RING_ITEMSIZE"},

      {0xa2ad, "VGT_REUSE_OFF"},
      {0xa1b8, "SPI_BARYC_CNTL"},

      {0x2c4c, "SPI_SHADER_USER_DATA_VS_0"},
      {0x2c4d, "SPI_SHADER_USER_DATA_VS_1"},
      {0x2c4e, "SPI_SHADER_USER_DATA_VS_2"},
      {0x2c4f, "SPI_SHADER_USER_DATA_VS_3"},
      {0x2c50, "SPI_SHADER_USER_DATA_VS_4"},
      {0x2c51, "SPI_SHADER_USER_DATA_VS_5"},
      {0x2c52, "SPI_SHADER_USER_DATA_VS_6"},
      {0x2c53, "SPI_SHADER_USER_DATA_VS_7"},
      {0x2c54, "SPI_SHADER_USER_DATA_VS_8"},
      {0x2c55, "SPI_SHADER_USER_DATA_VS_9"},
      {0x2c56, "SPI_SHADER_USER_DATA_VS_10"},
      {0x2c57, "SPI_SHADER_USER_DATA_VS_11"},
      {0x2c58, "SPI_SHADER_USER_DATA_VS_12"},
      {0x2c59, "SPI_SHADER_USER_DATA_VS_13"},
      {0x2c5a, "SPI_SHADER_USER_DATA_VS_14"},
      {0x2c5b, "SPI_SHADER_USER_DATA_VS_15"},
      {0x2c5c, "SPI_SHADER_USER_DATA_VS_16"},
      {0x2c5d, "SPI_SHADER_USER_DATA_VS_17"},
      {0x2c5e, "SPI_SHADER_USER_DATA_VS_18"},
      {0x2c5f, "SPI_SHADER_USER_DATA_VS_19"},
      {0x2c60, "SPI_SHADER_USER_DATA_VS_20"},
      {0x2c61, "SPI_SHADER_USER_DATA_VS_21"},
      {0x2c62, "SPI_SHADER_USER_DATA_VS_22"},
      {0x2c63, "SPI_SHADER_USER_DATA_VS_23"},
      {0x2c64, "SPI_SHADER_USER_DATA_VS_24"},
      {0x2c65, "SPI_SHADER_USER_DATA_VS_25"},
      {0x2c66, "SPI_SHADER_USER_DATA_VS_26"},
      {0x2c67, "SPI_SHADER_USER_DATA_VS_27"},
      {0x2c68, "SPI_SHADER_USER_DATA_VS_28"},
      {0x2c69, "SPI_SHADER_USER_DATA_VS_29"},
      {0x2c6a, "SPI_SHADER_USER_DATA_VS_30"},
      {0x2c6b, "SPI_SHADER_USER_DATA_VS_31"},

      {0x2c8c, "SPI_SHADER_USER_DATA_GS_0"},
      {0x2c8d, "SPI_SHADER_USER_DATA_GS_1"},
      {0x2c8e, "SPI_SHADER_USER_DATA_GS_2"},
      {0x2c8f, "SPI_SHADER_USER_DATA_GS_3"},
      {0x2c90, "SPI_SHADER_USER_DATA_GS_4"},
      {0x2c91, "SPI_SHADER_USER_DATA_GS_5"},
      {0x2c92, "SPI_SHADER_USER_DATA_GS_6"},
      {0x2c93, "SPI_SHADER_USER_DATA_GS_7"},
      {0x2c94, "SPI_SHADER_USER_DATA_GS_8"},
      {0x2c95, "SPI_SHADER_USER_DATA_GS_9"},
      {0x2c96, "SPI_SHADER_USER_DATA_GS_10"},
      {0x2c97, "SPI_SHADER_USER_DATA_GS_11"},
      {0x2c98, "SPI_SHADER_USER_DATA_GS_12"},
      {0x2c99, "SPI_SHADER_USER_DATA_GS_13"},
      {0x2c9a, "SPI_SHADER_USER_DATA_GS_14"},
      {0x2c9b, "SPI_SHADER_USER_DATA_GS_15"},
      {0x2c9c, "SPI_SHADER_USER_DATA_GS_16"},
      {0x2c9d, "SPI_SHADER_USER_DATA_GS_17"},
      {0x2c9e, "SPI_SHADER_USER_DATA_GS_18"},
      {0x2c9f, "SPI_SHADER_USER_DATA_GS_19"},
      {0x2ca0, "SPI_SHADER_USER_DATA_GS_20"},
      {0x2ca1, "SPI_SHADER_USER_DATA_GS_21"},
      {0x2ca2, "SPI_SHADER_USER_DATA_GS_22"},
      {0x2ca3, "SPI_SHADER_USER_DATA_GS_23"},
      {0x2ca4, "SPI_SHADER_USER_DATA_GS_24"},
      {0x2ca5, "SPI_SHADER_USER_DATA_GS_25"},
      {0x2ca6, "SPI_SHADER_USER_DATA_GS_26"},
      {0x2ca7, "SPI_SHADER_USER_DATA_GS_27"},
      {0x2ca8, "SPI_SHADER_USER_DATA_GS_28"},
      {0x2ca9, "SPI_SHADER_USER_DATA_GS_29"},
      {0x2caa, "SPI_SHADER_USER_DATA_GS_30"},
      {0x2cab, "SPI_SHADER_USER_DATA_GS_31"},

      {0x2ccc, "SPI_SHADER_USER_DATA_ES_0"},
      {0x2ccd, "SPI_SHADER_USER_DATA_ES_1"},
      {0x2cce, "SPI_SHADER_USER_DATA_ES_2"},
      {0x2ccf, "SPI_SHADER_USER_DATA_ES_3"},
      {0x2cd0, "SPI_SHADER_USER_DATA_ES_4"},
      {0x2cd1, "SPI_SHADER_USER_DATA_ES_5"},
      {0x2cd2, "SPI_SHADER_USER_DATA_ES_6"},
      {0x2cd3, "SPI_SHADER_USER_DATA_ES_7"},
      {0x2cd4, "SPI_SHADER_USER_DATA_ES_8"},
      {0x2cd5, "SPI_SHADER_USER_DATA_ES_9"},
      {0x2cd6, "SPI_SHADER_USER_DATA_ES_10"},
      {0x2cd7, "SPI_SHADER_USER_DATA_ES_11"},
      {0x2cd8, "SPI_SHADER_USER_DATA_ES_12"},
      {0x2cd9, "SPI_SHADER_USER_DATA_ES_13"},
      {0x2cda, "SPI_SHADER_USER_DATA_ES_14"},
      {0x2cdb, "SPI_SHADER_USER_DATA_ES_15"},
      {0x2cdc, "SPI_SHADER_USER_DATA_ES_16"},
      {0x2cdd, "SPI_SHADER_USER_DATA_ES_17"},
      {0x2cde, "SPI_SHADER_USER_DATA_ES_18"},
      {0x2cdf, "SPI_SHADER_USER_DATA_ES_19"},
      {0x2ce0, "SPI_SHADER_USER_DATA_ES_20"},
      {0x2ce1, "SPI_SHADER_USER_DATA_ES_21"},
      {0x2ce2, "SPI_SHADER_USER_DATA_ES_22"},
      {0x2ce3, "SPI_SHADER_USER_DATA_ES_23"},
      {0x2ce4, "SPI_SHADER_USER_DATA_ES_24"},
      {0x2ce5, "SPI_SHADER_USER_DATA_ES_25"},
      {0x2ce6, "SPI_SHADER_USER_DATA_ES_26"},
      {0x2ce7, "SPI_SHADER_USER_DATA_ES_27"},
      {0x2ce8, "SPI_SHADER_USER_DATA_ES_28"},
      {0x2ce9, "SPI_SHADER_USER_DATA_ES_29"},
      {0x2cea, "SPI_SHADER_USER_DATA_ES_30"},
      {0x2ceb, "SPI_SHADER_USER_DATA_ES_31"},

      {0x2c0c, "SPI_SHADER_USER_DATA_PS_0"},
      {0x2c0d, "SPI_SHADER_USER_DATA_PS_1"},
      {0x2c0e, "SPI_SHADER_USER_DATA_PS_2"},
      {0x2c0f, "SPI_SHADER_USER_DATA_PS_3"},
      {0x2c10, "SPI_SHADER_USER_DATA_PS_4"},
      {0x2c11, "SPI_SHADER_USER_DATA_PS_5"},
      {0x2c12, "SPI_SHADER_USER_DATA_PS_6"},
      {0x2c13, "SPI_SHADER_USER_DATA_PS_7"},
      {0x2c14, "SPI_SHADER_USER_DATA_PS_8"},
      {0x2c15, "SPI_SHADER_USER_DATA_PS_9"},
      {0x2c16, "SPI_SHADER_USER_DATA_PS_10"},
      {0x2c17, "SPI_SHADER_USER_DATA_PS_11"},
      {0x2c18, "SPI_SHADER_USER_DATA_PS_12"},
      {0x2c19, "SPI_SHADER_USER_DATA_PS_13"},
      {0x2c1a, "SPI_SHADER_USER_DATA_PS_14"},
      {0x2c1b, "SPI_SHADER_USER_DATA_PS_15"},
      {0x2c1c, "SPI_SHADER_USER_DATA_PS_16"},
      {0x2c1d, "SPI_SHADER_USER_DATA_PS_17"},
      {0x2c1e, "SPI_SHADER_USER_DATA_PS_18"},
      {0x2c1f, "SPI_SHADER_USER_DATA_PS_19"},
      {0x2c20, "SPI_SHADER_USER_DATA_PS_20"},
      {0x2c21, "SPI_SHADER_USER_DATA_PS_21"},
      {0x2c22, "SPI_SHADER_USER_DATA_PS_22"},
      {0x2c23, "SPI_SHADER_USER_DATA_PS_23"},
      {0x2c24, "SPI_SHADER_USER_DATA_PS_24"},
      {0x2c25, "SPI_SHADER_USER_DATA_PS_25"},
      {0x2c26, "SPI_SHADER_USER_DATA_PS_26"},
      {0x2c27, "SPI_SHADER_USER_DATA_PS_27"},
      {0x2c28, "SPI_SHADER_USER_DATA_PS_28"},
      {0x2c29, "SPI_SHADER_USER_DATA_PS_29"},
      {0x2c2a, "SPI_SHADER_USER_DATA_PS_30"},
      {0x2c2b, "SPI_SHADER_USER_DATA_PS_31"},

      {0x2e40, "COMPUTE_USER_DATA_0"},
      {0x2e41, "COMPUTE_USER_DATA_1"},
      {0x2e42, "COMPUTE_USER_DATA_2"},
      {0x2e43, "COMPUTE_USER_DATA_3"},
      {0x2e44, "COMPUTE_USER_DATA_4"},
      {0x2e45, "COMPUTE_USER_DATA_5"},
      {0x2e46, "COMPUTE_USER_DATA_6"},
      {0x2e47, "COMPUTE_USER_DATA_7"},
      {0x2e48, "COMPUTE_USER_DATA_8"},
      {0x2e49, "COMPUTE_USER_DATA_9"},
      {0x2e4a, "COMPUTE_USER_DATA_10"},
      {0x2e4b, "COMPUTE_USER_DATA_11"},
      {0x2e4c, "COMPUTE_USER_DATA_12"},
      {0x2e4d, "COMPUTE_USER_DATA_13"},
      {0x2e4e, "COMPUTE_USER_DATA_14"},
      {0x2e4f, "COMPUTE_USER_DATA_15"},

      {0x2e07, "COMPUTE_NUM_THREAD_X"},
      {0x2e08, "COMPUTE_NUM_THREAD_Y"},
      {0x2e09, "COMPUTE_NUM_THREAD_Z"},
      {0xa2db, "VGT_TF_PARAM"},
      {0xa2d6, "VGT_LS_HS_CONFIG"},
      {0xa287, "VGT_HOS_MIN_TESS_LEVEL"},
      {0xa286, "VGT_HOS_MAX_TESS_LEVEL"},
      {0xa2f8, "PA_SC_AA_CONFIG"},
      {0xa310, "PA_SC_SHADER_CONTROL"},
      {0xa313, "PA_SC_CONSERVATIVE_RASTERIZATION_CNTL"},

      {0x2d0c, "SPI_SHADER_USER_DATA_HS_0"},
      {0x2d0d, "SPI_SHADER_USER_DATA_HS_1"},
      {0x2d0e, "SPI_SHADER_USER_DATA_HS_2"},
      {0x2d0f, "SPI_SHADER_USER_DATA_HS_3"},
      {0x2d10, "SPI_SHADER_USER_DATA_HS_4"},
      {0x2d11, "SPI_SHADER_USER_DATA_HS_5"},
      {0x2d12, "SPI_SHADER_USER_DATA_HS_6"},
      {0x2d13, "SPI_SHADER_USER_DATA_HS_7"},
      {0x2d14, "SPI_SHADER_USER_DATA_HS_8"},
      {0x2d15, "SPI_SHADER_USER_DATA_HS_9"},
      {0x2d16, "SPI_SHADER_USER_DATA_HS_10"},
      {0x2d17, "SPI_SHADER_USER_DATA_HS_11"},
      {0x2d18, "SPI_SHADER_USER_DATA_HS_12"},
      {0x2d19, "SPI_SHADER_USER_DATA_HS_13"},
      {0x2d1a, "SPI_SHADER_USER_DATA_HS_14"},
      {0x2d1b, "SPI_SHADER_USER_DATA_HS_15"},
      {0x2d1c, "SPI_SHADER_USER_DATA_HS_16"},
      {0x2d1d, "SPI_SHADER_USER_DATA_HS_17"},
      {0x2d1e, "SPI_SHADER_USER_DATA_HS_18"},
      {0x2d1f, "SPI_SHADER_USER_DATA_HS_19"},
      {0x2d20, "SPI_SHADER_USER_DATA_HS_20"},
      {0x2d21, "SPI_SHADER_USER_DATA_HS_21"},
      {0x2d22, "SPI_SHADER_USER_DATA_HS_22"},
      {0x2d23, "SPI_SHADER_USER_DATA_HS_23"},
      {0x2d24, "SPI_SHADER_USER_DATA_HS_24"},
      {0x2d25, "SPI_SHADER_USER_DATA_HS_25"},
      {0x2d26, "SPI_SHADER_USER_DATA_HS_26"},
      {0x2d27, "SPI_SHADER_USER_DATA_HS_27"},
      {0x2d28, "SPI_SHADER_USER_DATA_HS_28"},
      {0x2d29, "SPI_SHADER_USER_DATA_HS_29"},
      {0x2d2a, "SPI_SHADER_USER_DATA_HS_30"},
      {0x2d2b, "SPI_SHADER_USER_DATA_HS_31"},

      {0x2d4c, "SPI_SHADER_USER_DATA_LS_0"},
      {0x2d4d, "SPI_SHADER_USER_DATA_LS_1"},
      {0x2d4e, "SPI_SHADER_USER_DATA_LS_2"},
      {0x2d4f, "SPI_SHADER_USER_DATA_LS_3"},
      {0x2d50, "SPI_SHADER_USER_DATA_LS_4"},
      {0x2d51, "SPI_SHADER_USER_DATA_LS_5"},
      {0x2d52, "SPI_SHADER_USER_DATA_LS_6"},
      {0x2d53, "SPI_SHADER_USER_DATA_LS_7"},
      {0x2d54, "SPI_SHADER_USER_DATA_LS_8"},
      {0x2d55, "SPI_SHADER_USER_DATA_LS_9"},
      {0x2d56, "SPI_SHADER_USER_DATA_LS_10"},
      {0x2d57, "SPI_SHADER_USER_DATA_LS_11"},
      {0x2d58, "SPI_SHADER_USER_DATA_LS_12"},
      {0x2d59, "SPI_SHADER_USER_DATA_LS_13"},
      {0x2d5a, "SPI_SHADER_USER_DATA_LS_14"},
      {0x2d5b, "SPI_SHADER_USER_DATA_LS_15"},

      {0xa2aa, "IA_MULTI_VGT_PARAM"},
      {0xa2a5, "VGT_GS_MAX_PRIMS_PER_SUBGROUP"},
      {0xa2e6, "VGT_STRMOUT_BUFFER_CONFIG"},
      {0xa2e5, "VGT_STRMOUT_CONFIG"},
      {0xa2b5, "VGT_STRMOUT_VTX_STRIDE_0"},
      {0xa2b9, "VGT_STRMOUT_VTX_STRIDE_1"},
      {0xa2bd, "VGT_STRMOUT_VTX_STRIDE_2"},
      {0xa2c1, "VGT_STRMOUT_VTX_STRIDE_3"},
      {0xa316, "VGT_VERTEX_REUSE_BLOCK_CNTL"},

      {0x2e28, "COMPUTE_PGM_RSRC3"},
      {0x2e2a, "COMPUTE_SHADER_CHKSUM"},
      {0x2e24, "COMPUTE_USER_ACCUM_0"},
      {0x2e25, "COMPUTE_USER_ACCUM_1"},
      {0x2e26, "COMPUTE_USER_ACCUM_2"},
      {0x2e27, "COMPUTE_USER_ACCUM_3"},
      {0xa1ff, "GE_MAX_OUTPUT_PER_SUBGROUP"},
      {0xa2d3, "GE_NGG_SUBGRP_CNTL"},
      {0xc25f, "GE_STEREO_CNTL"},
      {0xc262, "GE_USER_VGPR_EN"},
      {0xc258, "IA_MULTI_VGT_PARAM_PIPED"},
      {0xa210, "PA_STEREO_CNTL"},
      {0xa1c2, "SPI_SHADER_IDX_FORMAT"},
      {0x2c80, "SPI_SHADER_PGM_CHKSUM_GS"},
      {0x2d00, "SPI_SHADER_PGM_CHKSUM_HS"},
      {0x2c06, "SPI_SHADER_PGM_CHKSUM_PS"},
      {0x2c45, "SPI_SHADER_PGM_CHKSUM_VS"},
      {0x2c88, "SPI_SHADER_PGM_LO_GS"},
      {0x2cb2, "SPI_SHADER_USER_ACCUM_ESGS_0"},
      {0x2cb3, "SPI_SHADER_USER_ACCUM_ESGS_1"},
      {0x2cb4, "SPI_SHADER_USER_ACCUM_ESGS_2"},
      {0x2cb5, "SPI_SHADER_USER_ACCUM_ESGS_3"},
      {0x2d32, "SPI_SHADER_USER_ACCUM_LSHS_0"},
      {0x2d33, "SPI_SHADER_USER_ACCUM_LSHS_1"},
      {0x2d34, "SPI_SHADER_USER_ACCUM_LSHS_2"},
      {0x2d35, "SPI_SHADER_USER_ACCUM_LSHS_3"},
      {0x2c32, "SPI_SHADER_USER_ACCUM_PS_0"},
      {0x2c33, "SPI_SHADER_USER_ACCUM_PS_1"},
      {0x2c34, "SPI_SHADER_USER_ACCUM_PS_2"},
      {0x2c35, "SPI_SHADER_USER_ACCUM_PS_3"},
      {0x2c72, "SPI_SHADER_USER_ACCUM_VS_0"},
      {0x2c73, "SPI_SHADER_USER_ACCUM_VS_1"},
      {0x2c74, "SPI_SHADER_USER_ACCUM_VS_2"},
      {0x2c75, "SPI_SHADER_USER_ACCUM_VS_3"},

      {0, nullptr}};
  auto Entry = RegInfoTable;
  for (; Entry->Num && Entry->Num != RegNum; ++Entry)
    ;
  return Entry->Name;
}

// Convert the accumulated PAL metadata into an asm directive.
void AMDGPUPALMetadata::toString(std::string &String) {
  String.clear();
  if (!BlobType)
    return;
  ResolvedAll = DelayedExprs.resolveDelayedExpressions();
  raw_string_ostream Stream(String);
  if (isLegacy()) {
    if (MsgPackDoc.getRoot().getKind() == msgpack::Type::Nil)
      return;
    // Old linear reg=val format.
    Stream << '\t' << AMDGPU::PALMD::AssemblerDirective << ' ';
    auto Regs = getRegisters();
    for (auto I = Regs.begin(), E = Regs.end(); I != E; ++I) {
      if (I != Regs.begin())
        Stream << ',';
      unsigned Reg = I->first.getUInt();
      unsigned Val = I->second.getUInt();
      Stream << "0x" << Twine::utohexstr(Reg) << ",0x" << Twine::utohexstr(Val);
    }
    Stream << '\n';
    return;
  }

  // New msgpack-based format -- output as YAML (with unsigned numbers in hex),
  // but first change the registers map to use names.
  MsgPackDoc.setHexMode();
  auto &RegsObj = refRegisters();
  auto OrigRegs = RegsObj.getMap();
  RegsObj = MsgPackDoc.getMapNode();
  for (auto I : OrigRegs) {
    auto Key = I.first;
    if (const char *RegName = getRegisterName(Key.getUInt())) {
      std::string KeyName = Key.toString();
      KeyName += " (";
      KeyName += RegName;
      KeyName += ')';
      Key = MsgPackDoc.getNode(KeyName, /*Copy=*/true);
    }
    RegsObj.getMap()[Key] = I.second;
  }

  // Output as YAML.
  Stream << '\t' << AMDGPU::PALMD::AssemblerDirectiveBegin << '\n';
  MsgPackDoc.toYAML(Stream);
  Stream << '\t' << AMDGPU::PALMD::AssemblerDirectiveEnd << '\n';

  // Restore original registers map.
  RegsObj = OrigRegs;
}

// Convert the accumulated PAL metadata into a binary blob for writing as
// a .note record of the specified AMD type. Returns an empty blob if
// there is no PAL metadata,
void AMDGPUPALMetadata::toBlob(unsigned Type, std::string &Blob) {
  ResolvedAll = DelayedExprs.resolveDelayedExpressions();
  if (Type == ELF::NT_AMD_PAL_METADATA)
    toLegacyBlob(Blob);
  else if (Type)
    toMsgPackBlob(Blob);
}

void AMDGPUPALMetadata::toLegacyBlob(std::string &Blob) {
  Blob.clear();
  auto Registers = getRegisters();
  if (Registers.getMap().empty())
    return;
  raw_string_ostream OS(Blob);
  support::endian::Writer EW(OS, llvm::endianness::little);
  for (auto I : Registers.getMap()) {
    EW.write(uint32_t(I.first.getUInt()));
    EW.write(uint32_t(I.second.getUInt()));
  }
}

void AMDGPUPALMetadata::toMsgPackBlob(std::string &Blob) {
  Blob.clear();
  MsgPackDoc.writeToBlob(Blob);
}

// Set PAL metadata from YAML text. Returns false if failed.
bool AMDGPUPALMetadata::setFromString(StringRef S) {
  BlobType = ELF::NT_AMDGPU_METADATA;
  if (!MsgPackDoc.fromYAML(S))
    return false;

  // In the registers map, some keys may be of the form "0xa191
  // (SPI_PS_INPUT_CNTL_0)", in which case the YAML input code made it a
  // string. We need to turn it into a number.
  auto &RegsObj = refRegisters();
  auto OrigRegs = RegsObj;
  RegsObj = MsgPackDoc.getMapNode();
  Registers = RegsObj.getMap();
  bool Ok = true;
  for (auto I : OrigRegs.getMap()) {
    auto Key = I.first;
    if (Key.getKind() == msgpack::Type::String) {
      StringRef S = Key.getString();
      uint64_t Val;
      if (S.consumeInteger(0, Val)) {
        Ok = false;
        errs() << "Unrecognized PAL metadata register key '" << S << "'\n";
        continue;
      }
      Key = MsgPackDoc.getNode(uint64_t(Val));
    }
    Registers.getMap()[Key] = I.second;
  }
  return Ok;
}

// Reference (create if necessary) the node for the registers map.
msgpack::DocNode &AMDGPUPALMetadata::refRegisters() {
  auto &N =
      MsgPackDoc.getRoot()
          .getMap(/*Convert=*/true)[MsgPackDoc.getNode("amdpal.pipelines")]
          .getArray(/*Convert=*/true)[0]
          .getMap(/*Convert=*/true)[MsgPackDoc.getNode(".registers")];
  N.getMap(/*Convert=*/true);
  return N;
}

// Get (create if necessary) the registers map.
msgpack::MapDocNode AMDGPUPALMetadata::getRegisters() {
  if (Registers.isEmpty())
    Registers = refRegisters();
  return Registers.getMap();
}

// Reference (create if necessary) the node for the shader functions map.
msgpack::DocNode &AMDGPUPALMetadata::refShaderFunctions() {
  auto &N =
      MsgPackDoc.getRoot()
          .getMap(/*Convert=*/true)[MsgPackDoc.getNode("amdpal.pipelines")]
          .getArray(/*Convert=*/true)[0]
          .getMap(/*Convert=*/true)[MsgPackDoc.getNode(".shader_functions")];
  N.getMap(/*Convert=*/true);
  return N;
}

// Get (create if necessary) the shader functions map.
msgpack::MapDocNode AMDGPUPALMetadata::getShaderFunctions() {
  if (ShaderFunctions.isEmpty())
    ShaderFunctions = refShaderFunctions();
  return ShaderFunctions.getMap();
}

// Get (create if necessary) a function in the shader functions map.
msgpack::MapDocNode AMDGPUPALMetadata::getShaderFunction(StringRef Name) {
  auto Functions = getShaderFunctions();
  return Functions[Name].getMap(/*Convert=*/true);
}

msgpack::DocNode &AMDGPUPALMetadata::refComputeRegisters() {
  auto &N =
      MsgPackDoc.getRoot()
          .getMap(/*Convert=*/true)[MsgPackDoc.getNode("amdpal.pipelines")]
          .getArray(/*Convert=*/true)[0]
          .getMap(/*Convert=*/true)[MsgPackDoc.getNode(".compute_registers")];
  N.getMap(/*Convert=*/true);
  return N;
}

msgpack::MapDocNode AMDGPUPALMetadata::getComputeRegisters() {
  if (ComputeRegisters.isEmpty())
    ComputeRegisters = refComputeRegisters();
  return ComputeRegisters.getMap();
}

msgpack::DocNode &AMDGPUPALMetadata::refGraphicsRegisters() {
  auto &N =
      MsgPackDoc.getRoot()
          .getMap(/*Convert=*/true)[MsgPackDoc.getNode("amdpal.pipelines")]
          .getArray(/*Convert=*/true)[0]
          .getMap(/*Convert=*/true)[MsgPackDoc.getNode(".graphics_registers")];
  N.getMap(/*Convert=*/true);
  return N;
}

msgpack::MapDocNode AMDGPUPALMetadata::getGraphicsRegisters() {
  if (GraphicsRegisters.isEmpty())
    GraphicsRegisters = refGraphicsRegisters();
  return GraphicsRegisters.getMap();
}

// Return the PAL metadata hardware shader stage name.
static const char *getStageName(CallingConv::ID CC) {
  switch (CC) {
  case CallingConv::AMDGPU_PS:
    return ".ps";
  case CallingConv::AMDGPU_VS:
    return ".vs";
  case CallingConv::AMDGPU_GS:
    return ".gs";
  case CallingConv::AMDGPU_ES:
    return ".es";
  case CallingConv::AMDGPU_HS:
    return ".hs";
  case CallingConv::AMDGPU_LS:
    return ".ls";
  case CallingConv::AMDGPU_Gfx:
    llvm_unreachable("Callable shader has no hardware stage");
  default:
    return ".cs";
  }
}

msgpack::DocNode &AMDGPUPALMetadata::refHwStage() {
  auto &N =
      MsgPackDoc.getRoot()
          .getMap(/*Convert=*/true)[MsgPackDoc.getNode("amdpal.pipelines")]
          .getArray(/*Convert=*/true)[0]
          .getMap(/*Convert=*/true)[MsgPackDoc.getNode(".hardware_stages")];
  N.getMap(/*Convert=*/true);
  return N;
}

// Get (create if necessary) the .hardware_stages entry for the given calling
// convention.
msgpack::MapDocNode AMDGPUPALMetadata::getHwStage(unsigned CC) {
  if (HwStages.isEmpty())
    HwStages = refHwStage();
  return HwStages.getMap()[getStageName(CC)].getMap(/*Convert=*/true);
}

// Get .note record vendor name of metadata blob to be emitted.
const char *AMDGPUPALMetadata::getVendor() const {
  return isLegacy() ? ElfNote::NoteNameV2 : ElfNote::NoteNameV3;
}

// Get .note record type of metadata blob to be emitted:
// ELF::NT_AMD_PAL_METADATA (legacy key=val format), or
// ELF::NT_AMDGPU_METADATA (MsgPack format), or
// 0 (no PAL metadata).
unsigned AMDGPUPALMetadata::getType() const {
  return BlobType;
}

// Return whether the blob type is legacy PAL metadata.
bool AMDGPUPALMetadata::isLegacy() const {
  return BlobType == ELF::NT_AMD_PAL_METADATA;
}

// Set legacy PAL metadata format.
void AMDGPUPALMetadata::setLegacy() {
  BlobType = ELF::NT_AMD_PAL_METADATA;
}

// Erase all PAL metadata.
void AMDGPUPALMetadata::reset() {
  MsgPackDoc.clear();
  REM.clear();
  DelayedExprs.clear();
  Registers = MsgPackDoc.getEmptyNode();
  HwStages = MsgPackDoc.getEmptyNode();
  ShaderFunctions = MsgPackDoc.getEmptyNode();
}

bool AMDGPUPALMetadata::resolvedAllMCExpr() {
  return ResolvedAll && DelayedExprs.empty();
}

unsigned AMDGPUPALMetadata::getPALVersion(unsigned idx) {
  assert(idx < 2 &&
         "illegal index to PAL version - should be 0 (major) or 1 (minor)");
  if (!VersionChecked) {
    if (Version.isEmpty()) {
      auto &M = MsgPackDoc.getRoot().getMap(/*Convert=*/true);
      auto I = M.find(MsgPackDoc.getNode("amdpal.version"));
      if (I != M.end())
        Version = I->second;
    }
    VersionChecked = true;
  }
  if (Version.isEmpty())
    // Default to 2.6 if there's no version info
    return idx ? 6 : 2;
  return Version.getArray()[idx].getUInt();
}

unsigned AMDGPUPALMetadata::getPALMajorVersion() { return getPALVersion(0); }

unsigned AMDGPUPALMetadata::getPALMinorVersion() { return getPALVersion(1); }

// Set the field in a given .hardware_stages entry
void AMDGPUPALMetadata::setHwStage(unsigned CC, StringRef field, unsigned Val) {
  getHwStage(CC)[field] = Val;
}

void AMDGPUPALMetadata::setHwStage(unsigned CC, StringRef field, bool Val) {
  getHwStage(CC)[field] = Val;
}

void AMDGPUPALMetadata::setHwStage(unsigned CC, StringRef field,
                                   msgpack::Type Type, const MCExpr *Val) {
  DelayedExprs.assignDocNode(getHwStage(CC)[field], Type, Val);
}

void AMDGPUPALMetadata::setComputeRegisters(StringRef field, unsigned Val) {
  getComputeRegisters()[field] = Val;
}

void AMDGPUPALMetadata::setComputeRegisters(StringRef field, bool Val) {
  getComputeRegisters()[field] = Val;
}

msgpack::DocNode *AMDGPUPALMetadata::refComputeRegister(StringRef field) {
  auto M = getComputeRegisters();
  auto I = M.find(field);
  return I == M.end() ? nullptr : &I->second;
}

bool AMDGPUPALMetadata::checkComputeRegisters(StringRef field, unsigned Val) {
  if (auto N = refComputeRegister(field))
    return N->getUInt() == Val;
  return false;
}

bool AMDGPUPALMetadata::checkComputeRegisters(StringRef field, bool Val) {
  if (auto N = refComputeRegister(field))
    return N->getBool() == Val;
  return false;
}

void AMDGPUPALMetadata::setGraphicsRegisters(StringRef field, unsigned Val) {
  getGraphicsRegisters()[field] = Val;
}

void AMDGPUPALMetadata::setGraphicsRegisters(StringRef field, bool Val) {
  getGraphicsRegisters()[field] = Val;
}

void AMDGPUPALMetadata::setGraphicsRegisters(StringRef field1, StringRef field2,
                                             unsigned Val) {
  getGraphicsRegisters()[field1].getMap(true)[field2] = Val;
}

void AMDGPUPALMetadata::setGraphicsRegisters(StringRef field1, StringRef field2,
                                             bool Val) {
  getGraphicsRegisters()[field1].getMap(true)[field2] = Val;
}
