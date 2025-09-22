//===- LLToken.h - Token Codes for LLVM Assembly Files ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the enums for the .ll lexer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ASMPARSER_LLTOKEN_H
#define LLVM_ASMPARSER_LLTOKEN_H

namespace llvm {
namespace lltok {
enum Kind {
  // Markers
  Eof,
  Error,

  // Tokens with no info.
  dotdotdot, // ...
  equal,
  comma, // =  ,
  star,  // *
  lsquare,
  rsquare, // [  ]
  lbrace,
  rbrace, // {  }
  less,
  greater, // <  >
  lparen,
  rparen,  // (  )
  exclaim, // !
  bar,     // |
  colon,   // :
  hash,    // #

  kw_vscale,
  kw_x,
  kw_true,
  kw_false,
  kw_declare,
  kw_define,
  kw_global,
  kw_constant,

  kw_dso_local,
  kw_dso_preemptable,

  kw_private,
  kw_internal,
  kw_linkonce,
  kw_linkonce_odr,
  kw_weak, // Used as a linkage, and a modifier for "cmpxchg".
  kw_weak_odr,
  kw_appending,
  kw_dllimport,
  kw_dllexport,
  kw_common,
  kw_available_externally,
  kw_default,
  kw_hidden,
  kw_protected,
  kw_unnamed_addr,
  kw_local_unnamed_addr,
  kw_externally_initialized,
  kw_extern_weak,
  kw_external,
  kw_thread_local,
  kw_localdynamic,
  kw_initialexec,
  kw_localexec,
  kw_zeroinitializer,
  kw_undef,
  kw_poison,
  kw_null,
  kw_none,
  kw_to,
  kw_caller,
  kw_within,
  kw_from,
  kw_tail,
  kw_musttail,
  kw_notail,
  kw_target,
  kw_triple,
  kw_source_filename,
  kw_unwind,
  kw_datalayout,
  kw_volatile,
  kw_atomic,
  kw_unordered,
  kw_monotonic,
  kw_acquire,
  kw_release,
  kw_acq_rel,
  kw_seq_cst,
  kw_syncscope,
  kw_nnan,
  kw_ninf,
  kw_nsz,
  kw_arcp,
  kw_contract,
  kw_reassoc,
  kw_afn,
  kw_fast,
  kw_nuw,
  kw_nsw,
  kw_nusw,
  kw_exact,
  kw_disjoint,
  kw_inbounds,
  kw_nneg,
  kw_inrange,
  kw_addrspace,
  kw_section,
  kw_partition,
  kw_code_model,
  kw_alias,
  kw_ifunc,
  kw_module,
  kw_asm,
  kw_sideeffect,
  kw_inteldialect,
  kw_gc,
  kw_prefix,
  kw_prologue,
  kw_c,

  kw_cc,
  kw_ccc,
  kw_fastcc,
  kw_coldcc,
  kw_intel_ocl_bicc,
  kw_cfguard_checkcc,
  kw_x86_stdcallcc,
  kw_x86_fastcallcc,
  kw_x86_thiscallcc,
  kw_x86_vectorcallcc,
  kw_x86_regcallcc,
  kw_arm_apcscc,
  kw_arm_aapcscc,
  kw_arm_aapcs_vfpcc,
  kw_aarch64_vector_pcs,
  kw_aarch64_sve_vector_pcs,
  kw_aarch64_sme_preservemost_from_x0,
  kw_aarch64_sme_preservemost_from_x1,
  kw_aarch64_sme_preservemost_from_x2,
  kw_msp430_intrcc,
  kw_avr_intrcc,
  kw_avr_signalcc,
  kw_ptx_kernel,
  kw_ptx_device,
  kw_spir_kernel,
  kw_spir_func,
  kw_x86_64_sysvcc,
  kw_win64cc,
  kw_anyregcc,
  kw_swiftcc,
  kw_swifttailcc,
  kw_preserve_mostcc,
  kw_preserve_allcc,
  kw_preserve_nonecc,
  kw_ghccc,
  kw_x86_intrcc,
  kw_hhvmcc,
  kw_hhvm_ccc,
  kw_cxx_fast_tlscc,
  kw_amdgpu_vs,
  kw_amdgpu_ls,
  kw_amdgpu_hs,
  kw_amdgpu_es,
  kw_amdgpu_gs,
  kw_amdgpu_ps,
  kw_amdgpu_cs,
  kw_amdgpu_cs_chain,
  kw_amdgpu_cs_chain_preserve,
  kw_amdgpu_kernel,
  kw_amdgpu_gfx,
  kw_tailcc,
  kw_m68k_rtdcc,
  kw_graalcc,
  kw_riscv_vector_cc,

