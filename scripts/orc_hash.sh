#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) Meta Platforms, Inc. and affiliates.

set -e

printf '%s' '#define ORC_HASH '

awk '
/^#define ORC_(REG|TYPE)_/ { print }
/^struct orc_entry {$/ { p=1 }
p { print }
/^}/ { p=0 }' |
	sha1sum |
	cut -d " " -f 1 |
	sed 's/\([0-9a-f]\{2\}\)/0x\1,/g'
