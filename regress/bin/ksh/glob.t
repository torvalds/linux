name: glob-bad-1
description:
	Check that globbing isn't done when glob has syntax error
file-setup: dir 755 "[x"
file-setup: file 644 "[x/foo"
stdin:
	echo [*
	echo *[x
	echo [x/*
expected-stdout:
	[*
	*[x
	[x/foo
---

name: glob-bad-2
description:
	Check that symbolic links aren't stat()'d
category: !os:os2
file-setup: dir 755 "dir"
file-setup: symlink 644 "dir/abc"
	non-existent-file
stdin:
	echo d*/*
	echo d*/abc
expected-stdout:
	dir/abc
	dir/abc
---

name: glob-range-1
description:
	Test range matching
file-setup: file 644 ".bc"
file-setup: file 644 "abc"
file-setup: file 644 "bbc"
file-setup: file 644 "cbc"
file-setup: file 644 "-bc"
stdin:
	echo [ab-]*
	echo [-ab]*
	echo [!-ab]*
	echo [!ab]*
	echo []ab]*
expected-stdout:
	-bc abc bbc
	-bc abc bbc
	cbc
	-bc cbc
	abc bbc
---

name: glob-range-2
description:
	Test range matching
	(at&t ksh fails this; POSIX says invalid)
file-setup: file 644 "abc"
stdin:
	echo [a--]*
expected-stdout:
	[a--]*
---

name: glob-range-3
description:
	Check that globbing matches the right things...
file-setup: file 644 "aÂc"
stdin:
	echo a[Á-Ú]*
expected-stdout:
	aÂc
---

name: glob-range-4
description:
	Results unspecified according to POSIX
file-setup: file 644 ".bc"
stdin:
	echo [a.]*
expected-stdout:
	[a.]*
---

name: glob-range-5
description:
	Results unspecified according to POSIX
	(at&t ksh treats this like [a-cc-e]*)
file-setup: file 644 "abc"
file-setup: file 644 "bbc"
file-setup: file 644 "cbc"
file-setup: file 644 "dbc"
file-setup: file 644 "ebc"
file-setup: file 644 "-bc"
stdin:
	echo [a-c-e]*
expected-stdout:
	-bc abc bbc cbc ebc
---

name: glob-charclass-1
description:
	Check POSIX character class support
file-setup: file 644 "abc"
file-setup: file 644 "1bc"
file-setup: file 644 "@bc"
file-setup: file 644 "a.c"
stdin:
	echo [[:alnum:]]*
	echo *[[:punct:]]c
expected-stdout:
	1bc a.c abc
	a.c

---

name: glob-charclass-2
description:
	Check POSIX character class support (negative match)
file-setup: file 644 "abc"
file-setup: file 644 "1bc"
file-setup: file 644 "@bc"
file-setup: file 644 "a.c"
stdin:
	echo [![:alnum:]]*
	echo *[![:punct:]]c
expected-stdout:
	@bc
	1bc @bc abc

---
