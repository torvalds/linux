#	$OpenBSD: history.t,v 1.4 2017/10/23 17:16:10 anton Exp $

# Not tested yet:
#	- commands in history file are not numbered negatively
# (and a few hundred other things)

name: history-basic
description:
	See if we can test history at all
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo hi
	fc -l
expected-stdout:
	hi
	1	echo hi
expected-stderr-pattern:
	/^X*$/m
---

name: history-e-minus-1
description:
	Check if more recent command is executed
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo hi
	echo there
	fc -e -
expected-stdout:
	hi
	there
	there
expected-stderr-pattern:
	/^X*echo there\nX*$/m
---

name: history-e-minus-2
description:
	Check that repeated command is printed before command
	is re-executed.
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	exec 2>&1
	echo hi
	echo there
	fc -e -
expected-stdout-pattern:
	/X*hi\nX*there\nX*echo there\nthere\nX*/
expected-stderr-pattern:
	/^X*$/m
---

name: history-e-minus-3
description:
	fc -e - fails when there is no history
	(ksh93 has a bug that causes this to fail)
	(ksh88 loops on this)
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	fc -e -
	echo ok
expected-stdout:
	ok
expected-stderr-pattern:
	/^X*.*:.*history.*\nX*$/m
---

name: history-e-minus-4
description:
	Check if "fc -e -" command output goes to stdout.
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo abc
	fc -e - | (read x; echo "A $x")
	echo ok
expected-stdout:
	abc
	A abc
	ok
expected-stderr-pattern:
	/^X*echo abc\nX*/m
---

name: history-e-minus-5
description:
	fc is replaced in history by new command.
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo abc def
	echo ghi jkl
	fc -e - echo
	fc -l 2 4
expected-stdout:
	abc def
	ghi jkl
	ghi jkl
	2	echo ghi jkl
	3	echo ghi jkl
	4	fc -l 2 4
expected-stderr-pattern:
	/^X*echo ghi jkl\nX*$/m
---

