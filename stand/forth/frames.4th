\ Copyright (c) 2003 Scott Long <scottl@FreeBSD.org>
\ Copyright (c) 2012-2015 Devin Teske <dteske@FreeBSD.org>
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

marker task-frames.4th

vocabulary frame-drawing
only forth also frame-drawing definitions

\ XXX Filled boxes are left as an exercise for the reader... ;-/

variable h_el
variable v_el
variable lt_el
variable lb_el
variable rt_el
variable rb_el
variable fill

\ ASCII frames (used when serial console is detected)
 45 constant ascii_dash
 61 constant ascii_equal
124 constant ascii_pipe
 43 constant ascii_plus

\ Single frames
196 constant sh_el
179 constant sv_el
218 constant slt_el
192 constant slb_el
191 constant srt_el
217 constant srb_el
\ Double frames
205 constant dh_el
186 constant dv_el
201 constant dlt_el
200 constant dlb_el
187 constant drt_el
188 constant drb_el
\ Fillings
0 constant fill_none
32 constant fill_blank
176 constant fill_dark
177 constant fill_med
178 constant fill_bright

only forth definitions also frame-drawing

: hline	( len x y -- )	\ Draw horizontal single line
	at-xy		\ move cursor
	0 do
		h_el @ emit
	loop
;

: f_ascii ( -- )	( -- )	\ set frames to ascii
	ascii_dash h_el !
	ascii_pipe v_el !
	ascii_plus lt_el !
	ascii_plus lb_el !
	ascii_plus rt_el !
	ascii_plus rb_el !
;

: f_single	( -- )	\ set frames to single
	boot_serial? if f_ascii exit then
	sh_el h_el !
	sv_el v_el !
	slt_el lt_el !
	slb_el lb_el !
	srt_el rt_el !
	srb_el rb_el !
;

: f_double	( -- )	\ set frames to double
	boot_serial? if
		f_ascii
		ascii_equal h_el !
		exit
	then
	dh_el h_el !
	dv_el v_el !
	dlt_el lt_el !
	dlb_el lb_el !
	drt_el rt_el !
	drb_el rb_el !
;

: vline	( len x y -- )	\ Draw vertical single line
	2dup 4 pick
	0 do
		at-xy
		v_el @ emit
		1+
		2dup
	loop
	2drop 2drop drop
;

: box	( w h x y -- )	\ Draw a box
	2dup 1+ 4 pick 1- -rot
	vline		\ Draw left vert line
	2dup 1+ swap 5 pick + swap 4 pick 1- -rot
	vline		\ Draw right vert line
	2dup swap 1+ swap 5 pick 1- -rot
	hline		\ Draw top horiz line
	2dup swap 1+ swap 4 pick + 5 pick 1- -rot
	hline		\ Draw bottom horiz line
	2dup at-xy lt_el @ emit	\ Draw left-top corner
	2dup 4 pick + at-xy lb_el @ emit	\ Draw left bottom corner
	2dup swap 5 pick + swap at-xy rt_el @ emit	\ Draw right top corner
	2 pick + swap 3 pick + swap at-xy rb_el @ emit
	2drop
;

f_single
fill_none fill !

only forth definitions
