name: syntax-1
description:
	Check that lone ampersand is a syntax error
stdin:
	 &
expected-exit: e != 0
expected-stderr-pattern:
	/syntax error/
---

