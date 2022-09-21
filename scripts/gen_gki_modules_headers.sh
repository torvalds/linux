#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright 2022 Google LLC
# Author: ramjiyani@google.com (Ramji Jiyani)
#

#
# Generates header file with list of unprotected symbols
#
# Called By: KERNEL_SRC/kernel/Makefile if CONFIG_MODULE_SIG_PROTECT=y
#
# gki_module_unprotected.h: Symbols allowed to _access_ by unsigned modules
#
# If valid symbol file doesn't exists then still generates valid C header files for
# compilation to proceed with no symbols to protect
#

# Collect arguments from Makefile
TARGET=$1
SRCTREE=$2

set -e

#
# Common Definitions
#
# Use "make V=1" to debug this script.
case "$KBUILD_VERBOSE" in
*1*)
	set -x
	;;
esac

#
# generate_header():
# Args: $1 = Name of the header file
#       $2 = Input symbol list
#       $3 = Symbol type ("unprotected")
#
generate_header() {
	local header_file=$1
	local symbol_file=$2
	local symbol_type=$3

	if [ -f "${header_file}" ]; then
		rm -f -- "${header_file}"
	fi

	# Find Maximum symbol name length if valid symbol_file exist
	if [  -s "${symbol_file}" ]; then
		# Skip 1st line (symbol header), Trim white spaces & +1 for null termination
		local max_name_len=$(awk '
				{
					$1=$1;
					if ( length > L && NR > 1) {
						L=length
					}
				} END { print ++L }' "${symbol_file}")
	else
		# Set to 1 to generate valid C header file
		local max_name_len=1
	fi

	# Header generation
	cat > "${header_file}" <<- EOT
	/*
	 * DO NOT EDIT
	 *
	 * Build generated header file with unprotected symbols/exports
	 */

	#define NO_OF_$(printf ${symbol_type} | tr [:lower:] [:upper:])_SYMBOLS \\
	$(printf '\t')(sizeof(gki_${symbol_type}_symbols) / sizeof(gki_${symbol_type}_symbols[0]))
	#define MAX_$(printf ${symbol_type} | tr [:lower:] [:upper:])_NAME_LEN (${max_name_len})

	static const char gki_${symbol_type}_symbols[][MAX_$(printf ${symbol_type} |
							tr [:lower:] [:upper:])_NAME_LEN] = {
	EOT

	# If a valid symbol_file present add symbols in an array except the 1st line
	if [  -s "${symbol_file}" ]; then
		sed -e 's/^[ \t]*/\t"/;s/[ \t]*$/",/' "${symbol_file}" >> "${header_file}"
	fi

	# Terminate the file
	echo "};" >> "${header_file}"
}

# Sorted list of vendor symbols
GKI_VENDOR_SYMBOLS="${OUT_DIR}/abi_symbollist.raw"

generate_header "${TARGET}" "${GKI_VENDOR_SYMBOLS}" "unprotected"

