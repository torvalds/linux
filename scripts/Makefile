# SPDX-License-Identifier: GPL-2.0
###
# scripts contains sources for various helper programs used throughout
# the kernel for the build process.
# ---------------------------------------------------------------------------
# kallsyms:      Find all symbols in vmlinux

HOST_EXTRACFLAGS += -I$(srctree)/tools/include

always-$(CONFIG_BUILD_BIN2C)			+= bin2c
always-$(CONFIG_KALLSYMS)			+= kallsyms
always-$(BUILD_C_RECORDMCOUNT)			+= recordmcount
always-$(CONFIG_BUILDTIME_TABLE_SORT)		+= sorttable
always-$(CONFIG_ASN1)				+= asn1_compiler
always-$(CONFIG_MODULE_SIG_FORMAT)		+= sign-file
always-$(CONFIG_SYSTEM_TRUSTED_KEYRING)		+= extract-cert
always-$(CONFIG_SYSTEM_EXTRA_CERTIFICATE)	+= insert-sys-cert

HOSTCFLAGS_sorttable.o = -I$(srctree)/tools/include
HOSTCFLAGS_asn1_compiler.o = -I$(srctree)/include
HOSTLDLIBS_sign-file = -lcrypto
HOSTLDLIBS_extract-cert = -lcrypto

ifdef CONFIG_UNWINDER_ORC
ifeq ($(ARCH),x86_64)
ARCH := x86
endif
HOSTCFLAGS_sorttable.o += -I$(srctree)/tools/arch/x86/include
HOSTCFLAGS_sorttable.o += -DUNWINDER_ORC_ENABLED
HOSTLDLIBS_sorttable = -lpthread
endif

hostprogs := $(always-y) $(always-m)

# The following programs are only built on demand
hostprogs += unifdef

subdir-$(CONFIG_GCC_PLUGINS) += gcc-plugins
subdir-$(CONFIG_MODVERSIONS) += genksyms
subdir-$(CONFIG_SECURITY_SELINUX) += selinux

# Let clean descend into subdirs
subdir-	+= basic dtc gdb kconfig mod
