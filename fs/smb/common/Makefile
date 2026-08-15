# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for Linux filesystem routines that are shared by client and server.
#

obj-$(CONFIG_SMBFS) += cifs_md4.o
obj-$(CONFIG_SMBFS) += smb_compress.o

smb_compress-y := compress/compress.o compress/lz77.o
