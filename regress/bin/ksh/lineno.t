name: lineno-stdin
description:
	See if $LINENO is updated and can be modified.
stdin:
	echo A $LINENO
	echo B $LINENO
	LINENO=20
	echo C $LINENO
expected-stdout:
	A 1
	B 2
	C 20
---

name: lineno-inc
description:
	See if $LINENO is set for .'d files.
file-setup: file 644 "dotfile"
	echo dot A $LINENO
	echo dot B $LINENO
	LINENO=20
	echo dot C $LINENO
stdin:
	echo A $LINENO
	echo B $LINENO
	. ./dotfile
expected-stdout:
	A 1
	B 2
	dot A 1
	dot B 2
	dot C 20
---


name: lineno-func
description:
	See if $LINENO is set for commands in a function.
stdin:
	echo A $LINENO
	echo B $LINENO
	bar() {
	    echo func A $LINENO
	    echo func B $LINENO
	}
	bar
	echo C $LINENO
expected-stdout:
	A 1
	B 2
	func A 4
	func B 5
	C 8
---

name: lineno-unset
description:
	See if unsetting LINENO makes it non-magic.
file-setup: file 644 "dotfile"
	echo dot A $LINENO
	echo dot B $LINENO
stdin:
	unset LINENO
	echo A $LINENO
	echo B $LINENO
	bar() {
	    echo func A $LINENO
	    echo func B $LINENO
	}
	bar
	. ./dotfile
	echo C $LINENO
expected-stdout:
	A
	B
	func A
	func B
	dot A
	dot B
	C
---

name: lineno-unset-use
description:
	See if unsetting LINENO makes it non-magic even
	when it is re-used.
file-setup: file 644 "dotfile"
	echo dot A $LINENO
	echo dot B $LINENO
stdin:
	unset LINENO
	LINENO=3
	echo A $LINENO
	echo B $LINENO
	bar() {
	    echo func A $LINENO
	    echo func B $LINENO
	}
	bar
	. ./dotfile
	echo C $LINENO
expected-stdout:
	A 3
	B 3
	func A 3
	func B 3
	dot A 3
	dot B 3
	C 3
---

