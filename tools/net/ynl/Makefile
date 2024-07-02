# SPDX-License-Identifier: GPL-2.0

SUBDIRS = lib generated samples

all: $(SUBDIRS)

samples: | lib generated

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

.PHONY: all clean distclean $(SUBDIRS)
