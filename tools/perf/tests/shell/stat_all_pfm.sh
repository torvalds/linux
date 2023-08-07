#!/bin/sh
# perf all libpfm4 events test
# SPDX-License-Identifier: GPL-2.0

if perf version --build-options | grep HAVE_LIBPFM | grep -q OFF
then
  echo "Skipping, no libpfm4 support"
  exit 2
fi

err=0
for p in $(perf list --raw-dump pfm)
do
  if echo "$p" | grep -q unc_
  then
    echo "Skipping uncore event '$p' that may require additional options."
    continue
  fi
  echo "Testing $p"
  result=$(perf stat --pfm-events "$p" true 2>&1)
  x=$?
  if echo "$result" | grep -q "failed to parse event $p : invalid or missing unit mask"
  then
    continue
  fi
  if test "$x" -ne "0"
  then
    echo "Unexpected exit code '$x'"
    err=1
  fi
  if ! echo "$result" | grep -q "$p" && ! echo "$result" | grep -q "<not supported>"
  then
    # We failed to see the event and it is supported. Possibly the workload was
    # too small so retry with something longer.
    result=$(perf stat --pfm-events "$p" perf bench internals synthesize 2>&1)
    x=$?
    if test "$x" -ne "0"
    then
      echo "Unexpected exit code '$x'"
      err=1
    fi
    if ! echo "$result" | grep -q "$p"
    then
      echo "Event '$p' not printed in:"
      echo "$result"
      err=1
    fi
  fi
done

exit "$err"
