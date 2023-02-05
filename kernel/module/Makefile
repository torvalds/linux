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
# ANDROID: GKI: Generate headerfiles required for gki_module.o
#
# Dependencies on generated files need to be listed explicitly
$(obj)/gki_module.o: $(obj)/gki_module_protected_exports.h \
			$(obj)/gki_module_unprotected.h

ALL_KMI_SYMBOLS := all_kmi_symbols

$(obj)/gki_module_unprotected.h: $(ALL_KMI_SYMBOLS) \
				$(srctree)/scripts/gen_gki_modules_headers.sh
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/gen_gki_modules_headers.sh $@ \
	"$(srctree)" \
	$(ALL_KMI_SYMBOLS)

# Generate symbol list with union of all symbol list for arm64; empty for others
$(ALL_KMI_SYMBOLS): $(if $(filter arm64,$(ARCH)),$(wildcard $(srctree)/android/abi_gki_aarch64_*),)
	$(if $(strip $^),cat $^ > $(ALL_KMI_SYMBOLS), echo "" > $(ALL_KMI_SYMBOLS))

$(obj)/gki_module_protected_exports.h: $(srctree)/android/abi_gki_protected_exports \
				$(srctree)/scripts/gen_gki_modules_headers.sh
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/gen_gki_modules_headers.sh $@ \
	"$(srctree)" \
	$<
