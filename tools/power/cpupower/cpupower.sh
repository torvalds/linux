#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2012, Sébastien Luttringer
# Copyright (C) 2024, Francesco Poli <invernomuto@paranoici.org>

ESTATUS=0

# apply CPU clock frequency options
if test -n "$FREQ"
then
    cpupower frequency-set -f "$FREQ" > /dev/null || ESTATUS=1
elif test -n "${GOVERNOR}${MIN_FREQ}${MAX_FREQ}"
then
    cpupower frequency-set \
      ${GOVERNOR:+ -g "$GOVERNOR"} \
      ${MIN_FREQ:+ -d "$MIN_FREQ"} ${MAX_FREQ:+ -u "$MAX_FREQ"} \
      > /dev/null || ESTATUS=1
fi

# apply CPU policy options
if test -n "$PERF_BIAS"
then
    cpupower set -b "$PERF_BIAS" > /dev/null || ESTATUS=1
fi

exit $ESTATUS
