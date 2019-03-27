#!/bin/sh -
#
# Copyright (c) 1992 Diomidis Spinellis.
# Copyright (c) 1992, 1993
#	The Regents of the University of California.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the University nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
#	@(#)sed.test	8.1 (Berkeley) 6/6/93
#
#	$FreeBSD$
#

# sed Regression Tests
#
# The directory regress.test.out contains the expected test results
#
# These are the regression tests mostly created during the development
# of the BSD sed.  Each test should have a unique mark name, which is
# used for naming the corresponding file in regress.multitest.out.

SRCDIR=$(dirname $0)

main()
{
	REGRESS=${SRCDIR}/regress.multitest.out
	DICT=/usr/share/dict/words

	awk 'END { for (i = 1; i < 15; i++) print "l1_" i}' </dev/null >lines1
	awk 'END { for (i = 1; i < 10; i++) print "l2_" i}' </dev/null >lines2

	echo "1..130"

	exec 4>&1 5>&2
	tests
	exec 1>&4 2>&5

	# Remove temporary files
	rm -f current.out lines[1-4] script[1-2]
}

tests()
{
	SED=sed
	MARK=0

	test_args
	test_addr
	test_group
	test_acid
	test_branch
	test_pattern
	test_print
	test_subst
	test_error
	# Handle the result of the last test
	result
}

# Display a test's result
result()
{
	if [ "$TODO" = '1' ] ; then
		TODO='TODO '
	else
		TODO=''
	fi
	if ! [ -r $REGRESS/${TESTNAME} ] ; then
		echo "Seeding $REGRESS/${TESTNAME} with current result" 1>&2
		cp current.out $REGRESS/${TESTNAME}
	fi
	if diff -c $REGRESS/${TESTNAME} current.out ; then
		echo "ok $MARK $TESTNAME # $TODO$COMMENT"
	else
		echo "not ok $MARK $TESTNAME # $TODO$COMMENT"
	fi 1>&4 2>&5
}

# Mark the beginning of each test
mark()
{
	[ $MARK -gt 0 ] && result
	MARK=`expr $MARK + 1`
	TESTNAME=$1
	exec 1>&4 2>&5
	exec >"current.out"
}

test_args()
{
	COMMENT='Argument parsing - first type'
	mark '1.1'
	$SED 's/^/e1_/p' lines1
	mark '1.2' ; $SED -n 's/^/e1_/p' lines1
	mark '1.3'
	$SED 's/^/e1_/p' <lines1
	mark '1.4' ; $SED -n 's/^/e1_/p' <lines1
	COMMENT='Argument parsing - second type'
	mark '1.4.1'
	$SED -e '' <lines1
	echo 's/^/s1_/p' >script1
	echo 's/^/s2_/p' >script2
	mark '1.5'
	$SED -f script1 lines1
	mark '1.6'
	$SED -f script1 <lines1
	mark '1.7'
	$SED -e 's/^/e1_/p' lines1
	mark '1.8'
	$SED -e 's/^/e1_/p' <lines1
	mark '1.9' ; $SED -n -f script1 lines1
	mark '1.10' ; $SED -n -f script1 <lines1
	mark '1.11' ; $SED -n -e 's/^/e1_/p' lines1
	mark '1.12'
	$SED -n -e 's/^/e1_/p' <lines1
	mark '1.13'
	$SED -e 's/^/e1_/p' -e 's/^/e2_/p' lines1
	mark '1.14'
	$SED -f script1 -f script2 lines1
	mark '1.15'
	$SED -e 's/^/e1_/p' -f script1 lines1
	mark '1.16'
	$SED -e 's/^/e1_/p' lines1 lines1
	# POSIX D11.2:11251
	mark '1.17' ; $SED p <lines1 lines1
cat >script1 <<EOF
#n
# A comment

p
EOF
	mark '1.18' ; $SED -f script1 <lines1 lines1
}

