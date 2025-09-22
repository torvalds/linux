name: IFS-space-1
description:
	Simple test, default IFS
stdin:
	showargs() { for i; do echo -n " <$i>"; done; echo; }
	set -- A B C
	showargs 1 $*
	showargs 2 "$*"
	showargs 3 $@
	showargs 4 "$@"
expected-stdout:
	 <1> <A> <B> <C>
	 <2> <A B C>
	 <3> <A> <B> <C>
	 <4> <A> <B> <C>
---

name: IFS-colon-1
description:
	Simple test, IFS=:
stdin:
	showargs() { for i; do echo -n " <$i>"; done; echo; }
	IFS=:
	set -- A B C
	showargs 1 $*
	showargs 2 "$*"
	showargs 3 $@
	showargs 4 "$@"
expected-stdout:
	 <1> <A> <B> <C>
	 <2> <A:B:C>
	 <3> <A> <B> <C>
	 <4> <A> <B> <C>
---

name: IFS-null-1
description:
	Simple test, IFS=""
stdin:
	showargs() { for i; do echo -n " <$i>"; done; echo; }
	IFS=""
	set -- A B C
	showargs 1 $*
	showargs 2 "$*"
	showargs 3 $@
	showargs 4 "$@"
expected-stdout:
	 <1> <A> <B> <C>
	 <2> <ABC>
	 <3> <A> <B> <C>
	 <4> <A> <B> <C>
---

name: IFS-space-colon-1
description:
	Simple test, IFS=<white-space>:
stdin:
	showargs() { for i; do echo -n " <$i>"; done; echo; }
	IFS="$IFS:"
	set --
	showargs 1 $*
	showargs 2 "$*"
	showargs 3 $@
	showargs 4 "$@"
	showargs 5 : "$@"
expected-stdout:
	 <1>
	 <2> <>
	 <3>
	 <4>
	 <5> <:>
---

name: IFS-space-colon-2
description:
	Simple test, IFS=<white-space>:
	At&t ksh fails this, POSIX says the test is correct.
stdin:
	showargs() { for i; do echo -n " <$i>"; done; echo; }
	IFS="$IFS:"
	set --
	showargs :"$@"
expected-stdout:
	 <:>
---

name: IFS-space-colon-3
description:
	Simple test, IFS=<white-space>:
	pdksh fails both of these tests
stdin:
	showargs() { for i; do echo -n " <$i>"; done; echo; }
	IFS="$IFS:"
	x=
	set --
	showargs "$x$@"
	showargs "$@$x"
expected-fail: yes
expected-stdout:
	 <>
	 <>
---

name: IFS-space-colon-4
description:
	Simple test, IFS=<white-space>:
stdin:
	showargs() { for i; do echo -n " <$i>"; done; echo; }
	IFS="$IFS:"
	set --
	showargs "$@$@"
expected-stdout:
	
---

name: IFS-space-colon-5
description:
	Simple test, IFS=<white-space>:
	Don't know what POSIX thinks of this.  at&t ksh does not do this.
stdin:
	showargs() { for i; do echo -n " <$i>"; done; echo; }
	IFS="$IFS:"
	set --
	showargs "${@:-}"
expected-stdout:
	 <>
---

name: IFS-subst-1
description:
	Simple test, IFS=<white-space>:
stdin:
	showargs() { for i; do echo -n " <$i>"; done; echo; }
	IFS="$IFS:"
	x=":b: :"
	echo -n '1:'; for i in $x ; do echo -n " [$i]" ; done ; echo
	echo -n '2:'; for i in :b:: ; do echo -n " [$i]" ; done ; echo
	showargs 3 $x
	showargs 4 :b::
	x="a:b:"
	echo -n '5:'; for i in $x ; do echo -n " [$i]" ; done ; echo
	showargs 6 $x
	x="a::c"
	echo -n '7:'; for i in $x ; do echo -n " [$i]" ; done ; echo
	showargs 8 $x
	echo -n '9:'; for i in ${FOO-`echo -n h:i`th:ere} ; do echo -n " [$i]" ; done ; echo
	showargs 10 ${FOO-`echo -n h:i`th:ere}
	showargs 11 "${FOO-`echo -n h:i`th:ere}"
	x=" A :  B::D"
	echo -n '12:'; for i in $x ; do echo -n " [$i]" ; done ; echo
	showargs 13 $x
expected-stdout:
	1: [] [b] []
	2: [:b::]
	 <3> <> <b> <>
	 <4> <:b::>
	5: [a] [b]
	 <6> <a> <b>
	7: [a] [] [c]
	 <8> <a> <> <c>
	9: [h] [ith] [ere]
	 <10> <h> <ith> <ere>
	 <11> <h:ith:ere>
	12: [A] [B] [] [D]
	 <13> <A> <B> <> <D>
---

