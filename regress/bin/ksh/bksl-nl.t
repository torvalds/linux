#	$OpenBSD: bksl-nl.t,v 1.1 2013/12/02 20:39:44 millert Exp $

#
#  These tests deal with how \newline is handled in various situations.  The
# first group of tests are places where it shouldn't be collapsed, the next
# group of tests are places where it should be collapsed.
#
name: bksl-nl-ign-1
description:
	Check that \newline is not collasped after #
stdin:
	echo hi #there \
	echo folks
expected-stdout:
	hi
	folks
---

name: bksl-nl-ign-2
description:
	Check that \newline is not collasped inside single quotes
stdin:
	echo 'hi \
	there'
	echo folks
expected-stdout:
	hi \
	there
	folks
---

name: bksl-nl-ign-3
description:
	Check that \newline is not collasped inside single quotes
stdin:
	cat << \EOF
	hi \
	there
	EOF
expected-stdout:
	hi \
	there
---

name: blsk-nl-ign-4
description:
	Check interaction of aliases, single quotes and here-documents
	with backslash-newline
	(don't know what posix has to say about this)
stdin: 
	a=2
	alias x='echo hi
	cat << "EOF"
	foo\
	bar
	some'
	x
	more\
	stuff$a
	EOF
expected-stdout:
	hi
	foo\
	bar
	some
	more\
	stuff$a
---

name: blsk-nl-ign-5
description:
	Check what happens with backslash at end of input
	(the old bourne shell trashes them; so do we)
stdin: !
	echo `echo foo\\`bar
	echo hi\
expected-stdout:
	foobar
	hi
---


#
# Places \newline should be collapsed
#
name: bksl-nl-1
description:
	Check that \newline is collasped before, in the middle of, and
	after words
stdin:
	 	 	\
			 echo hi\
	There, \
	folks
expected-stdout:
	hiThere, folks
---

name: bksl-nl-2
description:
	Check that \newline is collasped in $ sequences
	(ksh93 fails this)
stdin:
	a=12
	ab=19
	echo $\
	a
	echo $a\
	b
	echo $\
	{a}
	echo ${a\
	b}
	echo ${ab\
	}
expected-stdout:
	12
	19
	12
	19
	19
---

name: bksl-nl-3
description:
	Check that \newline is collasped in $(..) and `...` sequences
	(ksh93 fails this)
stdin:
	echo $\
	(echo foobar1)
	echo $(\
	echo foobar2)
	echo $(echo foo\
	bar3)
	echo $(echo foobar4\
	)
	echo `
	echo stuff1`
	echo `echo st\
	uff2`
expected-stdout:
	foobar1
	foobar2
	foobar3
	foobar4
	stuff1
	stuff2
---

name: bksl-nl-4
description:
	Check that \newline is collasped in $((..)) sequences
	(ksh93 fails this)
stdin:
	echo $\
	((1+2))
	echo $(\
	(1+2+3))
	echo $((\
	1+2+3+4))
	echo $((1+\
	2+3+4+5))
	echo $((1+2+3+4+5+6)\
	)
expected-stdout:
	3
	6
	10
	15
	21
---

name: bksl-nl-5
description:
	Check that \newline is collasped in double quoted strings
stdin:
	echo "\
	hi"
	echo "foo\
	bar"
	echo "folks\
	"
expected-stdout:
	hi
	foobar
	folks
---

name: bksl-nl-6
description:
	Check that \newline is collasped in here document delimiters
	(ksh93 fails second part of this)
stdin:
	a=12
	cat << EO\
	F
	a=$a
	foo\
	bar
	EOF
	cat << E_O_F
	foo
	E_O_\
	F
	echo done
expected-stdout:
	a=12
	foobar
	foo
	done
---

name: bksl-nl-7
description:
	Check that \newline is collasped in double-quoted here-document
	delimiter.
stdin:
	a=12
	cat << "EO\
	F"
	a=$a
	foo\
	bar
	EOF
	echo done
expected-stdout:
	a=$a
	foo\
	bar
	done
---

name: bksl-nl-8
description:
	Check that \newline is collasped in various 2+ character tokens
	delimiter.
	(ksh93 fails this)
stdin:
	echo hi &\
	& echo there
	echo foo |\
	| echo bar
	cat <\
	< EOF
	stuff
	EOF
	cat <\
	<\
	- EOF
		more stuff
	EOF
	cat <<\
	EOF
	abcdef
	EOF
	echo hi >\
	> /dev/null
	echo $?
	i=1
	case $i in
	(\
	x|\
	1\
	) echo hi;\
	;
	(*) echo oops
	esac
expected-stdout:
	hi
	there
	foo
	stuff
	more stuff
	abcdef
	0
	hi
---

name: blsk-nl-9
description:
	Check that \ at the end of an alias is collapsed when followed
	by a newline
	(don't know what posix has to say about this)
stdin: 
	alias x='echo hi\'
	x
	echo there
expected-stdout:
	hiecho there
---

name: blsk-nl-10
description:
	Check that \newline in a keyword is collapsed
stdin: 
	i\
	f true; then\
	 echo pass; el\
	se echo fail; fi
expected-stdout:
	pass
---

#
# Places \newline should be collapsed (ksh extensions)
#

name: blsk-nl-ksh-1
description:
	Check that \newline is collapsed in extended globbing
	(ksh93 fails this)
stdin: 
	xxx=foo
	case $xxx in
	(f*\
	(\
	o\
	)\
	) echo ok ;;
	*) echo bad
	esac
expected-stdout:
	ok
---

name: blsk-nl-ksh-2
description:
	Check that \newline is collapsed in ((...)) expressions
	(ksh93 fails this)
stdin: 
	i=1
	(\
	(\
	i=i+2\
	)\
	)
	echo $i
expected-stdout:
	3
---

