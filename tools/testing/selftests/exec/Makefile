CFLAGS = -Wall

TEST_GEN_PROGS := execveat
TEST_GEN_FILES := execveat.symlink execveat.denatured script subdir
# Makefile is a run-time dependency, since it's accessed by the execveat test
TEST_FILES := Makefile

EXTRA_CLEAN := subdir.moved execveat.moved xxxxx*

include ../lib.mk

subdir:
	mkdir -p $@
script:
	echo '#!/bin/sh' > $@
	echo 'exit $$*' >> $@
	chmod +x $@
execveat.symlink: execveat
	ln -s -f $< $@
execveat.denatured: execveat
	cp $< $@
	chmod -x $@
