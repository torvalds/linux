//===--- Disasm.cpp - Disassembler for bytecode functions -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Dump method for Function which disassembles the bytecode.
//
//===----------------------------------------------------------------------===//

#include "Boolean.h"
#include "Context.h"
#include "EvaluationResult.h"
#include "Floating.h"
#include "Function.h"
#include "FunctionPointer.h"
#include "Integral.h"
#include "IntegralAP.h"
#include "InterpFrame.h"
#include "MemberPointer.h"
#include "Opcode.h"
#include "PrimType.h"
#include "Program.h"
#include "clang/AST/ASTDumperUtils.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Format.h"

using namespace clang;
using namespace clang::interp;

template <typename T> inline T ReadArg(Program &P, CodePtr &OpPC) {
  if constexpr (std::is_pointer_v<T>) {
    uint32_t ID = OpPC.read<uint32_t>();
    return reinterpret_cast<T>(P.getNativePointer(ID));
  } else {
    return OpPC.read<T>();
  }
}

template <> inline Floating ReadArg<Floating>(Program &P, CodePtr &OpPC) {
  Floating F = Floating::deserialize(*OpPC);
  OpPC += align(F.bytesToSerialize());
  return F;
}

template <>
inline IntegralAP<false> ReadArg<IntegralAP<false>>(Program &P, CodePtr &OpPC) {
  IntegralAP<false> I = IntegralAP<false>::deserialize(*OpPC);
  OpPC += align(I.bytesToSerialize());
  return I;
}

template <>
inline IntegralAP<true> ReadArg<IntegralAP<true>>(Program &P, CodePtr &OpPC) {
  IntegralAP<true> I = IntegralAP<true>::deserialize(*OpPC);
  OpPC += align(I.bytesToSerialize());
  return I;
}

LLVM_DUMP_METHOD void Function::dump() const { dump(llvm::errs()); }

LLVM_DUMP_METHOD void Function::dump(llvm::raw_ostream &OS) const {
  {
    ColorScope SC(OS, true, {llvm::raw_ostream::BRIGHT_GREEN, true});
    OS << getName() << " " << (const void *)this << "\n";
  }
  OS << "frame size: " << getFrameSize() << "\n";
  OS << "arg size:   " << getArgSize() << "\n";
  OS << "rvo:        " << hasRVO() << "\n";
  OS << "this arg:   " << hasThisPointer() << "\n";

  auto PrintName = [&OS](const char *Name) {
    OS << Name;
    long N = 30 - strlen(Name);
    if (N > 0)
      OS.indent(N);
  };

  for (CodePtr Start = getCodeBegin(), PC = Start; PC != getCodeEnd();) {
    size_t Addr = PC - Start;
    auto Op = PC.read<Opcode>();
    OS << llvm::format("%8d", Addr) << " ";
    switch (Op) {
#define GET_DISASM
#include "Opcodes.inc"
#undef GET_DISASM
    }
  }
}

LLVM_DUMP_METHOD void Program::dump() const { dump(llvm::errs()); }

static const char *primTypeToString(PrimType T) {
  switch (T) {
  case PT_Sint8:
    return "Sint8";
  case PT_Uint8:
    return "Uint8";
  case PT_Sint16:
    return "Sint16";
  case PT_Uint16:
    return "Uint16";
  case PT_Sint32:
    return "Sint32";
  case PT_Uint32:
    return "Uint32";
  case PT_Sint64:
    return "Sint64";
  case PT_Uint64:
    return "Uint64";
  case PT_IntAP:
    return "IntAP";
  case PT_IntAPS:
    return "IntAPS";
  case PT_Bool:
    return "Bool";
  case PT_Float:
    return "Float";
  case PT_Ptr:
    return "Ptr";
  case PT_FnPtr:
    return "FnPtr";
  case PT_MemberPtr:
    return "MemberPtr";
  }
  llvm_unreachable("Unhandled PrimType");
}

