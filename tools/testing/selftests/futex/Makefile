SUBDIRS := functional

TEST_PROGS := run.sh

.PHONY: all clean
all:
	for DIR in $(SUBDIRS); do $(MAKE) -C $$DIR $@ ; done

include ../lib.mk

override define RUN_TESTS
	./run.sh
endef

override define INSTALL_RULE
	mkdir -p $(INSTALL_PATH)
	install -t $(INSTALL_PATH) $(TEST_PROGS) $(TEST_PROGS_EXTENDED) $(TEST_FILES)

	@for SUBDIR in $(SUBDIRS); do \
		$(MAKE) -C $$SUBDIR INSTALL_PATH=$(INSTALL_PATH)/$$SUBDIR install; \
	done;
endef

override define EMIT_TESTS
	echo "./run.sh"
endef

clean:
	for DIR in $(SUBDIRS); do $(MAKE) -C $$DIR $@ ; done
