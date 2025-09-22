name: eglob-bad-1
description:
	Check that globbing isn't done when glob has syntax error
file-setup: file 644 "abcx"
file-setup: file 644 "abcz"
file-setup: file 644 "bbc"
stdin:
	echo !([*)*
	echo +(a|b[)*
expected-stdout:
	!([*)*
	+(a|b[)*
---

name: eglob-bad-2
description:
	Check that globbing isn't done when glob has syntax error
	(at&t ksh fails this test)
file-setup: file 644 "abcx"
file-setup: file 644 "abcz"
file-setup: file 644 "bbc"
stdin:
	echo [a*(]*)z
expected-stdout:
	[a*(]*)z
---

name: eglob-infinite-plus
description:
	Check that shell doesn't go into infinite loop expanding +(...)
	expressions.
file-setup: file 644 "abc"
time-limit: 3
stdin:
	echo +()c
	echo +()x
	echo +(*)c
	echo +(*)x
expected-stdout:
	+()c
	+()x
	abc
	+(*)x
---

name: eglob-subst-1
description:
	Check that eglobbing isn't done on substitution results
file-setup: file 644 "abc"
stdin:
	x='@(*)'
	echo $x
expected-stdout:
	@(*)
---

name: eglob-nomatch-1
description:
	Check that the pattern doesn't match
stdin:
	echo 1: no-file+(a|b)stuff
	echo 2: no-file+(a*(c)|b)stuff
	echo 3: no-file+((((c)))|b)stuff
expected-stdout:
	1: no-file+(a|b)stuff
	2: no-file+(a*(c)|b)stuff
	3: no-file+((((c)))|b)stuff
---

name: eglob-match-1
description:
	Check that the pattern matches correctly
file-setup: file 644 "abd"
file-setup: file 644 "acd"
file-setup: file 644 "abac"
stdin:
	echo 1: a+(b|c)d
	echo 2: a!(@(b|B))d
	echo 3: *(a(b|c))		# (...|...) can be used within X(..)
	echo 4: a[b*(foo|bar)]d		# patterns not special inside [...]
expected-stdout:
	1: abd acd
	2: acd
	3: abac
	4: abd
---

name: eglob-case-1
description:
	Simple negation tests
stdin:
	case foo in !(foo|bar)) echo yes;; *) echo no;; esac
	case bar in !(foo|bar)) echo yes;; *) echo no;; esac
expected-stdout:
	no
	no
---

name: eglob-case-2
description:
	Simple kleene tests
stdin:
	case foo in *(a|b[)) echo yes;; *) echo no;; esac
	case foo in *(a|b[)|f*) echo yes;; *) echo no;; esac
	case '*(a|b[)' in *(a|b[)) echo yes;; *) echo no;; esac
expected-stdout:
	no
	yes
	yes
---

name: eglob-trim-1
description:
	Eglobing in trim expressions...
	(at&t ksh fails this - docs say # matches shortest string, ## matches
	longest...)
stdin:
	x=abcdef
	echo 1: ${x#a|abc}
	echo 2: ${x##a|abc}
	echo 3: ${x%def|f}
	echo 4: ${x%%f|def}
expected-stdout:
	1: bcdef
	2: def
	3: abcde
	4: abc
---

name: eglob-trim-2
description:
	Check eglobing works in trims...
stdin:
	x=abcdef
	echo 1: ${x#*(a|b)cd}
	echo 2: "${x#*(a|b)cd}"
	echo 3: ${x#"*(a|b)cd"}
	echo 4: ${x#a(b|c)}
expected-stdout:
	1: ef
	2: ef
	3: abcdef
	4: cdef
---