test_addr()
{
	COMMENT='Address ranges'
	mark '2.1' ; $SED -n -e '4p' lines1
	mark '2.2' ; $SED -n -e '20p' lines1 lines2
	mark '2.3' ; $SED -n -e '$p' lines1
	mark '2.4' ; $SED -n -e '$p' lines1 lines2
	mark '2.5' ; $SED -n -e '$a\
hello' /dev/null
	mark '2.6' ; $SED -n -e '$p' lines1 /dev/null lines2
	# Should not print anything
	mark '2.7' ; $SED -n -e '20p' lines1
	mark '2.8' ; $SED -n -e '/NOTFOUND/p' lines1
	mark '2.9' ; $SED -n '/l1_7/p' lines1
	mark '2.10' ; $SED -n ' /l1_7/ p' lines1
	mark '2.11' ; $SED -n '\_l1\_7_p' lines1
	mark '2.12' ; $SED -n '1,4p' lines1
	mark '2.13' ; $SED -n '1,$p' lines1 lines2
	mark '2.14' ; $SED -n '1,/l2_9/p' lines1 lines2
	mark '2.15' ; $SED -n '/4/,$p' lines1 lines2
	mark '2.16' ; $SED -n '/4/,20p' lines1 lines2
	mark '2.17' ; $SED -n '/4/,/10/p' lines1 lines2
	mark '2.18' ; $SED -n '/l2_3/,/l1_8/p' lines1 lines2
	mark '2.19' ; $SED -n '12,3p' lines1 lines2
	mark '2.20' ; $SED -n '/l1_7/,3p' lines1 lines2
	mark '2.21' ; $SED -n '13,+4p' lines1 lines2
	mark '2.22' ; $SED -n '/l1_6/,+2p' lines1 lines2
	# For PR bin/192108
	mark '2.23'; $SED -n '12,+1p' lines1
}

test_group()
{
	COMMENT='Brace and other grouping'
	mark '3.1' ; $SED -e '
4,12 {
	s/^/^/
	s/$/$/
	s/_/T/
}' lines1
	mark '3.2' ; $SED -e '
4,12 {
	s/^/^/
	/6/,/10/ {
		s/$/$/
		/8/ s/_/T/
	}
}' lines1
	mark '3.3' ; $SED -e '
4,12 !{
	s/^/^/
	/6/,/10/ !{
		s/$/$/
		/8/ !s/_/T/
	}
}' lines1
	mark '3.4' ; $SED -e '4,12!s/^/^/' lines1
}

test_acid()
{
	COMMENT='Commands a c d and i'
	mark '4.1' ; $SED -n -e '
s/^/before_i/p
20i\
inserted
s/^/after_i/p
' lines1 lines2
	mark '4.2' ; $SED -n -e '
5,12s/^/5-12/
s/^/before_a/p
/5-12/a\
appended
s/^/after_a/p
' lines1 lines2
	mark '4.3'
	$SED -n -e '
s/^/^/p
/l1_/a\
appended
8,10N
s/$/$/p
' lines1 lines2
	mark '4.4' ; $SED -n -e '
c\
hello
' lines1
	mark '4.5' ; $SED -n -e '
8c\
hello
' lines1
	mark '4.6' ; $SED -n -e '
3,14c\
hello
' lines1
# SunOS and GNU sed behave differently.   We follow POSIX
	mark '4.7' ; $SED -n -e '
8,3c\
hello
' lines1
	mark '4.8' ; $SED d <lines1
}

test_branch()
{
	COMMENT='Labels and branching'
	mark '5.1' ; $SED -n -e '
b label4
:label3
s/^/label3_/p
b end
:label4
2,12b label1
b label2
:label1
s/^/label1_/p
b
:label2
s/^/label2_/p
b label3
:end
' lines1
	mark '5.2'
	$SED -n -e '
s/l1_/l2_/
t ok
b
:ok
s/^/tested /p
' lines1 lines2
# SunOS and GNU sed behave as follows: lines 9-$ aren't printed at all
	mark '5.3' ; $SED -n -e '
5,8b inside
1,5 {
	s/^/^/p
	:inside
	s/$/$/p
}
' lines1
# Check that t clears the substitution done flag
	mark '5.4' ; $SED -n -e '
1,8s/^/^/
t l1
:l1
t l2
s/$/$/p
b
:l2
s/^/ERROR/
' lines1
# Check that reading a line clears the substitution done flag
	mark '5.5'
	$SED -n -e '
t l2
1,8s/^/^/p
2,7N
b
:l2
s/^/ERROR/p
' lines1
	mark '5.6' ; $SED 5q lines1
	mark '5.7' ; $SED -e '
5i\
hello
5q' lines1
# Branch across block boundary
	mark '5.8' ; $SED -e '
{
:b
}
s/l/m/
tb' lines1
}

