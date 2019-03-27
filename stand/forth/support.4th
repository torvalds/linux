\ Copyright (c) 1999 Daniel C. Sobral <dcs@FreeBSD.org>
\ All rights reserved.
\ 
\ Redistribution and use in source and binary forms, with or without
\ modification, are permitted provided that the following conditions
\ are met:
\ 1. Redistributions of source code must retain the above copyright
\    notice, this list of conditions and the following disclaimer.
\ 2. Redistributions in binary form must reproduce the above copyright
\    notice, this list of conditions and the following disclaimer in the
\    documentation and/or other materials provided with the distribution.
\
\ THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
\ ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
\ IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
\ ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
\ FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
\ DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
\ OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
\ HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
\ LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
\ OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
\ SUCH DAMAGE.
\
\ $FreeBSD$

\ Loader.rc support functions:
\
\ initialize ( addr len -- )	as above, plus load_conf_files
\ load_conf ( addr len -- )	load conf file given
\ include_conf_files ( -- )	load all conf files in load_conf_files
\ print_syntax_error ( -- )	print line and marker of where a syntax
\				error was detected
\ print_line ( -- )		print last line processed
\ load_kernel ( -- )		load kernel
\ load_modules ( -- )		load modules flagged
\
\ Exported structures:
\
\ string			counted string structure
\	cell .addr			string address
\	cell .len			string length
\ module			module loading information structure
\	cell module.flag		should we load it?
\	string module.name		module's name
\	string module.loadname		name to be used in loading the module
\	string module.type		module's type
\	string module.args		flags to be passed during load
\	string module.beforeload	command to be executed before load
\	string module.afterload		command to be executed after load
\	string module.loaderror		command to be executed if load fails
\	cell module.next		list chain
\
\ Exported global variables;
\
\ string conf_files		configuration files to be loaded
\ cell modules_options		pointer to first module information
\ value verbose?		indicates if user wants a verbose loading
\ value any_conf_read?		indicates if a conf file was successfully read
\
\ Other exported words:
\    note, strlen is internal
\ strdup ( addr len -- addr' len)			similar to strdup(3)
\ strcat ( addr len addr' len' -- addr len+len' )	similar to strcat(3)
\ s' ( | string' -- addr len | )			similar to s"
\ rudimentary structure support

\ Exception values

1 constant ESYNTAX
2 constant ENOMEM
3 constant EFREE
4 constant ESETERROR	\ error setting environment variable
5 constant EREAD	\ error reading
6 constant EOPEN
7 constant EEXEC	\ XXX never catched
8 constant EBEFORELOAD
9 constant EAFTERLOAD

\ I/O constants

0 constant SEEK_SET
1 constant SEEK_CUR
2 constant SEEK_END

0 constant O_RDONLY
1 constant O_WRONLY
2 constant O_RDWR

\ Crude structure support

: structure:
  create here 0 , ['] drop , 0
  does> create here swap dup @ allot cell+ @ execute
;
: member: create dup , over , + does> cell+ @ + ;
: ;structure swap ! ;
: constructor! >body cell+ ! ;
: constructor: over :noname ;
: ;constructor postpone ; swap cell+ ! ; immediate
: sizeof ' >body @ state @ if postpone literal then ; immediate
: offsetof ' >body cell+ @ state @ if postpone literal then ; immediate
: ptr 1 cells member: ;
: int 1 cells member: ;

\ String structure

structure: string
	ptr .addr
	int .len
	constructor:
	  0 over .addr !
	  0 swap .len !
	;constructor
;structure


\ Module options linked list

structure: module
	int module.flag
	sizeof string member: module.name
	sizeof string member: module.loadname
	sizeof string member: module.type
	sizeof string member: module.args
	sizeof string member: module.beforeload
	sizeof string member: module.afterload
	sizeof string member: module.loaderror
	ptr module.next
;structure

\ Internal loader structures (preloaded_file, kernel_module, file_metadata)
\ must be in sync with the C struct in stand/common/bootstrap.h
structure: preloaded_file
	ptr pf.name
	ptr pf.type
	ptr pf.args
	ptr pf.metadata	\ file_metadata
	int pf.loader
	int pf.addr
	int pf.size
	ptr pf.modules	\ kernel_module
	ptr pf.next	\ preloaded_file
;structure

structure: kernel_module
	ptr km.name
	\ ptr km.args
	ptr km.fp	\ preloaded_file
	ptr km.next	\ kernel_module
;structure

structure: file_metadata
	int		md.size
	2 member:	md.type	\ this is not ANS Forth compatible (XXX)
	ptr		md.next	\ file_metadata
	0 member:	md.data	\ variable size
;structure

\ end of structures

\ Global variables

string conf_files
string nextboot_conf_file
create module_options sizeof module.next allot 0 module_options !
create last_module_option sizeof module.next allot 0 last_module_option !
0 value verbose?
0 value nextboot?

\ Support string functions
: strdup { addr len -- addr' len' }
  len allocate if ENOMEM throw then
  addr over len move len
;

: strcat  { addr len addr' len' -- addr len+len' }
  addr' addr len + len' move
  addr len len' +
;

: strchr { addr len c -- addr' len' }
  begin
    len
  while
    addr c@ c = if addr len exit then
    addr 1 + to addr
    len 1 - to len
  repeat
  0 0
;

: s' \ same as s", allows " in the string
  [char] ' parse
  state @ if postpone sliteral then
; immediate

: 2>r postpone >r postpone >r ; immediate
: 2r> postpone r> postpone r> ; immediate
: 2r@ postpone 2r> postpone 2dup postpone 2>r ; immediate

: getenv?  getenv -1 = if false else drop true then ;

\ determine if a word appears in a string, case-insensitive
: contains? ( addr1 len1 addr2 len2 -- 0 | -1 )
	2 pick 0= if 2drop 2drop true exit then
	dup 0= if 2drop 2drop false exit then
	begin
		begin
			swap dup c@ dup 32 = over 9 = or over 10 = or
			over 13 = or over 44 = or swap drop
		while 1+ swap 1- repeat
		swap 2 pick 1- over <
	while
		2over 2over drop over compare-insensitive 0= if
			2 pick over = if 2drop 2drop true exit then
			2 pick tuck - -rot + swap over c@ dup 32 =
			over 9 = or over 10 = or over 13 = or over 44 = or
			swap drop if 2drop 2drop true exit then
		then begin
			swap dup c@ dup 32 = over 9 = or over 10 = or
			over 13 = or over 44 = or swap drop
			if false else true then 2 pick 0> and
		while 1+ swap 1- repeat
		swap
	repeat
	2drop 2drop false
;

: boot_serial? ( -- 0 | -1 )
	s" console" getenv dup -1 <> if
		s" comconsole" 2swap contains?
	else drop false then
	s" boot_serial" getenv dup -1 <> if
		swap drop 0>
	else drop false then
	or \ console contains comconsole ( or ) boot_serial
	s" boot_multicons" getenv dup -1 <> if
		swap drop 0>
	else drop false then
	or \ previous boolean ( or ) boot_multicons
;

\ Private definitions

vocabulary support-functions
only forth also support-functions definitions

\ Some control characters constants

7 constant bell
8 constant backspace
9 constant tab
10 constant lf
13 constant <cr>

\ Read buffer size

80 constant read_buffer_size

\ Standard suffixes

: load_module_suffix		s" _load" ;
: module_loadname_suffix	s" _name" ;
: module_type_suffix		s" _type" ;
: module_args_suffix		s" _flags" ;
: module_beforeload_suffix	s" _before" ;
: module_afterload_suffix	s" _after" ;
: module_loaderror_suffix	s" _error" ;

\ Support operators

: >= < 0= ;
: <= > 0= ;

\ Assorted support functions

: free-memory free if EFREE throw then ;

: strget { var -- addr len } var .addr @ var .len @ ;

\ assign addr len to variable.
: strset  { addr len var -- } addr var .addr !  len var .len !  ;

\ free memory and reset fields
: strfree { var -- } var .addr @ ?dup if free-memory 0 0 var strset then ;

\ free old content, make a copy of the string and assign to variable
: string= { addr len var -- } var strfree addr len strdup var strset ;

: strtype ( str -- ) strget type ;

\ assign a reference to what is on the stack
: strref { addr len var -- addr len }
  addr var .addr ! len var .len ! addr len
;

\ unquote a string
: unquote ( addr len -- addr len )
  over c@ [char] " = if 2 chars - swap char+ swap then
;

\ Assignment data temporary storage

string name_buffer
string value_buffer

\ Line by line file reading functions
\
\ exported:
\	line_buffer
\	end_of_file?
\	fd
\	read_line
\	reset_line_reading

vocabulary line-reading
also line-reading definitions

\ File data temporary storage

string read_buffer
0 value read_buffer_ptr

\ File's line reading function

get-current ( -- wid ) previous definitions

string line_buffer
0 value end_of_file?
variable fd

>search ( wid -- ) definitions

: skip_newlines
  begin
    read_buffer .len @ read_buffer_ptr >
  while
    read_buffer .addr @ read_buffer_ptr + c@ lf = if
      read_buffer_ptr char+ to read_buffer_ptr
    else
      exit
    then
  repeat
;

: scan_buffer  ( -- addr len )
  read_buffer_ptr >r
  begin
    read_buffer .len @ r@ >
  while
    read_buffer .addr @ r@ + c@ lf = if
      read_buffer .addr @ read_buffer_ptr +  ( -- addr )
      r@ read_buffer_ptr -                   ( -- len )
      r> to read_buffer_ptr
      exit
    then
    r> char+ >r
  repeat
  read_buffer .addr @ read_buffer_ptr +  ( -- addr )
  r@ read_buffer_ptr -                   ( -- len )
  r> to read_buffer_ptr
;

: line_buffer_resize  ( len -- len )
  >r
  line_buffer .len @ if
    line_buffer .addr @
    line_buffer .len @ r@ +
    resize if ENOMEM throw then
  else
    r@ allocate if ENOMEM throw then
  then
  line_buffer .addr !
  r>
;
    
: append_to_line_buffer  ( addr len -- )
  line_buffer strget
  2swap strcat
  line_buffer .len !
  drop
;

: read_from_buffer
  scan_buffer            ( -- addr len )
  line_buffer_resize     ( len -- len )
  append_to_line_buffer  ( addr len -- )
;

: refill_required?
  read_buffer .len @ read_buffer_ptr =
  end_of_file? 0= and
;

: refill_buffer
  0 to read_buffer_ptr
  read_buffer .addr @ 0= if
    read_buffer_size allocate if ENOMEM throw then
    read_buffer .addr !
  then
  fd @ read_buffer .addr @ read_buffer_size fread
  dup -1 = if EREAD throw then
  dup 0= if true to end_of_file? then
  read_buffer .len !
;

get-current ( -- wid ) previous definitions >search ( wid -- )

: reset_line_reading
  0 to read_buffer_ptr
;

: read_line
  line_buffer strfree
  skip_newlines
  begin
    read_from_buffer
    refill_required?
  while
    refill_buffer
  repeat
;

only forth also support-functions definitions

\ Conf file line parser:
\ <line> ::= <spaces><name><spaces>'='<spaces><value><spaces>[<comment>] |
\            <spaces>[<comment>]
\ <name> ::= <letter>{<letter>|<digit>|'_'}
\ <value> ::= '"'{<character_set>|'\'<anything>}'"' | <name>
\ <character_set> ::= ASCII 32 to 126, except '\' and '"'
\ <comment> ::= '#'{<anything>}
\
\ exported:
\	line_pointer
\	process_conf

0 value line_pointer

vocabulary file-processing
also file-processing definitions

\ parser functions
\
\ exported:
\	get_assignment

vocabulary parser
also parser definitions

0 value parsing_function
0 value end_of_line

: end_of_line?  line_pointer end_of_line = ;

\ classifiers for various character classes in the input line

: letter?
  line_pointer c@ >r
  r@ [char] A >=
  r@ [char] Z <= and
  r@ [char] a >=
  r> [char] z <= and
  or
;

: digit?
  line_pointer c@ >r
  r@ [char] - =
  r@ [char] 0 >=
  r> [char] 9 <= and
  or
;

: quote?  line_pointer c@ [char] " = ;

: assignment_sign?  line_pointer c@ [char] = = ;

: comment?  line_pointer c@ [char] # = ;

: space?  line_pointer c@ bl = line_pointer c@ tab = or ;

: backslash?  line_pointer c@ [char] \ = ;

: underscore?  line_pointer c@ [char] _ = ;

: dot?  line_pointer c@ [char] . = ;

\ manipulation of input line
: skip_character line_pointer char+ to line_pointer ;

: skip_to_end_of_line end_of_line to line_pointer ;

: eat_space
  begin
    end_of_line? if 0 else space? then
  while
    skip_character
  repeat
;

: parse_name  ( -- addr len )
  line_pointer
  begin
    end_of_line? if 0 else letter? digit? underscore? dot? or or or then
  while
    skip_character
  repeat
  line_pointer over -
  strdup
;

: remove_backslashes  { addr len | addr' len' -- addr' len' }
  len allocate if ENOMEM throw then
  to addr'
  addr >r
  begin
    addr c@ [char] \ <> if
      addr c@ addr' len' + c!
      len' char+ to len'
    then
    addr char+ to addr
    r@ len + addr =
  until
  r> drop
  addr' len'
;

: parse_quote  ( -- addr len )
  line_pointer
  skip_character
  end_of_line? if ESYNTAX throw then
  begin
    quote? 0=
  while
    backslash? if
      skip_character
      end_of_line? if ESYNTAX throw then
    then
    skip_character
    end_of_line? if ESYNTAX throw then 
  repeat
  skip_character
  line_pointer over -
  remove_backslashes
;

: read_name
  parse_name		( -- addr len )
  name_buffer strset
;

: read_value
  quote? if
    parse_quote		( -- addr len )
  else
    parse_name		( -- addr len )
  then
  value_buffer strset
;

: comment
  skip_to_end_of_line
;

: white_space_4
  eat_space
  comment? if ['] comment to parsing_function exit then
  end_of_line? 0= if ESYNTAX throw then
;

: variable_value
  read_value
  ['] white_space_4 to parsing_function
;

: white_space_3
  eat_space
  letter? digit? quote? or or if
    ['] variable_value to parsing_function exit
  then
  ESYNTAX throw
;

: assignment_sign
  skip_character
  ['] white_space_3 to parsing_function
;

: white_space_2
  eat_space
  assignment_sign? if ['] assignment_sign to parsing_function exit then
  ESYNTAX throw
;

: variable_name
  read_name
  ['] white_space_2 to parsing_function
;

: white_space_1
  eat_space
  letter?  if ['] variable_name to parsing_function exit then
  comment? if ['] comment to parsing_function exit then
  end_of_line? 0= if ESYNTAX throw then
;

get-current ( -- wid ) previous definitions >search ( wid -- )

: get_assignment
  line_buffer strget + to end_of_line
  line_buffer .addr @ to line_pointer
  ['] white_space_1 to parsing_function
  begin
    end_of_line? 0=
  while
    parsing_function execute
  repeat
  parsing_function ['] comment =
  parsing_function ['] white_space_1 =
  parsing_function ['] white_space_4 =
  or or 0= if ESYNTAX throw then
;

only forth also support-functions also file-processing definitions

\ Process line

: assignment_type?  ( addr len -- flag )
  name_buffer strget
  compare 0=
;

: suffix_type?  ( addr len -- flag )
  name_buffer .len @ over <= if 2drop false exit then
  name_buffer .len @ over - name_buffer .addr @ +
  over compare 0=
;

: loader_conf_files?  s" loader_conf_files" assignment_type?  ;

: nextboot_flag?  s" nextboot_enable" assignment_type?  ;

: nextboot_conf? s" nextboot_conf" assignment_type?  ;

: verbose_flag? s" verbose_loading" assignment_type?  ;

: execute? s" exec" assignment_type?  ;

: module_load? load_module_suffix suffix_type? ;

: module_loadname?  module_loadname_suffix suffix_type?  ;

: module_type?  module_type_suffix suffix_type?  ;

: module_args?  module_args_suffix suffix_type?  ;

: module_beforeload?  module_beforeload_suffix suffix_type?  ;

: module_afterload?  module_afterload_suffix suffix_type?  ;

: module_loaderror?  module_loaderror_suffix suffix_type?  ;

\ build a 'set' statement and execute it
: set_environment_variable
  name_buffer .len @ value_buffer .len @ + 5 chars + \ size of result string
  allocate if ENOMEM throw then
  dup 0  \ start with an empty string and append the pieces
  s" set " strcat
  name_buffer strget strcat
  s" =" strcat
  value_buffer strget strcat
  ['] evaluate catch if
    2drop free drop
    ESETERROR throw
  else
    free-memory
  then
;

: set_conf_files
  set_environment_variable
  s" loader_conf_files" getenv conf_files string=
;

: set_nextboot_conf
  value_buffer strget unquote nextboot_conf_file string=
;

: append_to_module_options_list  ( addr -- )
  module_options @ 0= if
    dup module_options !
    last_module_option !
  else
    dup last_module_option @ module.next !
    last_module_option !
  then
;

: set_module_name  { addr -- }	\ check leaks
  name_buffer strget addr module.name string=
;

: yes_value?
  value_buffer strget	\ XXX could use unquote
  2dup s' "YES"' compare >r
  2dup s' "yes"' compare >r
  2dup s" YES" compare >r
  s" yes" compare r> r> r> and and and 0=
;

: find_module_option  ( -- addr | 0 ) \ return ptr to entry matching name_buffer
  module_options @
  begin
    dup
  while
    dup module.name strget
    name_buffer strget
    compare 0= if exit then
    module.next @
  repeat
;

: new_module_option  ( -- addr )
  sizeof module allocate if ENOMEM throw then
  dup sizeof module erase
  dup append_to_module_options_list
  dup set_module_name
;

: get_module_option  ( -- addr )
  find_module_option
  ?dup 0= if new_module_option then
;

: set_module_flag
  name_buffer .len @ load_module_suffix nip - name_buffer .len !
  yes_value? get_module_option module.flag !
;

: set_module_args
  name_buffer .len @ module_args_suffix nip - name_buffer .len !
  value_buffer strget unquote
  get_module_option module.args string=
;

: set_module_loadname
  name_buffer .len @ module_loadname_suffix nip - name_buffer .len !
  value_buffer strget unquote
  get_module_option module.loadname string=
;

: set_module_type
  name_buffer .len @ module_type_suffix nip - name_buffer .len !
  value_buffer strget unquote
  get_module_option module.type string=
;

: set_module_beforeload
  name_buffer .len @ module_beforeload_suffix nip - name_buffer .len !
  value_buffer strget unquote
  get_module_option module.beforeload string=
;

: set_module_afterload
  name_buffer .len @ module_afterload_suffix nip - name_buffer .len !
  value_buffer strget unquote
  get_module_option module.afterload string=
;

: set_module_loaderror
  name_buffer .len @ module_loaderror_suffix nip - name_buffer .len !
  value_buffer strget unquote
  get_module_option module.loaderror string=
;

: set_nextboot_flag
  yes_value? to nextboot?
;

: set_verbose
  yes_value? to verbose?
;

: execute_command
  value_buffer strget unquote
  ['] evaluate catch if EEXEC throw then
;

: process_assignment
  name_buffer .len @ 0= if exit then
  loader_conf_files?	if set_conf_files exit then
  nextboot_flag?	if set_nextboot_flag exit then
  nextboot_conf?	if set_nextboot_conf exit then
  verbose_flag?		if set_verbose exit then
  execute?		if execute_command exit then
  module_load?		if set_module_flag exit then
  module_loadname?	if set_module_loadname exit then
  module_type?		if set_module_type exit then
  module_args?		if set_module_args exit then
  module_beforeload?	if set_module_beforeload exit then
  module_afterload?	if set_module_afterload exit then
  module_loaderror?	if set_module_loaderror exit then
  set_environment_variable
;

\ free_buffer  ( -- )
\
\ Free some pointers if needed. The code then tests for errors
\ in freeing, and throws an exception if needed. If a pointer is
\ not allocated, it's value (0) is used as flag.

: free_buffers
  name_buffer strfree
  value_buffer strfree
;

\ Higher level file processing

get-current ( -- wid ) previous definitions >search ( wid -- )

: process_conf
  begin
    end_of_file? 0=
  while
    free_buffers
    read_line
    get_assignment
    ['] process_assignment catch
    ['] free_buffers catch
    swap throw throw
  repeat
;

: peek_file ( addr len -- )
  0 to end_of_file?
  reset_line_reading
  O_RDONLY fopen fd !
  fd @ -1 = if EOPEN throw then
  free_buffers
  read_line
  get_assignment
  ['] process_assignment catch
  ['] free_buffers catch
  fd @ fclose
  swap throw throw
;
  
only forth also support-functions definitions

\ Interface to loading conf files

: load_conf  ( addr len -- )
  0 to end_of_file?
  reset_line_reading
  O_RDONLY fopen fd !
  fd @ -1 = if EOPEN throw then
  ['] process_conf catch
  fd @ fclose
  throw
;

: print_line line_buffer strtype cr ;

: print_syntax_error
  line_buffer strtype cr
  line_buffer .addr @
  begin
    line_pointer over <>
  while
    bl emit char+
  repeat
  drop
  ." ^" cr
;


\ Debugging support functions

only forth definitions also support-functions

: test-file 
  ['] load_conf catch dup .
  ESYNTAX = if cr print_syntax_error then
;

\ find a module name, leave addr on the stack (0 if not found)
: find-module ( <module> -- ptr | 0 )
  bl parse ( addr len )
  module_options @ >r ( store current pointer )
  begin
    r@
  while
    2dup ( addr len addr len )
    r@ module.name strget
    compare 0= if drop drop r> exit then ( found it )
    r> module.next @ >r
  repeat
  type ."  was not found" cr r>
;

: show-nonempty ( addr len mod -- )
  strget dup verbose? or if
    2swap type type cr
  else
    drop drop drop drop
  then ;

: show-one-module { addr -- addr }
  ." Name:        " addr module.name strtype cr
  s" Path:        " addr module.loadname show-nonempty
  s" Type:        " addr module.type show-nonempty
  s" Flags:       " addr module.args show-nonempty
  s" Before load: " addr module.beforeload show-nonempty
  s" After load:  " addr module.afterload show-nonempty
  s" Error:       " addr module.loaderror show-nonempty
  ." Status:      " addr module.flag @ if ." Load" else ." Don't load" then cr
  cr
  addr
;

: show-module-options
  module_options @
  begin
    ?dup
  while
    show-one-module
    module.next @
  repeat
;

: free-one-module { addr -- addr }
  addr module.name strfree
  addr module.loadname strfree
  addr module.type strfree
  addr module.args strfree
  addr module.beforeload strfree
  addr module.afterload strfree
  addr module.loaderror strfree
  addr
;

: free-module-options
  module_options @
  begin
    ?dup
  while
    free-one-module
    dup module.next @
    swap free-memory
  repeat
  0 module_options !
  0 last_module_option !
;

only forth also support-functions definitions

\ Variables used for processing multiple conf files

string current_file_name_ref	\ used to print the file name

\ Indicates if any conf file was successfully read

0 value any_conf_read?

\ loader_conf_files processing support functions

: get_conf_files ( -- addr len )  \ put addr/len on stack, reset var
  conf_files strget 0 0 conf_files strset
;

: skip_leading_spaces  { addr len pos -- addr len pos' }
  begin
    pos len = if 0 else addr pos + c@ bl = then
  while
    pos char+ to pos
  repeat
  addr len pos
;

\ return the file name at pos, or free the string if nothing left
: get_file_name  { addr len pos -- addr len pos' addr' len' || 0 }
  pos len = if 
    addr free abort" Fatal error freeing memory"
    0 exit
  then
  pos >r
  begin
    \ stay in the loop until have chars and they are not blank
    pos len = if 0 else addr pos + c@ bl <> then
  while
    pos char+ to pos
  repeat
  addr len pos addr r@ + pos r> -
;

: get_next_file  ( addr len ptr -- addr len ptr' addr' len' | 0 )
  skip_leading_spaces
  get_file_name
;

: print_current_file
  current_file_name_ref strtype
;

: process_conf_errors
  dup 0= if true to any_conf_read? drop exit then
  >r 2drop r>
  dup ESYNTAX = if
    ." Warning: syntax error on file " print_current_file cr
    print_syntax_error drop exit
  then
  dup ESETERROR = if
    ." Warning: bad definition on file " print_current_file cr
    print_line drop exit
  then
  dup EREAD = if
    ." Warning: error reading file " print_current_file cr drop exit
  then
  dup EOPEN = if
    verbose? if ." Warning: unable to open file " print_current_file cr then
    drop exit
  then
  dup EFREE = abort" Fatal error freeing memory"
  dup ENOMEM = abort" Out of memory"
  throw  \ Unknown error -- pass ahead
;

\ Process loader_conf_files recursively
\ Interface to loader_conf_files processing

: include_conf_files
  get_conf_files 0	( addr len offset )
  begin
    get_next_file ?dup ( addr len 1 | 0 )
  while
    current_file_name_ref strref
    ['] load_conf catch
    process_conf_errors
    conf_files .addr @ if recurse then
  repeat
;

: get_nextboot_conf_file ( -- addr len )
  nextboot_conf_file strget
;

: rewrite_nextboot_file ( -- )
  get_nextboot_conf_file
  O_WRONLY fopen fd !
  fd @ -1 = if EOPEN throw then
  fd @ s' nextboot_enable="NO" ' fwrite ( fd buf len -- nwritten ) drop
  fd @ fclose
;

: include_nextboot_file ( -- )
  get_nextboot_conf_file
  ['] peek_file catch if 2drop then
  nextboot? if
    get_nextboot_conf_file
    current_file_name_ref strref
    ['] load_conf catch
    process_conf_errors
    ['] rewrite_nextboot_file catch if 2drop then
  then
;

\ Module loading functions

: load_parameters  { addr -- addr addrN lenN ... addr1 len1 N }
  addr
  addr module.args strget
  addr module.loadname .len @ if
    addr module.loadname strget
  else
    addr module.name strget
  then
  addr module.type .len @ if
    addr module.type strget
    s" -t "
    4 ( -t type name flags )
  else
    2 ( name flags )
  then
;

: before_load  ( addr -- addr )
  dup module.beforeload .len @ if
    dup module.beforeload strget
    ['] evaluate catch if EBEFORELOAD throw then
  then
;

: after_load  ( addr -- addr )
  dup module.afterload .len @ if
    dup module.afterload strget
    ['] evaluate catch if EAFTERLOAD throw then
  then
;

: load_error  ( addr -- addr )
  dup module.loaderror .len @ if
    dup module.loaderror strget
    evaluate  \ This we do not intercept so it can throw errors
  then
;

: pre_load_message  ( addr -- addr )
  verbose? if
    dup module.name strtype
    ." ..."
  then
;

: load_error_message verbose? if ." failed!" cr then ;

: load_successful_message verbose? if ." ok" cr then ;

: load_module
  load_parameters load
;

: process_module  ( addr -- addr )
  pre_load_message
  before_load
  begin
    ['] load_module catch if
      dup module.loaderror .len @ if
        load_error			\ Command should return a flag!
      else 
        load_error_message true		\ Do not retry
      then
    else
      after_load
      load_successful_message true	\ Successful, do not retry
    then
  until
;

: process_module_errors  ( addr ior -- )
  dup EBEFORELOAD = if
    drop
    ." Module "
    dup module.name strtype
    dup module.loadname .len @ if
      ." (" dup module.loadname strtype ." )"
    then
    cr
    ." Error executing "
    dup module.beforeload strtype cr	\ XXX there was a typo here
    abort
  then

  dup EAFTERLOAD = if
    drop
    ." Module "
    dup module.name .addr @ over module.name .len @ type
    dup module.loadname .len @ if
      ." (" dup module.loadname strtype ." )"
    then
    cr
    ." Error executing "
    dup module.afterload strtype cr
    abort
  then

  throw  \ Don't know what it is all about -- pass ahead
;

\ Module loading interface

\ scan the list of modules, load enabled ones.
: load_modules  ( -- ) ( throws: abort & user-defined )
  module_options @	( list_head )
  begin
    ?dup
  while
    dup module.flag @ if
      ['] process_module catch
      process_module_errors
    then
    module.next @
  repeat
;

\ h00h00 magic used to try loading either a kernel with a given name,
\ or a kernel with the default name in a directory of a given name
\ (the pain!)

: bootpath s" /boot/" ;
: modulepath s" module_path" ;

\ Functions used to save and restore module_path's value.
: saveenv ( addr len | -1 -- addr' len | 0 -1 )
  dup -1 = if 0 swap exit then
  strdup
;
: freeenv ( addr len | 0 -1 )
  -1 = if drop else free abort" Freeing error" then
;
: restoreenv  ( addr len | 0 -1 -- )
  dup -1 = if ( it wasn't set )
    2drop
    modulepath unsetenv
  else
    over >r
    modulepath setenv
    r> free abort" Freeing error"
  then
;

: clip_args   \ Drop second string if only one argument is passed
  1 = if
    2swap 2drop
    1
  else
    2
  then
;

also builtins

\ Parse filename from a semicolon-separated list

\ replacement, not working yet
: newparse-; { addr len | a1 -- a' len-x addr x }
  addr len [char] ; strchr dup if	( a1 len1 )
    swap to a1	( store address )
    1 - a1 @ 1 + swap ( remove match )
    addr a1 addr -
  else
    0 0 addr len
  then
;

: parse-; ( addr len -- addr' len-x addr x )
  over 0 2swap			( addr 0 addr len )
  begin
    dup 0 <>			( addr 0 addr len )
  while
    over c@ [char] ; <>		( addr 0 addr len flag )
  while
    1- swap 1+ swap
    2swap 1+ 2swap
  repeat then
  dup 0 <> if
    1- swap 1+ swap
  then
  2swap
;

\ Try loading one of multiple kernels specified

: try_multiple_kernels ( addr len addr' len' args -- flag )
  >r
  begin
    parse-; 2>r
    2over 2r>
    r@ clip_args
    s" DEBUG" getenv? if
      s" echo Module_path: ${module_path}" evaluate
      ." Kernel     : " >r 2dup type r> cr
      dup 2 = if ." Flags      : " >r 2over type r> cr then
    then
    1 load
  while
    dup 0=
  until
    1 >r \ Failure
  else
    0 >r \ Success
  then
  2drop 2drop
  r>
  r> drop
;

\ Try to load a kernel; the kernel name is taken from one of
\ the following lists, as ordered:
\
\   1. The "bootfile" environment variable
\   2. The "kernel" environment variable
\
\ Flags are passed, if available. If not, dummy values must be given.
\
\ The kernel gets loaded from the current module_path.

: load_a_kernel ( flags len 1 | x x 0 -- flag )
  local args
  2local flags
  0 0 2local kernel
  end-locals

  \ Check if a default kernel name exists at all, exits if not
  s" bootfile" getenv dup -1 <> if
    to kernel
    flags kernel args 1+ try_multiple_kernels
    dup 0= if exit then
  then
  drop

  s" kernel" getenv dup -1 <> if
    to kernel
  else
    drop
    1 exit \ Failure
  then

  \ Try all default kernel names
  flags kernel args 1+ try_multiple_kernels
;

\ Try to load a kernel; the kernel name is taken from one of
\ the following lists, as ordered:
\
\   1. The "bootfile" environment variable
\   2. The "kernel" environment variable
\
\ Flags are passed, if provided.
\
\ The kernel will be loaded from a directory computed from the
\ path given. Two directories will be tried in the following order:
\
\   1. /boot/path
\   2. path
\
\ The module_path variable is overridden if load is successful, by
\ prepending the successful path.

: load_from_directory ( path len 1 | flags len' path len 2 -- flag )
  local args
  2local path
  args 1 = if 0 0 then
  2local flags
  0 0 2local oldmodulepath \ like a string
  0 0 2local newmodulepath \ like a string
  end-locals

  \ Set the environment variable module_path, and try loading
  \ the kernel again.
  modulepath getenv saveenv to oldmodulepath

  \ Try prepending /boot/ first
  bootpath nip path nip + 	\ total length
  oldmodulepath nip dup -1 = if
    drop
  else
    1+ +			\ add oldpath -- XXX why the 1+ ?
  then
  allocate if ( out of memory ) 1 exit then \ XXX throw ?

  0
  bootpath strcat
  path strcat
  2dup to newmodulepath
  modulepath setenv

  \ Try all default kernel names
  flags args 1- load_a_kernel
  0= if ( success )
    oldmodulepath nip -1 <> if
      newmodulepath s" ;" strcat
      oldmodulepath strcat
      modulepath setenv
      newmodulepath drop free-memory
      oldmodulepath drop free-memory
    then
    0 exit
  then

  \ Well, try without the prepended /boot/
  path newmodulepath drop swap move
  newmodulepath drop path nip
  2dup to newmodulepath
  modulepath setenv

  \ Try all default kernel names
  flags args 1- load_a_kernel
  if ( failed once more )
    oldmodulepath restoreenv
    newmodulepath drop free-memory
    1
  else
    oldmodulepath nip -1 <> if
      newmodulepath s" ;" strcat
      oldmodulepath strcat
      modulepath setenv
      newmodulepath drop free-memory
      oldmodulepath drop free-memory
    then
    0
  then
;

\ Try to load a kernel; the kernel name is taken from one of
\ the following lists, as ordered:
\
\   1. The "bootfile" environment variable
\   2. The "kernel" environment variable
\   3. The "path" argument
\
\ Flags are passed, if provided.
\
\ The kernel will be loaded from a directory computed from the
\ path given. Two directories will be tried in the following order:
\
\   1. /boot/path
\   2. path
\
\ Unless "path" is meant to be kernel name itself. In that case, it
\ will first be tried as a full path, and, next, search on the
\ directories pointed by module_path.
\
\ The module_path variable is overridden if load is successful, by
\ prepending the successful path.

: load_directory_or_file ( path len 1 | flags len' path len 2 -- flag )
  local args
  2local path
  args 1 = if 0 0 then
  2local flags
  end-locals

  \ First, assume path is an absolute path to a directory
  flags path args clip_args load_from_directory
  dup 0= if exit else drop then

  \ Next, assume path points to the kernel
  flags path args try_multiple_kernels
;

: initialize  ( addr len -- )
  strdup conf_files strset
;

: kernel_options ( -- addr len 1 | 0 )
  s" kernel_options" getenv
  dup -1 = if drop 0 else 1 then
;

: standard_kernel_search  ( flags 1 | 0 -- flag )
  local args
  args 0= if 0 0 then
  2local flags
  s" kernel" getenv
  dup -1 = if 0 swap then
  2local path
  end-locals

  path nip -1 = if ( there isn't a "kernel" environment variable )
    flags args load_a_kernel
  else
    flags path args 1+ clip_args load_directory_or_file
  then
;

: load_kernel  ( -- ) ( throws: abort )
  kernel_options standard_kernel_search
  abort" Unable to load a kernel!"
;

: load_xen ( -- flag )
  s" xen_kernel" getenv dup -1 <> if
    1 1 load ( c-addr/u flag N -- flag )
  else
    drop
    0 ( -1 -- flag )
  then
;

: load_xen_throw ( -- ) ( throws: abort )
  load_xen
  abort" Unable to load Xen!"
;

: set_defaultoptions  ( -- )
  s" kernel_options" getenv dup -1 = if
    drop
  else
    s" temp_options" setenv
  then
;

\ pick the i-th argument, i starts at 0
: argv[]  ( aN uN ... a1 u1 N i -- aN uN ... a1 u1 N ai+1 ui+1 )
  2dup = if 0 0 exit then	\ out of range
  dup >r
  1+ 2* ( skip N and ui )
  pick
  r>
  1+ 2* ( skip N and ai )
  pick
;

: drop_args  ( aN uN ... a1 u1 N -- )
  0 ?do 2drop loop
;

: argc
  dup
;

: queue_argv  ( aN uN ... a1 u1 N a u -- a u aN uN ... a1 u1 N+1 )
  >r
  over 2* 1+ -roll
  r>
  over 2* 1+ -roll
  1+
;

: unqueue_argv  ( aN uN ... a1 u1 N -- aN uN ... a2 u2 N-1 a1 u1 )
  1- -rot
;

\ compute the length of the buffer including the spaces between words
: strlen(argv) ( aN uN .. a1 u1 N -- aN uN .. a1 u1 N len )
  dup 0= if 0 exit then
  0 >r	\ Size
  0 >r	\ Index
  begin
    argc r@ <>
  while
    r@ argv[]
    nip
    r> r> rot + 1+
    >r 1+ >r
  repeat
  r> drop
  r>
;

: concat_argv  ( aN uN ... a1 u1 N -- a u )
  strlen(argv) allocate if ENOMEM throw then
  0 2>r ( save addr 0 on return stack )

  begin
    dup
  while
    unqueue_argv ( ... N a1 u1 )
    2r> 2swap	 ( old a1 u1 )
    strcat
    s"  " strcat ( append one space ) \ XXX this gives a trailing space
    2>r		( store string on the result stack )
  repeat
  drop_args
  2r>
;

: set_tempoptions  ( addrN lenN ... addr1 len1 N -- addr len 1 | 0 )
  \ Save the first argument, if it exists and is not a flag
  argc if
    0 argv[] drop c@ [char] - <> if
      unqueue_argv 2>r  \ Filename
      1 >r		\ Filename present
    else
      0 >r		\ Filename not present
    then
  else
    0 >r		\ Filename not present
  then

  \ If there are other arguments, assume they are flags
  ?dup if
    concat_argv
    2dup s" temp_options" setenv
    drop free if EFREE throw then
  else
    set_defaultoptions
  then

  \ Bring back the filename, if one was provided
  r> if 2r> 1 else 0 then
;

: get_arguments ( -- addrN lenN ... addr1 len1 N )
  0
  begin
    \ Get next word on the command line
    parse-word
  ?dup while
    queue_argv
  repeat
  drop ( empty string )
;

: load_kernel_and_modules  ( args -- flag )
  set_tempoptions
  argc >r
  s" temp_options" getenv dup -1 <> if
    queue_argv
  else
    drop
  then
  load_xen
  ?dup 0= if ( success )
    r> if ( a path was passed )
      load_directory_or_file
    else
      standard_kernel_search
    then
    ?dup 0= if ['] load_modules catch then
  then
;

only forth definitions
