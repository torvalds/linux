//===- MILexer.h - Lexer for machine instructions ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the function that lexes the machine instruction source
// string.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_MIRPARSER_MILEXER_H
#define LLVM_LIB_CODEGEN_MIRPARSER_MILEXER_H

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace llvm {

class Twine;

/// A token produced by the machine instruction lexer.
struct MIToken {
  enum TokenKind {
    // Markers
    Eof,
    Error,
    Newline,

    // Tokens with no info.
    comma,
    equal,
    underscore,
    colon,
    coloncolon,
    dot,
    exclaim,
    lparen,
    rparen,
    lbrace,
    rbrace,
    plus,
    minus,
    less,
    greater,

    // Keywords
    kw_implicit,
    kw_implicit_define,
    kw_def,
    kw_dead,
    kw_dereferenceable,
    kw_killed,
    kw_undef,
    kw_internal,
    kw_early_clobber,
    kw_debug_use,
    kw_renamable,
    kw_tied_def,
    kw_frame_setup,
    kw_frame_destroy,
    kw_nnan,
    kw_ninf,
    kw_nsz,
    kw_arcp,
    kw_contract,
    kw_afn,
    kw_reassoc,
    kw_nusw,
    kw_nuw,
    kw_nsw,
    kw_exact,
    kw_nofpexcept,
    kw_unpredictable,
    kw_nneg,
    kw_disjoint,
    kw_debug_location,
    kw_debug_instr_number,
    kw_dbg_instr_ref,
    kw_cfi_same_value,
    kw_cfi_offset,
    kw_cfi_rel_offset,
    kw_cfi_def_cfa_register,
    kw_cfi_def_cfa_offset,
    kw_cfi_adjust_cfa_offset,
    kw_cfi_escape,
    kw_cfi_def_cfa,
    kw_cfi_llvm_def_aspace_cfa,
    kw_cfi_register,
    kw_cfi_remember_state,
    kw_cfi_restore,
    kw_cfi_restore_state,
    kw_cfi_undefined,
    kw_cfi_window_save,
    kw_cfi_aarch64_negate_ra_sign_state,
    kw_blockaddress,
    kw_intrinsic,
    kw_target_index,
    kw_half,
    kw_bfloat,
    kw_float,
    kw_double,
    kw_x86_fp80,
    kw_fp128,
    kw_ppc_fp128,
    kw_target_flags,
    kw_volatile,
    kw_non_temporal,
    kw_invariant,
    kw_align,
    kw_basealign,
    kw_addrspace,
    kw_stack,
    kw_got,
    kw_jump_table,
    kw_constant_pool,
    kw_call_entry,
    kw_custom,
    kw_liveout,
    kw_landing_pad,
    kw_inlineasm_br_indirect_target,
    kw_ehfunclet_entry,
    kw_liveins,
    kw_successors,
    kw_floatpred,
    kw_intpred,
    kw_shufflemask,
    kw_pre_instr_symbol,
    kw_post_instr_symbol,
    kw_heap_alloc_marker,
    kw_pcsections,
    kw_cfi_type,
    kw_bbsections,
    kw_bb_id,
    kw_unknown_size,
    kw_unknown_address,
    kw_ir_block_address_taken,
    kw_machine_block_address_taken,
    kw_call_frame_size,
    kw_noconvergent,

    // Metadata types.
    kw_distinct,

    // Named metadata keywords
    md_tbaa,
    md_alias_scope,
    md_noalias,
    md_range,
    md_diexpr,
    md_dilocation,

    // Identifier tokens
    Identifier,
    NamedRegister,
    NamedVirtualRegister,
    MachineBasicBlockLabel,
    MachineBasicBlock,
    StackObject,
    FixedStackObject,
    NamedGlobalValue,
    GlobalValue,
    ExternalSymbol,
    MCSymbol,

    // Other tokens
    IntegerLiteral,
    FloatingPointLiteral,
    HexLiteral,
    VectorLiteral,
    VirtualRegister,
    ConstantPoolItem,
    JumpTableIndex,
    NamedIRBlock,
    IRBlock,
    NamedIRValue,
    IRValue,
    QuotedIRValue, // `<constant value>`
    SubRegisterIndex,
    StringConstant
  };

private:
  TokenKind Kind = Error;
  StringRef Range;
  StringRef StringValue;
  std::string StringValueStorage;
  APSInt IntVal;

public:
  MIToken() = default;

  MIToken &reset(TokenKind Kind, StringRef Range);

  MIToken &setStringValue(StringRef StrVal);
  MIToken &setOwnedStringValue(std::string StrVal);
  MIToken &setIntegerValue(APSInt IntVal);

  TokenKind kind() const { return Kind; }

  bool isError() const { return Kind == Error; }

  bool isNewlineOrEOF() const { return Kind == Newline || Kind == Eof; }

  bool isErrorOrEOF() const { return Kind == Error || Kind == Eof; }

  bool isRegister() const {
    return Kind == NamedRegister || Kind == underscore ||
           Kind == NamedVirtualRegister || Kind == VirtualRegister;
  }

  bool isRegisterFlag() const {
    return Kind == kw_implicit || Kind == kw_implicit_define ||
           Kind == kw_def || Kind == kw_dead || Kind == kw_killed ||
           Kind == kw_undef || Kind == kw_internal ||
           Kind == kw_early_clobber || Kind == kw_debug_use ||
           Kind == kw_renamable;
  }

  bool isMemoryOperandFlag() const {
    return Kind == kw_volatile || Kind == kw_non_temporal ||
           Kind == kw_dereferenceable || Kind == kw_invariant ||
           Kind == StringConstant;
  }

  bool is(TokenKind K) const { return Kind == K; }

  bool isNot(TokenKind K) const { return Kind != K; }

  StringRef::iterator location() const { return Range.begin(); }

  StringRef range() const { return Range; }

  /// Return the token's string value.
  StringRef stringValue() const { return StringValue; }

  const APSInt &integerValue() const { return IntVal; }

  bool hasIntegerValue() const {
    return Kind == IntegerLiteral || Kind == MachineBasicBlock ||
           Kind == MachineBasicBlockLabel || Kind == StackObject ||
           Kind == FixedStackObject || Kind == GlobalValue ||
           Kind == VirtualRegister || Kind == ConstantPoolItem ||
           Kind == JumpTableIndex || Kind == IRBlock || Kind == IRValue;
  }
};

/// Consume a single machine instruction token in the given source and return
/// the remaining source string.
StringRef lexMIToken(
    StringRef Source, MIToken &Token,
    function_ref<void(StringRef::iterator, const Twine &)> ErrorCallback);

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_MIRPARSER_MILEXER_H
