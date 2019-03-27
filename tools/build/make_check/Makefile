# $FreeBSD$

.MAKE.MODE=	normal

# Failure is handled by the invoker, don't kill other legs of a parallel build.
MAKE_JOB_ERROR_TOKEN=no

# Test for broken LHS expansion.
# This *must* cause make(1) to detect a recursive variable, and fail as such.
.if make(lhs_expn)
FOO=		${BAR}
BAR${NIL}=	${FOO}
FOO${BAR}=	${FOO}
.endif

DATA1=	helllo
DATA2:=	${DATA1}
DATA3=	${DATA2:S/ll/rr/g}
DATA4:=	${DATA2:S/ll/rr/g}
DATA2?=	allo
DATA5:= ${DATA2:S/ll/ii/g} ${DATA1:S/ll/rr/g}
DATA2=	yello
DATA1:=	${DATA5:S/l/r/g}
NIL=

SMAKE=	MAKEFLAGS= ${MAKE} -C ${.CURDIR}

all:
	@echo '1..16'
	@${SMAKE} C_check || { ${MAKE} -C ${.CURDIR} failure ; }
	@echo "ok 1 - C_check # Test of -C flag existence detected no regression."
	@echo 1:${DATA1} 2:${DATA2} 3:${DATA3} 4:${DATA4} 5:${DATA5} | \
		diff -u ${.CURDIR}/regress.variables.out - || \
		${SMAKE} failure
	@echo "ok 2 - test_variables # Test variables detected no regression, output matches."
	@${SMAKE} double 2>/dev/null || ${SMAKE} failure
	@echo "ok 3 - test_targets # Test targets detected no regression."
	@${SMAKE} sysvmatch || ${SMAKE} failure
	@echo "ok 4 - sysvmatch # Test sysvmatch detected no regression."
	@! ${SMAKE} lhs_expn && true || ${SMAKE} failure
	@echo "ok 5 lhs_expn # Test lhs_expn detected no regression."
	@${SMAKE} notdef || ${SMAKE} failure
	@echo "ok 6 - notdef # Test notdef detected no regression."
	@${SMAKE} modifiers || ${SMAKE} failure
	@echo "ok 7 - modifiers # Test modifiers detected no regression."
	@${SMAKE} arith_expr || ${SMAKE} failure
	@echo "ok 8 arith_expr # Test arith_expr detected no regression."
	@${SMAKE} PATH_exists || ${SMAKE} failure
	@echo "ok 9 PATH_exists # Test PATH_exists detected no regression."
	@${SMAKE} double_quotes || ${SMAKE} failure
	@echo "ok 10 double_quotes # Test double_quotes detected no regression."
	@! ${SMAKE} double_quotes2 >/dev/null 2>&1 && true || ${SMAKE} failure
	@echo "ok 11 double_quotes2 # Test double_quotes2 detected no regression."
	@${SMAKE} pass_cmd_vars || ${SMAKE} failure
	@echo "ok 12 pass_cmd_vars # Test pass_cmd_vars detected no regression."
	@${SMAKE} plus_flag || ${SMAKE} failure
	@echo "ok 13 plus_flag # Test plus_flag detected no regression."
	@! ${SMAKE} shell >/dev/null 2>&1 && true || ${SMAKE} failure
	@echo "ok 14 shell # Test shell detected no regression."
	@${SMAKE} shell_1 || ${SMAKE} failure
	@echo "ok 15 shell_1 # Test shell_1 detected no regression."
.if !defined(.PARSEDIR)
	@${SMAKE} shell_2 || ${SMAKE} failure
	@echo "ok 16 shell_2 # Test shell_2 detected no regression."
.endif

.if make(C_check)
C_check:
.endif

.if make(double)
# Doubly-defined targets.  make(1) will warn, but use the "right" one.  If it
# switches to using the "non-right" one, it breaks things worse than a little
# regression test.
double:
	@true

double:
	@false
.endif

.if make(sysvmatch)
# Some versions of FreeBSD make(1) do not handle a nil LHS in sysvsubst.
sysvmatch:
	@echo EMPTY ${NIL:=foo} LHS | \
		diff -u ${.CURDIR}/regress.sysvmatch.out - || false
.endif

# A bogus target for the lhs_expn test;  If this is reached, then the make(1)
# program has not errored out because of the recursion caused by not expanding
# the left-hand-side's embedded variables above.
lhs_expn:
	@true

.if make(notdef)
# make(1) claims to only evaluate a conditional as far as is necessary
# to determine its value; that was not always the case.
.undef notdef
notdef:
.if defined(notdef) && ${notdef:M/}
.endif
.endif

.if make(modifiers)
.if defined(.PARSEDIR)
# check if bmake can expand plain variables
.MAKE.EXPAND_VARIABLES= yes
x!= ${SMAKE} -V .CURDIR:H
modifiers:
.if ${.CURDIR:H} != "$x"
	@false
.endif
.else
# See if make(1) supports the C modifier.
modifiers: dollarV
	@if ${SMAKE} -V .CURDIR:C/.// 2>&1 >/dev/null | \
	    grep -q "Unknown modifier 'C'"; then \
		false; \
	fi

# check that make -V '${VAR}' works
V_expn != V_OK=ok ${SMAKE} -r -f /dev/null -V '$${V_OK}'
dollarV:
.if ${V_expn} == ""
	@false
.endif
.endif
.endif

.if make(arith_expr)
arith_expr:
# See if arithmetic expression parsing is broken.
# The different spacing below is intentional.
VALUE=	0
.if (${VALUE} < 0)||(${VALUE}>0)
.endif
.endif

