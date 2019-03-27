\ Copyright (c) 2012 Devin Teske <dteske@FreeBSD.org>
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

marker task-menusets.4th

vocabulary menusets-infrastructure
only forth also menusets-infrastructure definitions

variable menuset_use_name

create menuset_affixbuf	255 allot
create menuset_x        1   allot
create menuset_y        1   allot

: menuset-loadvar ( -- )

	\ menuset_use_name is true or false
	\ $type should be set to one of:
	\ 	menu toggled ansi
	\ $var should be set to one of:
	\ 	caption command keycode text ...
	\ $affix is either prefix (menuset_use_name is true)
	\               or infix (menuset_use_name is false)

	s" set cmdbuf='set ${type}_${var}=\$'" evaluate
	s" cmdbuf" getenv swap drop ( -- u1 ) \ get string length
	menuset_use_name @ true = if
		s" set cmdbuf=${cmdbuf}${affix}${type}_${var}"
		( u1 -- u1 c-addr2 u2 )
	else
		s" set cmdbuf=${cmdbuf}${type}set${affix}_${var}"
		( u1 -- u1 c-addr2 u2 )
	then
	evaluate ( u1 c-addr2 u2 -- u1 )
	s" cmdbuf" getenv ( u1 -- u1 c-addr2 u2 )
	rot 2 pick 2 pick over + -rot + tuck -
		( u1 c-addr2 u2 -- c-addr2 u2 c-addr1 u1 )
		\ Generate a string representing rvalue inheritance var
	getenv dup -1 = if
		( c-addr2 u2 c-addr1 u1 -- c-addr2 u2 -1 )
		\ NOT set -- clean up the stack
		drop ( c-addr2 u2 -1 -- c-addr2 u2 )
		2drop ( c-addr2 u2 -- )
	else
		( c-addr2 u2 c-addr1 u1 -- c-addr2 u2 c-addr1 u1 )
		\ SET -- execute cmdbuf (c-addr2/u2) to inherit value
		2drop ( c-addr2 u2 c-addr1 u1 -- c-addr2 u2 )
		evaluate ( c-addr2 u2 -- )
	then

	s" cmdbuf" unsetenv
;

: menuset-unloadvar ( -- )

	\ menuset_use_name is true or false
	\ $type should be set to one of:
	\ 	menu toggled ansi
	\ $var should be set to one of:
	\ 	caption command keycode text ...
	\ $affix is either prefix (menuset_use_name is true)
	\               or infix (menuset_use_name is false)

	menuset_use_name @ true = if
		s" set buf=${affix}${type}_${var}"
	else
		s" set buf=${type}set${affix}_${var}"
	then
	evaluate
	s" buf" getenv unsetenv
	s" buf" unsetenv
;

: menuset-loadmenuvar ( -- )
	s" set type=menu" evaluate
	menuset-loadvar
;

: menuset-unloadmenuvar ( -- )
	s" set type=menu" evaluate
	menuset-unloadvar
;

: menuset-loadxvar ( -- )

	\ menuset_use_name is true or false
	\ $type should be set to one of:
	\ 	menu toggled ansi
	\ $var should be set to one of:
	\ 	caption command keycode text ...
	\ $x is "1" through "8"
	\ $affix is either prefix (menuset_use_name is true)
	\               or infix (menuset_use_name is false)

	s" set cmdbuf='set ${type}_${var}[${x}]=\$'" evaluate
	s" cmdbuf" getenv swap drop ( -- u1 ) \ get string length
	menuset_use_name @ true = if
		s" set cmdbuf=${cmdbuf}${affix}${type}_${var}[${x}]"
		( u1 -- u1 c-addr2 u2 )
	else
		s" set cmdbuf=${cmdbuf}${type}set${affix}_${var}[${x}]"
		( u1 -- u1 c-addr2 u2 )
	then
	evaluate ( u1 c-addr2 u2 -- u1 )
	s" cmdbuf" getenv ( u1 -- u1 c-addr2 u2 )
	rot 2 pick 2 pick over + -rot + tuck -
		( u1 c-addr2 u2 -- c-addr2 u2 c-addr1 u1 )
		\ Generate a string representing rvalue inheritance var
	getenv dup -1 = if
		( c-addr2 u2 c-addr1 u1 -- c-addr2 u2 -1 )
		\ NOT set -- clean up the stack
		drop ( c-addr2 u2 -1 -- c-addr2 u2 )
		2drop ( c-addr2 u2 -- )
	else
		( c-addr2 u2 c-addr1 u1 -- c-addr2 u2 c-addr1 u1 )
		\ SET -- execute cmdbuf (c-addr2/u2) to inherit value
		2drop ( c-addr2 u2 c-addr1 u1 -- c-addr2 u2 )
		evaluate ( c-addr2 u2 -- )
	then

	s" cmdbuf" unsetenv
