//===- AMDKernelCodeTUtils.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file - utility functions to parse/print AMDGPUMCKernelCodeT structure
//
//===----------------------------------------------------------------------===//

#include "AMDKernelCodeTUtils.h"
#include "AMDKernelCodeT.h"
#include "SIDefines.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "Utils/SIDefinesUtils.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::AMDGPU;

// Generates the following for AMDGPUMCKernelCodeT struct members:
//   - HasMemberXXXXX class
//     A check to see if AMDGPUMCKernelCodeT has a specific member so it can
//     determine which of the original amd_kernel_code_t members are duplicated
//     (if the names don't match, the table driven strategy won't work).
//   - IsMCExprXXXXX class
//     Check whether a AMDGPUMCKernelcodeT struct member is MCExpr-ified or not.
//   - GetMemberXXXXX class
//     A retrieval helper for said member (of type const MCExpr *&). Will return
//     a `Phony` const MCExpr * initialized to nullptr to preserve reference
//     returns.
#define GEN_HAS_MEMBER(member)                                                 \
  class HasMember##member {                                                    \
  private:                                                                     \
    struct KnownWithMember {                                                   \
      int member;                                                              \
    };                                                                         \
    class AmbiguousDerived : public AMDGPUMCKernelCodeT,                       \
                             public KnownWithMember {};                        \
    template <typename U>                                                      \
    static constexpr std::false_type Test(decltype(U::member) *);              \
    template <typename U> static constexpr std::true_type Test(...);           \
                                                                               \
  public:                                                                      \
    static constexpr bool RESULT =                                             \
        std::is_same_v<decltype(Test<AmbiguousDerived>(nullptr)),              \
                       std::true_type>;                                        \
  };                                                                           \
  class IsMCExpr##member {                                                     \
    template <typename U,                                                      \
              typename std::enable_if_t<                                       \
                  HasMember##member::RESULT &&                                 \
                      std::is_same_v<decltype(U::member), const MCExpr *>,     \
                  U> * = nullptr>                                              \
    static constexpr std::true_type HasMCExprType(decltype(U::member) *);      \
    template <typename U> static constexpr std::false_type HasMCExprType(...); \
                                                                               \
  public:                                                                      \
    static constexpr bool RESULT =                                             \
        std::is_same_v<decltype(HasMCExprType<AMDGPUMCKernelCodeT>(nullptr)),  \
                       std::true_type>;                                        \
  };                                                                           \
  class GetMember##member {                                                    \
  public:                                                                      \
    static const MCExpr *Phony;                                                \
    template <typename U, typename std::enable_if_t<IsMCExpr##member::RESULT,  \
                                                    U> * = nullptr>            \
    static const MCExpr *&Get(U &C) {                                          \
      assert(IsMCExpr##member::RESULT &&                                       \
             "Trying to retrieve member that does not exist.");                \
      return C.member;                                                         \
    }                                                                          \
    template <typename U, typename std::enable_if_t<!IsMCExpr##member::RESULT, \
                                                    U> * = nullptr>            \
    static const MCExpr *&Get(U &C) {                                          \
      return Phony;                                                            \
    }                                                                          \
  };                                                                           \
  const MCExpr *GetMember##member::Phony = nullptr;

// Cannot generate class declarations using the table driver approach (see table
// in AMDKernelCodeTInfo.h). Luckily, if any are missing here or eventually
// added to the table, an error should occur when trying to retrieve the table
// in getMCExprIndexTable.
GEN_HAS_MEMBER(amd_code_version_major)
GEN_HAS_MEMBER(amd_code_version_minor)
GEN_HAS_MEMBER(amd_machine_kind)
GEN_HAS_MEMBER(amd_machine_version_major)
GEN_HAS_MEMBER(amd_machine_version_minor)
GEN_HAS_MEMBER(amd_machine_version_stepping)

GEN_HAS_MEMBER(kernel_code_entry_byte_offset)
GEN_HAS_MEMBER(kernel_code_prefetch_byte_size)

