CFLAGS = -Wall

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

TEST_GEN_PROGS := execveat
TEST_GEN_FILES := execveat.symlink execveat.denatured script subdir
# Makefile is a run-time dependency, since it's accessed by the execveat test
TEST_FILES := Makefile

include ../lib.mk

clean:
	rm -rf $(TEST_GEN_PROGS) $(TEST_GEN_FILES) subdir.moved execveat.moved xxxxx*
