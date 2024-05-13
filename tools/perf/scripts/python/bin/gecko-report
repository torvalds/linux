#!/bin/bash
# description: create firefox gecko profile json format from perf.data
if [ "$*" = "-i -" ]; then
perf script -s "$PERF_EXEC_PATH"/scripts/python/gecko.py
else
perf script -s "$PERF_EXEC_PATH"/scripts/python/gecko.py -- "$@"
fi
