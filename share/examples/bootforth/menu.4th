\ Simple greeting screen, presenting basic options.
\ XXX This is far too trivial - I don't have time now to think
\ XXX about something more fancy... :-/
\ $FreeBSD$

: title
	f_single
	60 11 10 4 box
	29 4 at-xy 15 fg 7 bg
	." Welcome to BootFORTH!"
	me
;

: menu
	2 fg
	20 7 at-xy 
	." 1.  Start FreeBSD /kernel."
	20 8 at-xy
	." 2.  Interact with BootFORTH."
	20 9 at-xy
	." 3.  Reboot."
	me
;

: tkey	( d -- flag | char )
	seconds +
	begin 1 while
	    dup seconds u< if
		drop
		-1
		exit
	    then
	    key? if
		drop
		key
		exit
	    then
	repeat
;

: prompt
	14 fg
	20 11 at-xy
	." Enter your option (1,2,3): "
	10 tkey
	dup 32 = if
	    drop key
	then
	dup 0< if
	    drop 49
	then
	dup emit
	me
;

: help_text
	10 18 at-xy ." * Choose 1 if you just want to run FreeBSD."
	10 19 at-xy ." * Choose 2 if you want to use bootloader facilities."
	12 20 at-xy ." See '?' for available commands, and 'words' for"
	12 21 at-xy ." complete list of Forth words."
	10 22 at-xy ." * Choose 3 in order to warm boot your machine."
;

: (boot) 0 boot ;
: (reboot) 0 reboot ;

: main_menu
	begin 1 while
		clear
		f_double
		79 23 1 1 box
		title
		menu
		help_text
		prompt
		cr cr cr
		dup 49 = if
			drop
			1 25 at-xy cr
			." Loading kernel. Please wait..." cr
			['] (boot) catch abort" Error booting"
		then
		dup 50 = if
			drop
			1 25 at-xy cr
			exit
		then
		dup 51 = if
			drop
			1 25 at-xy cr
			['] (reboot) catch abort" Error rebooting"
		then
		20 12 at-xy
		." Key " emit ."  is not a valid option!"
		20 13 at-xy
		." Press any key to continue..."
		key drop
	repeat
;
