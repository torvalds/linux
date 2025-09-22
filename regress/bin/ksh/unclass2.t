name: xxx-subsitution-eval-order
description:
	Check order of evaluation of expressions
stdin:
	i=1 x= y=
	set -A A abc def GHI j G k
	echo ${A[x=(i+=1)]#${A[y=(i+=2)]}}
	echo $x $y
expected-stdout:
	HI
	2 4
---

name: xxx-set-option-1
description:
	Check option parsing in set
stdin:
	set -vsA foo -- A 1 3 2
	echo ${foo[*]}
expected-stderr:
	echo ${foo[*]}
expected-stdout:
	1 2 3 A
---

name: xxx-exec-1
description:
	Check that exec exits for built-ins
arguments: !-i!
stdin:
	exec print hi
	echo still herre
expected-stdout:
	hi
expected-stderr-pattern: /.*/
---

name: xxx-while-1
description:
	Check the return value of while loops
	XXX need to do same for for/select/until loops
stdin:
	i=x
	while [ $i != xxx ] ; do
	    i=x$i
	    if [ $i = xxx ] ; then
		false
		continue
	    fi
	done
	echo loop1=$?
	
	i=x
	while [ $i != xxx ] ; do
	    i=x$i
	    if [ $i = xxx ] ; then
		false
		break
	    fi
	done
	echo loop2=$?
	
	i=x
	while [ $i != xxx ] ; do
	    i=x$i
	    false
	done
	echo loop3=$?
expected-stdout:
	loop1=0
	loop2=0
	loop3=1
---

name: xxx-status-1
description:
	Check that blank lines don't clear $?
arguments: !-i!
stdin:
	(exit 1)
	echo $?
	(exit 1)
	
	echo $?
	true
expected-stdout:
	1
	1
expected-stderr-pattern: /.*/
---

name: xxx-status-2
description:
	Check that $? is preserved in subshells, includes, traps.
stdin:
	(exit 1)
	
	echo blank: $?
	
	(exit 2)
	(echo subshell: $?)
	
	echo 'echo include: $?' > foo
	(exit 3)
	. ./foo
	
	trap 'echo trap: $?' ERR
	(exit 4)
	echo exit: $?
expected-stdout:
	blank: 1
	subshell: 2
	include: 3
	trap: 4
	exit: 4
---

name: xxx-clean-chars-1
description:
	Check MAGIC character is stuffed correctly
stdin:
	echo `echo [£`
expected-stdout:
	[£
---

name: xxx-param-subst-qmark-1
description:
	Check suppresion of error message with null string.  According to
	POSIX, it shouldn't print the error as `word' isn't ommitted.
stdin:
	unset foo
	x=
	echo x${foo?$x}
expected-exit: 1
expected-fail: yes
expected-stderr-pattern: !/not set/
---

name: xxx-param-_-1
description:
	Check c flag is set.
arguments: !-c!echo "[$-]"!
expected-stdout-pattern: /^\[.*c.*\]$/
---

name: env-prompt
description:
	Check that prompt not printed when processing ENV
env-setup: !ENV=./foo!
file-setup: file 644 "foo"
	XXX=_
	PS1=X
	false && echo hmmm
arguments: !-i!
stdin:
	echo hi${XXX}there
expected-stdout:
	hi_there
expected-stderr-pattern:
	/^XX$/m
---

name: single-quotes-in-braces
description:
	Check that single quotes inside unquoted {} are treated as quotes
stdin:
	foo=1
	echo ${foo:+'blah  $foo'}
expected-stdout:
	blah  $foo
---

name: single-quotes-in-quoted-braces
description:
	Check that single quotes inside quoted {} are treated as
	normal char
stdin:
	foo=1
	echo "${foo:+'blah  $foo'}"
expected-stdout:
	'blah  1'
---

name: single-quotes-in-braces-nested
description:
	Check that single quotes inside unquoted {} are treated as quotes,
	even if that's inside a double-quoted command expansion
stdin:
	foo=1
	echo "$( echo ${foo:+'blah  $foo'})"
expected-stdout:
	blah  $foo
---

name: single-quotes-in-brace-pattern
description:
	Check that single quotes inside {} pattern are treated as quotes
stdin:
	foo=1234
	echo ${foo%'2'*} "${foo%'2'*}" ${foo%2'*'} "${foo%2'*'}"
expected-stdout:
	1 1 1234 1234
---

name: single-quotes-in-heredoc-braces
description:
	Check that single quotes inside {} in heredoc are treated
	as normal char
stdin:
	foo=1
	cat <<EOM
	${foo:+'blah  $foo'}
	EOM
expected-stdout:
	'blah  1'
---

name: single-quotes-in-nested-braces
description:
	Check that single quotes inside nested unquoted {} are
	treated as quotes
stdin:
	foo=1
	echo ${foo:+${foo:+'blah  $foo'}}
expected-stdout:
	blah  $foo
---

name: single-quotes-in-nested-quoted-braces
description:
	Check that single quotes inside nested quoted {} are treated
	as normal char
stdin:
	foo=1
	echo "${foo:+${foo:+'blah  $foo'}}"
expected-stdout:
	'blah  1'
---

name: single-quotes-in-nested-braces-nested
description:
	Check that single quotes inside nested unquoted {} are treated
	as quotes, even if that's inside a double-quoted command expansion
stdin:
	foo=1
	echo "$( echo ${foo:+${foo:+'blah  $foo'}})"
expected-stdout:
	blah  $foo
---

name: single-quotes-in-nested-brace-pattern
description:
	Check that single quotes inside nested {} pattern are treated as quotes
stdin:
	foo=1234
	echo ${foo:+${foo%'2'*}} "${foo:+${foo%'2'*}}" ${foo:+${foo%2'*'}} "${foo:+${foo%2'*'}}"
expected-stdout:
	1 1 1234 1234
---

name: single-quotes-in-heredoc-nested-braces
description:
	Check that single quotes inside nested {} in heredoc are treated
	as normal char
stdin:
	foo=1
	cat <<EOM
	${foo:+${foo:+'blah  $foo'}}
	EOM
expected-stdout:
	'blah  1'
---
