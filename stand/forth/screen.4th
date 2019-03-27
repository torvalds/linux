\ Copyright (c) 2003 Scott Long <scottl@FreeBSD.org>
\ Copyright (c) 2015 Devin Teske <dteske@FreeBSD.org>
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

marker task-screen.4th

\ emit Esc-[
: escc ( -- ) 27 emit [char] [ emit ;

\ Home cursor ( Esc-[H )
: ho ( -- ) escc [char] H emit ;

\ Clear from current position to end of display ( Esc-[J )
: cld ( -- ) escc [char] J emit ;

\ clear screen
: clear ( -- ) ho cld ;

\ move cursor to x rows, y cols (1-based coords) ( Esc-[%d;%dH )
: at-xy ( x y -- ) escc .# [char] ; emit .# [char] H emit ;

\ Set foreground color ( Esc-[3%dm )
: fg ( x -- ) escc 3 .# .# [char] m emit ;

\ Set background color ( Esc-[4%dm )
: bg ( x -- ) escc 4 .# .# [char] m emit ;

\ Mode end (clear attributes)
: me ( -- ) escc [char] m emit ;

\ Enable bold mode ( Esc-[1m )
: b ( -- ) escc 1 .# [char] m emit ;

\ Disable bold mode ( Esc-[22m )
: -b ( -- ) escc 22 .# [char] m emit ;

\ Enable inverse foreground/background mode ( Esc-[7m )
: inv ( -- ) escc 7 .# [char] m emit ;

\ Disable inverse foreground/background mode ( Esc-[27m )
: -inv ( -- ) escc 27 .# [char] m emit ;

\ Convert all occurrences of given character (c) in string (c-addr/u) to Esc
: escc! ( c-addr/u c -- c-addr/u )
	2 pick 2 pick
	begin dup 0> while
		over c@ 3 pick = if over 27 swap c! then
		1- swap 1+ swap
	repeat
	2drop drop
;
