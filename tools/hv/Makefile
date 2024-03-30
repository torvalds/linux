# SPDX-License-Identifier: GPL-2.0
# Makefile for Hyper-V tools
include ../scripts/Makefile.include

ARCH := $(shell uname -m 2>/dev/null)
sbindir ?= /usr/sbin
libexecdir ?= /usr/libexec
sharedstatedir ?= /var/lib

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

# Do not use make's built-in rules
# (this improves performance and avoids hard-to-debug behaviour);
MAKEFLAGS += -r

override CFLAGS += -O2 -Wall -g -D_GNU_SOURCE -I$(OUTPUT)include

ALL_TARGETS := hv_kvp_daemon hv_vss_daemon
ifneq ($(ARCH), aarch64)
ALL_TARGETS += hv_fcopy_uio_daemon
endif
ALL_PROGRAMS := $(patsubst %,$(OUTPUT)%,$(ALL_TARGETS))

ALL_SCRIPTS := hv_get_dhcp_info.sh hv_get_dns_info.sh hv_set_ifconfig.sh

all: $(ALL_PROGRAMS)

export srctree OUTPUT CC LD CFLAGS
include $(srctree)/tools/build/Makefile.include

HV_KVP_DAEMON_IN := $(OUTPUT)hv_kvp_daemon-in.o
$(HV_KVP_DAEMON_IN): FORCE
	$(Q)$(MAKE) $(build)=hv_kvp_daemon
$(OUTPUT)hv_kvp_daemon: $(HV_KVP_DAEMON_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

HV_VSS_DAEMON_IN := $(OUTPUT)hv_vss_daemon-in.o
$(HV_VSS_DAEMON_IN): FORCE
	$(Q)$(MAKE) $(build)=hv_vss_daemon
$(OUTPUT)hv_vss_daemon: $(HV_VSS_DAEMON_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

HV_FCOPY_UIO_DAEMON_IN := $(OUTPUT)hv_fcopy_uio_daemon-in.o
$(HV_FCOPY_UIO_DAEMON_IN): FORCE
	$(Q)$(MAKE) $(build)=hv_fcopy_uio_daemon
$(OUTPUT)hv_fcopy_uio_daemon: $(HV_FCOPY_UIO_DAEMON_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(ALL_PROGRAMS)
	find $(or $(OUTPUT),.) -name '*.o' -delete -o -name '\.*.d' -delete

install: $(ALL_PROGRAMS)
	install -d -m 755 $(DESTDIR)$(sbindir); \
	install -d -m 755 $(DESTDIR)$(libexecdir)/hypervkvpd; \
	install -d -m 755 $(DESTDIR)$(sharedstatedir); \
	for program in $(ALL_PROGRAMS); do \
		install $$program -m 755 $(DESTDIR)$(sbindir);	\
	done; \
	install -m 755 lsvmbus $(DESTDIR)$(sbindir); \
	for script in $(ALL_SCRIPTS); do \
		install $$script -m 755 $(DESTDIR)$(libexecdir)/hypervkvpd/$${script%.sh}; \
	done

FORCE:

.PHONY: all install clean FORCE prepare
