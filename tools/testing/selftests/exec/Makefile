CFLAGS = -Wall

TEST_GEN_PROGS := execveat
TEST_GEN_FILES := execveat.symlink execveat.denatured script subdir
# Makefile is a run-time dependency, since it's accessed by the execveat test
TEST_FILES := Makefile

EXTRA_CLEAN := $(OUTPUT)/subdir.moved $(OUTPUT)/execveat.moved $(OUTPUT)/xxxxx*

include ../lib.mk

$(OUTPUT)/subdir:
	mkdir -p $@
$(OUTPUT)/script:
	echo '#!/bin/sh' > $@
	echo 'exit $$*' >> $@
	chmod +x $@
$(OUTPUT)/execveat.symlink: $(OUTPUT)/execveat
	cd $(OUTPUT) && ln -s -f $(shell basename $<) $(shell basename $@)
$(OUTPUT)/execveat.denatured: $(OUTPUT)/execveat
	cp $< $@
	chmod -x $@