test_pattern()
{
COMMENT='Pattern space commands'
# Check that the pattern space is deleted
	mark '6.1' ; $SED -n -e '
c\
changed
p
' lines1
	mark '6.2' ; $SED -n -e '
4d
p
' lines1
	mark '6.3'
	$SED -e 'N;N;N;D' lines1
	mark '6.4' ; $SED -e '
2h
3H
4g
5G
6x
6p
6x
6p
' lines1
	mark '6.5' ; $SED -e '4n' lines1
	mark '6.6' ; $SED -n -e '4n' lines1
}

test_print()
{
	COMMENT='Print and file routines'
	awk 'END {for (i = 1; i < 256; i++) printf("%c", i);print "\n"}' \
		</dev/null >lines3
	# GNU and SunOS sed behave differently here
	mark '7.1'
	$SED -n l lines3
	mark '7.2' ; $SED -e '/l2_/=' lines1 lines2
	rm -f lines4
	mark '7.3' ; $SED -e '3,12w lines4' lines1
	COMMENT='w results'
	cat lines4
	mark '7.4' ; $SED -e '4r lines2' lines1
	mark '7.5' ; $SED -e '5r /dev/dds' lines1
	mark '7.6' ; $SED -e '6r /dev/null' lines1
	mark '7.7'
	sed '200q' $DICT | sed 's$.*$s/^/&/w tmpdir/&$' >script1
	rm -rf tmpdir
	mkdir tmpdir
	$SED -f script1 lines1
	cat tmpdir/*
	rm -rf tmpdir
	mark '7.8'
	echo line1 > lines3
	echo "" >> lines3
	$SED -n -e '$p' lines3 /dev/null
		
}

test_subst()
{
	COMMENT='Substitution commands'
	mark '8.1' ; $SED -e 's/./X/g' lines1
	mark '8.2' ; $SED -e 's,.,X,g' lines1
# SunOS sed thinks we are escaping . as wildcard, not as separator
	mark '8.3'
	$SED -e 's.\..X.g' lines1
	mark '8.4' ; $SED -e 's/[\/]/Q/' lines1
	mark '8.5' ; $SED -e 's_\__X_' lines1
	mark '8.6' ; $SED -e 's/./(&)/g' lines1
	mark '8.7' ; $SED -e 's/./(\&)/g' lines1
	mark '8.8' ; $SED -e 's/\(.\)\(.\)\(.\)/x\3x\2x\1/g' lines1
	mark '8.9' ; $SED -e 's/_/u0\
u1\
u2/g' lines1
	mark '8.10'
	$SED -e 's/./X/4' lines1
	rm -f lines4
	mark '8.11' ; $SED -e 's/1/X/w lines4' lines1
	COMMENT='s wfile results'
	cat lines4
	mark '8.12' ; $SED -e 's/[123]/X/g' lines1
	mark '8.13' ; $SED -e 'y/0123456789/9876543210/' lines1
	mark '8.14' ; 
	$SED -e 'y10\123456789198765432\101' lines1
	mark '8.15' ; $SED -e '1N;2y/\n/X/' lines1
	mark '8.16'
	echo 'eeefff' | $SED -e '
		p
		s/e/X/p
		:x
		s//Y/p 
		# Establish limit counter in the hold space
		# GNU sed version 3.02 enters into an infinite loop here
		x 
		/.\{10\}/ {
			s/.*/ERROR/
			b
		}
		s/.*/&./
		x 
		/f/bx
	'
	# POSIX does not say that this should work,
	# but it does for GNU, BSD, and SunOS
	mark '8.17' ; $SED -e 's/[/]/Q/' lines1

	COMMENT='[ as an s delimiter and its escapes'
	mark '8.18' ; $SED -e 's[_[X[' lines1
	# This is a matter of interpretation
	# POSIX 1003.1, 2004 says "Within the BRE and the replacement,
	# the BRE delimiter itself can be used as a *literal* character
	# if it is preceded by a backslash"
	# SunOS 5.1 /usr/bin/sed and Mac OS X follow the literal POSIX
	# interpretation.
	# GNU sed version 4.1.5 treats \[ as the beginning of a character
	# set specification (both with --posix and without).
	mark '8.19' ; sed 's/l/[/' lines1 | $SED -e 's[\[.[X['
	mark '8.20' ; sed 's/l/[/' lines1 | $SED -e 's[\[.[X\[['
	COMMENT='\ in y command'
	mark '8.21'
	echo 'a\b(c' |
	$SED 'y%ABCDEFGHIJKLMNOPQRSTUVWXYZ, /\\()"%abcdefghijklmnopqrstuvwxyz,------%'
	COMMENT='\n in a character class and a BRE'
	mark '8.22' ; (echo 1; echo 2) | $SED -n '1{;N;s/[\n]/X/;p;}'
	mark '8.23' ; (echo 1; echo 2) | $SED -n '1{;N;s/\n/X/;p;}'
}