GEN_HAS_MEMBER(granulated_workitem_vgpr_count)
GEN_HAS_MEMBER(granulated_wavefront_sgpr_count)
GEN_HAS_MEMBER(priority)
GEN_HAS_MEMBER(float_mode)
GEN_HAS_MEMBER(priv)
GEN_HAS_MEMBER(enable_dx10_clamp)
GEN_HAS_MEMBER(debug_mode)
GEN_HAS_MEMBER(enable_ieee_mode)
GEN_HAS_MEMBER(enable_wgp_mode)
GEN_HAS_MEMBER(enable_mem_ordered)
GEN_HAS_MEMBER(enable_fwd_progress)

GEN_HAS_MEMBER(enable_sgpr_private_segment_wave_byte_offset)
GEN_HAS_MEMBER(user_sgpr_count)
GEN_HAS_MEMBER(enable_trap_handler)
GEN_HAS_MEMBER(enable_sgpr_workgroup_id_x)
GEN_HAS_MEMBER(enable_sgpr_workgroup_id_y)
GEN_HAS_MEMBER(enable_sgpr_workgroup_id_z)
GEN_HAS_MEMBER(enable_sgpr_workgroup_info)
GEN_HAS_MEMBER(enable_vgpr_workitem_id)
GEN_HAS_MEMBER(enable_exception_msb)
GEN_HAS_MEMBER(granulated_lds_size)
GEN_HAS_MEMBER(enable_exception)

GEN_HAS_MEMBER(enable_sgpr_private_segment_buffer)
GEN_HAS_MEMBER(enable_sgpr_dispatch_ptr)
GEN_HAS_MEMBER(enable_sgpr_queue_ptr)
GEN_HAS_MEMBER(enable_sgpr_kernarg_segment_ptr)
GEN_HAS_MEMBER(enable_sgpr_dispatch_id)
GEN_HAS_MEMBER(enable_sgpr_flat_scratch_init)
GEN_HAS_MEMBER(enable_sgpr_private_segment_size)
GEN_HAS_MEMBER(enable_sgpr_grid_workgroup_count_x)
GEN_HAS_MEMBER(enable_sgpr_grid_workgroup_count_y)
GEN_HAS_MEMBER(enable_sgpr_grid_workgroup_count_z)
GEN_HAS_MEMBER(enable_wavefront_size32)
GEN_HAS_MEMBER(enable_ordered_append_gds)
GEN_HAS_MEMBER(private_element_size)
GEN_HAS_MEMBER(is_ptr64)
GEN_HAS_MEMBER(is_dynamic_callstack)
GEN_HAS_MEMBER(is_debug_enabled)
GEN_HAS_MEMBER(is_xnack_enabled)

GEN_HAS_MEMBER(workitem_private_segment_byte_size)
GEN_HAS_MEMBER(workgroup_group_segment_byte_size)
GEN_HAS_MEMBER(gds_segment_byte_size)
GEN_HAS_MEMBER(kernarg_segment_byte_size)
GEN_HAS_MEMBER(workgroup_fbarrier_count)
GEN_HAS_MEMBER(wavefront_sgpr_count)
GEN_HAS_MEMBER(workitem_vgpr_count)
GEN_HAS_MEMBER(reserved_vgpr_first)
GEN_HAS_MEMBER(reserved_vgpr_count)
GEN_HAS_MEMBER(reserved_sgpr_first)
GEN_HAS_MEMBER(reserved_sgpr_count)
GEN_HAS_MEMBER(debug_wavefront_private_segment_offset_sgpr)
GEN_HAS_MEMBER(debug_private_segment_buffer_sgpr)
GEN_HAS_MEMBER(kernarg_segment_alignment)
GEN_HAS_MEMBER(group_segment_alignment)
GEN_HAS_MEMBER(private_segment_alignment)
GEN_HAS_MEMBER(wavefront_size)
GEN_HAS_MEMBER(call_convention)
GEN_HAS_MEMBER(runtime_loader_kernel_symbol)

static ArrayRef<StringLiteral> get_amd_kernel_code_t_FldNames() {
  static constexpr StringLiteral const Table[] = {
      "", // not found placeholder
#define RECORD(name, altName, print, parse) #name
#include "Utils/AMDKernelCodeTInfo.h"
#undef RECORD
  };
  return ArrayRef(Table);
}

static ArrayRef<StringLiteral> get_amd_kernel_code_t_FldAltNames() {
  static constexpr StringLiteral const Table[] = {
      "", // not found placeholder
#define RECORD(name, altName, print, parse) #altName
#include "Utils/AMDKernelCodeTInfo.h"
#undef RECORD
  };
  return ArrayRef(Table);
}

