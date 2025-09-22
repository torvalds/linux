" Vim filetype plugin file
" Language: LLVM TableGen
" Maintainer: The LLVM team, http://llvm.org/

if exists("b:did_ftplugin")
  finish
endif
let b:did_ftplugin = 1

setlocal matchpairs+=<:>
setlocal softtabstop=2 shiftwidth=2
setlocal expandtab
