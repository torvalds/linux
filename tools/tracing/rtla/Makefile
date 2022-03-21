NAME	:=	rtla
# Follow the kernel version
VERSION :=	$(shell cat VERSION 2> /dev/null || make -sC ../../.. kernelversion)

# From libtracefs:
# Makefiles suck: This macro sets a default value of $(2) for the
# variable named by $(1), unless the variable has been set by
# environment or command line. This is necessary for CC and AR
# because make sets default values, so the simpler ?= approach
# won't work as expected.
define allow-override
  $(if $(or $(findstring environment,$(origin $(1))),\
            $(findstring command line,$(origin $(1)))),,\
    $(eval $(1) = $(2)))
endef

# Allow setting CC and AR, or setting CROSS_COMPILE as a prefix.
$(call allow-override,CC,$(CROSS_COMPILE)gcc)
$(call allow-override,AR,$(CROSS_COMPILE)ar)
$(call allow-override,STRIP,$(CROSS_COMPILE)strip)
$(call allow-override,PKG_CONFIG,pkg-config)
$(call allow-override,LD_SO_CONF_PATH,/etc/ld.so.conf.d/)
$(call allow-override,LDCONFIG,ldconfig)

INSTALL	=	install
FOPTS	:=	-flto=auto -ffat-lto-objects -fexceptions -fstack-protector-strong \
		-fasynchronous-unwind-tables -fstack-clash-protection
WOPTS	:= 	-Wall -Werror=format-security -Wp,-D_FORTIFY_SOURCE=2 -Wp,-D_GLIBCXX_ASSERTIONS -Wno-maybe-uninitialized

TRACEFS_HEADERS	:= $$($(PKG_CONFIG) --cflags libtracefs)

CFLAGS	:=	-O -g -DVERSION=\"$(VERSION)\" $(FOPTS) $(MOPTS) $(WOPTS) $(TRACEFS_HEADERS)
LDFLAGS	:=	-ggdb
LIBS	:=	$$($(PKG_CONFIG) --libs libtracefs) -lprocps

SRC	:=	$(wildcard src/*.c)
HDR	:=	$(wildcard src/*.h)
OBJ	:=	$(SRC:.c=.o)
DIRS	:=	src
FILES	:=	Makefile README.txt
CEXT	:=	bz2
TARBALL	:=	$(NAME)-$(VERSION).tar.$(CEXT)
TAROPTS	:=	-cvjf $(TARBALL)
BINDIR	:=	/usr/bin
DATADIR	:=	/usr/share
DOCDIR	:=	$(DATADIR)/doc
MANDIR	:=	$(DATADIR)/man
LICDIR	:=	$(DATADIR)/licenses
SRCTREE	:=	$(if $(BUILD_SRC),$(BUILD_SRC),$(CURDIR))

# If running from the tarball, man pages are stored in the Documentation
# dir. If running from the kernel source, man pages are stored in
# Documentation/tools/rtla/.
ifneq ($(wildcard Documentation/.*),)
DOCSRC	=	Documentation/
else
DOCSRC	=	$(SRCTREE)/../../../Documentation/tools/rtla/
endif

.PHONY:	all
all:	rtla

rtla: $(OBJ)
	$(CC) -o rtla $(LDFLAGS) $(OBJ) $(LIBS)

static: $(OBJ)
	$(CC) -o rtla-static $(LDFLAGS) --static $(OBJ) $(LIBS) -lpthread -ldl

.PHONY: install
install: doc_install
	$(INSTALL) -d -m 755 $(DESTDIR)$(BINDIR)
	$(INSTALL) rtla -m 755 $(DESTDIR)$(BINDIR)
	$(STRIP) $(DESTDIR)$(BINDIR)/rtla
	@test ! -f $(DESTDIR)$(BINDIR)/osnoise || rm $(DESTDIR)$(BINDIR)/osnoise
	ln -s $(DESTDIR)$(BINDIR)/rtla $(DESTDIR)$(BINDIR)/osnoise
	@test ! -f $(DESTDIR)$(BINDIR)/timerlat || rm $(DESTDIR)$(BINDIR)/timerlat
	ln -s $(DESTDIR)$(BINDIR)/rtla $(DESTDIR)$(BINDIR)/timerlat

.PHONY: clean tarball
clean: doc_clean
	@test ! -f rtla || rm rtla
	@test ! -f rtla-static || rm rtla-static
	@test ! -f src/rtla.o || rm src/rtla.o
	@test ! -f $(TARBALL) || rm -f $(TARBALL)
	@rm -rf *~ $(OBJ) *.tar.$(CEXT)

tarball: clean
	rm -rf $(NAME)-$(VERSION) && mkdir $(NAME)-$(VERSION)
	echo $(VERSION) > $(NAME)-$(VERSION)/VERSION
	cp -r $(DIRS) $(FILES) $(NAME)-$(VERSION)
	mkdir $(NAME)-$(VERSION)/Documentation/
	cp -rp $(SRCTREE)/../../../Documentation/tools/rtla/* $(NAME)-$(VERSION)/Documentation/
	tar $(TAROPTS) --exclude='*~' $(NAME)-$(VERSION)
	rm -rf $(NAME)-$(VERSION)

.PHONY: doc doc_clean doc_install
doc:
	$(MAKE) -C $(DOCSRC)

doc_clean:
	$(MAKE) -C $(DOCSRC) clean

doc_install:
	$(MAKE) -C $(DOCSRC) install