static ArrayRef<bool> hasMCExprVersionTable() {
  static bool const Table[] = {
#define RECORD(name, altName, print, parse) (IsMCExpr##name::RESULT)
#include "Utils/AMDKernelCodeTInfo.h"
#undef RECORD
  };
  return ArrayRef(Table);
}

using RetrieveFx = const MCExpr *&(*)(AMDGPUMCKernelCodeT &);

static ArrayRef<RetrieveFx> getMCExprIndexTable() {
  static const RetrieveFx Table[] = {
#define RECORD(name, altName, print, parse) GetMember##name::Get
#include "Utils/AMDKernelCodeTInfo.h"
#undef RECORD
  };
  return ArrayRef(Table);
}

static StringMap<int> createIndexMap(ArrayRef<StringLiteral> names,
                                     ArrayRef<StringLiteral> altNames) {
  StringMap<int> map;
  assert(names.size() == altNames.size());
  for (unsigned i = 0; i < names.size(); ++i) {
    map.insert(std::pair(names[i], i));
    map.insert(std::pair(altNames[i], i));
  }
  return map;
}

static int get_amd_kernel_code_t_FieldIndex(StringRef name) {
  static const auto map = createIndexMap(get_amd_kernel_code_t_FldNames(),
                                         get_amd_kernel_code_t_FldAltNames());
  return map.lookup(name) - 1; // returns -1 if not found
}

class PrintField {
public:
  template <typename T, T AMDGPUMCKernelCodeT::*ptr,
            typename std::enable_if_t<!std::is_integral_v<T>, T> * = nullptr>
  static void printField(StringRef Name, const AMDGPUMCKernelCodeT &C,
                         raw_ostream &OS, MCContext &Ctx) {
    OS << Name << " = ";
    const MCExpr *Value = C.*ptr;
    int64_t Val;
    if (Value->evaluateAsAbsolute(Val))
      OS << Val;
    else
      Value->print(OS, Ctx.getAsmInfo());
  }

  template <typename T, T AMDGPUMCKernelCodeT::*ptr,
            typename std::enable_if_t<std::is_integral_v<T>, T> * = nullptr>
  static void printField(StringRef Name, const AMDGPUMCKernelCodeT &C,
                         raw_ostream &OS, MCContext &) {
    OS << Name << " = " << (int)(C.*ptr);
  }
};

template <typename T, T AMDGPUMCKernelCodeT::*ptr, int shift, int width = 1>
static void printBitField(StringRef Name, const AMDGPUMCKernelCodeT &C,
                          raw_ostream &OS, MCContext &) {
  const auto Mask = (static_cast<T>(1) << width) - 1;
  OS << Name << " = " << (int)((C.*ptr >> shift) & Mask);
}

using PrintFx = void (*)(StringRef, const AMDGPUMCKernelCodeT &, raw_ostream &,
                         MCContext &);

