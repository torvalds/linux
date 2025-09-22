" Vim indent file
" Language:   llvm
" Maintainer: The LLVM team, http://llvm.org/
" What this indent plugin currently does:
"  - If no other rule matches copy indent from previous non-empty,
"    non-commented line
"  - On '}' align the same as the line containing the matching '{'
"  - If previous line ends with ':' increase indentation
"  - If the current line ends with ':' indent at the same level as the
"    enclosing '{'/'}' block
" Stuff that would be nice to add:
"  - Continue comments on next line
"  - If there is an opening+unclosed parenthesis on previous line indent to that
if exists("b:did_indent")
  finish
endif
let b:did_indent = 1

setlocal shiftwidth=2 expandtab

setlocal indentkeys=0{,0},<:>,!^F,o,O,e
setlocal indentexpr=GetLLVMIndent()

if exists("*GetLLVMIndent")
  finish
endif

function! FindOpenBrace(lnum)
  call cursor(a:lnum, 1)
  return searchpair('{', '', '}', 'bW')
endfun

function! GetLLVMIndent()
  " On '}' align the same as the line containing the matching '{'
  let thisline = getline(v:lnum)
  if thisline =~ '^\s*}'
    call cursor(v:lnum, 1)
    silent normal %
    let opening_lnum = line('.')
    if opening_lnum != v:lnum
      return indent(opening_lnum)
    endif
  endif

  " Indent labels the same as the current opening block
  if thisline =~ ':\s*$'
    let blockbegin = FindOpenBrace(v:lnum)
    if blockbegin > 0
      return indent(blockbegin)
    endif
  endif

  " Find a non-blank not-completely commented line above the current line.
  let prev_lnum = prevnonblank(v:lnum - 1)
  while prev_lnum > 0 && synIDattr(synID(prev_lnum, indent(prev_lnum)+1, 0), "name") =? "string\|comment"
    let prev_lnum = prevnonblank(prev_lnum-1)
  endwhile
  " Hit the start of the file, use zero indent.
  if prev_lnum == 0
    return 0
  endif

  let ind = indent(prev_lnum)
  let prevline = getline(prev_lnum)

  " Add a 'shiftwidth' after lines that start a block or labels
  if prevline =~ '{\s*$' || prevline =~ ':\s*$'
    let ind = ind + &shiftwidth
  endif

  return ind
endfunction
