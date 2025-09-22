name: alias-1
description:
	Check that recursion is detected/avoided in aliases.
stdin:
	alias fooBar=fooBar
	fooBar
	exit 0
expected-stderr-pattern:
	/fooBar.*not found.*/
---

name: alias-2
description:
	Check that recursion is detected/avoided in aliases.
stdin:
	alias fooBar=barFoo
	alias barFoo=fooBar
	fooBar
	barFoo
	exit 0
expected-stderr-pattern:
	/fooBar.*not found.*\n.*barFoo.*not found/
---

name: alias-3
description:
	Check that recursion is detected/avoided in aliases.
stdin:
	alias Echo='echo '
	alias fooBar=barFoo
	alias barFoo=fooBar
	Echo fooBar
	unalias barFoo
	Echo fooBar
expected-stdout:
	fooBar
	barFoo
---

name: alias-4
description:
	Check that alias expansion isn't done on keywords (in keyword
	postitions).
stdin:
	alias Echo='echo '
	alias while=While
	while false; do echo hi ; done
	Echo while
expected-stdout:
	While
---

name: alias-5
description:
	Check that alias expansion done after alias with trailing space.
stdin:
	alias Echo='echo '
	alias foo='bar stuff '
	alias bar='Bar1 Bar2 '
	alias stuff='Stuff'
	alias blah='Blah'
	Echo foo blah
expected-stdout:
	Bar1 Bar2 Stuff Blah
---

name: alias-6
description:
	Check that alias expansion done after alias with trailing space.
stdin:
	alias Echo='echo '
	alias foo='bar bar'
	alias bar='Bar '
	alias blah=Blah
	Echo foo blah
expected-stdout:
	Bar Bar Blah
---

name: alias-7
description:
	Check that alias expansion done after alias with trailing space
	after a keyword.
stdin:
	alias X='case '
	alias Y=Z
	X Y in 'Y') echo is y ;; Z) echo is z ; esac
expected-stdout:
	is z
---

name: alias-8
description:
	Check that newlines in an alias don't cause the command to be lost.
stdin:
	alias foo='
	
	
	echo hi
	
	
	
	echo there
	
	
	'
	foo
expected-stdout:
	hi
	there
---

