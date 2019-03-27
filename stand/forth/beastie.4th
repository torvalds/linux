\ Copyright (c) 2003 Scott Long <scottl@FreeBSD.org>
\ Copyright (c) 2003 Aleksander Fafula <alex@fafula.com>
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

marker task-beastie.4th

only forth definitions

variable logoX
variable logoY

\ Initialize logo placement to defaults
46 logoX !
4  logoY !

\ This function draws any number of beastie logos at (loader_logo_x,
\ loader_logo_y) if defined, else (46,4) (to the right of the menu). To choose
\ your beastie, set the variable `loader_logo' to the respective logo name.
\ 
\ NOTE: Each is defined as a logo function in /boot/logo-${loader_logo}.4th
\ NOTE: If `/boot/logo-${loader_logo}.4th' does not exist or does not define
\       a `logo' function, no beastie is drawn.
\ 
: draw-beastie ( -- ) \ at (loader_logo_x,loader_logo_y), else (46,4)

	s" loader_logo_x" getenv dup -1 <> if
		?number 1 = if logoX ! then
	else drop then
	s" loader_logo_y" getenv dup -1 <> if
		?number 1 = if logoY ! then
	else drop then


	\ If `logo' is defined, execute it
	s" logo" sfind ( -- xt|0 bool ) if
		logoX @ logoY @ rot execute
	else
		\ Not defined; try-include desired logo file
		drop ( xt = 0 ) \ cruft
		s" loader_logo" getenv dup -1 = over 0= or if
			dup 0= if 2drop else drop then \ getenv result unused
			loader_color? if
				s" try-include /boot/logo-orb.4th"
			else
				s" try-include /boot/logo-orbbw.4th"
			then
		else
			2drop ( c-addr/u -- ) \ getenv result unused
			s" try-include /boot/logo-${loader_logo}.4th"
		then
		evaluate
		1 spaces

		\ Execute `logo' if defined now
		s" logo" sfind if
			logoX @ logoY @ rot execute
		else drop then
	then
;

also support-functions

: beastie-start ( -- ) \ starts the menu
	s" beastie_disable" getenv dup -1 <> if
		s" YES" compare-insensitive 0= if
			any_conf_read? if
				load_xen_throw
				load_kernel
				load_modules
			then
			exit \ to autoboot (default)
		then
	else drop then

	s" loader_delay" getenv -1 = if
		s" include /boot/menu.rc" evaluate
	else
		drop
		." Loading Menu (Ctrl-C to Abort)" cr
		s" set delay_command='include /boot/menu.rc'" evaluate
		s" set delay_showdots" evaluate
		delay_execute
	then
;

only forth definitions
