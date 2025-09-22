name: xxx-quoted-newline-1
description:
	Check that \<newline> works inside of ${}
stdin:
	abc=2
	echo ${ab\
	c}
expected-stdout:
	2
---

name: xxx-quoted-newline-2
description:
	Check that \<newline> works at the start of a here document
stdin:
	cat << EO\
	F
	hi
	EOF
expected-stdout:
	hi
---

name: xxx-quoted-newline-3
description:
	Check that \<newline> works at the end of a here document
stdin:
	cat << EOF
	hi
	EO\
	F
expected-stdout:
	hi
---

name: xxx-multi-assignment-cmd
description:
	Check that assignments in a command affect subsequent assignments
	in the same command
stdin:
	FOO=abc
	FOO=123 BAR=$FOO
	echo $BAR
expected-stdout:
	123
---

name: xxx-exec-environment-1
description:
	Check to see if exec sets it's environment correctly
stdin:
	FOO=bar exec env
expected-stdout-pattern:
	/(^|.*\n)FOO=bar\n/
---

name: xxx-exec-environment-2
description:
	Check to make sure exec doesn't change environment if a program
	isn't exec-ed
# Under os/2, _emx_sig environment variable changes.
category: !os:os2
stdin:
	env > bar1
	FOO=bar exec; env > bar2
	cmp -s bar1 bar2
---

name: quoted-brace-expansion-1
stdin:
	echo "${foo:-"a"}*"
expected-stdout:
	a*
---

name: quoted-brace-expansion-2
stdin:
	foo='bar'
	echo "${foo+(a)}*"
expected-stdout:
	(a)*
---

name: xxx-prefix-strip-1
stdin:
	foo='a cdef'
	echo ${foo#a c}
expected-stdout:
	def
---

name: xxx-prefix-strip-2
stdin:
	set a c
	x='a cdef'
	echo ${x#$*}
expected-stdout:
	def
---

name: xxx-variable-syntax-1
stdin:
	echo ${:}
expected-stderr-pattern:
	/bad substitution/
expected-exit: 1
---
