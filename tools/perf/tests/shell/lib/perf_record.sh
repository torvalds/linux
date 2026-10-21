# SPDX-License-Identifier: GPL-2.0

PERF_RECORD_LOGS=()

perf_record_with_retry() {
  local perfdata="$1"
  local check_cmd="$2"
  local testprog_base="$3"
  shift 3

  local logfile
  logfile=$(mktemp /tmp/__perf_record_retry.XXXXXX)
  PERF_RECORD_LOGS+=("$logfile")

  # Save the e flag state and disable it
  local save_e
  if [[ $- == *e* ]]; then
    save_e="set -e"
  else
    save_e="set +e"
  fi
  set +e

  local duration
  local first_run=true
  local ret=1
  local cmd_prefix="perf record"
  if [ -n "${PERF_RECORD_CMD}" ]; then
    cmd_prefix="${PERF_RECORD_CMD}"
  fi

  for duration in 0.01 0.1 0.3 1.0 2.0; do
    rm -f "${perfdata}".old
    ${cmd_prefix} "$@" -o "${perfdata}" ${testprog_base} ${duration} > "$logfile" 2>&1
    local record_exit=$?

    if [ "$first_run" = true ] && [ $record_exit -ne 0 ]; then
      ret=2
      break
    fi
    first_run=false

    if [ -e "${perfdata}" ] && eval "${check_cmd}"; then
      ret=0
      break
    fi
  done

  eval "$save_e"
  return $ret
}

perf_record_cleanup() {
  for logfile in "${PERF_RECORD_LOGS[@]}"; do
    rm -f "$logfile"
  done
  PERF_RECORD_LOGS=()
}
