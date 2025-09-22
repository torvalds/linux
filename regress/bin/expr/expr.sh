#!/bin/ksh
# $OpenBSD: expr.sh,v 1.2 2018/03/31 15:52:31 schwarze Exp $

: ${EXPR=expr}

function test_expr {
	#echo "Testing: `eval echo $1`"
	res=`eval $EXPR $1 2>&1`
	if [ "$res" != "$2" ]; then
		echo "Expected $2, got $res from expression: `eval echo $1`"
		exit 1
	fi
}
	
# The first arg will get eval'd so escape any meta characters
# The 2nd arg is an expected string/response from expr for that op.

# Test overflow cases
test_expr '4611686018427387904 + 4611686018427387903' '9223372036854775807'
test_expr '4611686018427387904 + 4611686018427387904' "expr: overflow"
test_expr '-4611686018427387904 + -4611686018427387904' '-9223372036854775808'
test_expr '-4611686018427387904 + -4611686018427387905' 'expr: overflow'
test_expr '4611686018427387904 - -4611686018427387903' '9223372036854775807'
test_expr '4611686018427387904 - -4611686018427387904' "expr: overflow"
test_expr '-4611686018427387904 - 4611686018427387903' '-9223372036854775807'
test_expr '-4611686018427387904 - 4611686018427387904' '-9223372036854775808'
test_expr '-4611686018427387904 - 4611686018427387905' "expr: overflow"
test_expr '-4611686018427387904 \* 1' '-4611686018427387904'
test_expr '-4611686018427387904 \* -1' '4611686018427387904'
test_expr '-4611686018427387904 \* 2' '-9223372036854775808'
test_expr '-4611686018427387904 \* 3' "expr: overflow"
test_expr '-4611686018427387904 \* -2' "expr: overflow"
test_expr '4611686018427387904 \* 1' '4611686018427387904'
test_expr '4611686018427387904 \* 2' "expr: overflow"
test_expr '4611686018427387904 \* 3' "expr: overflow"
test_expr '4611686018427387903 \* 2' '9223372036854775806'
test_expr '-4611686018427387905 \* 2' 'expr: overflow'
test_expr '4611686018427387904 / 0' 'expr: division by zero'
test_expr '-9223372036854775808 / -1' 'expr: overflow'
test_expr '-9223372036854775808 % -1' '0'

# Test from gtk-- configure that cause problems on old expr
test_expr '3 \> 3 \| 3 = 3 \& 4 \> 4 \| 3 = 3 \& 4 = 4 \& 5 \>= 5' '1'
test_expr '3 \> 3 \| 3 = 3 \& 4 \> 4 \| 3 = 3 \& 4 = 4 \& 5 \>= 6' '0'
test_expr '3 \> 3 \| 3 = 3 \& 4 \> 4 \| 3 = 3 \& 4 = 3 \& 5 \>= 5' '0'
test_expr '3 \> 3 \| 3 = 3 \& 4 \> 4 \| 3 = 2 \& 4 = 4 \& 5 \>= 5' '0'
test_expr '3 \> 2 \| 3 = 3 \& 4 \> 4 \| 3 = 3 \& 4 = 4 \& 5 \>= 6' '1'
test_expr '3 \> 3 \| 3 = 3 \& 4 \> 3 \| 3 = 3 \& 4 = 4 \& 5 \>= 5' '1'

# Basic precendence test with the : operator vs. math
test_expr '2 : 4 / 2' '0'
test_expr '4 : 4 % 3' '1'

# Dangling arithemtic operator
test_expr '.java_wrapper : /' '0'
test_expr '4 : \*' '0'
test_expr '4 : +' '0'
test_expr '4 : -' '0'
test_expr '4 : /' '0'
test_expr '4 : %' '0'

# Basic math test
test_expr '2 + 4 \* 5' '22'

# Basic functional tests
test_expr '2' '2'
test_expr '-4' '-4'
test_expr 'hello' 'hello'

# Compare operator precendence test
test_expr '2 \> 1 \* 17' '0'

# Compare operator tests
test_expr '2 \!= 5' '1'
test_expr '2 \!= 2' '0'
test_expr '2 \<= 3' '1'
test_expr '2 \<= 2' '1'
test_expr '2 \<= 1' '0'
test_expr '2 \< 3' '1'
test_expr '2 \< 2' '0'
test_expr '2 = 2' '1'
test_expr '2 = 4' '0'
test_expr '2 \>= 1' '1'
test_expr '2 \>= 2' '1'
test_expr '2 \>= 3' '0'
test_expr '2 \> 1' '1'
test_expr '2 \> 2' '0'

# Known failure once
test_expr '1 \* -1' '-1'

# Test a known case that should fail
test_expr '- 1 + 5' 'expr: syntax error'

# Double check negative numbers
test_expr '1 - -5' '6'

# More complex math test for precedence
test_expr '-3 + -1 \* 4 + 3 / -6' '-7'

# The next two are messy but the shell escapes cause that. 
# Test precendence
test_expr 'X1/2/3 : X\\\(.\*[^/]\\\)//\*[^/][^/]\*/\*$ \| . : \\\(.\\\)' '1/2'

# Test proper () returning \1 from a regex
test_expr '1/2 : .\*/\\\(.\*\\\)' '2'

# Test integer edge cases
test_expr '9223372036854775806 + 0' '9223372036854775806'
test_expr '9223372036854775806 + 1' '9223372036854775807'
test_expr '9223372036854775807 + 0' '9223372036854775807'
test_expr '9223372036854775806 + 2' 'expr: overflow'
test_expr '9223372036854775807 + 1' 'expr: overflow'
test_expr '9223372036854775808 + 0' \
    'expr: number "9223372036854775808" is too large'
test_expr '-9223372036854775807 + 0' '-9223372036854775807'
test_expr '-9223372036854775807 - 1' '-9223372036854775808'
test_expr '-9223372036854775808 + 0' '-9223372036854775808'
test_expr '-9223372036854775807 - 2' 'expr: overflow'
test_expr '-9223372036854775808 - 1' 'expr: overflow'
test_expr '-9223372036854775809 + 0' \
    'expr: number "-9223372036854775809" is too small'
test_expr '0 - -9223372036854775807' '9223372036854775807'
test_expr '1 - -9223372036854775806' '9223372036854775807'
test_expr '0 - -9223372036854775808' 'expr: overflow'
test_expr '1 - -9223372036854775807' 'expr: overflow'
test_expr '-36854775808 - \( -9223372036854775807 - 1 \)' '9223372000000000000'

exit 0
