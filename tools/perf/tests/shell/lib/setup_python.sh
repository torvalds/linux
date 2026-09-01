#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

if [ "x$PYTHON" = "x" ]
then
  python3 --version >/dev/null 2>&1 && PYTHON=python3
fi
if [ "x$PYTHON" = "x" ]
then
  python --version >/dev/null 2>&1 && PYTHON=python
fi
if [ "x$PYTHON" = "x" ]
then
  echo Skipping test, python not detected please set environment variable PYTHON.
  exit 2
fi

# Set PYTHONPATH to find the in-tree built perf.so first, avoiding system-wide perf.so
if [ -n "$PERF_EXEC_PATH" ] && [ -d "$PERF_EXEC_PATH/python" ]; then
  PYTHONPATH_DIR="$PERF_EXEC_PATH/python"
elif [ -d "$(dirname "$0")/../../python" ]; then
  PYTHONPATH_DIR="$(dirname "$0")/../../python"
elif [ -d "$(dirname "$0")/../python" ]; then
  PYTHONPATH_DIR="$(dirname "$0")/../python"
fi

if [ -n "$PYTHONPATH_DIR" ]; then
  export PYTHONPATH="$PYTHONPATH_DIR${PYTHONPATH:+:$PYTHONPATH}"
fi