  // Attributes:
  kw_attributes,
  kw_sync,
  kw_async,
#define GET_ATTR_NAMES
#define ATTRIBUTE_ENUM(ENUM_NAME, DISPLAY_NAME) \
  kw_##DISPLAY_NAME,
#include "llvm/IR/Attributes.inc"

  // Memory attribute:
  kw_read,
  kw_write,
  kw_readwrite,
  kw_argmem,
  kw_inaccessiblemem,

  // Legacy memory attributes:
  kw_argmemonly,
  kw_inaccessiblememonly,
  kw_inaccessiblemem_or_argmemonly,

  // nofpclass attribute:
  kw_all,
  kw_nan,
  kw_snan,
  kw_qnan,
  kw_inf,
  // kw_ninf, - already an fmf
  kw_pinf,
  kw_norm,
  kw_nnorm,
  kw_pnorm,
  // kw_sub,  - already an instruction
  kw_nsub,
  kw_psub,
  kw_zero,
  kw_nzero,
  kw_pzero,

  kw_type,
  kw_opaque,

  kw_comdat,

  // Comdat types
  kw_any,
  kw_exactmatch,
  kw_largest,
  kw_nodeduplicate,
  kw_samesize,

  kw_eq,
  kw_ne,
  kw_slt,
  kw_sgt,
  kw_sle,
  kw_sge,
  kw_ult,
  kw_ugt,
  kw_ule,
  kw_uge,
  kw_oeq,
  kw_one,
  kw_olt,
  kw_ogt,
  kw_ole,
  kw_oge,
  kw_ord,
  kw_uno,
  kw_ueq,
  kw_une,

  // atomicrmw operations that aren't also instruction keywords.
  kw_xchg,
  kw_nand,
  kw_max,
  kw_min,
  kw_umax,
  kw_umin,
  kw_fmax,
  kw_fmin,
  kw_uinc_wrap,
  kw_udec_wrap,

  // Instruction Opcodes (Opcode in UIntVal).
  kw_fneg,
  kw_add,
  kw_fadd,
  kw_sub,
  kw_fsub,
  kw_mul,
  kw_fmul,
  kw_udiv,
  kw_sdiv,
  kw_fdiv,
  kw_urem,
  kw_srem,
  kw_frem,
  kw_shl,
  kw_lshr,
  kw_ashr,
  kw_and,
  kw_or,
  kw_xor,
  kw_icmp,
  kw_fcmp,

  kw_phi,
  kw_call,
  kw_trunc,
  kw_zext,
  kw_sext,
  kw_fptrunc,
  kw_fpext,
  kw_uitofp,
  kw_sitofp,
  kw_fptoui,
  kw_fptosi,
  kw_inttoptr,
  kw_ptrtoint,
  kw_bitcast,
  kw_addrspacecast,
  kw_select,
  kw_va_arg,

  kw_landingpad,
  kw_personality,
  kw_cleanup,
  kw_catch,
  kw_filter,

  kw_ret,
  kw_br,
  kw_switch,
  kw_indirectbr,
  kw_invoke,
  kw_resume,
  kw_unreachable,
  kw_cleanupret,
  kw_catchswitch,
  kw_catchret,
  kw_catchpad,
  kw_cleanuppad,
  kw_callbr,

  kw_alloca,
  kw_load,
  kw_store,
  kw_fence,
  kw_cmpxchg,
  kw_atomicrmw,
  kw_getelementptr,

  kw_extractelement,
  kw_insertelement,
  kw_shufflevector,
  kw_splat,
  kw_extractvalue,
  kw_insertvalue,
  kw_blockaddress,
  kw_dso_local_equivalent,
  kw_no_cfi,
  kw_ptrauth,

  kw_freeze,

  // Metadata types.
  kw_distinct,

  // Use-list order directives.
  kw_uselistorder,
  kw_uselistorder_bb,

