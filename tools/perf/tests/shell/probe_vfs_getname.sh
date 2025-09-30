#!/bin/bash
# Add vfs_getname probe to get syscall args filenames (exclusive)

# SPDX-License-Identifier: GPL-2.0
# Arnaldo Carvalho de Melo <acme@kernel.org>, 2017

# shellcheck source=lib/probe.sh
. "$(dirname $0)"/lib/probe.sh

skip_if_no_perf_probe || exit 2
[ "$(id -u)" = 0 ] || exit 2

# shellcheck source=lib/probe_vfs_getname.sh
. "$(dirname $0)"/lib/probe_vfs_getname.sh

add_probe_vfs_getname
err=$?

if [ $err -eq 1 ] ; then
	skip_if_no_debuginfo
	err=$?
fi

cleanup_probe_vfs_getname
exit $err
