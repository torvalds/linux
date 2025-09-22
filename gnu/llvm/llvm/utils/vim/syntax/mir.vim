" Vim syntax file
" Language:   mir
" Maintainer: The LLVM team, http://llvm.org/
" Version:      $Revision$

if version < 600
  syntax clear
elseif exists("b:current_syntax")
  finish
endif

syn case match

" MIR is embedded in a yaml container, so we load all of the yaml syntax.
runtime! syntax/yaml.vim
unlet b:current_syntax

" The first document of a file is allowed to contain an LLVM IR module inside
" a top-level yaml block string.
syntax include @LLVM syntax/llvm.vim
" FIXME: This should only be allowed for the first document of the file
syntax region llvm start=/\(^---\s*|\)\@<=/ end=/\(^\.\.\.\)\@=/ contains=@LLVM

" The `body:` field of a document contains the MIR dump of the function
syntax include @MIR syntax/machine-ir.vim
syntax region mir start=/\(^body:\s*|\)\@<=/ end=/\(^[^[:space:]]\)\@=/ contains=@MIR

" Syntax-highlight lit test commands and bug numbers.
syn match  mirSpecialComment /#\s*PR\d*\s*$/
syn match  mirSpecialComment /#\s*REQUIRES:.*$/
syn match  mirSpecialComment /#\s*RUN:.*$/
syn match  mirSpecialComment /#\s*ALLOW_RETRIES:.*$/
syn match  mirSpecialComment /#\s*CHECK:.*$/
syn match  mirSpecialComment "\v#\s*CHECK-(NEXT|NOT|DAG|SAME|LABEL):.*$"
syn match  mirSpecialComment /#\s*XFAIL:.*$/

if version >= 508 || !exists("did_c_syn_inits")
  if version < 508
    let did_c_syn_inits = 1
    command -nargs=+ HiLink hi link <args>
  else
    command -nargs=+ HiLink hi def link <args>
  endif

  HiLink mirSpecialComment SpecialComment

  delcommand HiLink
endif

let b:current_syntax = "mir"
