# SPDX-License-Identifier: GPL-2.0
# Makefile for Hyper-V tools

WARNINGS = -Wall -Wextra
CFLAGS = $(WARNINGS) -g $(shell getconf LFS_CFLAGS)

CFLAGS += -D__EXPORTED_HEADERS__ -I../../include/uapi -I../../include

sbindir ?= /usr/sbin
libexecdir ?= /usr/libexec
sharedstatedir ?= /var/lib

ALL_PROGRAMS := hv_kvp_daemon hv_vss_daemon hv_fcopy_daemon

ALL_SCRIPTS := hv_get_dhcp_info.sh hv_get_dns_info.sh hv_set_ifconfig.sh

all: $(ALL_PROGRAMS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) hv_kvp_daemon hv_vss_daemon hv_fcopy_daemon

install: all
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
