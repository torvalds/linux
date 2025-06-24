#!/bin/bash
# DRM PMU
# SPDX-License-Identifier: GPL-2.0

set -e

output=$(mktemp /tmp/perf.drm_pmu.XXXXXX.txt)

cleanup() {
  rm -f "${output}"

  trap - EXIT TERM INT
}

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

# Array to store file descriptors and device names
declare -A device_fds

# Open all devices and store file descriptors. Opening the device will create a
# /proc/$$/fdinfo file containing the DRM statistics.
fd_count=3 # Start with file descriptor 3
for device in /dev/dri/*
do
  if [[ ! -c "$device" ]]
  then
    continue
  fi
  major=$(stat -c "%Hr" "$device")
  if [[ "$major" != 226 ]]
  then
    continue
  fi
  echo "Opening $device"
  eval "exec $fd_count<\"$device\""
  echo "fdinfo for: $device (FD: $fd_count)"
  cat "/proc/$$/fdinfo/$fd_count"
  echo
  device_fds["$device"]="$fd_count"
  fd_count=$((fd_count + 1))
done

if [[ ${#device_fds[@]} -eq 0 ]]
then
  echo "No DRM devices found [Skip]"
  cleanup
  exit 2
fi

# For each DRM event
err=0
for p in $(perf list --raw-dump drm-)
do
  echo -n "Testing perf stat of $p. "
  perf stat -e "$p" --pid=$$ true > "$output" 2>&1
  if ! grep -q "$p" "$output"
  then
    echo "Missing DRM event in: [Failed]"
    cat "$output"
    err=1
  else
    echo "[OK]"
  fi
done

# Close all file descriptors
for fd in "${device_fds[@]}"; do
  eval "exec $fd<&-"
done

# Finished
cleanup
exit $err
