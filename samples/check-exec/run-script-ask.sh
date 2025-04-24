#!/usr/bin/env sh
# SPDX-License-Identifier: BSD-3-Clause

DIR="$(dirname -- "$0")"

PATH="${PATH}:${DIR}"

set -x
"${DIR}/script-ask.inc"