name: history-list-1
description:
	List lists correct range
	(ksh88 fails 'cause it lists the fc command)
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo line 1
	echo line 2
	echo line 3
	fc -l -- -2
expected-stdout:
	line 1
	line 2
	line 3
	2	echo line 2
	3	echo line 3
expected-stderr-pattern:
	/^X*$/m
---

name: history-list-2
description:
	Lists oldest history if given pre-historic number
	(ksh93 has a bug that causes this to fail)
	(ksh88 fails 'cause it lists the fc command)
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo line 1
	echo line 2
	echo line 3
	fc -l -- -40
expected-stdout:
	line 1
	line 2
	line 3
	1	echo line 1
	2	echo line 2
	3	echo line 3
expected-stderr-pattern:
	/^X*$/m
---

name: history-list-3
description:
	Can give number `options' to fc
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo line 1
	echo line 2
	echo line 3
	echo line 4
	fc -l -3 -2
expected-stdout:
	line 1
	line 2
	line 3
	line 4
	2	echo line 2
	3	echo line 3
expected-stderr-pattern:
	/^X*$/m
---

name: history-list-4
description:
	-1 refers to previous command
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo line 1
	echo line 2
	echo line 3
	echo line 4
	fc -l -1 -1
expected-stdout:
	line 1
	line 2
	line 3
	line 4
	4	echo line 4
expected-stderr-pattern:
	/^X*$/m
---

name: history-list-5
description:
	List command stays in history
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo line 1
	echo line 2
	echo line 3
	echo line 4
	fc -l -1 -1
	fc -l -2 -1
expected-stdout:
	line 1
	line 2
	line 3
	line 4
	4	echo line 4
	4	echo line 4
	5	fc -l -1 -1
expected-stderr-pattern:
	/^X*$/m
---

name: history-list-6
description:
	HISTSIZE limits about of history kept.
	(ksh88 fails 'cause it lists the fc command)
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!HISTSIZE=3!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo line 1
	echo line 2
	echo line 3
	echo line 4
	echo line 5
	fc -l
expected-stdout:
	line 1
	line 2
	line 3
	line 4
	line 5
	4	echo line 4
	5	echo line 5
expected-stderr-pattern:
	/^X*$/m
---

name: history-list-7
description:
	fc allows too old/new errors in range specification
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!HISTSIZE=3!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo line 1
	echo line 2
	echo line 3
	echo line 4
	echo line 5
	fc -l 1 30
expected-stdout:
	line 1
	line 2
	line 3
	line 4
	line 5
	4	echo line 4
	5	echo line 5
	6	fc -l 1 30
expected-stderr-pattern:
	/^X*$/m
---

name: history-list-r-1
description:
	test -r flag in history
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo line 1
	echo line 2
	echo line 3
	echo line 4
	echo line 5
	fc -l -r 2 4
expected-stdout:
	line 1
	line 2
	line 3
	line 4
	line 5
	4	echo line 4
	3	echo line 3
	2	echo line 2
expected-stderr-pattern:
	/^X*$/m
---

name: history-list-r-2
description:
	If first is newer than last, -r is implied.
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo line 1
	echo line 2
	echo line 3
	echo line 4
	echo line 5
	fc -l 4 2
expected-stdout:
	line 1
	line 2
	line 3
	line 4
	line 5
	4	echo line 4
	3	echo line 3
	2	echo line 2
expected-stderr-pattern:
	/^X*$/m
---

name: history-list-r-3
description:
	If first is newer than last, -r is cancelled.
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo line 1
	echo line 2
	echo line 3
	echo line 4
	echo line 5
	fc -l -r 4 2
expected-stdout:
	line 1
	line 2
	line 3
	line 4
	line 5
	2	echo line 2
	3	echo line 3
	4	echo line 4
expected-stderr-pattern:
	/^X*$/m
---

name: history-subst-1
description:
	Basic substitution
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo abc def
	echo ghi jkl
	fc -e - abc=AB 'echo a'
expected-stdout:
	abc def
	ghi jkl
	AB def
expected-stderr-pattern:
	/^X*echo AB def\nX*$/m
---

name: history-subst-2
description:
	Does subst find previous command?
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo abc def
	echo ghi jkl
	fc -e - jkl=XYZQRT 'echo g'
expected-stdout:
	abc def
	ghi jkl
	ghi XYZQRT
expected-stderr-pattern:
	/^X*echo ghi XYZQRT\nX*$/m
---

name: history-subst-3
description:
	Does subst find previous command when no arguments given
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo abc def
	echo ghi jkl
	fc -e - jkl=XYZQRT
expected-stdout:
	abc def
	ghi jkl
	ghi XYZQRT
expected-stderr-pattern:
	/^X*echo ghi XYZQRT\nX*$/m
---

name: history-subst-4
description:
	Global substitutions work
	(ksh88 and ksh93 do not have -g option)
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo abc def asjj sadjhasdjh asdjhasd
	fc -e - -g a=FooBAR
expected-stdout:
	abc def asjj sadjhasdjh asdjhasd
	FooBARbc def FooBARsjj sFooBARdjhFooBARsdjh FooBARsdjhFooBARsd
expected-stderr-pattern:
	/^X*echo FooBARbc def FooBARsjj sFooBARdjhFooBARsdjh FooBARsdjhFooBARsd\nX*$/m
---

name: history-subst-5
description:
	Make sure searches don't find current (fc) command
	(ksh88/ksh93 don't have the ? prefix thing so they fail this test)
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo abc def
	echo ghi jkl
	fc -e - abc=AB \?abc
expected-stdout:
	abc def
	ghi jkl
	AB def
expected-stderr-pattern:
	/^X*echo AB def\nX*$/m
---

name: history-ed-1
description:
	Basic (ed) editing works (assumes you have generic ed editor
	that prints no prompts).
# No ed on os/2 (yet?).
category: !os:os2
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo abc def
	fc echo
	s/abc/FOOBAR/
	w
	q
expected-stdout:
	abc def
	13
	16
	FOOBAR def
expected-stderr-pattern:
	/^X*echo FOOBAR def\nX*$/m
---

name: history-ed-2
description:
	Correct command is edited when number given
category: !os:os2
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo line 1
	echo line 2 is here
	echo line 3
	echo line 4
	fc 2
	s/is here/is changed/
	w
	q
expected-stdout:
	line 1
	line 2 is here
	line 3
	line 4
	20
	23
	line 2 is changed
expected-stderr-pattern:
	/^X*echo line 2 is changed\nX*$/m
---

name: history-ed-3
description:
	Newly created multi line commands show up as single command
	in history.
	(NOTE: will fail if using COMPLEX HISTORY compile time option)
	(ksh88 fails 'cause it lists the fc command)
category: !os:os2
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo abc def
	fc echo
	s/abc/FOOBAR/
	$a
	echo a new line
	.
	w
	q
	fc -l
expected-stdout:
	abc def
	13
	32
	FOOBAR def
	a new line
	1	echo abc def
	2	echo FOOBAR def
		echo a new line
expected-stderr-pattern:
	/^X+echo FOOBAR def\necho a new line\nX*$/m
---

name: history-ignoredups-1
description:
	Duplicate subsequent commands are ignored.
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!HISTCONTROL=ignoredups!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo a
	echo a
	fc -l
expected-stdout:
	a
	a
	2	echo a
expected-stderr-pattern:
	/^X*$/m
---

name: history-ignorespace-1
description:
	A command prefixed with space is discarded from history.
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!HISTCONTROL=ignorespace!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo a
	 echo b
	fc -l
expected-stdout:
	a
	b
	2	echo a
expected-stderr-pattern:
	/^X*$/m
---

name: history-histcontrol-1
description:
	Both ignoredups and ignorespace can be specified.
	Unrecognized and empty options are ignored.
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!HISTCONTROL=ignoredups:ignorespace:foo::!
file-setup: file 644 "Env"
	PS1=X
stdin:
	echo a
	 echo b
	echo a
	fc -l
expected-stdout:
	a
	b
	a
	3	echo a
expected-stderr-pattern:
	/^X*$/m
---

name: history-corrupt-1
description:
	Every valid history line prior to a corrupted line is loaded.
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: !file 644 "hist.file"
	ls
	cd
file-setup: file 644 "Env"
	PS1=X
stdin:
	fc -l
expected-stdout:
	1	ls
expected-stderr-pattern:
	/^[^:]+: history file is corrupt\nX*$/m
---

name: history-long-lines-1
description:
	Long lines are ignored.
arguments: !-i!
env-setup: !ENV=./Env!HISTFILE=hist.file!
file-setup: file 644 "hist.file"
file-setup: file 644 "Env"
	PS1=X
perl-setup:
	system('(echo ls; jot -b c -s "" 4096; jot -b d -s "" 4096; echo w) ' .
		'>>hist.file');
stdin:
	fc -l
expected-stdout:
	1	ls
	2	w
expected-stderr-pattern:
	/^[^:]+: ignored history line\(s\) longer than \d+ bytes\nX*$/m
---
