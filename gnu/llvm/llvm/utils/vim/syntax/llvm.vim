" Vim syntax file
" Language:   llvm
" Maintainer: The LLVM team, http://llvm.org/
" Version:      $Revision$

if version < 600
  syntax clear
elseif exists("b:current_syntax")
  finish
endif

syn case match

" Types.
" Types also include struct, array, vector, etc. but these don't
" benefit as much from having dedicated highlighting rules.
syn keyword llvmType void half bfloat float double x86_fp80 fp128 ppc_fp128
syn keyword llvmType label metadata x86_mmx x86_amx
syn keyword llvmType type label opaque token ptr
syn match   llvmType /\<i\d\+\>/

" Instructions.
" The true and false tokens can be used for comparison opcodes, but it's
" much more common for these tokens to be used for boolean constants.
syn keyword llvmStatement add addrspacecast alloca and arcp ashr atomicrmw
syn keyword llvmStatement bitcast br catchpad catchswitch catchret call callbr
syn keyword llvmStatement cleanuppad cleanupret cmpxchg eq exact extractelement
syn keyword llvmStatement extractvalue fadd fast fcmp fdiv fence fmul fneg fpext
syn keyword llvmStatement fptosi fptoui fptrunc free freeze frem fsub
syn keyword llvmStatement getelementptr icmp inbounds indirectbr insertelement
syn keyword llvmStatement insertvalue inttoptr invoke landingpad load lshr
syn keyword llvmStatement malloc max min mul nand ne ninf nnan nsw nsz nuw oeq
syn keyword llvmStatement oge ogt ole olt one or ord phi ptrtoint resume ret
syn keyword llvmStatement sdiv select sext sge sgt shl shufflevector sitofp
syn keyword llvmStatement sle slt srem store sub switch trunc udiv ueq uge ugt
syn keyword llvmStatement uitofp ule ult umax umin une uno unreachable unwind
syn keyword llvmStatement urem va_arg xchg xor zext

" Debug records.
syn match llvmStatement /\v#dbg_(assign|declare|label|value)/

" Keywords.
syn keyword llvmKeyword
      \ acq_rel
      \ acquire
      \ addrspace
      \ alias
      \ align
      \ alignstack
      \ allocsize
      \ alwaysinline
      \ appending
      \ argmemonly
      \ arm_aapcs_vfpcc
      \ arm_aapcscc
      \ arm_apcscc
      \ asm
      \ atomic
      \ available_externally
      \ blockaddress
      \ builtin
      \ byref
      \ byval
      \ c
      \ caller
      \ catch
      \ cc
      \ ccc
      \ cleanup
      \ cold
      \ coldcc
      \ comdat
      \ common
      \ constant
      \ convergent
      \ datalayout
      \ declare
      \ default
      \ define
      \ deplibs
      \ dereferenceable
      \ dereferenceable_or_null
      \ distinct
      \ dllexport
      \ dllimport
      \ dso_local
      \ dso_preemptable
      \ except
      \ extern_weak
      \ external
      \ externally_initialized
      \ fastcc
      \ filter
      \ from
      \ gc
      \ global
      \ hhvm_ccc
      \ hhvmcc
      \ hidden
      \ hot
      \ immarg
      \ inaccessiblemem_or_argmemonly
      \ inaccessiblememonly
      \ inalloca
      \ initialexec
      \ inlinehint
      \ inreg
      \ intel_ocl_bicc
      \ inteldialect
      \ internal
      \ jumptable
      \ linkonce
      \ linkonce_odr
      \ local_unnamed_addr
      \ localdynamic
      \ localexec
      \ minsize
      \ module
      \ monotonic
      \ msp430_intrcc
      \ mustprogress
      \ musttail
      \ naked
      \ nest
      \ noalias
      \ nobuiltin
      \ nocallback
      \ nocapture
      \ nocf_check
      \ no_cfi
      \ noduplicate
      \ nofree
      \ noimplicitfloat
      \ noinline
      \ nomerge
      \ nonlazybind
      \ nonnull
      \ noprofile
      \ norecurse
      \ noredzone
      \ noreturn
      \ nosync
      \ noundef
      \ nounwind
      \ nosanitize_bounds
      \ nosanitize_coverage
      \ null_pointer_is_valid
      \ optdebug
      \ optforfuzzing
      \ optnone
      \ optsize
      \ personality
      \ preallocated
      \ private
      \ protected
      \ ptrauth
      \ ptx_device
      \ ptx_kernel
      \ readnone
      \ readonly
      \ release
      \ returned
      \ returns_twice
      \ safestack
      \ sanitize_address
      \ sanitize_hwaddress
      \ sanitize_memory
      \ sanitize_memtag
      \ sanitize_thread
      \ section
      \ seq_cst
      \ shadowcallstack
      \ sideeffect
      \ signext
      \ source_filename
      \ speculatable
      \ speculative_load_hardening
      \ spir_func
      \ spir_kernel
      \ splat
      \ sret
      \ ssp
      \ sspreq
      \ sspstrong
      \ strictfp
      \ swiftcc
      \ swifterror
      \ swifttailcc
      \ swiftself
      \ syncscope
      \ tail
      \ tailcc
      \ target
      \ thread_local
      \ to
      \ triple
      \ unnamed_addr
      \ unordered
      \ uselistorder
      \ uselistorder_bb
      \ uwtable
      \ volatile
      \ weak
      \ weak_odr
      \ willreturn
      \ win64cc
      \ within
      \ writeonly
      \ x86_64_sysvcc
      \ x86_fastcallcc
      \ x86_stdcallcc
      \ x86_thiscallcc
      \ zeroext