LLVM_DUMP_METHOD void Program::dump(llvm::raw_ostream &OS) const {
  {
    ColorScope SC(OS, true, {llvm::raw_ostream::BRIGHT_RED, true});
    OS << "\n:: Program\n";
  }

  {
    ColorScope SC(OS, true, {llvm::raw_ostream::WHITE, true});
    OS << "Total memory : " << Allocator.getTotalMemory() << " bytes\n";
    OS << "Global Variables: " << Globals.size() << "\n";
  }
  unsigned GI = 0;
  for (const Global *G : Globals) {
    const Descriptor *Desc = G->block()->getDescriptor();
    Pointer GP = getPtrGlobal(GI);

    OS << GI << ": " << (const void *)G->block() << " ";
    {
      ColorScope SC(OS, true,
                    GP.isInitialized()
                        ? TerminalColor{llvm::raw_ostream::GREEN, false}
                        : TerminalColor{llvm::raw_ostream::RED, false});
      OS << (GP.isInitialized() ? "initialized " : "uninitialized ");
    }
    Desc->dump(OS);

    if (GP.isInitialized() && Desc->IsTemporary) {
      if (const auto *MTE =
              dyn_cast_if_present<MaterializeTemporaryExpr>(Desc->asExpr());
          MTE && MTE->getLifetimeExtendedTemporaryDecl()) {
        if (const APValue *V =
                MTE->getLifetimeExtendedTemporaryDecl()->getValue()) {
          OS << " (global temporary value: ";
          {
            ColorScope SC(OS, true, {llvm::raw_ostream::BRIGHT_MAGENTA, true});
            std::string VStr;
            llvm::raw_string_ostream SS(VStr);
            V->dump(SS, Ctx.getASTContext());

            for (unsigned I = 0; I != VStr.size(); ++I) {
              if (VStr[I] == '\n')
                VStr[I] = ' ';
            }
            VStr.pop_back(); // Remove the newline (or now space) at the end.
            OS << VStr;
          }
          OS << ')';
        }
      }
    }

    OS << "\n";
    if (GP.isInitialized() && Desc->isPrimitive() && !Desc->isDummy()) {
      OS << "   ";
      {
        ColorScope SC(OS, true, {llvm::raw_ostream::BRIGHT_CYAN, false});
        OS << primTypeToString(Desc->getPrimType()) << " ";
      }
      TYPE_SWITCH(Desc->getPrimType(), { GP.deref<T>().print(OS); });
      OS << "\n";
    }
    ++GI;
  }

  {
    ColorScope SC(OS, true, {llvm::raw_ostream::WHITE, true});
    OS << "Functions: " << Funcs.size() << "\n";
  }
  for (const auto &Func : Funcs) {
    Func.second->dump();
  }
  for (const auto &Anon : AnonFuncs) {
    Anon->dump();
  }
}

LLVM_DUMP_METHOD void Descriptor::dump() const {
  dump(llvm::errs());
  llvm::errs() << '\n';
}

LLVM_DUMP_METHOD void Descriptor::dump(llvm::raw_ostream &OS) const {
  // Source
  {
    ColorScope SC(OS, true, {llvm::raw_ostream::BLUE, true});
    if (const auto *ND = dyn_cast_if_present<NamedDecl>(asDecl()))
      ND->printQualifiedName(OS);
    else if (asExpr())
      OS << "Expr " << (const void *)asExpr();
  }

  // Print a few interesting bits about the descriptor.
  if (isPrimitiveArray())
    OS << " primitive-array";
  else if (isCompositeArray())
    OS << " composite-array";
  else if (isRecord())
    OS << " record";
  else if (isPrimitive())
    OS << " primitive";

  if (isZeroSizeArray())
    OS << " zero-size-array";
  else if (isUnknownSizeArray())
    OS << " unknown-size-array";

  if (isDummy())
    OS << " dummy";
}

LLVM_DUMP_METHOD void InlineDescriptor::dump(llvm::raw_ostream &OS) const {
  {
    ColorScope SC(OS, true, {llvm::raw_ostream::BLUE, true});
    OS << "InlineDescriptor " << (const void *)this << "\n";
  }
  OS << "Offset: " << Offset << "\n";
  OS << "IsConst: " << IsConst << "\n";
  OS << "IsInitialized: " << IsInitialized << "\n";
  OS << "IsBase: " << IsBase << "\n";
  OS << "IsActive: " << IsActive << "\n";
  OS << "IsFieldMutable: " << IsFieldMutable << "\n";
  OS << "Desc: ";
  if (Desc)
    Desc->dump(OS);
  else
    OS << "nullptr";
  OS << "\n";
}