test_error()
{
	COMMENT='Error cases'
	mark '9.1' ; $SED -x 2>/dev/null ; echo $?
	mark '9.2' ; $SED -f 2>/dev/null ; echo $?
	mark '9.3' ; $SED -e 2>/dev/null ; echo $?
	mark '9.4' ; $SED -f /dev/xyzzyxyzy 2>/dev/null ; echo $?
	mark '9.5' ; $SED p /dev/xyzzyxyzy 2>/dev/null ; echo $?
	mark '9.6' ; $SED -f /bin/sh 2>/dev/null ; echo $?
	mark '9.7' ; $SED '{' 2>/dev/null ; echo $?
	mark '9.8' ; $SED '{' 2>/dev/null ; echo $?
	mark '9.9' ; $SED '/hello/' 2>/dev/null ; echo $?
	mark '9.10' ; $SED '1,/hello/' 2>/dev/null ; echo $?
	mark '9.11' ; $SED -e '-5p' 2>/dev/null ; echo $?
	mark '9.12' ; $SED '/jj' 2>/dev/null ; echo $?
	mark '9.13' ; $SED 'a hello' 2>/dev/null ; echo $?
	mark '9.14' ; $SED 'a \ hello' 2>/dev/null ; echo $?
	mark '9.15' ; $SED 'b foo' 2>/dev/null ; echo $?
	mark '9.16' ; $SED 'd hello' 2>/dev/null ; echo $?
	mark '9.17' ; $SED 's/aa' 2>/dev/null ; echo $?
	mark '9.18' ; $SED 's/aa/' 2>/dev/null ; echo $?
	mark '9.19' ; $SED 's/a/b' 2>/dev/null ; echo $?
	mark '9.20' ; $SED 's/a/b/c/d' 2>/dev/null ; echo $?
	mark '9.21' ; $SED 's/a/b/ 1 2' 2>/dev/null ; echo $?
	mark '9.22' ; $SED 's/a/b/ 1 g' 2>/dev/null ; echo $?
	mark '9.23' ; $SED 's/a/b/w' 2>/dev/null ; echo $?
	mark '9.24' ; $SED 'y/aa' 2>/dev/null ; echo $?
	mark '9.25' ; $SED 'y/aa/b/' 2>/dev/null ; echo $?
	mark '9.26' ; $SED 'y/aa/' 2>/dev/null ; echo $?
	mark '9.27' ; $SED 'y/a/b' 2>/dev/null ; echo $?
	mark '9.28' ; $SED 'y/a/b/c/d' 2>/dev/null ; echo $?
	mark '9.29' ; $SED '!' 2>/dev/null ; echo $?
	mark '9.30' ; $SED supercalifrangolisticexprialidociussupercalifrangolisticexcius 2>/dev/null ; echo $?
	mark '9.31' ; $SED '' /dev/null 2>/dev/null ; echo $?
}

main