static ArrayRef<PrintFx> getPrinterTable() {
  static const PrintFx Table[] = {
#define COMPPGM1(name, aname, AccMacro)                                        \
  COMPPGM(name, aname, C_00B848_##AccMacro, S_00B848_##AccMacro, 0)
#define COMPPGM2(name, aname, AccMacro)                                        \
  COMPPGM(name, aname, C_00B84C_##AccMacro, S_00B84C_##AccMacro, 32)
#define PRINTFIELD(sname, aname, name) PrintField::printField<FLD_T(name)>
#define PRINTCOMP(Complement, PGMType)                                         \
  [](StringRef Name, const AMDGPUMCKernelCodeT &C, raw_ostream &OS,            \
     MCContext &Ctx) {                                                         \
    OS << Name << " = ";                                                       \
    auto [Shift, Mask] = getShiftMask(Complement);                             \
    const MCExpr *Value;                                                       \
    if (PGMType == 0) {                                                        \
      Value =                                                                  \
          maskShiftGet(C.compute_pgm_resource1_registers, Mask, Shift, Ctx);   \
    } else {                                                                   \
      Value =                                                                  \
          maskShiftGet(C.compute_pgm_resource2_registers, Mask, Shift, Ctx);   \
    }                                                                          \
    int64_t Val;                                                               \
    if (Value->evaluateAsAbsolute(Val))                                        \
      OS << Val;                                                               \
    else                                                                       \
      Value->print(OS, Ctx.getAsmInfo());                                      \
  }
#define RECORD(name, altName, print, parse) print
#include "Utils/AMDKernelCodeTInfo.h"
#undef RECORD
  };
  return ArrayRef(Table);
}

static bool expectAbsExpression(MCAsmParser &MCParser, int64_t &Value,
                                raw_ostream &Err) {

  if (MCParser.getLexer().isNot(AsmToken::Equal)) {
    Err << "expected '='";
    return false;
  }
  MCParser.getLexer().Lex();

  if (MCParser.parseAbsoluteExpression(Value)) {
    Err << "integer absolute expression expected";
    return false;
  }
  return true;
}

template <typename T, T AMDGPUMCKernelCodeT::*ptr>
static bool parseField(AMDGPUMCKernelCodeT &C, MCAsmParser &MCParser,
                       raw_ostream &Err) {
  int64_t Value = 0;
  if (!expectAbsExpression(MCParser, Value, Err))
    return false;
  C.*ptr = (T)Value;
  return true;
}

template <typename T, T AMDGPUMCKernelCodeT::*ptr, int shift, int width = 1>
static bool parseBitField(AMDGPUMCKernelCodeT &C, MCAsmParser &MCParser,
                          raw_ostream &Err) {
  int64_t Value = 0;
  if (!expectAbsExpression(MCParser, Value, Err))
    return false;
  const uint64_t Mask = ((UINT64_C(1) << width) - 1) << shift;
  C.*ptr &= (T)~Mask;
  C.*ptr |= (T)((Value << shift) & Mask);
  return true;
}

static bool parseExpr(MCAsmParser &MCParser, const MCExpr *&Value,
                      raw_ostream &Err) {
  if (MCParser.getLexer().isNot(AsmToken::Equal)) {
    Err << "expected '='";
    return false;
  }
  MCParser.getLexer().Lex();

  if (MCParser.parseExpression(Value)) {
    Err << "Could not parse expression";
    return false;
  }
  return true;
}

using ParseFx = bool (*)(AMDGPUMCKernelCodeT &, MCAsmParser &, raw_ostream &);

static ArrayRef<ParseFx> getParserTable() {
  static const ParseFx Table[] = {
#define COMPPGM1(name, aname, AccMacro)                                        \
  COMPPGM(name, aname, G_00B848_##AccMacro, C_00B848_##AccMacro, 0)
#define COMPPGM2(name, aname, AccMacro)                                        \
  COMPPGM(name, aname, G_00B84C_##AccMacro, C_00B84C_##AccMacro, 32)
#define PARSECOMP(Complement, PGMType)                                         \
  [](AMDGPUMCKernelCodeT &C, MCAsmParser &MCParser,                            \
     raw_ostream &Err) -> bool {                                               \
    MCContext &Ctx = MCParser.getContext();                                    \
    const MCExpr *Value;                                                       \
    if (!parseExpr(MCParser, Value, Err))                                      \
      return false;                                                            \
    auto [Shift, Mask] = getShiftMask(Complement);                             \
    Value = maskShiftSet(Value, Mask, Shift, Ctx);                             \
    const MCExpr *Compl = MCConstantExpr::create(Complement, Ctx);             \
    if (PGMType == 0) {                                                        \
      C.compute_pgm_resource1_registers = MCBinaryExpr::createAnd(             \
          C.compute_pgm_resource1_registers, Compl, Ctx);                      \
      C.compute_pgm_resource1_registers = MCBinaryExpr::createOr(              \
          C.compute_pgm_resource1_registers, Value, Ctx);                      \
    } else {                                                                   \
      C.compute_pgm_resource2_registers = MCBinaryExpr::createAnd(             \
          C.compute_pgm_resource2_registers, Compl, Ctx);                      \
      C.compute_pgm_resource2_registers = MCBinaryExpr::createOr(              \
          C.compute_pgm_resource2_registers, Value, Ctx);                      \
    }                                                                          \
    return true;                                                               \
  }
#define RECORD(name, altName, print, parse) parse
#include "Utils/AMDKernelCodeTInfo.h"
#undef RECORD
  };
  return ArrayRef(Table);
}

static void printAmdKernelCodeField(const AMDGPUMCKernelCodeT &C, int FldIndex,
                                    raw_ostream &OS, MCContext &Ctx) {
  auto Printer = getPrinterTable()[FldIndex];
  if (Printer)
    Printer(get_amd_kernel_code_t_FldNames()[FldIndex + 1], C, OS, Ctx);
}

void AMDGPUMCKernelCodeT::initDefault(const MCSubtargetInfo *STI,
                                      MCContext &Ctx, bool InitMCExpr) {
  AMDGPUMCKernelCodeT();

  AMDGPU::initDefaultAMDKernelCodeT(*this, STI);

  if (InitMCExpr) {
    const MCExpr *ZeroExpr = MCConstantExpr::create(0, Ctx);
    compute_pgm_resource1_registers =
        MCConstantExpr::create(Lo_32(compute_pgm_resource_registers), Ctx);
    compute_pgm_resource2_registers =
        MCConstantExpr::create(Hi_32(compute_pgm_resource_registers), Ctx);
    is_dynamic_callstack = ZeroExpr;
    wavefront_sgpr_count = ZeroExpr;
    workitem_vgpr_count = ZeroExpr;
    workitem_private_segment_byte_size = ZeroExpr;
  }
}

void AMDGPUMCKernelCodeT::validate(const MCSubtargetInfo *STI, MCContext &Ctx) {
  int64_t Value;
  if (!compute_pgm_resource1_registers->evaluateAsAbsolute(Value))
    return;

  if (G_00B848_DX10_CLAMP(Value) && AMDGPU::isGFX12Plus(*STI)) {
    Ctx.reportError({}, "enable_dx10_clamp=1 is not allowed on GFX12+");
    return;
  }

  if (G_00B848_IEEE_MODE(Value) && AMDGPU::isGFX12Plus(*STI)) {
    Ctx.reportError({}, "enable_ieee_mode=1 is not allowed on GFX12+");
    return;
  }

  if (G_00B848_WGP_MODE(Value) && !AMDGPU::isGFX10Plus(*STI)) {
    Ctx.reportError({}, "enable_wgp_mode=1 is only allowed on GFX10+");
    return;
  }

  if (G_00B848_MEM_ORDERED(Value) && !AMDGPU::isGFX10Plus(*STI)) {
    Ctx.reportError({}, "enable_mem_ordered=1 is only allowed on GFX10+");
    return;
  }

  if (G_00B848_FWD_PROGRESS(Value) && !AMDGPU::isGFX10Plus(*STI)) {
    Ctx.reportError({}, "enable_fwd_progress=1 is only allowed on GFX10+");
    return;
  }
}

const MCExpr *&AMDGPUMCKernelCodeT::getMCExprForIndex(int Index) {
  static const auto IndexTable = getMCExprIndexTable();
  return IndexTable[Index](*this);
}

bool AMDGPUMCKernelCodeT::ParseKernelCodeT(StringRef ID, MCAsmParser &MCParser,
                                           raw_ostream &Err) {
  const int Idx = get_amd_kernel_code_t_FieldIndex(ID);
  if (Idx < 0) {
    Err << "unexpected amd_kernel_code_t field name " << ID;
    return false;
  }

  if (hasMCExprVersionTable()[Idx]) {
    const MCExpr *Value;
    if (!parseExpr(MCParser, Value, Err))
      return false;
    getMCExprForIndex(Idx) = Value;
    return true;
  }
  auto Parser = getParserTable()[Idx];
  return Parser ? Parser(*this, MCParser, Err) : false;
}

void AMDGPUMCKernelCodeT::EmitKernelCodeT(raw_ostream &OS, MCContext &Ctx) {
  const int Size = hasMCExprVersionTable().size();
  for (int i = 0; i < Size; ++i) {
    OS << "\t\t";
    if (hasMCExprVersionTable()[i]) {
      OS << get_amd_kernel_code_t_FldNames()[i + 1] << " = ";
      int64_t Val;
      const MCExpr *Value = getMCExprForIndex(i);
      if (Value->evaluateAsAbsolute(Val))
        OS << Val;
      else
        Value->print(OS, Ctx.getAsmInfo());
    } else {
      printAmdKernelCodeField(*this, i, OS, Ctx);
    }
    OS << '\n';
  }
}

void AMDGPUMCKernelCodeT::EmitKernelCodeT(MCStreamer &OS, MCContext &Ctx) {
  OS.emitIntValue(amd_kernel_code_version_major, /*Size=*/4);
  OS.emitIntValue(amd_kernel_code_version_minor, /*Size=*/4);
  OS.emitIntValue(amd_machine_kind, /*Size=*/2);
  OS.emitIntValue(amd_machine_version_major, /*Size=*/2);
  OS.emitIntValue(amd_machine_version_minor, /*Size=*/2);
  OS.emitIntValue(amd_machine_version_stepping, /*Size=*/2);
  OS.emitIntValue(kernel_code_entry_byte_offset, /*Size=*/8);
  OS.emitIntValue(kernel_code_prefetch_byte_offset, /*Size=*/8);
  OS.emitIntValue(kernel_code_prefetch_byte_size, /*Size=*/8);
  OS.emitIntValue(reserved0, /*Size=*/8);

  if (compute_pgm_resource1_registers != nullptr)
    OS.emitValue(compute_pgm_resource1_registers, /*Size=*/4);
  else
    OS.emitIntValue(Lo_32(compute_pgm_resource_registers),
                    /*Size=*/4);

  if (compute_pgm_resource2_registers != nullptr)
    OS.emitValue(compute_pgm_resource2_registers, /*Size=*/4);
  else
    OS.emitIntValue(Hi_32(compute_pgm_resource_registers),
                    /*Size=*/4);

  if (is_dynamic_callstack != nullptr) {
    const MCExpr *CodeProps = MCConstantExpr::create(code_properties, Ctx);
    CodeProps = MCBinaryExpr::createOr(
        CodeProps,
        maskShiftSet(is_dynamic_callstack,
                     (1 << AMD_CODE_PROPERTY_IS_DYNAMIC_CALLSTACK_WIDTH) - 1,
                     AMD_CODE_PROPERTY_IS_DYNAMIC_CALLSTACK_SHIFT, Ctx),
        Ctx);
    OS.emitValue(CodeProps, /*Size=*/4);
  } else
    OS.emitIntValue(code_properties, /*Size=*/4);

  if (workitem_private_segment_byte_size != nullptr)
    OS.emitValue(workitem_private_segment_byte_size, /*Size=*/4);
  else
    OS.emitIntValue(0, /*Size=*/4);

  OS.emitIntValue(workgroup_group_segment_byte_size, /*Size=*/4);
  OS.emitIntValue(gds_segment_byte_size, /*Size=*/4);
  OS.emitIntValue(kernarg_segment_byte_size, /*Size=*/8);
  OS.emitIntValue(workgroup_fbarrier_count, /*Size=*/4);

  if (wavefront_sgpr_count != nullptr)
    OS.emitValue(wavefront_sgpr_count, /*Size=*/2);
  else
    OS.emitIntValue(0, /*Size=*/2);

  if (workitem_vgpr_count != nullptr)
    OS.emitValue(workitem_vgpr_count, /*Size=*/2);
  else
    OS.emitIntValue(0, /*Size=*/2);

  OS.emitIntValue(reserved_vgpr_first, /*Size=*/2);
  OS.emitIntValue(reserved_vgpr_count, /*Size=*/2);
  OS.emitIntValue(reserved_sgpr_first, /*Size=*/2);
  OS.emitIntValue(reserved_sgpr_count, /*Size=*/2);
  OS.emitIntValue(debug_wavefront_private_segment_offset_sgpr,
                  /*Size=*/2);
  OS.emitIntValue(debug_private_segment_buffer_sgpr, /*Size=*/2);
  OS.emitIntValue(kernarg_segment_alignment, /*Size=*/1);
  OS.emitIntValue(group_segment_alignment, /*Size=*/1);
  OS.emitIntValue(private_segment_alignment, /*Size=*/1);
  OS.emitIntValue(wavefront_size, /*Size=*/1);

  OS.emitIntValue(call_convention, /*Size=*/4);
  OS.emitBytes(StringRef((const char *)reserved3, /*Size=*/12));
  OS.emitIntValue(runtime_loader_kernel_symbol, /*Size=*/8);
  OS.emitBytes(StringRef((const char *)control_directives, /*Size=*/16 * 8));
}
