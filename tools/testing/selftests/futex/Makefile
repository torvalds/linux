SUBDIRS := functional

.PHONY: all clean
all:
	for DIR in $(SUBDIRS); do $(MAKE) -C $$DIR $@ ; done

run_tests: all
	./run.sh

clean:
	for DIR in $(SUBDIRS); do $(MAKE) -C $$DIR $@ ; done
