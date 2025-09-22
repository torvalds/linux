#	$OpenBSD: regress.t,v 1.4 2018/04/24 10:27:25 kn Exp $

#
# The first 39 of these tests are from the old Bugs script.
#

name: regression-1
description:
	Lex array code had problems with this.
stdin:
	echo foo[
	n=bar
	echo "hi[ $n ]=1"
expected-stdout:
	foo[
	hi[ bar ]=1
---


name: regression-2
description:
	When PATH is set before running a command, the new path is
	not used in doing the path search
		$ echo echo hi > /tmp/q ; chmod a+rx /tmp/q
		$ PATH=/tmp q
		q: not found
		$
	in comexec() the two lines
		while (*vp != NULL)
			(void) typeset(*vp++, xxx, 0);
	need to be moved out of the switch to before findcom() is
	called - I don't know what this will break.
stdin:
	: ${PWD:-`pwd 2> /dev/null`}
	: ${PWD:?"PWD not set - can't do test"}
	mkdir Y
	cat > Y/xxxscript << EOF
	#!/bin/sh
	# Need to restore path so echo can be found (some shells don't have
	# it as a built-in)
	PATH=\$OLDPATH
	echo hi
	exit 0
	EOF
	chmod a+rx Y/xxxscript
	export OLDPATH="$PATH"
	PATH=$PWD/Y xxxscript
	exit $?
expected-stdout:
	hi
---


#
#   3. Sun OS 4.0.x (This seems to be a problem with sun's PENDIN not being done
#      properly)
#   	sleep 5^J ls^J ls^J ls		[only first ls runs]
#   	vi ... ZZ (while waiting type)	[some of the input gets eaten]
#      [not present in SunOS 4.1.x]
#echo "  [No automatic test for bug 3 - interactive]"


#
#   4. (fixed)
#
#echo "  [Don't know what bug 4 was]"


#
#   5. Everywhere
#   	File name completion (^X,*) does not mesh well with cd and
#   	symbolic links.  cd does path simplification wrt $PWD before
#   	doing the actual chdir(), while file name completion does
#   	not do the simplification. E.g., you are in directory A
#   	which has a symbolic link to directory B, you create a file
#   	called foobar and you then cd to the symlink to B, and type
#   		$ echo ../foo^X
#   	and the shell beeps at you.  Would be more consistent to
#   	do the completion after simplifing the `$PWD/..'.
#echo "  [No automatic test for bug 5 - interactive]"


name: regression-6
description:
	Parsing of $(..) expressions is non-optimal.  It is
	impossible to have any parentheses inside the expression.
	I.e.,
		$ ksh -c 'echo $(echo \( )'
		no closing quote
		$ ksh -c 'echo $(echo "(" )'
		no closing quote
		$
	The solution is to hack the parsing clode in lex.c, the
	question is how to hack it: should any parentheses be
	escaped by a backslash, or should recursive parsing be done
	(so quotes could also be used to hide hem).  The former is
	easier, the later better...
stdin:
	echo $(echo \()
expected-stdout:
	(
---


#
#   7. (fixed)
#
#echo "  [Don't know what bug 7 was]"


#
#   8. Everywhere - NOT A BUG - this is what at&t ksh88 does
#   	Strange typset -x behaviour in functions.  The following function
#   	does not set the environment variable BLAH outside the function:
#   		function blah
#   		{
#   			typeset -x BLAH=foobar
#   		}
#   	This function does work:
#   		function blah
#   		{ BLAH=foobar; export BLAH
#   		}
#echo '  [Bug 8 was bogus]'


name: regression-9
description:
	Continue in a for loop does not work right:
		for i in a b c ; do
			if [ $i = b ] ; then
				continue
			fi
			echo $i
		done
	Prints a forever...
stdin:
	first=yes
	for i in a b c ; do
		if [ $i = b ] ; then
			if [ $first = no ] ; then
				echo 'continue in for loop broken'
				break	# hope break isn't broken too :-)
			fi
			first=no
			continue
		fi
	done
	echo bye
expected-stdout:
	bye
---


name: regression-10
description:
	The following:
		set -- `false`
		echo $?
	shoud not print 0. (according to /bin/sh, at&t ksh88, and the
	getopt(1) man page - not according to POSIX)
stdin:
	set -- `false`
	echo $?
expected-stdout:
	1
---


name: regression-11
description:
	The following:
		x=/foo/bar/blah
		echo ${x##*/}
	should echo blah but on some machines echos /foo/bar/blah.
stdin:
	x=/foo/bar/blah
	echo ${x##*/}
expected-stdout:
	blah
---


name: regression-12
description:
	Both of the following echos produce the same output under sh/ksh.att:
		#!/bin/sh
		x="foo	bar"
		echo "`echo \"$x\"`"
		echo "`echo "$x"`"
	pdksh produces different output for the former (foo instead of foo\tbar)
stdin:
	x="foo	bar"
	echo "`echo \"$x\"`"
	echo "`echo "$x"`"
expected-stdout:
	foo	bar
	foo	bar
---


name: regression-13
description:
	The following command hangs forever:
		$ (: ; cat /etc/termcap) | sleep 2
	This is because the shell forks a shell to run the (..) command
	and this shell has the pipe open.  When the sleep dies, the cat
	doesn't get a SIGPIPE 'cause a process (ie, the second shell)
	still has the pipe open.
	
	NOTE: this test provokes a bizarre bug in ksh93 (shell starts reading
	      commands from /etc/termcap..)
time-limit: 10
stdin:
	echo A line of text that will be duplicated quite a number of times.> t1
	cat t1 t1 t1 t1  t1 t1 t1 t1  t1 t1 t1 t1  t1 t1 t1 t1  > t2
	cat t2 t2 t2 t2  t2 t2 t2 t2  t2 t2 t2 t2  t2 t2 t2 t2  > t1
	cat t1 t1 t1 t1 > t2
	(: ; cat t2) | sleep 1
---


name: regression-14
description:
	The command
		$ (foobar) 2> /dev/null
	generates no output under /bin/sh, but pdksh produces the error
		foobar: not found
	Also, the command
		$ foobar 2> /dev/null
	generates an error under /bin/sh and pdksh, but at&t ksh88 produces
	no error (redirected to /dev/null).
stdin:
	(you/should/not/see/this/error/1) 2> /dev/null
	you/should/not/see/this/error/2 2> /dev/null
	true
---


name: regression-15
description:
	The command
		$ whence foobar
	generates a blank line under pdksh and sets the exit status to 0.
	at&t ksh88 generates no output and sets the exit status to 1.  Also,
	the command
		$ whence foobar cat
	generates no output under at&t ksh88 (pdksh generates a blank line
	and /bin/cat).
stdin:
	whence does/not/exist > /dev/null
	echo 1: $?
	echo 2: $(whence does/not/exist | wc -l)
	echo 3: $(whence does/not/exist cat | wc -l)
expected-stdout:
	1: 1
	2: 0
	3: 0
---


name: regression-16
description:
	${var%%expr} seems to be broken in many places.  On the mips
	the commands
		$ read line < /etc/passwd
		$ echo $line
		root:0:1:...
		$ echo ${line%%:*}
		root
		$ echo $line
		root
		$
	change the value of line.  On sun4s & pas, the echo ${line%%:*} doesn't
	work.  Haven't checked elsewhere...
script:
	read x
	y=$x
	echo ${x%%:*}
	echo $x
stdin:
	root:asdjhasdasjhs:0:1:Root:/:/bin/sh
expected-stdout:
	root
	root:asdjhasdasjhs:0:1:Root:/:/bin/sh
---


name: regression-17
description:
	The command
		. /foo/bar
	should set the exit status to non-zero (sh and at&t ksh88 do).
	XXX doting a non existent file is a fatal error for a script
stdin:
	. does/not/exist
expected-exit: e != 0
expected-stderr-pattern: /.?/
---


#
#   18. Everywhere
#   	In vi mode ^X (and *) can dump core:
#   		$ ab[cd^XMemory fault (core dumped)
#echo "  [No automatic test for bug 18 - interactive]"


name: regression-19
description:
	Both of the following echos should produce the same thing, but don't:
		$ x=foo/bar
		$ echo ${x%/*}
		foo
		$ echo "${x%/*}"
		foo/bar
stdin:
	x=foo/bar
	echo "${x%/*}"
expected-stdout:
	foo
---


#
#   20. (same as 18)
#


name: regression-21
description:
	backslash does not work as expected in case labels:
	$ x='-x'
	$ case $x in
	-\?) echo hi
	esac
	hi
	$ x='-?'
	$ case $x in
	-\\?) echo hi
	esac
	hi
	$
stdin:
	case -x in
	-\?)	echo fail
	esac
---


name: regression-22
description:
	Quoting backquotes inside backquotes doesn't work:
	$ echo `echo hi \`echo there\` folks`
	asks for more info.  sh and at&t ksh88 both echo
	hi there folks
stdin:
	echo `echo hi \`echo there\` folks`
expected-stdout:
	hi there folks
---


name: regression-23
description:
	)) is not treated `correctly':
	    $ (echo hi ; (echo there ; echo folks))
	    missing ((
	    $
	instead of (as sh and ksh.att)
	    $ (echo hi ; (echo there ; echo folks))
	    hi
	    there
	    folks
	    $
stdin:
	( : ; ( : ; echo hi))
expected-stdout:
	hi
---


#
#   24. strangeness with file name completion involving symlinks to nowhere
#   	$ mkdir foo foo/bar
#   	$ ln -s /stuff/junk foo/bar/xx
#   	$ echo foo/*/xx 
#	(beep)
#   	$
#echo "  [No automatic test for bug 24 - interactive]"


name: regression-25
description:
	Check reading stdin in a while loop.  The read should only read
	a single line, not a whole stdio buffer; the cat should get
	the rest.
stdin:
	(echo a; echo b) | while read x ; do
	    echo $x
	    cat > /dev/null
	done
expected-stdout:
	a
---


name: regression-26
description:
	Check reading stdin in a while loop.  The read should read both
	lines, not just the first.
script:
	a=
	while [ "$a" != xxx ] ; do
	    last=$x
	    read x
	    cat /dev/null | sed 's/x/y/'
	    a=x$a
	done
	echo $last
stdin:
	a
	b
expected-stdout:
	b
---


name: regression-27
description:
	The command
		. /does/not/exist
	should cause a script to exit.
stdin:
	. does/not/exist
	echo hi
expected-exit: e != 0
expected-stderr-pattern: /does\/not\/exist/
---


name: regression-28
description:
	variable assignements not detected well
stdin:
	a.x=1 echo hi
expected-exit: e != 0
expected-stderr-pattern: /a\.x=1/
---


name: regression-29
description:
	alias expansion different from at&t ksh88
stdin:
	alias a='for ' b='i in'
	a b hi ; do echo $i ; done
expected-stdout:
	hi
---


name: regression-30
description:
	strange characters allowed inside ${...}
stdin:
	echo ${a{b}}
expected-exit: e != 0
expected-stderr-pattern: /.?/
---


name: regression-31
description:
	Does read handle partial lines correctly
script:
	a= ret=
	while [ "$a" != xxx ] ; do
	    read x y z
	    ret=$?
	    a=x$a
	done
	echo "[$x]"
	echo $ret
stdin: !
	a A aA
	b B Bb
	c
expected-stdout:
	[c]
	1
---


name: regression-32
description:
	Does read set variables to null at eof?
script:
	a=
	while [ "$a" != xxx ] ; do
	    read x y z
	    a=x$a
	done
	echo 1: ${x-x not set} ${y-y not set} ${z-z not set} 
	echo 2: ${x:+x not null} ${y:+y not null} ${z:+z not null}
stdin:
	a A Aa
	b B Bb
expected-stdout:
	1:
	2:
---


name: regression-33
description:
	Does umask print a leading 0 when umask is 3 digits?
stdin:
	umask 222
	umask
expected-stdout:
	0222
---


#
#
#	Does umask print a umask of 0 sanely?
#	There is lots of variety here (0, 00, 000, and 0000 have all been
#	seen in various shells...)
#
#echo '  [Bug 34 was bogus]'


name: regression-35
description:
	Tempory files used for here-docs in functions get trashed after
	the function is parsed (before it is executed)
stdin:
	f1() {
		cat <<- EOF
			F1
		EOF
		f2() {
			cat <<- EOF
				F2
			EOF
		}
	}
	f1
	f2
	unset -f f1
	f2
expected-stdout:
	F1
	F2
	F2
---


name: regression-36
description:
	Command substitution breaks reading in while loop
	(test from <sjg@void.zen.oz.au>)
stdin:
	(echo abcdef; echo; echo 123) | 
	    while read line
	    do
	      # the following line breaks it
	      c=`echo $line | wc -c`
	      echo $c
	    done
expected-stdout:
	7
	1
	4
---


name: regression-37
description:
	Machines with broken times() (reported by <sjg@void.zen.oz.au>)
	time does not report correct real time
stdin:
	time sleep 1
expected-stderr-pattern: !/^\s*0\.0[\s\d]+real|^\s*real[\s]+0+\.0/
---


name: regression-38
description:
	set -e doesn't ignore exit codes for if/while/until/&&/||/!.
arguments: !-e!
stdin:
	if false; then echo hi ; fi
	false || true
	false && true
	while false; do echo hi; done 
	echo ok
expected-stdout:
	ok
---

name: regression-39
description:
	set -e: errors in command substitutions aren't ignored
arguments: !-e!
stdin:
	echo `false; echo hi` $(< this-file-does-not-exist)
expected-stdout:
	
expected-stderr-pattern: /this-file-does-not-exist/
---

name: regression-40
description:
	This used to cause a core dump
env-setup: !RANDOM=12!
stdin:
	echo hi
expected-stdout:
	hi
---

name: regression-41
description:
	foo should be set to bar (should not be empty)
stdin:
	foo=`
	echo bar`
	echo "($foo)"
expected-stdout:
	(bar)
---

name: regression-42
description:
	Can't use command line assignments to assign readonly parameters.
stdin:
	foo=bar
	readonly foo
	foo=stuff env | grep '^foo'
expected-exit: e != 0
expected-stderr-pattern:
	/.*read *only.*/
---

name: regression-43
description:
	Can subshells be prefixed by redirections (historical shells allow
	this)
stdin:
	< /dev/null (sed 's/^/X/')
---

name: regression-44
description:
	getopts sets OPTIND correctly for unparsed option
stdin:
	set -- -a -a -x
	while getopts :a optc; do
	    echo "OPTARG=$OPTARG, OPTIND=$OPTIND, optc=$optc."
	done
	echo done
expected-stdout:
	OPTARG=, OPTIND=2, optc=a.
	OPTARG=, OPTIND=3, optc=a.
	OPTARG=x, OPTIND=3, optc=?.
	done
---

name: regression-45
description:
	Parameter assignments with [] recognized correctly
stdin:
	FOO=*[12]
	BAR=abc[
	MORE=[abc]
	JUNK=a[bc
	echo "<$FOO>"
	echo "<$BAR>"
	echo "<$MORE>"
	echo "<$JUNK>"
expected-stdout:
	<*[12]>
	<abc[>
	<[abc]>
	<a[bc>
---

name: regression-46
description:
	Check that alias expansion works in command substitutions and
	at the end of file.
stdin:
	alias x='echo hi'
	FOO="`x` "
	echo "[$FOO]"
	x
expected-stdout:
	[hi ]
	hi
---

name: regression-47
description:
	Check that aliases are fully read.
stdin:
	alias x='echo hi;
	echo there'
	x
	echo done
expected-stdout:
	hi
	there
	done
---

name: regression-48
description:
	Check that (here doc) temp files are not left behind after an exec.
stdin:
	mkdir foo || exit 1
	TMPDIR=$PWD/foo $0 <<- 'EOF'
		x() {
			sed 's/^/X /' << E_O_F
			hi
			there
			folks
			E_O_F
			echo "done ($?)"
		}
		echo=echo; [ -x /bin/echo ] && echo=/bin/echo
		exec $echo subtest-1 hi
	EOF
	echo subtest-1 foo/*
	TMPDIR=$PWD/foo $0 <<- 'EOF'
		echo=echo; [ -x /bin/echo ] && echo=/bin/echo
		sed 's/^/X /' << E_O_F; exec $echo subtest-2 hi
		a
		few
		lines
		E_O_F
	EOF
	echo subtest-2 foo/*
expected-stdout:
	subtest-1 hi
	subtest-1 foo/*
	X a
	X few
	X lines
	subtest-2 hi
	subtest-2 foo/*
---

name: regression-49
description:
	Check that unset params with attributes are reported by set, those
	sans attributes are not.
stdin:
	unset FOO BAR
	echo X$FOO
	export BAR
	typeset -i BLAH
	set | grep FOO
	set | grep BAR
	set | grep BLAH
expected-stdout:
	X
	BAR
	BLAH
---

name: regression-50
description:
	Check that aliases do not use continuation prompt after trailing
	semi-colon.
file-setup: file 644 "env"
	PS1=Y
	PS2=X
env-setup: !ENV=./env!
arguments: !-i!
stdin:
	alias foo='echo hi ; '
	foo
	foo echo there
expected-stdout: 
	hi
	hi
	there
expected-stderr-pattern:
	/^YYYY$/m
---

name: regression-51
description:
	Check that set allows both +o and -o options on same command line.
stdin:
	set a b c
	set -o noglob +o allexport
	echo A: $*, *
expected-stdout: 
	A: a b c, *
---

name: regression-52
description:
	Check that globing works in pipelined commands
file-setup: file 644 "env"
	PS1=P
file-setup: file 644 "abc"
	stuff
env-setup: !ENV=./env!
arguments: !-i!
stdin:
	sed 's/^/X /' < ab*
	echo mark 1
	sed 's/^/X /' < ab* | sed 's/^/Y /'
	echo mark 2
expected-stdout: 
	X stuff
	mark 1
	Y X stuff
	mark 2
expected-stderr-pattern:
	/^PPPPP$/m
---

name: regression-53
description:
	Check that getopts works in functions
stdin:
	#!/bin/ksh
	
	bfunc() {
	    echo bfunc: enter "(args: $*; OPTIND=$OPTIND)"
	    while getopts B oc; do
		case $oc in
		  (B)
		    echo bfunc: B option
		    ;;
		  (*)
		    echo bfunc: odd option "($oc)"
		    ;;
		esac
	    done
	    echo bfunc: leave
	}
	
	function kfunc {
	    echo kfunc: enter "(args: $*; OPTIND=$OPTIND)"
	    while getopts K oc; do
		case $oc in
		  (K)
		    echo kfunc: K option
		    ;;
		  (*)
		    echo bfunc: odd option "($oc)"
		    ;;
		esac
	    done
	    echo kfunc: leave
	}
	
	set -- -f -b -k -l
	echo "line 1: OPTIND=$OPTIND"
	getopts kbfl optc
	echo "line 2: ret=$?, optc=$optc, OPTIND=$OPTIND"
	bfunc -BBB blah
	echo "line 3: OPTIND=$OPTIND"
	getopts kbfl optc
	echo "line 4: ret=$?, optc=$optc, OPTIND=$OPTIND"
	kfunc -KKK blah
	echo "line 5: OPTIND=$OPTIND"
	getopts kbfl optc
	echo "line 6: ret=$?, optc=$optc, OPTIND=$OPTIND"
	echo
	
	OPTIND=1
	set -- -fbkl
	echo "line 10: OPTIND=$OPTIND"
	getopts kbfl optc
	echo "line 20: ret=$?, optc=$optc, OPTIND=$OPTIND"
	bfunc -BBB blah
	echo "line 30: OPTIND=$OPTIND"
	getopts kbfl optc
	echo "line 40: ret=$?, optc=$optc, OPTIND=$OPTIND"
	kfunc -KKK blah
	echo "line 50: OPTIND=$OPTIND"
	getopts kbfl optc
	echo "line 60: ret=$?, optc=$optc, OPTIND=$OPTIND"
expected-stdout: 
	line 1: OPTIND=1
	line 2: ret=0, optc=f, OPTIND=2
	bfunc: enter (args: -BBB blah; OPTIND=2)
	bfunc: B option
	bfunc: B option
	bfunc: leave
	line 3: OPTIND=2
	line 4: ret=0, optc=b, OPTIND=3
	kfunc: enter (args: -KKK blah; OPTIND=1)
	kfunc: K option
	kfunc: K option
	kfunc: K option
	kfunc: leave
	line 5: OPTIND=3
	line 6: ret=0, optc=k, OPTIND=4
	
	line 10: OPTIND=1
	line 20: ret=0, optc=f, OPTIND=2
	bfunc: enter (args: -BBB blah; OPTIND=2)
	bfunc: B option
	bfunc: B option
	bfunc: leave
	line 30: OPTIND=2
	line 40: ret=1, optc=?, OPTIND=2
	kfunc: enter (args: -KKK blah; OPTIND=1)
	kfunc: K option
	kfunc: K option
	kfunc: K option
	kfunc: leave
	line 50: OPTIND=2
	line 60: ret=1, optc=?, OPTIND=2
---


name: regression-54
description:
	Check that ; is not required before the then in if (( ... )) then ...
stdin:
	if (( 1 )) then
	    echo ok dparen
	fi
	if [[ -n 1 ]] then
	    echo ok dbrackets
	fi
expected-stdout: 
	ok dparen
	ok dbrackets
---


name: regression-55
description:
	Check ${foo:%bar} is allowed (ksh88 allows it...)
stdin:
	x=fooXbarXblah
	echo 1 ${x%X*}
	echo 2 ${x:%X*}
	echo 3 ${x%%X*}
	echo 4 ${x:%%X*}
	echo 5 ${x#*X}
	echo 6 ${x:#*X}
	echo 7 ${x##*X}
	echo 8 ${x:##*X}
expected-stdout: 
	1 fooXbar
	2 fooXbar
	3 foo
	4 foo
	5 barXblah
	6 barXblah
	7 blah
	8 blah
---


name: regression-56
description:
	Check eval vs substitution exit codes
	(this is what ksh88 does)
stdin:
	eval $(false)
	echo A $?
	eval ' $(false)'
	echo B $?
	eval " $(false)"
	echo C $?
	eval "eval $(false)"
	echo D $?
	eval 'eval '"$(false)"
	echo E $?
	IFS="$IFS:"
	eval $(echo :; false)
	echo F $?
expected-stdout: 
	A 1
	B 1
	C 1
	D 0
	E 0
	F 1
---

name: regression-57
description:
	Check if typeset output is correct for
	uninitialized array elements.
stdin:
	typeset -i xxx[4]
	echo A
	typeset -i | grep xxx | sed 's/^/    /'
	echo B
	typeset | grep xxx | sed 's/^/    /'
	
	xxx[1]=2+5
	echo M
	typeset -i | grep xxx | sed 's/^/    /'
	echo N
	typeset | grep xxx | sed 's/^/    /'
expected-stdout: 
	A
	    xxx
	B
	    typeset -i xxx
	M
	    xxx[1]=7
	N
	    typeset -i xxx
---

name: regression-58
description:
	Check if trap exit is ok (exit not mistaken for signal name)
stdin:
	trap 'echo hi' exit
	trap exit 1
expected-stdout: 
	hi
---

name: regression-59
description:
	Check if ${#array[*]} is calculated correctly.
stdin:
	a[12]=hi
	a[8]=there
	echo ${#a[*]}
expected-stdout: 
	2
---

name: regression-60
description:
	Check if default exit status is previous command
stdin:
	(true; exit)
	echo A $?
	(false; exit)
	echo B $?
	( (exit 103) ; exit)
	echo C $?
expected-stdout: 
	A 0
	B 1
	C 103
---

name: regression-61
description:
	Check if EXIT trap is executed for sub shells.
stdin:
	trap 'echo parent exit' EXIT
	echo start
	(echo A; echo A last)
	echo B
	(echo C; trap 'echo sub exit' EXIT; echo C last)
	echo parent last
expected-stdout: 
	start
	A
	A last
	B
	C
	C last
	sub exit
	parent last
	parent exit
---

name: regression-62
description:
	Check if test -nt/-ot succeeds if second(first) file is missing.
stdin:
	touch a
	test a -nt b && echo nt OK || echo nt BAD
	test b -ot a && echo ot OK || echo ot BAD
expected-stdout: 
	nt OK
	ot OK
---

name: regression-63
description:
	Check octal escape expansion in prompts.
file-setup: file 644 "env"
	PS1=\\131
	PS2=\\130
env-setup: !ENV=./env!
arguments: !-i!
stdin:
	echo hello
	for i in 1 2 3; do
		echo there
	done
expected-stdout: 
	hello
	there
	there
	there
expected-stderr-pattern:
	/^YYXXY$/m
---

name: regression-64
description:
	Check whether time keeps the pipeline's stack intact.
stdin:
	time for i in _ ; do echo body ; done
expected-stdout:
	body
expected-stderr-pattern:
	!/^\s*0\.0[\s\d]+real|^\s*real[\s]+0+\.0/
---
