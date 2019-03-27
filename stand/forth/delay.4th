\ Copyright (c) 2008-2015 Devin Teske <dteske@FreeBSD.org>
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

marker task-delay.4th

vocabulary delay-processing
only forth also delay-processing definitions

2  constant delay_default \ Default delay (in seconds)
3  constant etx_key       \ End-of-Text character produced by Ctrl+C
13 constant enter_key     \ Carriage-Return character produce by ENTER
27 constant esc_key       \ Escape character produced by ESC or Ctrl+[

variable delay_tstart     \ state variable used for delay timing
variable delay_delay      \ determined configurable delay duration
variable delay_cancelled  \ state variable for user cancellation
variable delay_showdots   \ whether continually print dots while waiting

only forth definitions also delay-processing

: delay_execute ( -- )

	\ make sure that we have a command to execute
	s" delay_command" getenv dup -1 = if
		drop exit 
	then

	\ read custom time-duration (if set)
	s" loader_delay" getenv dup -1 = if
		drop          \ no custom duration (remove dup'd bunk -1)
		delay_default \ use default setting (replacing bunk -1)
	else
		\ make sure custom duration is a number
		?number 0= if
			delay_default \ use default if otherwise
		then
	then

	\ initialize state variables
	delay_delay !          \ stored value is on the stack from above
	seconds delay_tstart ! \ store the time we started
	0 delay_cancelled !    \ boolean flag indicating user-cancelled event

	false delay_showdots ! \ reset to zero and read from environment
	s" delay_showdots" getenv dup -1 <> if
		2drop \ don't need the value, just existence
		true delay_showdots !
	else
		drop
	then

	\ Loop until we have exceeded the desired time duration
	begin
		25 ms \ sleep for 25 milliseconds (40 iterations/sec)

		\ throw some dots up on the screen if desired
		delay_showdots @ if
			." ." \ dots visually aid in the perception of time
		then

		\ was a key depressed?
		key? if
			key \ obtain ASCII value for keystroke
			dup enter_key = if
				-1 delay_delay ! \ break loop
			then
			dup etx_key = swap esc_key = OR if
				-1 delay_delay !     \ break loop
				-1 delay_cancelled ! \ set cancelled flag
			then
		then

		\ if the time duration is set to zero, loop forever
		\ waiting for either ENTER or Ctrl-C/Escape to be pressed
		delay_delay @ 0> if
			\ calculate elapsed time
			seconds delay_tstart @ - delay_delay @ >
		else
			-1 \ break loop
		then
	until

	\ if we were throwing up dots, throw up a line-break
	delay_showdots @ if
		cr
	then

	\ did the user press either Ctrl-C or Escape?
	delay_cancelled @ if
		2drop \ we don't need the command string anymore
	else
		evaluate \ evaluate/execute the command string
 	then
;

only forth definitions
