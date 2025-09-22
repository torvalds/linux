name: heredoc-1
description:
	Check ordering/content of redundent here documents.
stdin:
	cat << EOF1 << EOF2 
	hi
	EOF1
	there
	EOF2
expected-stdout:
	there
---

name: heredoc-2
description:
	Check quoted here-doc is protected.
stdin:
	a=foo
	cat << 'EOF'
	hi\
	there$a
	stuff
	EO\
	F
	EOF
expected-stdout:
	hi\
	there$a
	stuff
	EO\
	F
---

name: heredoc-3
description:
	Check that newline isn't needed after heredoc-delimiter marker.
stdin: !
	cat << EOF
	hi
	there
	EOF
expected-stdout:
	hi
	there
---

name: heredoc-4
description:
	Check that an error occurs if the heredoc-delimiter is missing.
stdin: !
	cat << EOF
	hi
	there
expected-exit: e > 0
expected-stderr-pattern: /.*/
---

name: heredoc-5
description:
	Check that backslash quotes a $, ` and \ and kills a \newline
stdin: 
	a=BAD
	b=ok
	cat << EOF
	h\${a}i
	h\\${b}i
	th\`echo not-run\`ere
	th\\`echo is-run`ere
	fol\\ks
	more\\
	last \
	line
	EOF
expected-stdout:
	h${a}i
	h\oki
	th`echo not-run`ere
	th\is-runere
	fol\ks
	more\
	last line
---

name: heredoc-6
description:
	Check that \newline in initial here-delim word doesn't imply
	a quoted here-doc.
stdin: 
	a=i
	cat << EO\
	F
	h$a
	there
	EOF
expected-stdout:
	hi
	there
---

name: heredoc-7
description:
	Check that double quoted $ expressions in here delimiters are
	not expanded and match the delimiter.
	POSIX says only quote removal is applied to the delimiter.
stdin: 
	a=b
	cat << "E$a"
	hi
	h$a
	hb
	E$a
	echo done
expected-stdout:
	hi
	h$a
	hb
	done
---

name: heredoc-8
description:
	Check that double quoted escaped $ expressions in here
	delimiters are not expanded and match the delimiter.
	POSIX says only quote removal is applied to the delimiter
	(\ counts as a quote).
stdin: 
	a=b
	cat << "E\$a"
	hi
	h$a
	h\$a
	hb
	h\b
	E$a
	echo done
expected-stdout:
	hi
	h$a
	h\$a
	hb
	h\b
	done
---

name: heredoc-tmpfile-1
description:
	Check that heredoc temp files aren't removed too soon or too late.
	Heredoc in simple command.
stdin:
	TMPDIR=$PWD
	eval '
		cat <<- EOF
		hi
		EOF
		for i in a b ; do
			cat <<- EOF
			more
			EOF
		done
	    ' &
	sleep 1
	echo Left overs: *
expected-stdout:
	hi
	more
	more
	Left overs: *
---

name: heredoc-tmpfile-2
description:
	Check that heredoc temp files aren't removed too soon or too late.
	Heredoc in function, multiple calls to function.
stdin:
	TMPDIR=$PWD
	eval '
		foo() {
			cat <<- EOF
			hi
			EOF
		}
		foo
		foo
	    ' &
	sleep 1
	echo Left overs: *
expected-stdout:
	hi
	hi
	Left overs: *
---

name: heredoc-tmpfile-3
description:
	Check that heredoc temp files aren't removed too soon or too late.
	Heredoc in function in loop, multiple calls to function.
stdin:
	TMPDIR=$PWD
	eval '
		foo() {
			cat <<- EOF
			hi
			EOF
		}
		for i in a b; do
			foo
			foo() {
				cat <<- EOF
				folks $i
				EOF
			}
		done
		foo
	    ' &
	sleep 1
	echo Left overs: *
expected-stdout:
	hi
	folks b
	folks b
	Left overs: *
---

name: heredoc-tmpfile-4
description:
	Check that heredoc temp files aren't removed too soon or too late.
	Backgrounded simple command with here doc
stdin:
	TMPDIR=$PWD
	eval '
		cat <<- EOF &
		hi
		EOF
	    ' &
	sleep 1
	echo Left overs: *
expected-stdout:
	hi
	Left overs: *
---

name: heredoc-tmpfile-5
description:
	Check that heredoc temp files aren't removed too soon or too late.
	Backgrounded subshell command with here doc
stdin:
	TMPDIR=$PWD
	eval '
	      (
		sleep 1	# so parent exits
		echo A
		cat <<- EOF
		hi
		EOF
		echo B
	      ) &
	    ' &
	sleep 2
	echo Left overs: *
expected-stdout:
	A
	hi
	B
	Left overs: *
---

name: heredoc-tmpfile-6
description:
	Check that heredoc temp files aren't removed too soon or too late.
	Heredoc in pipeline.
stdin:
	TMPDIR=$PWD
	eval '
		cat <<- EOF | sed "s/hi/HI/"
		hi
		EOF
	    ' &
	sleep 1
	echo Left overs: *
expected-stdout:
	HI
	Left overs: *
---

name: heredoc-tmpfile-7
description:
	Check that heredoc temp files aren't removed too soon or too late.
	Heredoc in backgrounded pipeline.
stdin:
	TMPDIR=$PWD
	eval '
		cat <<- EOF | sed 's/hi/HI/' &
		hi
		EOF
	    ' &
	sleep 1
	echo Left overs: *
expected-stdout:
	HI
	Left overs: *
---

name: heredoc-tmpfile-8
description:
	Check that heredoc temp files aren't removed too soon or too late.
	Heredoc in function, backgrounded call to function.
stdin:
	TMPDIR=$PWD
	# Background eval so main shell doesn't do parsing
	eval '
		foo() {
			cat <<- EOF
			hi
			EOF
		}
		foo
		# sleep so eval can die
		(sleep 1; foo) &
		(sleep 1; foo) &
		foo
	    ' &
	sleep 2
	echo Left overs: *
expected-stdout:
	hi
	hi
	hi
	hi
	Left overs: *
---