;

: menuset-unloadxvar ( -- )

	\ menuset_use_name is true or false
	\ $type should be set to one of:
	\ 	menu toggled ansi
	\ $var should be set to one of:
	\ 	caption command keycode text ...
	\ $x is "1" through "8"
	\ $affix is either prefix (menuset_use_name is true)
	\               or infix (menuset_use_name is false)

	menuset_use_name @ true = if
		s" set buf=${affix}${type}_${var}[${x}]"
	else
		s" set buf=${type}set${affix}_${var}[${x}]"
	then
	evaluate
	s" buf" getenv unsetenv
	s" buf" unsetenv
;

: menuset-loadansixvar ( -- )
	s" set type=ansi" evaluate
	menuset-loadxvar
;

: menuset-unloadansixvar ( -- )
	s" set type=ansi" evaluate
	menuset-unloadxvar
;

: menuset-loadmenuxvar ( -- )
	s" set type=menu" evaluate
	menuset-loadxvar
;

: menuset-unloadmenuxvar ( -- )
	s" set type=menu" evaluate
	menuset-unloadxvar
;

: menuset-loadtoggledxvar ( -- )
	s" set type=toggled" evaluate
	menuset-loadxvar
;

: menuset-unloadtoggledxvar ( -- )
	s" set type=toggled" evaluate
	menuset-unloadxvar
;

: menuset-loadxyvar ( -- )

	\ menuset_use_name is true or false
	\ $type should be set to one of:
	\ 	menu toggled ansi
	\ $var should be set to one of:
	\ 	caption command keycode text ...
	\ $x is "1" through "8"
	\ $y is "0" through "9"
	\ $affix is either prefix (menuset_use_name is true)
	\               or infix (menuset_use_name is false)

	s" set cmdbuf='set ${type}_${var}[${x}][${y}]=\$'" evaluate
	s" cmdbuf" getenv swap drop ( -- u1 ) \ get string length
	menuset_use_name @ true = if
		s" set cmdbuf=${cmdbuf}${affix}${type}_${var}[${x}][${y}]"
		( u1 -- u1 c-addr2 u2 )
	else
		s" set cmdbuf=${cmdbuf}${type}set${affix}_${var}[${x}][${y}]"
		( u1 -- u1 c-addr2 u2 )
	then
	evaluate ( u1 c-addr2 u2 -- u1 )
	s" cmdbuf" getenv ( u1 -- u1 c-addr2 u2 )
	rot 2 pick 2 pick over + -rot + tuck -
		( u1 c-addr2 u2 -- c-addr2 u2 c-addr1 u1 )
		\ Generate a string representing rvalue inheritance var
	getenv dup -1 = if
		( c-addr2 u2 c-addr1 u1 -- c-addr2 u2 -1 )
		\ NOT set -- clean up the stack
		drop ( c-addr2 u2 -1 -- c-addr2 u2 )
		2drop ( c-addr2 u2 -- )
	else
		( c-addr2 u2 c-addr1 u1 -- c-addr2 u2 c-addr1 u1 )
		\ SET -- execute cmdbuf (c-addr2/u2) to inherit value
		2drop ( c-addr2 u2 c-addr1 u1 -- c-addr2 u2 )
		evaluate ( c-addr2 u2 -- )
	then

	s" cmdbuf" unsetenv