.if make(PATH_exists)
PATH_exists:
.PATH: ${.CURDIR}
.if !exists(${.CURDIR}/) || !exists(${.CURDIR}/.) || !exists(${.CURDIR}/..)
.error exists() failed
.endif
.endif

.if make(double_quotes)
VALUE=	foo ""
double_quotes:
.if ${VALUE:S/$//} != ${VALUE}
.error "" reduced to "
.endif
.endif

.if make(double_quotes2)
double_quotes2:
	@cat /dev/null ""
.endif

#
# Check passing of variable via MAKEFLAGS
#
.if make(pass_cmd_vars)
pass_cmd_vars:
	@${SMAKE} CMD1=cmd1 CMD2=cmd2 pass_cmd_vars_1
	@${SMAKE} CMD1=cmd1 CMD2=cmd2 pass_cmd_vars_2
	@${SMAKE} CMD1=cmd1 CMD2=cmd2 pass_cmd_vars_3
	@${SMAKE} CMD1=cmd1 CMD2=cmd2 pass_cmd_vars_4
.endif

#
# Check that the variable definition arrived from the calling make
#
.if make(pass_cmd_vars_1)
# These values should get overridden by the commandline
CMD1=oops1
CMD2=oops2
pass_cmd_vars_1:
	@:

.if ${CMD1} != cmd1 || ${CMD2} != cmd2
.error variables not passed through MAKEFLAGS
.endif
.endif

.if make(pass_cmd_vars_2)
# Check that we cannot override the passed variables
CMD1=foo1
CMD2=foo2

.if ${CMD1} != cmd1 || ${CMD2} != cmd2
.error MAKEFLAGS-passed variables overridden
.endif

pass_cmd_vars_2:
	@:
.endif

.if make(pass_cmd_vars_3)
# Check that we can override the passed variables on the next sub-make's
# command line

pass_cmd_vars_3:
	@${SMAKE} CMD1=foo1 pass_cmd_vars_3_1
.endif

.if make(pass_cmd_vars_3_1)
.if ${CMD1} != foo1 || ${CMD2} != cmd2
.error MAKEFLAGS-passed variables not overridden on command line
.endif
pass_cmd_vars_3_1:
	@:
.endif

.if make(pass_cmd_vars_4)
# Ensure that a variable assignment passed via MAKEFLAGS may be overwritten
# by evaluating the .MAKEFLAGS target.

.MAKEFLAGS: CMD1=baz1

pass_cmd_vars_4:
	@${SMAKE} pass_cmd_vars_4_1

.if ${CMD1} != baz1 || ${CMD2} != cmd2
.error MAKEFLAGS-passed variables not overridden via .MAKEFLAGS target
.endif

.endif
.if make(pass_cmd_vars_4_1)
.if ${CMD1} != baz1 || ${CMD2} != cmd2
.error MAKEFLAGS-passed variables not overridden via .MAKEFLAGS target (2)
.endif
pass_cmd_vars_4_1:
	@:
.endif

#
# Test whether make supports the '+' flag (meaning: execute even with -n)
#
.if make(plus_flag)
OUT != ${SMAKE} -n plus_flag_1
.if ${OUT:M/tmp} != "/tmp"
.error make doesn't handle + flag
.endif
plus_flag:
	@:
.endif
.if make(plus_flag_1)
plus_flag_1:
	+@cd /tmp; pwd
.endif

.if make(shell)
# Test if make fully supports the .SHELL specification.
.SHELL: path=/nonexistent
A!= echo ok
shell:
.endif

.if make(shell_1)
# Test if setting the shell by name only works. Because we have no ksh
# in the base system we test that we can set sh and csh. We try only exact
# matching names and do not exercise the rather strange matching algorithm.
shell_1:
	@${SMAKE} shell_1_csh
	@${SMAKE} shell_1_sh
.endif
.if make(shell_1_csh)
.SHELL: name="csh"
shell_1_csh:
	@ps -ax -opid,command | awk '$$1=="'$$$$'" { print $$2 }' | grep -E -q '^(/bin/)?csh$$'
.endif
.if make(shell_1_sh)
.SHELL: name="sh"
shell_1_sh:
	@ps -ax -opid,command | awk '$$1=="'$$$$'" { print $$2 }' | grep -E -q '^(/bin/)?sh$$'
.endif

.if make(shell_2)
# Test if we can replace the shell specification. We do this by using
# a shell scripts that prints us its arguments and standard input as the shell
shell_2: shell_test
	@${SMAKE} -B shell_2B | \
		diff -u ${.CURDIR}/regress.shell_2B.out - || false
	@${SMAKE} -j1 shell_2j | \
		diff -u ${.CURDIR}/regress.shell_2j.out - || false
.endif

.if make(shell_2B)
.SHELL: name="echo" path="${.OBJDIR}/shell_test" quiet="be quiet" echo="be verbose" filter="be verbose" echoFlag="x" errFlag="y" hasErrCtl=y check="check errors" ignore="ignore errors"

shell_2B:
	-@funny $$
	funnier $$
.endif

.if make(shell_2j)
.SHELL: name="echo" path="${.OBJDIR}/shell_test" quiet="be quiet" echo="be verbose" filter="be verbose" echoFlag="x" errFlag="y" hasErrCtl=y check="check errors" ignore="ignore errors"

shell_2j:
	-@funny $$
	funnier $$
.endif

failure:
	@echo "not ok # Test failed: regression detected.  See above."
	@false

CLEANFILES= shell_test

.include <bsd.obj.mk>
