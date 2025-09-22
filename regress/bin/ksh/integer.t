name: integer-base-err-1
description:
	Can't have 0 base (causes shell to exit)
expected-exit: e != 0
stdin:
	typeset -i i
	i=3
	i=0#4
	echo $i
expected-stderr-pattern:
	/^.*:.*0#4.*\n$/
---

name: integer-base-err-2
description:
	Can't have multiple bases in a `constant' (causes shell to exit)
	(ksh88 fails this test)
expected-exit: e != 0
stdin:
	typeset -i i
	i=3
	i=2#110#11
	echo $i
expected-stderr-pattern:
	/^.*:.*2#110#11.*\n$/
---

name: integer-base-err-3
description:
	Syntax errors in expressions and effects on bases
	(interactive so errors don't cause exits)
	(ksh88 fails this test - shell exits, even with -i)
arguments: !-i!
stdin:
	PS1= # minimize prompt hassles
	typeset -i4 a=10
	typeset -i a=2+
	echo $a
	typeset -i4 a=10
	typeset -i2 a=2+
	echo $a
expected-stderr-pattern:
	/^([#\$] )?.*:.*2+.*\n.*:.*2+.*\n$/m
expected-stdout:
	4#22
	4#22
---

name: integer-base-err-4
description:
	Are invalid digits (according to base) errors?
	(ksh93 fails this test)
expected-exit: e != 0
stdin:
	typeset -i i;
	i=3#4
expected-stderr-pattern:
	/^([#\$] )?.*:.*3#4.*\n$/
---


name: integer-base-1
description:
	Missing number after base is treated as 0.
stdin:
	typeset -i i
	i=3
	i=2#
	echo $i
expected-stdout:
	0
---

name: integer-base-2
description:
	Check `stickyness' of base in various situations
stdin:
	typeset -i i=8
	echo $i
	echo ---------- A
	typeset -i4 j=8
	echo $j
	echo ---------- B
	typeset -i k=8
	typeset -i4 k=8
	echo $k
	echo ---------- C
	typeset -i4 l
	l=3#10
	echo $l
	echo ---------- D
	typeset -i m
	m=3#10
	echo $m
	echo ---------- E
	n=2#11
	typeset -i n
	echo $n
	n=10
	echo $n
	echo ---------- F
	typeset -i8 o=12
	typeset -i4 o
	echo $o
	echo ---------- G
	typeset -i p
	let p=8#12
	echo $p
expected-stdout:
	8
	---------- A
	4#20
	---------- B
	4#20
	---------- C
	4#3
	---------- D
	3#10
	---------- E
	2#11
	2#1010
	---------- F
	4#30
	---------- G
	8#12
---

name: integer-base-3
description:
	More base parsing (hmm doesn't test much..)
stdin:
	typeset -i aa
	aa=1+12#10+2
	echo $aa
	typeset -i bb
	bb=1+$aa
	echo $bb
	typeset -i bb
	bb=$aa
	echo $bb
	typeset -i cc
	cc=$aa
	echo $cc
expected-stdout:
	15
	16
	15
	15
---

name: integer-base-4
description:
	Check that things not declared as integers are not made integers,
	also, check if base is not reset by -i with no arguments.
	(ksh93 fails - prints 10#20 - go figure)
stdin:
	xx=20
	let xx=10
	typeset -i | grep '^xx='
	typeset -i4 a=10
	typeset -i a=20
	echo $a
expected-stdout:
	4#110
---

name: integer-base-5
description:
	More base stuff
stdin:
	typeset -i4 a=3#10
	echo $a
	echo --
	typeset -i j=3
	j=~3
	echo $j
	echo --
	typeset -i k=1
	x[k=k+1]=3
	echo $k
	echo --
	typeset -i l
	for l in 1 2+3 4; do echo $l; done
expected-stdout:
	4#3
	--
	-4
	--
	2
	--
	1
	5
	4
---

name: integer-base-6
description:
	Even more base stuff
	(ksh93 fails this test - prints 0)
stdin:
	typeset -i7 i
	i=
	echo $i
expected-stdout:
	7#0
---

name: integer-base-7
description:
	Check that non-integer parameters don't get bases assigned
stdin:
	echo $(( zz = 8#100 ))
	echo $zz
expected-stdout:
	64
	64
---

name: integer-1
description:
	Check that 64 bit integers get assigned
stdin:
	echo $(( zz = 0x7fffffffffffffff))
	echo $zz
expected-stdout:
	9223372036854775807
	9223372036854775807
---

