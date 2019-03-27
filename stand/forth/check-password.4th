\ Copyright (c) 2006-2015 Devin Teske <dteske@FreeBSD.org>
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

marker task-check-password.4th

include /boot/screen.4th

vocabulary password-processing
only forth also password-processing definitions

13  constant enter_key       \ The decimal ASCII value for Enter key
8   constant bs_key          \ The decimal ASCII value for Backspace key
21  constant ctrl_u          \ The decimal ASCII value for Ctrl-U sequence
255 constant readmax         \ Maximum number of characters for the password

variable read-tick           \ Twiddle position (used by read)
variable read-start          \ Starting X offset (column)(used by read)

create readval readmax allot \ input obtained (up to readmax characters)
variable readlen             \ input length

\ This function blocks program flow (loops forever) until a key is pressed.
\ The key that was pressed is added to the top of the stack in the form of its
\ decimal ASCII representation. Note: the stack cannot be empty when this
\ function starts or an underflow exception will occur. Simplest way to prevent
\ this is to pass 0 as a stack parameter (ie. `0 sgetkey'). This function is
\ called by the read function. You need not call it directly. NOTE: arrow keys
\ show as 0 on the stack
\ 
: sgetkey ( -- )

	begin \ Loop forever
		key? if \ Was a key pressed? (see loader(8))
			drop \ Remove stack-cruft
			key  \ Get the key that was pressed

			\ Check key pressed (see loader(8)) and input limit
			dup 0<> if ( and ) readlen @ readmax < if
				\ Spin the twiddle and then exit this function
				read-tick @ dup 1+ 4 mod read-tick !
				2 spaces
				dup 0 = if ( 1 ) ." /" else
				dup 1 = if ( 2 ) ." -" else
				dup 2 = if ( 3 ) ." \" else
				dup 3 = if ( 4 ) ." |" else
					1 spaces
				then then then then drop
				read-start @ 25 at-xy
				exit
			then then

			\ Always allow Backspace, Enter, and Ctrl-U
			dup bs_key = if exit then
			dup enter_key = if exit then
			dup ctrl_u = if exit then
		then
		50 ms \ Sleep for 50 milliseconds (see loader(8))
	again
;

: cfill ( c c-addr/u -- )
	begin dup 0> while
		-rot 2dup c! 1+ rot 1-
	repeat 2drop drop
;

: read-reset ( -- )
	0 readlen !
	0 readval readmax cfill
;

: read ( c-addr/u -- ) \ Expects string prompt as stack input

	0 25 at-xy           \ Move the cursor to the bottom-left
	dup 1+ read-start !  \ Store X offset after the prompt
	0 readlen !          \ Initialize the read length
	type                 \ Print the prompt

	begin \ Loop forever

		0 sgetkey \ Block here, waiting for a key to be pressed

		\ We are not going to echo the password to the screen (for
		\ security reasons). If Enter is pressed, we process the
		\ password, otherwise augment the key to a string.

		dup enter_key = if
			drop     \ Clean up stack cruft
			3 spaces \ Erase the twiddle
			10 emit  \ Echo new line
			exit
		else dup ctrl_u = if
			3 spaces read-start @ 25 at-xy \ Erase the twiddle
			0 readlen ! \ Reset input to NULL
		else dup bs_key = if
			readlen @ 1 - dup readlen ! \ Decrement input length
			dup 0< if drop 0 dup readlen ! then \ Don't go negative
			0= if 3 spaces read-start @ 25 at-xy then \ Twiddle
		else dup \ Store the character
			\ NB: sgetkey prevents overflow by way of blocking
			\     at readmax except for Backspace or Enter
			readlen @ 1+ dup readlen ! 1- readval + c!
		then then then

		drop \ last key pressed
	again \ Enter was not pressed; repeat
;

only forth definitions also password-processing also support-functions

: check-password ( -- )

	\ Do not allow the user to proceed beyond this point if a boot-lock
	\ password has been set (preventing even boot from proceeding)
	s" bootlock_password" getenv dup -1 <> if
		dup readmax > if drop readmax then
		begin
			s" Boot Password: " read ( prompt -- )
			2dup readval readlen @ compare 0<>
		while
			3000 ms ." loader: incorrect password" 10 emit
		repeat
		2drop read-reset
	else drop then

	\ Prompt for GEOM ELI (geli(8)) passphrase if enabled
	s" geom_eli_passphrase_prompt" getenv dup -1 <> if
		s" YES" compare-insensitive 0= if
			s" GELI Passphrase: " read ( prompt -- )
			readval readlen @ s" kern.geom.eli.passphrase" setenv
			read-reset
		then
	else drop then

	\ Exit if a password was not set
	s" password" getenv -1 = if exit else drop then

	\ We should prevent the user from visiting the menu or dropping to the
	\ interactive loader(8) prompt, but still allow the machine to boot...

	any_conf_read? if load_kernel load_modules then
	0 autoboot

	\ Only reached if autoboot fails for any reason (including if/when
	\ the user aborts/escapes the countdown sequence leading to boot).

	s" password" getenv dup readmax > if drop readmax then
	begin
		s" Password: " read ( prompt -- )
		2dup readval readlen @ compare 0= if \ Correct password?
			2drop read-reset exit
		then
		3000 ms ." loader: incorrect password" 10 emit
	again
;

only forth definitions
