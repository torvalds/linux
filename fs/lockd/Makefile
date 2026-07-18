# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux lock manager stuff
#

ccflags-y += -I$(src)			# needed for trace events

obj-$(CONFIG_LOCKD) += lockd.o

lockd-y := clntlock.o clntproc.o clntxdr.o host.o svc.o svclock.o \
	   svcshare.o svcproc.o svcsubs.o mon.o trace.o netlink.o \
	   nlm3xdr_gen.o
lockd-$(CONFIG_LOCKD_V4) += clnt4xdr.o svc4proc.o nlm4xdr_gen.o
lockd-$(CONFIG_PROC_FS) += procfs.o

#
# XDR code generation (requires Python and additional packages)
#
# The generated *xdr_gen.{h,c} files are checked into git. Normal kernel
# builds do not require the xdrgen tool or its Python dependencies.
#
# Developers modifying .x files in Documentation/sunrpc/xdr/ should run
# "make xdrgen" to regenerate the affected files.
#
.PHONY: xdrgen

XDRGEN			= ../../tools/net/sunrpc/xdrgen/xdrgen

XDRGEN_DEFINITIONS	= ../../include/linux/sunrpc/xdrgen/nlm4.h \
			  ../../include/linux/sunrpc/xdrgen/nlm3.h
XDRGEN_DECLARATIONS	= nlm4xdr_gen.h nlm3xdr_gen.h
XDRGEN_SOURCE		= nlm4xdr_gen.c nlm3xdr_gen.c

xdrgen: $(XDRGEN_DEFINITIONS) $(XDRGEN_DECLARATIONS) $(XDRGEN_SOURCE)

../../include/linux/sunrpc/xdrgen/nlm4.h: ../../Documentation/sunrpc/xdr/nlm4.x
	$(XDRGEN) definitions $< > $@

../../include/linux/sunrpc/xdrgen/nlm3.h: ../../Documentation/sunrpc/xdr/nlm3.x
	$(XDRGEN) definitions $< > $@

nlm4xdr_gen.h: ../../Documentation/sunrpc/xdr/nlm4.x
	$(XDRGEN) declarations $< > $@

nlm3xdr_gen.h: ../../Documentation/sunrpc/xdr/nlm3.x
	$(XDRGEN) declarations $< > $@

nlm4xdr_gen.c: ../../Documentation/sunrpc/xdr/nlm4.x
	$(XDRGEN) source --peer server $< > $@

nlm3xdr_gen.c: ../../Documentation/sunrpc/xdr/nlm3.x
	$(XDRGEN) source --peer server $< > $@
