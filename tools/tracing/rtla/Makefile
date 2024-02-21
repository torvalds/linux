NAME	:=	rtla
# Follow the kernel version
VERSION :=	$(shell cat VERSION 2> /dev/null || make -sC ../../.. kernelversion | grep -v make)

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
MKDIR	=	mkdir
FOPTS	:=	-flto=auto -ffat-lto-objects -fexceptions -fstack-protector-strong \
		-fasynchronous-unwind-tables -fstack-clash-protection
WOPTS	:= 	-Wall -Werror=format-security -Wp,-D_FORTIFY_SOURCE=2 -Wp,-D_GLIBCXX_ASSERTIONS -Wno-maybe-uninitialized

ifeq ($(CC),clang)
  FOPTS := $(filter-out -ffat-lto-objects, $(FOPTS))
  WOPTS := $(filter-out -Wno-maybe-uninitialized, $(WOPTS))
endif

TRACEFS_HEADERS	:= $$($(PKG_CONFIG) --cflags libtracefs)

CFLAGS	:=	-O -g -DVERSION=\"$(VERSION)\" $(FOPTS) $(MOPTS) $(WOPTS) $(TRACEFS_HEADERS) $(EXTRA_CFLAGS)
LDFLAGS	:=	-flto=auto -ggdb $(EXTRA_LDFLAGS)
LIBS	:=	$$($(PKG_CONFIG) --libs libtracefs)

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
SRCTREE	:=	$(or $(BUILD_SRC),$(CURDIR))

# If running from the tarball, man pages are stored in the Documentation
# dir. If running from the kernel source, man pages are stored in
# Documentation/tools/rtla/.
ifneq ($(wildcard Documentation/.*),)
DOCSRC	=	Documentation/
else
DOCSRC	=	$(SRCTREE)/../../../Documentation/tools/rtla/
endif

LIBTRACEEVENT_MIN_VERSION = 1.5
LIBTRACEFS_MIN_VERSION = 1.3

.PHONY:	all warnings show_warnings
all:	warnings rtla

TEST_LIBTRACEEVENT = $(shell sh -c "$(PKG_CONFIG) --atleast-version $(LIBTRACEEVENT_MIN_VERSION) libtraceevent > /dev/null 2>&1 || echo n")
ifeq ("$(TEST_LIBTRACEEVENT)", "n")
WARNINGS = show_warnings
MISSING_LIBS += echo "**   libtraceevent version $(LIBTRACEEVENT_MIN_VERSION) or higher";
MISSING_PACKAGES += "libtraceevent-devel"
MISSING_SOURCE += echo "**  https://git.kernel.org/pub/scm/libs/libtrace/libtraceevent.git/ ";
endif

TEST_LIBTRACEFS = $(shell sh -c "$(PKG_CONFIG) --atleast-version $(LIBTRACEFS_MIN_VERSION) libtracefs > /dev/null 2>&1 || echo n")
ifeq ("$(TEST_LIBTRACEFS)", "n")
WARNINGS = show_warnings
MISSING_LIBS += echo "**   libtracefs version $(LIBTRACEFS_MIN_VERSION) or higher";
MISSING_PACKAGES += "libtracefs-devel"
MISSING_SOURCE += echo "**  https://git.kernel.org/pub/scm/libs/libtrace/libtracefs.git/ ";
endif

define show_dependencies
	@echo "********************************************";				\
	echo "** NOTICE: Failed build dependencies";					\
	echo "**";									\
	echo "** Required Libraries:";							\
	$(MISSING_LIBS)									\
	echo "**";									\
	echo "** Consider installing the latest libtracefs from your";			\
	echo "** distribution, e.g., 'dnf install $(MISSING_PACKAGES)' on Fedora,";	\
	echo "** or from source:";							\
	echo "**";									\
	$(MISSING_SOURCE)								\
	echo "**";									\
	echo "********************************************"
endef

show_warnings:
	$(call show_dependencies);

ifneq ("$(WARNINGS)", "")
ERROR_OUT = $(error Please add the necessary dependencies)

warnings: $(WARNINGS)
	$(ERROR_OUT)
endif

rtla: $(OBJ)
	$(CC) -o rtla $(LDFLAGS) $(OBJ) $(LIBS)

static: $(OBJ)
	$(CC) -o rtla-static $(LDFLAGS) --static $(OBJ) $(LIBS) -lpthread -ldl

.PHONY: install
install: doc_install
	$(MKDIR) -p $(DESTDIR)$(BINDIR)
	$(INSTALL) rtla -m 755 $(DESTDIR)$(BINDIR)
	$(STRIP) $(DESTDIR)$(BINDIR)/rtla
	@test ! -f $(DESTDIR)$(BINDIR)/osnoise || rm $(DESTDIR)$(BINDIR)/osnoise
	ln -s rtla $(DESTDIR)$(BINDIR)/osnoise
	@test ! -f $(DESTDIR)$(BINDIR)/hwnoise || rm $(DESTDIR)$(BINDIR)/hwnoise
	ln -s rtla $(DESTDIR)$(BINDIR)/hwnoise
	@test ! -f $(DESTDIR)$(BINDIR)/timerlat || rm $(DESTDIR)$(BINDIR)/timerlat
	ln -s rtla $(DESTDIR)$(BINDIR)/timerlat

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
