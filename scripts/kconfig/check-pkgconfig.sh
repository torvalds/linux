#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Check for pkg-config presence

if [ -z $(command -v pkg-config) ]; then
	echo "'make *config' requires 'pkg-config'. Please install it." 1>&2
	exit 1
fi
