#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -- $(ls -dn "$1")
printf '%s\n' "$5"
