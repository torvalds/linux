# SPDX-License-Identifier: GPL-2.0-only
SUBDIRS := ion

TEST_PROGS := run.sh

.PHONY: all clean

include ../lib.mk

all:
	@for DIR in $(SUBDIRS); do		\
		BUILD_TARGET=$(OUTPUT)/$$DIR;	\
		mkdir $$BUILD_TARGET  -p;	\
		make OUTPUT=$$BUILD_TARGET -C $$DIR $@;\
		#SUBDIR test prog name should be in the form: SUBDIR_test.sh \
		TEST=$$DIR"_test.sh"; \
		if [ -e $$DIR/$$TEST ]; then \
			rsync -a $$DIR/$$TEST $$BUILD_TARGET/; \
		fi \
	done

override define INSTALL_RULE
	mkdir -p $(INSTALL_PATH)
install -t $(INSTALL_PATH) $(TEST_PROGS) $(TEST_PROGS_EXTENDED) $(TEST_FILES)  $(TEST_GEN_PROGS) $(TEST_CUSTOM_PROGS) $(TEST_GEN_PROGS_EXTENDED) $(TEST_GEN_FILES)

	@for SUBDIR in $(SUBDIRS); do \
		BUILD_TARGET=$(OUTPUT)/$$SUBDIR;	\
		mkdir $$BUILD_TARGET  -p;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$SUBDIR INSTALL_PATH=$(INSTALL_PATH)/$$SUBDIR install; \
	done;
endef

override define CLEAN
	@for DIR in $(SUBDIRS); do		\
		BUILD_TARGET=$(OUTPUT)/$$DIR;	\
		mkdir $$BUILD_TARGET  -p;	\
		make OUTPUT=$$BUILD_TARGET -C $$DIR $@;\
	done
endef
