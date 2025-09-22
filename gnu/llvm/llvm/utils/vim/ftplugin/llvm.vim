" Vim filetype plugin file
" Language: LLVM Assembly
" Maintainer: The LLVM team, http://llvm.org/

if exists("b:did_ftplugin")
  finish
endif
let b:did_ftplugin = 1

setlocal softtabstop=2 shiftwidth=2
setlocal expandtab
setlocal comments+=:;
setlocal commentstring=;\ %s