  // Summary index keywords
  kw_path,
  kw_hash,
  kw_gv,
  kw_guid,
  kw_name,
  kw_summaries,
  kw_flags,
  kw_blockcount,
  kw_linkage,
  kw_visibility,
  kw_notEligibleToImport,
  kw_live,
  kw_dsoLocal,
  kw_canAutoHide,
  kw_importType,
  kw_definition,
  kw_declaration,
  kw_function,
  kw_insts,
  kw_funcFlags,
  kw_readNone,
  kw_readOnly,
  kw_noRecurse,
  kw_returnDoesNotAlias,
  kw_noInline,
  kw_alwaysInline,
  kw_noUnwind,
  kw_mayThrow,
  kw_hasUnknownCall,
  kw_mustBeUnreachable,
  kw_calls,
  kw_callee,
  kw_params,
  kw_param,
  kw_hotness,
  kw_unknown,
  kw_critical,
  kw_relbf,
  kw_variable,
  kw_vTableFuncs,
  kw_virtFunc,
  kw_aliasee,
  kw_refs,
  kw_typeIdInfo,
  kw_typeTests,
  kw_typeTestAssumeVCalls,
  kw_typeCheckedLoadVCalls,
  kw_typeTestAssumeConstVCalls,
  kw_typeCheckedLoadConstVCalls,
  kw_vFuncId,
  kw_offset,
  kw_args,
  kw_typeid,
  kw_typeidCompatibleVTable,
  kw_summary,
  kw_typeTestRes,
  kw_kind,
  kw_unsat,
  kw_byteArray,
  kw_inline,
  kw_single,
  kw_allOnes,
  kw_sizeM1BitWidth,
  kw_alignLog2,
  kw_sizeM1,
  kw_bitMask,
  kw_inlineBits,
  kw_vcall_visibility,
  kw_wpdResolutions,
  kw_wpdRes,
  kw_indir,
  kw_singleImpl,
  kw_branchFunnel,
  kw_singleImplName,
  kw_resByArg,
  kw_byArg,
  kw_uniformRetVal,
  kw_uniqueRetVal,
  kw_virtualConstProp,
  kw_info,
  kw_byte,
  kw_bit,
  kw_varFlags,
  // The following are used by MemProf summary info.
  kw_callsites,
  kw_clones,
  kw_stackIds,
  kw_allocs,
  kw_versions,
  kw_memProf,
  kw_notcold,

  // GV's with __attribute__((no_sanitize("address"))), or things in
  // -fsanitize-ignorelist when built with ASan.
  kw_no_sanitize_address,
  // GV's with __attribute__((no_sanitize("hwaddress"))), or things in
  // -fsanitize-ignorelist when built with HWASan.
  kw_no_sanitize_hwaddress,
  // GV's where the clang++ frontend (when ASan is used) notes that this is
  // dynamically initialized, and thus needs ODR detection.
  kw_sanitize_address_dyninit,

  // Unsigned Valued tokens (UIntVal).
  LabelID,    // 42:
  GlobalID,   // @42
  LocalVarID, // %42
  AttrGrpID,  // #42
  SummaryID,  // ^42

  // String valued tokens (StrVal).
  LabelStr,         // foo:
  GlobalVar,        // @foo @"foo"
  ComdatVar,        // $foo
  LocalVar,         // %foo %"foo"
  MetadataVar,      // !foo
  StringConstant,   // "foo"
  DwarfTag,         // DW_TAG_foo
  DwarfAttEncoding, // DW_ATE_foo
  DwarfVirtuality,  // DW_VIRTUALITY_foo
  DwarfLang,        // DW_LANG_foo
  DwarfCC,          // DW_CC_foo
  EmissionKind,     // lineTablesOnly
  NameTableKind,    // GNU
  DwarfOp,          // DW_OP_foo
  DIFlag,           // DIFlagFoo
  DISPFlag,         // DISPFlagFoo
  DwarfMacinfo,     // DW_MACINFO_foo
  ChecksumKind,     // CSK_foo
  DbgRecordType,    // dbg_foo

  // Type valued tokens (TyVal).
  Type,

  APFloat, // APFloatVal
  APSInt   // APSInt
};
} // end namespace lltok
} // end namespace llvm

#endif