" Obsolete keywords.
syn keyword llvmError  getresult begin end

" Misc syntax.
syn match   llvmNoName /[%@!]\d\+\>/
syn match   llvmNumber /-\?\<\d\+\>/
syn match   llvmFloat  /-\?\<\d\+\.\d*\(e[+-]\d\+\)\?\>/
syn match   llvmFloat  /\<0x[KLMHR]\?\x\+\>/
syn keyword llvmBoolean true false
syn keyword llvmConstant zeroinitializer undef null none poison vscale
syn match   llvmComment /;.*$/
syn region  llvmString start=/"/ skip=/\\"/ end=/"/
syn match   llvmLabel /[-a-zA-Z$._][-a-zA-Z$._0-9]*:/
syn match   llvmIdentifier /[%@][-a-zA-Z$._][-a-zA-Z$._0-9]*/

" Named metadata and specialized metadata keywords.
syn match   llvmIdentifier /![-a-zA-Z$._][-a-zA-Z$._0-9]*\ze\s*$/
syn match   llvmIdentifier /![-a-zA-Z$._][-a-zA-Z$._0-9]*\ze\s*[=!]/
syn match   llvmType /!\zs\a\+\ze\s*(/
syn match   llvmConstant /\<DW_TAG_[a-z_]\+\>/
syn match   llvmConstant /\<DW_ATE_[a-zA-Z_]\+\>/
syn match   llvmConstant /\<DW_OP_[a-zA-Z0-9_]\+\>/
syn match   llvmConstant /\<DW_LANG_[a-zA-Z0-9_]\+\>/
syn match   llvmConstant /\<DW_VIRTUALITY_[a-z_]\+\>/
syn match   llvmConstant /\<DIFlag[A-Za-z]\+\>/

" Syntax-highlight lit test commands and bug numbers.
syn match  llvmSpecialComment /;\s*PR\d*\s*$/
syn match  llvmSpecialComment /;\s*REQUIRES:.*$/
syn match  llvmSpecialComment /;\s*RUN:.*$/
syn match  llvmSpecialComment /;\s*ALLOW_RETRIES:.*$/
syn match  llvmSpecialComment /;\s*CHECK:.*$/
syn match  llvmSpecialComment /;\s*CHECK-EMPTY:\s*$/
syn match  llvmSpecialComment /\v;\s*CHECK-(NEXT|NOT|DAG|SAME|LABEL|COUNT-\d+):.*$/
syn match  llvmSpecialComment /;\s*XFAIL:.*$/

if version >= 508 || !exists("did_c_syn_inits")
  if version < 508
    let did_c_syn_inits = 1
    command -nargs=+ HiLink hi link <args>
  else
    command -nargs=+ HiLink hi def link <args>
  endif

  HiLink llvmType Type
  HiLink llvmStatement Statement
  HiLink llvmNumber Number
  HiLink llvmComment Comment
  HiLink llvmString String
  HiLink llvmLabel Label
  HiLink llvmKeyword Keyword
  HiLink llvmBoolean Boolean
  HiLink llvmFloat Float
  HiLink llvmNoName Identifier
  HiLink llvmConstant Constant
  HiLink llvmSpecialComment SpecialComment
  HiLink llvmError Error
  HiLink llvmIdentifier Identifier

  delcommand HiLink
endif

let b:current_syntax = "llvm"
