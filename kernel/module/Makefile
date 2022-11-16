# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for linux kernel module support
#

# These are called from save_stack_trace() on slub debug path,
# and produce insane amounts of uninteresting coverage.
KCOV_INSTRUMENT_module.o := n

obj-y += main.o strict_rwx.o
obj-$(CONFIG_MODULE_DECOMPRESS) += decompress.o
obj-$(CONFIG_MODULE_SIG) += signing.o
obj-$(CONFIG_MODULE_SIG_PROTECT) += gki_module.o
obj-$(CONFIG_LIVEPATCH) += livepatch.o
obj-$(CONFIG_MODULES_TREE_LOOKUP) += tree_lookup.o
obj-$(CONFIG_DEBUG_KMEMLEAK) += debug_kmemleak.o
obj-$(CONFIG_KALLSYMS) += kallsyms.o
obj-$(CONFIG_PROC_FS) += procfs.o
obj-$(CONFIG_SYSFS) += sysfs.o
obj-$(CONFIG_KGDB_KDB) += kdb.o
obj-$(CONFIG_MODVERSIONS) += version.o
obj-$(CONFIG_MODULE_UNLOAD_TAINT_TRACKING) += tracking.o

#
# ANDROID: GKI: Generate headerfile required for gki_module.o
#
# Dependencies on generated files need to be listed explicitly
$(obj)/gki_module.o: $(obj)/gki_module_unprotected.h

$(obj)/gki_module_unprotected.h: $(srctree)/scripts/gen_gki_modules_headers.sh \
                                $(if $(wildcard ${OUT_DIR}/abi_symbollist.raw), ${OUT_DIR}/abi_symbollist.raw)
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/gen_gki_modules_headers.sh $@ \
	"$(srctree)"
