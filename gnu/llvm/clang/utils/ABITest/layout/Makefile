# Usage: make test.N.report 
#
# COUNT can be over-ridden to change the number of tests generated per
# file, and TESTARGS is used to change the type generation. Make sure
# to 'make clean' after changing either of these parameters.

ABITESTGEN := ../ABITestGen.py
TESTARGS := --max-args 0 --test-layout
COUNT := 1000
TIMEOUT := 5

CFLAGS := -std=gnu99

X_COMPILER := llvm-gcc
Y_COMPILER := clang
CC := gcc

ifeq (0, 0)
X_CFLAGS := -m32
Y_CFLAGS := -m32
CC_CFLAGS := -m32
else
X_CFLAGS := -m64
Y_CFLAGS := -m64
CC_CFLAGS := -m64
endif

.PHONY: test.%.report
test.%.report: test.%.x.diff test.%.y.diff
	@for t in $^; do \
		if [ -s $$t ]; then \
			echo "TEST $*: $$t failed"; \
		fi; \
	done

.PHONY: test.%.build
test.%.build: test.%.ref test.%.x test.%.y
	@true

###

.PRECIOUS: test.%.x.diff
test.%.x.diff: test.%.ref.out test.%.x.out
	-diff $^ > $@
.PRECIOUS: test.%.y.diff
test.%.y.diff: test.%.ref.out test.%.y.out
	-diff $^ > $@

.PRECIOUS: test.%.out
test.%.out: test.%
	-./$< > $@

.PRECIOUS: test.%.ref
test.%.ref: test.%.c
	$(CC) $(CFLAGS) $(CC_CFLAGS) -o $@ $^
.PRECIOUS: test.%.x
test.%.x: test.%.c
	$(X_COMPILER) $(CFLAGS) $(X_CFLAGS) -o $@ $^
.PRECIOUS: test.%.y
test.%.y: test.%.c
	$(Y_COMPILER) $(CFLAGS) $(Y_CFLAGS) -o $@ $^

.PRECIOUS: test.%.c
test.%.c: $(ABITESTGEN)
	$(ABITESTGEN) $(TESTARGS) -o $@ --min=$(shell expr $* '*' $(COUNT))  --count=$(COUNT)

clean:	
	rm -f test.* *~
