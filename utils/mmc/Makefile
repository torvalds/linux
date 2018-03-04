CC ?= gcc

CROSS_COMPILE=arm-linux-gnueabihf-

ifdef CROSS_COMPILE
  CC = $(CROSS_COMPILE)gcc
  LD = $(CROSS_COMPILE)ld
endif

AM_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_FORTIFY_SOURCE=2
CFLAGS ?= -g -O2
objects = \
	mmc.o \
	mmc_cmds.o \
	lsmmc.o \
	3rdparty/hmac_sha/hmac_sha2.o \
	3rdparty/hmac_sha/sha2.o

CHECKFLAGS = -Wall -Werror -Wuninitialized -Wundef

DEPFLAGS = -Wp,-MMD,$(@D)/.$(@F).d,-MT,$@

override CFLAGS := $(CHECKFLAGS) $(AM_CFLAGS) $(CFLAGS)

INSTALL = install
prefix ?= /usr/local
bindir = $(prefix)/bin
LIBS=
RESTORE_LIBS=

progs = mmc

# make C=1 to enable sparse
ifdef C
	check = sparse $(CHECKFLAGS)
endif

all: $(progs) manpages

.c.o:
ifdef C
	$(check) $<
endif
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

mmc: $(objects)
	$(CC) $(CFLAGS) -o $@ $(objects) $(LDFLAGS) $(LIBS)

manpages:
	$(MAKE) -C man

install-man:
	$(MAKE) -C man install

clean:
	rm -f $(progs) $(objects)
	$(MAKE) -C man clean

install: $(progs) install-man
	$(INSTALL) -m755 -d $(DESTDIR)$(bindir)
	$(INSTALL) $(progs) $(DESTDIR)$(bindir)

-include $(foreach obj,$(objects), $(dir $(obj))/.$(notdir $(obj)).d)

.PHONY: all clean install manpages install-man
