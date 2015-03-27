export srctree := ../../../..
export CC      := gcc
export LD      := ld
export AR      := ar

build := -f $(srctree)/tools/build/Makefile.build dir=. obj
ex: ex-in.o libex-in.o
	gcc -o $@ $^

ex.%: FORCE
	make -f $(srctree)/tools/build/Makefile.build dir=. $@

ex-in.o: FORCE
	make $(build)=ex

libex-in.o: FORCE
	make $(build)=libex

clean:
	find . -name '*.o' -delete -o -name '\.*.cmd' -delete -o -name '\.*.d' -delete
	rm -f ex ex.i ex.s

.PHONY: FORCE
