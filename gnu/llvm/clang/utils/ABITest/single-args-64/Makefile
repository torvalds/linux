# Usage: make test.N.report 
#
# COUNT can be over-ridden to change the number of tests generated per
# file, and TESTARGS is used to change the type generation. Make sure
# to 'make clean' after changing either of these parameters.

X_CFLAGS := -m64
Y_CFLAGS := -m64
CC_CFLAGS := -m64

include ../Makefile.test.common

TESTARGS += --no-function-return --max-args 1
