# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the linux journaling routines.
#

ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE=ANDROID_GKI_VFS_EXPORT_ONLY

obj-$(CONFIG_JBD2) += jbd2.o

jbd2-objs := transaction.o commit.o recovery.o checkpoint.o revoke.o journal.o
