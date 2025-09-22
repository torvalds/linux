name: arith-lazy-1
description:
	Check that only one side of ternary operator is evaluated
stdin:
	x=i+=2
	y=j+=2
	typeset -i i=1 j=1
	echo $((1 ? 20 : (x+=2)))
	echo $i,$x
	echo $((0 ? (y+=2) : 30))
	echo $j,$y
expected-stdout:
	20
	1,i+=2
	30
	1,j+=2
---

name: arith-lazy-2
description:
	Check that assignments not done on non-evaluated side of ternary
	operator
stdin:
	x=i+=2
	y=j+=2
	typeset -i i=1 j=1
	echo $((1 ? 20 : (x+=2)))
	echo $i,$x
	echo $((0 ? (y+=2) : 30))
	echo $i,$y
expected-stdout:
	20
	1,i+=2
	30
	1,j+=2
---

name: arith-ternary-prec-1
description:
	Check precidance of ternary operator vs assignment
stdin:
	typeset -i x=2
	y=$((1 ? 20 : x+=2))
expected-exit: e != 0
expected-stderr-pattern:
	/.*:.*1 \? 20 : x\+=2.*lvalue.*\n$/
---

name: arith-ternary-prec-2
description:
	Check precidance of ternary operator vs assignment
stdin:
	typeset -i x=2
	echo $((0 ? x+=2 : 20))
expected-stdout:
	20
---

name: arith-div-assoc-1
description:
	Check associativity of division operator
stdin:
	echo $((20 / 2 / 2))
expected-stdout:
	5
---

name: arith-assop-assoc-1
description:
	Check associativity of assignment-operator operator
stdin:
	typeset -i i=1 j=2 k=3
	echo $((i += j += k))
	echo $i,$j,$k
expected-stdout:
	6
	6,5,3
---

name: check-octal-valid-1
description:
	Check octal notation (valid input)
stdin:
	echo $((00)),$((-00)),$((007)),$((-007)),$((010)),$((-010))
	echo $((010 + 1))
expected-stdout:
	0,0,7,-7,8,-8
	9

---

name: check-octal-invalid-1
description:
	Check octal notation (invalid input)
stdin:
	echo $((08))
expected-exit: e != 0
expected-stderr-pattern:
	/.*:.*08.*bad number/

---
name: check-hex-valid-1
description:
	Check hex notation (valid input)
stdin:
	echo $((0x0)),$((-0x0)),$((0xf)),$((-0xf)),$((0x10)),$((-0x10))
	echo $((0x10 + 1))
expected-stdout:
	0,0,15,-15,16,-16
	17

---

name: check-hex-invalid-1
description:
	Check hex notation (invalid input)
stdin:
	echo $((0xg))
expected-exit: e != 0
expected-stderr-pattern:
	/.*:.* 0xg.*bad number/

---

name: arith-recurse-1
description:
	Check that arithmetic evaluation substitutes integer values
	of variables recursively.
stdin:
	vb=va
	va=42
	echo $((vb))
expected-stdout:
	42

---

name: arith-recurse-2
description:
	Check that variables can be used as array indices.
stdin:
	vb='aa[va]'
	set -A aa 40 41 42 43
	va=2
	echo ${aa[va]}
	echo ${aa[$va]}
	echo $((aa[va]))
	echo $((aa[$va]))
	echo $((vb))
expected-stdout:
	42
	42
	42
	42
	42

---

name: arith-subst-1
description:
	Check that arithmetic evaluation does not apply parameter
	substitution to the values of variables.
stdin:
	va=17
	vb='$va'
	echo $((vb))
expected-exit: e != 0
expected-stderr-pattern:
	/.*:.*\$va.*unexpected.*\$/

---

name: arith-subst-2
description:
	Check that arithmetic evaluation does not apply parameter
	substitution to arry indices inside the values of variables.
stdin:
	set -A aa 40 41 42 43
	va=2
	vb='aa[$va]'
	echo $((vb))
expected-exit: e != 0
expected-stderr-pattern:
	/.*:.*\$va.*unexpected.*\$/

---
