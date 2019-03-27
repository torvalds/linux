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

marker task-brand.4th

variable brandX
variable brandY

\ Initialize brand placement to defaults
2 brandX !
1 brandY !

\ This function draws any number of company brands at (loader_brand_x,
\ loader_brand_y) if defined, or (2,1) (top-left). To choose your brand, set
\ the variable `loader_brand' to the respective brand name.
\ 
\ NOTE: Each is defined as a brand function in /boot/brand-${loader_brand}.4th
\ NOTE: If `/boot/brand-${loader_brand}.4th' does not exist or does not define
\       a `brand' function, no brand is drawn.
\ 
: draw-brand ( -- ) \ at (loader_brand_x,loader_brand_y), else (2,1)

	s" loader_brand_x" getenv dup -1 <> if
		?number 1 = if brandX ! then
	else drop then
 	s" loader_brand_y" getenv dup -1 <> if
 		?number 1 = if brandY ! then
 	else drop then

	\ If `brand' is defined, execute it
	s" brand" sfind ( -- xt|0 bool ) if
		brandX @ brandY @ rot execute
	else
		\ Not defined; try-include desired brand file
		drop ( xt = 0 ) \ cruft
		s" loader_brand" getenv dup -1 = over 0= or if
			dup 0= if 2drop else drop then \ getenv result unused
			s" try-include /boot/brand-fbsd.4th"
		else
			2drop ( c-addr/u -- ) \ getenv result unused
			s" try-include /boot/brand-${loader_brand}.4th"
		then
		evaluate
		1 spaces

		\ Execute `brand' if defined now
		s" brand" sfind if
			brandX @ brandY @ rot execute
		else drop then
	then
;
