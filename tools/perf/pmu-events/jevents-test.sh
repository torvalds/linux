#!/bin/sh
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
# Validate that the legacy jevents and jevents.py produce identical output.
set -e

JEVENTS="$1"
JEVENTS_PY="$2"
ARCH_PATH="$3"
JEVENTS_C_GENERATED=$(mktemp /tmp/jevents_c.XXXXX.c)
JEVENTS_PY_GENERATED=$(mktemp /tmp/jevents_py.XXXXX.c)

cleanup() {
  rm "$JEVENTS_C_GENERATED" "$JEVENTS_PY_GENERATED"
  trap - exit term int
}
trap cleanup exit term int

for path in "$ARCH_PATH"/*
do
  arch=$(basename $path)
  if [ "$arch" = "test" ]
  then
    continue
  fi
  echo "Checking architecture: $arch"
  echo "Generating using jevents.c"
  "$JEVENTS" "$arch" "$ARCH_PATH" "$JEVENTS_C_GENERATED"
  echo "Generating using jevents.py"
  "$JEVENTS_PY" "$arch" "$ARCH_PATH" "$JEVENTS_PY_GENERATED"
  echo "Diffing"
  diff -u "$JEVENTS_C_GENERATED" "$JEVENTS_PY_GENERATED"
done
cleanup
