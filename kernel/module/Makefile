# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for linux kernel module support
#

# These are called from save_stack_trace() on slub debug path,
# and produce insane amounts of uninteresting coverage.
KCOV_INSTRUMENT_module.o := n

obj-y += main.o
obj-$(CONFIG_MODULE_DECOMPRESS) += decompress.o
obj-$(CONFIG_MODULE_SIG) += signing.o
