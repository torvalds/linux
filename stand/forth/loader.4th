\ Copyright (c) 1999 Daniel C. Sobral <dcs@FreeBSD.org>
\ Copyright (c) 2011-2015 Devin Teske <dteske@FreeBSD.org>
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

only forth definitions

s" arch-i386" environment? [if] [if]
	s" loader_version" environment?  [if]
		11 < [if]
			.( Loader version 1.1+ required) cr
			abort
		[then]
	[else]
		.( Could not get loader version!) cr
		abort
	[then]
[then] [then]

256 dictthreshold !  \ 256 cells minimum free space
2048 dictincrease !  \ 2048 additional cells each time

include /boot/support.4th
include /boot/color.4th
include /boot/delay.4th
include /boot/check-password.4th

only forth definitions

: bootmsg ( -- )
  loader_color? dup ( -- bool bool )
  if 7 fg 4 bg then
  ." Booting..."
  if me then
  cr
;

: try-menu-unset
  \ menu-unset may not be present
  s" beastie_disable" getenv
  dup -1 <> if
    s" YES" compare-insensitive 0= if
      exit
    then
  else
    drop
  then
  s" menu-unset"
  sfind if
    execute
  else
    drop
  then
  s" menusets-unset"
  sfind if
    execute
  else
    drop
  then
;

only forth also support-functions also builtins definitions

: boot
  0= if ( interpreted ) get_arguments then

  \ Unload only if a path was passed
  dup if
    >r over r> swap
    c@ [char] - <> if
      0 1 unload drop
    else
      s" kernelname" getenv? if ( a kernel has been loaded )
        try-menu-unset
        bootmsg 1 boot exit
      then
      load_kernel_and_modules
      ?dup if exit then
      try-menu-unset
      bootmsg 0 1 boot exit
    then
  else
    s" kernelname" getenv? if ( a kernel has been loaded )
      try-menu-unset
      bootmsg 1 boot exit
    then
    load_kernel_and_modules
    ?dup if exit then
    try-menu-unset
    bootmsg 0 1 boot exit
  then
  load_kernel_and_modules
  ?dup 0= if bootmsg 0 1 boot then
;

\ ***** boot-conf
\
\	Prepares to boot as specified by loaded configuration files.

: boot-conf
  0= if ( interpreted ) get_arguments then
  0 1 unload drop
  load_kernel_and_modules
  ?dup 0= if 0 1 autoboot then
;

also forth definitions previous

builtin: boot
builtin: boot-conf

only forth definitions also support-functions

\ ***** start
\
\       Initializes support.4th global variables, sets loader_conf_files,
\       processes conf files, and, if any one such file was successfully
\       read to the end, loads kernel and modules.

: start  ( -- ) ( throws: abort & user-defined )
  s" /boot/defaults/loader.conf" initialize
  include_conf_files
  include_nextboot_file
  \ If the user defined a post-initialize hook, call it now
  s" post-initialize" sfind if execute else drop then
  \ Will *NOT* try to load kernel and modules if no configuration file
  \ was successfully loaded!
  any_conf_read? if
    s" loader_delay" getenv -1 = if
      load_xen_throw
      load_kernel
      load_modules
    else
      drop
      ." Loading Kernel and Modules (Ctrl-C to Abort)" cr
      s" also support-functions" evaluate
      s" set delay_command='load_xen_throw load_kernel load_modules'" evaluate
      s" set delay_showdots" evaluate
      delay_execute
    then
  then
;

\ ***** initialize
\
\	Overrides support.4th initialization word with one that does
\	everything start one does, short of loading the kernel and
\	modules. Returns a flag.

: initialize ( -- flag )
  s" /boot/defaults/loader.conf" initialize
  include_conf_files
  include_nextboot_file
  \ If the user defined a post-initialize hook, call it now
  s" post-initialize" sfind if execute else drop then
  any_conf_read?
;

\ ***** read-conf
\
\	Read a configuration file, whose name was specified on the command
\	line, if interpreted, or given on the stack, if compiled in.

: (read-conf)  ( addr len -- )
  conf_files string=
  include_conf_files \ Will recurse on new loader_conf_files definitions
;

: read-conf  ( <filename> | addr len -- ) ( throws: abort & user-defined )
  state @ if
    \ Compiling
    postpone (read-conf)
  else
    \ Interpreting
    bl parse (read-conf)
  then
; immediate

\ show, enable, disable, toggle module loading. They all take module from
\ the next word

: set-module-flag ( module_addr val -- ) \ set and print flag
  over module.flag !
  dup module.name strtype
  module.flag @ if ."  will be loaded" else ."  will not be loaded" then cr
;

: enable-module find-module ?dup if true set-module-flag then ;

: disable-module find-module ?dup if false set-module-flag then ;

: toggle-module find-module ?dup if dup module.flag @ 0= set-module-flag then ;

\ ***** show-module
\
\	Show loading information about a module.

: show-module ( <module> -- ) find-module ?dup if show-one-module then ;

\ Words to be used inside configuration files

: retry false ;         \ For use in load error commands
: ignore true ;         \ For use in load error commands

\ Return to strict forth vocabulary

: #type
  over - >r
  type
  r> spaces
;

: .? 2 spaces 2swap 15 #type 2 spaces type cr ;

\ Execute the ? command to print all the commands defined in
\ C, then list the ones we support here. Please note that this
\ doesn't use pager_* routines that the C implementation of ?
\ does, so these will always appear, even if you stop early
\ there. And they may cause the commands to scroll off the
\ screen if the number of commands modulus LINES is close
\ to LINEs....
: ?
  ['] ? execute
  s" boot-conf" s" load kernel and modules, then autoboot" .?
  s" read-conf" s" read a configuration file" .?
  s" enable-module" s" enable loading of a module" .?
  s" disable-module" s" disable loading of a module" .?
  s" toggle-module" s" toggle loading of a module" .?
  s" show-module" s" show module load data" .?
  s" try-include" s" try to load/interpret files" .?
;

: try-include ( -- ) \ see loader.4th(8)
  ['] include ( -- xt ) \ get the execution token of `include'
  catch ( xt -- exception# | 0 ) if \ failed
    LF parse ( c -- s-addr/u ) 2drop \ advance >in to EOL (drop data)
    \ ... prevents words unused by `include' from being interpreted
  then
; immediate \ interpret immediately for access to `source' (aka tib)

only forth definitions
