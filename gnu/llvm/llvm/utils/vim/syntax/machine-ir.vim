" Vim syntax file
" Language:   mir
" Maintainer: The LLVM team, http://llvm.org/
" Version:      $Revision$

syn case match

" FIXME: MIR doesn't actually match LLVM IR. Stop including it all as a
" fallback once enough is implemented.
" See the MIR LangRef: https://llvm.org/docs/MIRLangRef.html
unlet b:current_syntax  " Unlet so that the LLVM syntax will load
runtime! syntax/llvm.vim
unlet b:current_syntax

syn match   mirType /\<[sp]\d\+\>/

" Opcodes. Matching instead of listing them because individual targets can add
" these. FIXME: Maybe use some more context to make this more accurate?
syn match   mirStatement /\<[A-Z][A-Za-z0-9_]*\>/

syn match   mirPReg /$[-a-zA-Z$._][-a-zA-Z$._0-9]*/

if version >= 508 || !exists("did_c_syn_inits")
  if version < 508
    let did_c_syn_inits = 1
    command -nargs=+ HiLink hi link <args>
  else
    command -nargs=+ HiLink hi def link <args>
  endif

  HiLink mirType Type
  HiLink mirStatement Statement
  HiLink mirPReg Identifier

  delcommand HiLink
endif

let b:current_syntax = "mir"
