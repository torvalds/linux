# $FreeBSD$

REGRESSION_START($1)

echo '1..26'

REGRESSION_TEST(`G', `sed G <${SRCDIR}/regress.in')
REGRESSION_TEST(`P', `sed P <${SRCDIR}/regress.in')
REGRESSION_TEST(`psl', `sed \$!g\;P\;D <${SRCDIR}/regress.in')
REGRESSION_TEST(`bcb', `sed s/X/$(jot -n -bx -s "" 2043)\\\\zz/ <${SRCDIR}/regress.in')
REGRESSION_TEST(`y', `echo -n foo | sed y/o/O/')
REGRESSION_TEST(`sg', `echo foo | sed s/,*/,/g')
REGRESSION_TEST(`s3', `echo foo | sed s/,*/,/3')
REGRESSION_TEST(`s4', `echo foo | sed s/,*/,/4')
REGRESSION_TEST(`s5', `echo foo | sed s/,*/,/5')
REGRESSION_TEST(`c0', `sed ''`c\
foo
''`<${SRCDIR}/regress.in')
REGRESSION_TEST(`c1', `sed ''`4,$c\
foo
''`<${SRCDIR}/regress.in')
REGRESSION_TEST(`c2', `sed ''`3,9c\
foo
''`<${SRCDIR}/regress.in')
REGRESSION_TEST(`c3', `sed ''`3,/no such string/c\
foo
''`<${SRCDIR}/regress.in')
REGRESSION_TEST(`b2a', `sed ''`2,3b
1,2d''` <${SRCDIR}/regress.in')

`
inplace_test()
{
	expr="$1"
	rc=0
	ns=$(jot 5)
	ins= outs= _ins=
	for n in $ns; do
		jot -w "l${n}_%d" 9 | tee lines.in.$n lines._in.$n | \
		    sed "$expr" > lines.out.$n
		ins="$ins lines.in.$n"
		outs="$outs lines.out.$n"
		_ins="$_ins lines._in.$n"
	done
	sed "$expr" $_ins > lines.out

	sed -i "" "$expr" $ins
	sed -I "" "$expr" $_ins

	for n in $ns; do
		diff -u lines.out.$n lines.in.$n || rc=1
	done
	cat $_ins | diff -u lines.out - || rc=1
	rm -f $ins $outs $_ins lines.out

	return $rc
}
'

REGRESSION_TEST_FREEFORM(`inplace1', `inplace_test 3,6d')
REGRESSION_TEST_FREEFORM(`inplace2', `inplace_test 8,30d')
REGRESSION_TEST_FREEFORM(`inplace3', `inplace_test 20,99d')
REGRESSION_TEST_FREEFORM(`inplace4', `inplace_test "{;{;8,30d;};}"')
REGRESSION_TEST_FREEFORM(`inplace5', `inplace_test "3x;6G"')

REGRESSION_TEST(`icase1', `sed /SED/Id <${SRCDIR}/regress.in')
REGRESSION_TEST(`icase2', `sed s/SED/Foo/I <${SRCDIR}/regress.in')
REGRESSION_TEST(`icase3', `sed s/SED/Foo/ <${SRCDIR}/regress.in')
REGRESSION_TEST(`icase4', `sed s/SED/Foo/i <${SRCDIR}/regress.in')

REGRESSION_TEST(`hanoi', `echo ":abcd: : :" | sed -f ${SRCDIR}/hanoi.sed')
REGRESSION_TEST(`math', `echo "4+7*3+2^7/3" | sed -f ${SRCDIR}/math.sed')
REGRESSION_TEST(`not', `echo foo | sed "1!!s/foo/bar/"')

REGRESSION_END()