;

: menuset-unloadxyvar ( -- )

	\ menuset_use_name is true or false
	\ $type should be set to one of:
	\ 	menu toggled ansi
	\ $var should be set to one of:
	\ 	caption command keycode text ...
	\ $x is "1" through "8"
	\ $y is "0" through "9"
	\ $affix is either prefix (menuset_use_name is true)
	\               or infix (menuset_use_name is false)

	menuset_use_name @ true = if
		s" set buf=${affix}${type}_${var}[${x}][${y}]"
	else
		s" set buf=${type}set${affix}_${var}[${x}][${y}]"
	then
	evaluate
	s" buf" getenv unsetenv
	s" buf" unsetenv
;

: menuset-loadansixyvar ( -- )
	s" set type=ansi" evaluate
	menuset-loadxyvar
;

: menuset-unloadansixyvar ( -- )
	s" set type=ansi" evaluate
	menuset-unloadxyvar
;

: menuset-loadmenuxyvar ( -- )
	s" set type=menu" evaluate
	menuset-loadxyvar
;

: menuset-unloadmenuxyvar ( -- )
	s" set type=menu" evaluate
	menuset-unloadxyvar
;

: menuset-setnum-namevar ( N -- C-Addr/U )

	s" menuset_nameNNNNN" ( n -- n c-addr1 u1 )	\ variable basename
	drop 12 ( n c-addr1 u1 -- n c-addr1 12 )	\ remove "NNNNN"
	rot     ( n c-addr1 12 -- c-addr1 12 n )	\ move number on top

	\ convert to string
	s>d <# #s #> ( c-addr1 12 n -- c-addr1 12 c-addr2 u2 )

	\ Combine strings
	begin ( using u2 in c-addr2/u2 pair as countdown to zero )
		over	( c-addr1 u1 c-addr2 u2 -- continued below )
			( c-addr1 u1 c-addr2 u2 c-addr2 ) \ copy src-addr
		c@	( c-addr1 u1 c-addr2 u2 c-addr2 -- continued below )
			( c-addr1 u1 c-addr2 u2 c ) \ get next src-addr byte
		4 pick 4 pick
			( c-addr1 u1 c-addr2 u2 c -- continued below )
			( c-addr1 u1 c-addr2 u2 c c-addr1 u1 )
			\ get destination c-addr1/u1 pair
		+	( c-addr1 u1 c-addr2 u2 c c-addr1 u1 -- cont. below )
			( c-addr1 u1 c-addr2 u2 c c-addr3 )
			\ combine dest-c-addr to get dest-addr for byte
		c!	( c-addr1 u1 c-addr2 u2 c c-addr3 -- continued below )
			( c-addr1 u1 c-addr2 u2 )
			\ store the current src-addr byte into dest-addr

		2swap 1+ 2swap	\ increment u1 in destination c-addr1/u1 pair
		swap 1+ swap	\ increment c-addr2 in source c-addr2/u2 pair
		1-		\ decrement u2 in the source c-addr2/u2 pair

		dup 0= \ time to break?
	until

	2drop	( c-addr1 u1 c-addr2 u2 -- c-addr1 u1 )
		\ drop temporary number-format conversion c-addr2/u2
;

