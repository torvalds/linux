# SPDX-License-Identifier: GPL-2.0

include ../../scripts/Makefile.arch

INSTALL	?= install
prefix  ?= /usr
ifeq ($(LP64), 1)
  libdir_relative = lib64
else
  libdir_relative = lib
endif
libdir  ?= $(prefix)/$(libdir_relative)
includedir ?= $(prefix)/include

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
	rm -rf pyynl/__pycache__
	rm -rf pyynl/lib/__pycache__
	rm -rf pyynl.egg-info
	rm -rf build

install: libynl.a lib/*.h
	@echo -e "\tINSTALL libynl.a"
	@$(INSTALL) -d $(DESTDIR)$(libdir)
	@$(INSTALL) -m 0644 libynl.a $(DESTDIR)$(libdir)/libynl.a
	@echo -e "\tINSTALL libynl headers"
	@$(INSTALL) -d $(DESTDIR)$(includedir)/ynl
	@$(INSTALL) -m 0644 lib/*.h $(DESTDIR)$(includedir)/ynl/
	@echo -e "\tINSTALL pyynl"
	@pip install --prefix=$(DESTDIR)$(prefix) .
	@make -C generated install

.PHONY: all clean distclean install $(SUBDIRS)
