# SPDX-License-Identifier: GPL-2.0

SUBDIRS = lib generated samples

all: $(SUBDIRS) libynl.a

samples: | lib generated
libynl.a: | lib generated
	@echo -e "\tAR $@"
	@ar rcs $@ lib/ynl.o generated/*-user.o

$(SUBDIRS):
	@if [ -f "$@/Makefile" ] ; then \
		$(MAKE) -C $@ ; \
	fi

clean distclean:
	@for dir in $(SUBDIRS) ; do \
		if [ -f "$$dir/Makefile" ] ; then \
			$(MAKE) -C $$dir $@; \
		fi \
	done
	rm -f libynl.a

.PHONY: all clean distclean $(SUBDIRS)
