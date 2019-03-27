\ Screen manipulation related words.
\ $FreeBSD$

marker task-screen.4th

: escc	( -- )	\ emit Esc-[
	91 27 emit emit
;

: ho	( -- )	\ Home cursor
	escc 72 emit	\ Esc-[H
;

: cld	( -- )	\ Clear from current position to end of display
	escc 74 emit	\ Esc-[J
;

: clear	( -- )	\ clear screen
	ho cld
;

: at-xy	( x y -- )	\ move cursor to x rows, y cols (1-based coords)
	escc .# 59 emit .# 72 emit	\ Esc-[%d;%dH
;

: fg	( x -- )	\ Set foreground color
	escc 3 .# .# 109 emit	\ Esc-[3%dm
;

: bg	( x -- )	\ Set background color
	escc 4 .# .# 109 emit	\ Esc-[4%dm
;

: me	( -- )	\ Mode end (clear attributes)
	escc 109 emit
;
