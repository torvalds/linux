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
	." 1.  Start FreeBSD with /boot/stable.conf."
        20 8 at-xy
        ." 2.  Start FreeBSD with /boot/current.conf."
	20 9 at-xy
	." 3.  Start FreeBSD with standard configuration. "
	20 10 at-xy
	." 4.  Reboot."
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
	20 12 at-xy
	." Enter your option (1,2,3,4): "
	10 tkey
	dup 32 = if
	    drop key
	then
	dup 0< if
	    drop 51
	then
	dup emit
	me
;

: help_text
        10 18 at-xy ." * Choose 1 or 2 to run special configuration file."
	10 19 at-xy ." * Choose 3 to proceed with standard bootstrapping."
	12 20 at-xy ." See '?' for available commands, and 'words' for"
	12 21 at-xy ." complete list of Forth words."
	10 22 at-xy ." * Choose 4 in order to warm boot your machine."
;

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
			." Loading /boot/stable.conf. Please wait..." cr
			s" /boot/stable.conf" read-conf
			0 boot-conf exit
		then
		dup 50 = if
			drop
			1 25 at-xy cr
			." Loading /boot/current.conf. Please wait..." cr
			s" /boot/current.conf" read-conf
			0 boot-conf exit
		then
		dup 51 = if
			drop
			1 25 at-xy cr
			." Proceeding with standard boot. Please wait..." cr
			0 boot-conf exit
		then
		dup 52 = if
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

