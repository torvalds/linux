#!/bin/bash

#
# Build U-Boot image when `mkimage' tool is available.
#

MKIMAGE=$(type -path mkimage)

if [ -z "${MKIMAGE}" ]; then
	# Doesn't exist
	echo '"mkimage" command not found - U-Boot images will not be built' >&2
	exit 0;
fi

# Call "mkimage" to create U-Boot image
${MKIMAGE} "$@"
