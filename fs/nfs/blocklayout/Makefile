# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the pNFS block layout driver kernel module
#
obj-$(CONFIG_PNFS_BLOCK) += blocklayoutdriver.o

blocklayoutdriver-y += blocklayout.o dev.o extent_tree.o
blocklayoutdriver-$(CONFIG_PNFS_BLOCK_LAYOUT) += rpc_pipefs.o
