#	$OpenBSD: read.t,v 1.2 2022/10/16 12:34:13 kn Exp $

#
# To test:
#   POSIX:
#	- if no -r, \ is escape character
#	    - \newline disappear
#	    - \<IFS> -> don't break here
#	    - \<anything-else> -> <anything-else>
#	- if -r, backslash is not special
#	- if stdin is tty and shell interactive
#	    - prompt for continuation if \newline (prompt to stderr)
#	    - a here-document isn't terminated after newline ????
#	- remaining vars set to empty string (not null)
#	- check field splitting
#	- left over fields and their separators assigned to last var
#	- exit status is normally 0
#	- exit status is > 0 on eof
#	- exit status > 0 on error
#	- signals interrupt reads
#   extra:
#	- can't change read-only variables
#	- error if var name bogus
#	- set -o allexport effects read
# ksh:
#	x check default variable: REPLY
#	- check -p, -s, -u options
#	- check var?prompt stuff
#	- "echo a b | read x y" sets x,y in parent shell (at&t)
#
name: read-IFS-1
description:
	Simple test, default IFS
stdin:
	echo "A B " > IN
	unset x y z
	read x y z < IN
	echo 1: "x[$x] y[$y] z[$z]"
	echo 1a: ${z-z not set}
	read x < IN
	echo 2: "x[$x]"
expected-stdout:
	1: x[A] y[B] z[]
	1a:
	2: x[A B]
---

name: read-ksh-1
description:
	If no var specified, REPLY is used
stdin:
	echo "abc" > IN
	read < IN
	echo "[$REPLY]";
expected-stdout:
	[abc]
---

name: signal-aborts-endless-read
description:
	Check that an endless read can be interrupted.
# XXX ^C does nothing, needs uncatchable SIGKILL to stop
expected-fail: yes
stdin:
	exec timeout --preserve-status -s INT -k 0.5s -- 0.1s $PROG -c '
		read < /dev/zero
	'
# 2/9 == INT/KILL
# XXX using signal expressions like 's == 2' only works with ksh
expected-exit: e == 128 + 2
---