LLVM_DUMP_METHOD void InterpFrame::dump(llvm::raw_ostream &OS,
                                        unsigned Indent) const {
  unsigned Spaces = Indent * 2;
  {
    ColorScope SC(OS, true, {llvm::raw_ostream::BLUE, true});
    OS.indent(Spaces);
    if (getCallee())
      describe(OS);
    else
      OS << "Frame (Depth: " << getDepth() << ")";
    OS << "\n";
  }
  OS.indent(Spaces) << "Function: " << getFunction();
  if (const Function *F = getFunction()) {
    OS << " (" << F->getName() << ")";
  }
  OS << "\n";
  OS.indent(Spaces) << "This: " << getThis() << "\n";
  OS.indent(Spaces) << "RVO: " << getRVOPtr() << "\n";

  while (const InterpFrame *F = this->Caller) {
    F->dump(OS, Indent + 1);
    F = F->Caller;
  }
}

LLVM_DUMP_METHOD void Record::dump(llvm::raw_ostream &OS, unsigned Indentation,
                                   unsigned Offset) const {
  unsigned Indent = Indentation * 2;
  OS.indent(Indent);
  {
    ColorScope SC(OS, true, {llvm::raw_ostream::BLUE, true});
    OS << getName() << "\n";
  }

  unsigned I = 0;
  for (const Record::Base &B : bases()) {
    OS.indent(Indent) << "- Base " << I << ". Offset " << (Offset + B.Offset)
                      << "\n";
    B.R->dump(OS, Indentation + 1, Offset + B.Offset);
    ++I;
  }

  I = 0;
  for (const Record::Field &F : fields()) {
    OS.indent(Indent) << "- Field " << I << ": ";
    {
      ColorScope SC(OS, true, {llvm::raw_ostream::BRIGHT_RED, true});
      OS << F.Decl->getName();
    }
    OS << ". Offset " << (Offset + F.Offset) << "\n";
    ++I;
  }

  I = 0;
  for (const Record::Base &B : virtual_bases()) {
    OS.indent(Indent) << "- Virtual Base " << I << ". Offset "
                      << (Offset + B.Offset) << "\n";
    B.R->dump(OS, Indentation + 1, Offset + B.Offset);
    ++I;
  }
}

LLVM_DUMP_METHOD void Block::dump(llvm::raw_ostream &OS) const {
  {
    ColorScope SC(OS, true, {llvm::raw_ostream::BRIGHT_BLUE, true});
    OS << "Block " << (const void *)this;
  }
  OS << " (";
  Desc->dump(OS);
  OS << ")\n";
  unsigned NPointers = 0;
  for (const Pointer *P = Pointers; P; P = P->Next) {
    ++NPointers;
  }
  OS << "  Pointers: " << NPointers << "\n";
  OS << "  Dead: " << IsDead << "\n";
  OS << "  Static: " << IsStatic << "\n";
  OS << "  Extern: " << IsExtern << "\n";
  OS << "  Initialized: " << IsInitialized << "\n";
}

LLVM_DUMP_METHOD void EvaluationResult::dump() const {
  assert(Ctx);
  auto &OS = llvm::errs();
  const ASTContext &ASTCtx = Ctx->getASTContext();

  switch (Kind) {
  case Empty:
    OS << "Empty\n";
    break;
  case RValue:
    OS << "RValue: ";
    std::get<APValue>(Value).dump(OS, ASTCtx);
    break;
  case LValue: {
    assert(Source);
    QualType SourceType;
    if (const auto *D = Source.dyn_cast<const Decl *>()) {
      if (const auto *VD = dyn_cast<ValueDecl>(D))
        SourceType = VD->getType();
    } else if (const auto *E = Source.dyn_cast<const Expr *>()) {
      SourceType = E->getType();
    }

    OS << "LValue: ";
    if (const auto *P = std::get_if<Pointer>(&Value))
      P->toAPValue(ASTCtx).printPretty(OS, ASTCtx, SourceType);
    else if (const auto *FP = std::get_if<FunctionPointer>(&Value)) // Nope
      FP->toAPValue(ASTCtx).printPretty(OS, ASTCtx, SourceType);
    OS << "\n";
    break;
  }
  case Invalid:
    OS << "Invalid\n";
    break;
  case Valid:
    OS << "Valid\n";
    break;
  }
}
