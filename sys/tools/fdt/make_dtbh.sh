#!/bin/sh
#
# $FreeBSD$

# Script generates a $2/fdt_static_dtb.h file.

dtb_base_name=`basename $1 .dts`
echo '#define FDT_DTB_FILE "'${dtb_base_name}.dtb'"' > $2/fdt_static_dtb.h
