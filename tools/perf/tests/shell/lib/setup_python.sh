#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

if [ -z "$PYTHON" ]
then
  python3 --version >/dev/null 2>&1 && PYTHON=python3
fi
if [ -z "$PYTHON" ]
then
  python --version >/dev/null 2>&1 && PYTHON=python
fi
if [ -z "$PYTHON" ]
then
  echo Skipping test, python not detected please set environment variable PYTHON.
  exit 2
fi
export PYTHON

# Set PYTHONPATH to find the built perf.so and standalone scripts first,
# avoiding system-wide perf.so
if [ -n "$PERF_EXEC_PATH" ] && [ -d "$PERF_EXEC_PATH/python" ]; then
  PYTHONPATH_DIR="$PERF_EXEC_PATH/python"
elif [ -n "${BASH_SOURCE[0]}" ] && [ -d "$(dirname "${BASH_SOURCE[0]}")/../../../python" ]; then
  PYTHONPATH_DIR="$(dirname "${BASH_SOURCE[0]}")/../../../python"
elif [ -d "$(dirname "$0")/../../../python" ]; then
  PYTHONPATH_DIR="$(dirname "$0")/../../../python"
elif [ -d "$(dirname "$0")/../../python" ]; then
  PYTHONPATH_DIR="$(dirname "$0")/../../python"
elif [ -d "$(dirname "$0")/../python" ]; then
  PYTHONPATH_DIR="$(dirname "$0")/../python"
fi

if [ -n "${BASH_SOURCE[0]}" ] && [ -d "$(dirname "${BASH_SOURCE[0]}")/../../../python" ]; then
  SRC_PYTHON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../python" && pwd)"
  export PYTHONPATH="$SRC_PYTHON_DIR${PYTHONPATH:+:$PYTHONPATH}"
  if [ -z "$PERF_EXEC_PATH" ] || [ ! -f "$PERF_EXEC_PATH/python/perf_live.py" ]; then
    PERF_EXEC_PATH="$(dirname "$SRC_PYTHON_DIR")"
    export PERF_EXEC_PATH
  fi
fi

if [ -n "$PYTHONPATH_DIR" ]; then
  export PYTHONPATH="$PYTHONPATH_DIR${PYTHONPATH:+:$PYTHONPATH}"
fi

PERF_BIN=$(which perf 2>/dev/null || true)
if [ -n "$PERF_BIN" ] && [ -d "$(dirname "$PERF_BIN")/python" ]; then
  PERF_BIN_PYTHON="$(dirname "$PERF_BIN")/python"
  export PYTHONPATH="$PERF_BIN_PYTHON${PYTHONPATH:+:$PYTHONPATH}"
fi
