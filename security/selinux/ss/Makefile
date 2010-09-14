#
# Makefile for building the SELinux security server as part of the kernel tree.
#

EXTRA_CFLAGS += -Isecurity/selinux -Isecurity/selinux/include
obj-y := ss.o

ss-y := ebitmap.o hashtab.o symtab.o sidtab.o avtab.o policydb.o services.o conditional.o mls.o status.o