: menuset-checksetnum ( N -- )

	\ 
	\ adjust input to be both positive and no-higher than 65535
	\ 
	abs dup 65535 > if drop 65535 then ( n -- n )

	\
	\ The next few blocks will determine if we should use the default
	\ methodology (referencing the original numeric stack-input), or if-
	\ instead $menuset_name{N} has been defined wherein we would then
	\ use the value thereof as the prefix to every menu variable.
	\ 

	false menuset_use_name ! \ assume name is not set

	menuset-setnum-namevar 
	\ 
	\ We now have a string that is the assembled variable name to check
	\ for... $menuset_name{N}. Let's check for it.
	\ 
	2dup ( c-addr1 u1 -- c-addr1 u1 c-addr1 u1 ) \ save a copy
	getenv dup -1 <> if ( c-addr1 u1 c-addr1 u1 -- c-addr1 u1 c-addr2 u2 )
		\ The variable is set. Let's clean up the stack leaving only
		\ its value for later use.

		true menuset_use_name !
		2swap 2drop	( c-addr1 u1 c-addr2 u2 -- c-addr2 u2 )
				\ drop assembled variable name, leave the value
	else ( c-addr1 u1 c-addr1 u1 -- c-addr1 u1 -1 ) \ no such variable
		\ The variable is not set. Let's clean up the stack leaving the
		\ string [portion] representing the original numeric input.

		drop ( c-addr1 u1 -1 -- c-addr1 u1 ) \ drop -1 result
		12 - swap 12 + swap ( c-addr1 u1 -- c-addr2 u2 )
			\ truncate to original numeric stack-input
	then

	\ 
	\ Now, depending on whether $menuset_name{N} has been set, we have
	\ either the value thereof to be used as a prefix to all menu_*
	\ variables or we have a string representing the numeric stack-input
	\ to be used as a "set{N}" infix to the same menu_* variables.
	\ 
	\ For example, if the stack-input is 1 and menuset_name1 is NOT set
	\ the following variables will be referenced:
	\ 	ansiset1_caption[x]		-> ansi_caption[x]
	\ 	ansiset1_caption[x][y]		-> ansi_caption[x][y]
	\ 	menuset1_acpi			-> menu_acpi
	\ 	menuset1_caption[x]		-> menu_caption[x]
	\ 	menuset1_caption[x][y]		-> menu_caption[x][y]
	\ 	menuset1_command[x]		-> menu_command[x]
	\ 	menuset1_init			-> ``evaluated''
	\ 	menuset1_init[x]		-> menu_init[x]
	\ 	menuset1_kernel			-> menu_kernel
	\ 	menuset1_keycode[x]		-> menu_keycode[x]
	\ 	menuset1_options		-> menu_options
	\ 	menuset1_optionstext		-> menu_optionstext
	\ 	menuset1_reboot			-> menu_reboot
	\ 	toggledset1_ansi[x]		-> toggled_ansi[x]
	\ 	toggledset1_text[x]		-> toggled_text[x]
	\ otherwise, the following variables are referenced (where {name}
	\ represents the value of $menuset_name1 (given 1 as stack-input):
	\ 	{name}ansi_caption[x]		-> ansi_caption[x]
	\ 	{name}ansi_caption[x][y]	-> ansi_caption[x][y]
	\ 	{name}menu_acpi			-> menu_acpi
	\ 	{name}menu_caption[x]		-> menu_caption[x]
	\ 	{name}menu_caption[x][y]	-> menu_caption[x][y]
	\ 	{name}menu_command[x]		-> menu_command[x]
	\ 	{name}menu_init			-> ``evaluated''
	\ 	{name}menu_init[x]		-> menu_init[x]
	\ 	{name}menu_kernel		-> menu_kernel
	\ 	{name}menu_keycode[x]		-> menu_keycode[x]
	\ 	{name}menu_options		-> menu_options
	\ 	{name}menu_optionstext		-> menu_optionstext
	\ 	{name}menu_reboot		-> menu_reboot
	\ 	{name}toggled_ansi[x]		-> toggled_ansi[x]
	\ 	{name}toggled_text[x]		-> toggled_text[x]
	\ 
	\ Note that menuset{N}_init and {name}menu_init are the initializers
	\ for the entire menu (for wholly dynamic menus) opposed to the per-
	\ menuitem initializers (with [x] afterward). The whole-menu init
	\ routine is evaluated and not passed down to $menu_init (which
	\ would result in double evaluation). By doing this, the initializer
	\ can initialize the menuset before we transfer it to active-duty.
	\ 

	\ 
	\ Copy our affixation (prefix or infix depending on menuset_use_name)
	\ to our buffer so that we can safely use the s-quote (s") buf again.
	\ 
	menuset_affixbuf 0 2swap ( c-addr2 u2 -- c-addr1 0 c-addr2 u2 )
	begin ( using u2 in c-addr2/u2 pair as countdown to zero )
		over ( c-addr1 u1 c-addr2 u2 -- c-addr1 u1 c-addr2 u2 c-addr2 )
		c@   ( c-addr1 u1 c-addr2 u2 -- c-addr1 u1 c-addr2 u2 c )
		4 pick 4 pick
		     ( c-addr1 u1 c-addr2 u2 c -- continued below )
		     ( c-addr1 u1 c-addr2 u2 c c-addr1 u1 )
		+    ( c-addr1 u1 c-addr2 u2 c c-addr1 u1 -- continued below )
		     ( c-addr1 u1 c-addr2 u2 c c-addr3 )
		c!   ( c-addr1 u1 c-addr2 u2 c c-addr3 -- continued below )
		     ( c-addr1 u1 c-addr2 u2 )
		2swap 1+ 2swap	\ increment affixbuf byte position/count
		swap 1+ swap	\ increment strbuf pointer (source c-addr2)
		1-		\ decrement strbuf byte count (source u2)
		dup 0=          \ time to break?
	until
	2drop ( c-addr1 u1 c-addr2 u2 -- c-addr1 u1 ) \ drop strbuf c-addr2/u2

	\
	\ Create a variable for referencing our affix data (prefix or infix
	\ depending on menuset_use_name as described above). This variable will
	\ be temporary and only used to simplify cmdbuf assembly.
	\ 
	s" affix" setenv ( c-addr1 u1 -- )
;

: menuset-cleanup ( -- )
	s" type"  unsetenv
	s" var"   unsetenv
	s" x"     unsetenv
	s" y"     unsetenv
	s" affix" unsetenv
;

only forth definitions also menusets-infrastructure

: menuset-loadsetnum ( N -- )

	menuset-checksetnum ( n -- )

	\ 
	\ From here out, we use temporary environment variables to make
	\ dealing with variable-length strings easier.
	\ 
	\ menuset_use_name is true or false
	\ $affix should be used appropriately w/respect to menuset_use_name
	\ 

	\ ... menu_init ...
	s" set var=init" evaluate
	menuset-loadmenuvar

	\ If menu_init was set by the above, evaluate it here-and-now
	\ so that the remaining variables are influenced by its actions
	s" menu_init" 2dup getenv dup -1 <> if
		2swap unsetenv \ don't want later menu-create to re-call this
		evaluate
	else
		drop 2drop ( n c-addr u -1 -- n )
	then

	[char] 1 ( -- x ) \ Loop range ASCII '1' (49) to '8' (56)
	begin
		dup menuset_x tuck c! 1 s" x" setenv \ set loop iterator and $x

		s" set var=caption" evaluate

		\ ... menu_caption[x] ...
		menuset-loadmenuxvar

		\ ... ansi_caption[x] ...
		menuset-loadansixvar

		[char] 0 ( x -- x y ) \ Inner Loop ASCII '1' (48) to '9' (57)
		begin
			dup menuset_y tuck c! 1 s" y" setenv
				\ set inner loop iterator and $y

			\ ... menu_caption[x][y] ...
			menuset-loadmenuxyvar

			\ ... ansi_caption[x][y] ...
			menuset-loadansixyvar

			1+ dup 57 > ( x y -- y' 0|-1 ) \ increment and test
		until
		drop ( x y -- x )

		\ ... menu_command[x] ...
		s" set var=command" evaluate
		menuset-loadmenuxvar

		\ ... menu_init[x] ...
		s" set var=init" evaluate
		menuset-loadmenuxvar

		\ ... menu_keycode[x] ...
		s" set var=keycode" evaluate
		menuset-loadmenuxvar

		\ ... toggled_text[x] ...
		s" set var=text" evaluate
		menuset-loadtoggledxvar

		\ ... toggled_ansi[x] ...
		s" set var=ansi" evaluate
		menuset-loadtoggledxvar

		1+ dup 56 > ( x -- x' 0|-1 ) \ increment iterator
		                             \ continue if less than 57
	until
	drop ( x -- ) \ loop iterator

	\ ... menu_reboot ...
	s" set var=reboot" evaluate
	menuset-loadmenuvar

	\ ... menu_acpi ...
	s" set var=acpi" evaluate
	menuset-loadmenuvar

	\ ... menu_kernel ...
	s" set var=kernel" evaluate
	menuset-loadmenuvar

	\ ... menu_options ...
	s" set var=options" evaluate
	menuset-loadmenuvar

	\ ... menu_optionstext ...
	s" set var=optionstext" evaluate
	menuset-loadmenuvar

	menuset-cleanup
;

: menusets-unset ( -- )

	s" menuset_initial" unsetenv

	1 begin
		dup menuset-checksetnum ( n n -- n )

		dup menuset-setnum-namevar ( n n -- n )
		unsetenv

		\ If the current menuset does not populate the first menuitem,
		\ we stop completely.

		menuset_use_name @ true = if
			s" set buf=${affix}menu_caption[1]"
		else
			s" set buf=menuset${affix}_caption[1]"
		then
		evaluate s" buf" getenv getenv -1 = if
			drop ( n -- )
			s" buf" unsetenv
			menuset-cleanup
			exit
		else
			drop ( n c-addr2 -- n ) \ unused
		then

		[char] 1 ( n -- n x ) \ Loop range ASCII '1' (49) to '8' (56)
		begin
			dup menuset_x tuck c! 1 s" x" setenv \ set $x to x

			s" set var=caption" evaluate
			menuset-unloadmenuxvar
			menuset-unloadmenuxvar
			menuset-unloadansixvar
			[char] 0 ( n x -- n x y ) \ Inner loop '0' to '9'
			begin
				dup menuset_y tuck c! 1 s" y" setenv
					\ sets $y to y
				menuset-unloadmenuxyvar
				menuset-unloadansixyvar
				1+ dup 57 > ( n x y -- n x y' 0|-1 )
			until
			drop ( n x y -- n x )
			s" set var=command" evaluate menuset-unloadmenuxvar
			s" set var=init"    evaluate menuset-unloadmenuxvar
			s" set var=keycode" evaluate menuset-unloadmenuxvar
			s" set var=text"    evaluate menuset-unloadtoggledxvar
			s" set var=ansi"    evaluate menuset-unloadtoggledxvar

			1+ dup 56 > ( x -- x' 0|-1 ) \ increment and test
		until
		drop ( n x -- n ) \ loop iterator

		s" set var=acpi"        evaluate menuset-unloadmenuvar
		s" set var=init"        evaluate menuset-unloadmenuvar
		s" set var=kernel"      evaluate menuset-unloadmenuvar
		s" set var=options"     evaluate menuset-unloadmenuvar
		s" set var=optionstext" evaluate menuset-unloadmenuvar
		s" set var=reboot"      evaluate menuset-unloadmenuvar

		1+ dup 65535 > ( n -- n' 0|-1 ) \ increment and test
	until
	drop ( n' -- ) \ loop iterator

	s" buf" unsetenv
	menuset-cleanup
;

only forth definitions

: menuset-loadinitial ( -- )
	s" menuset_initial" getenv dup -1 <> if
		?number 0<> if
			menuset-loadsetnum
		then
	else
		drop \ cruft
	then
;
