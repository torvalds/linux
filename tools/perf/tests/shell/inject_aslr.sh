#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# perf inject --aslr test

set -e
set -o pipefail

shelldir=$(dirname "$0")
# shellcheck source=lib/perf_has_symbol.sh
. "${shelldir}"/lib/perf_has_symbol.sh

sym="noploop"

skip_test_missing_symbol ${sym}

# Create global temp directory
temp_dir=$(mktemp -d /tmp/perf-test-aslr.XXXXXXXXXX)

prog="perf test -w noploop"
[ "$(uname -m)" = "s390x" ] && prog="$prog 3"
err=0
kprog="dd if=/dev/urandom of=/dev/null bs=1M count=50"

cleanup() {
  local exit_code=${1:-$?}
  trap - EXIT TERM INT
  if [ "${exit_code}" -ne 0 ] || [ "${err}" -ne 0 ]; then
    echo "Test failed! Preserving temp directory: ${temp_dir}"
    return
  fi
  # Check if temp_dir is set and looks sane before removing
  if [[ "${temp_dir}" =~ ^/tmp/perf-test-aslr\. ]]; then
    rm -rf "${temp_dir}"
  fi
}

trap_cleanup() {
  local exit_code=$?
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cleanup ${exit_code}
  exit ${exit_code}
}
trap trap_cleanup EXIT TERM INT

get_noploop_addr() {
  local file=$1
  perf script -i "$file" | awk '
    BEGIN { found=0 }
    {
      for (i=1; i<=NF; i++) {
        if ($i ~ /noploop\+/) {
          if (!found) {
            print $(i-1)
            found=1
          }
        }
      }
    }'
}

test_basic_aslr() {
  echo "Test basic ASLR remapping"
  local data
  data=$(mktemp "${temp_dir}/perf.data.basic.XXXXXX")
  local data2
  data2=$(mktemp "${temp_dir}/perf.data2.basic.XXXXXX")

  perf record -e task-clock:u -o "${data}" ${prog}
  perf inject -v --aslr -i "${data}" -o "${data2}"

  orig_addr=$(get_noploop_addr "${data}")
  new_addr=$(get_noploop_addr "${data2}")

  echo "Basic ASLR: orig_addr=$orig_addr, new_addr=$new_addr"

  if [ -z "$orig_addr" ]; then
    echo "Basic ASLR test [Failed - no noploop samples in original file]"
    err=1
  elif [ -z "$new_addr" ]; then
    echo "Basic ASLR test [Failed - could not find remapped address]"
    err=1
  elif [ "$orig_addr" = "$new_addr" ]; then
    echo "Basic ASLR test [Failed - addresses are not remapped]"
    err=1
  else
    echo "Basic ASLR test [Success]"
  fi
}

test_pipe_aslr() {
  echo "Test pipe mode ASLR remapping"
  local data
  data=$(mktemp "${temp_dir}/perf.data.pipe.XXXXXX")
  local data2
  data2=$(mktemp "${temp_dir}/perf.data2.pipe.XXXXXX")

  # Use tee to save the original pipe data for comparison
  perf record -e task-clock:u -o - ${prog} | tee "${data}" | perf inject --aslr -o "${data2}"

  orig_addr=$(get_noploop_addr "${data}")
  new_addr=$(get_noploop_addr "${data2}")

  echo "Pipe ASLR: orig_addr=$orig_addr, new_addr=$new_addr"

  if [ -z "$orig_addr" ]; then
    echo "Pipe ASLR test [Failed - no noploop samples in original file]"
    err=1
  elif [ -z "$new_addr" ]; then
    echo "Pipe ASLR test [Failed - could not find remapped address]"
    err=1
  elif [ "$orig_addr" = "$new_addr" ]; then
    echo "Pipe ASLR test [Failed - addresses are not remapped]"
    err=1
  else
    echo "Pipe ASLR test [Success]"
  fi
}

test_callchain_aslr() {
  echo "Test Callchain ASLR remapping"
  local data
  data=$(mktemp "${temp_dir}/perf.data.callchain.XXXXXX")
  local data2
  data2=$(mktemp "${temp_dir}/perf.data2.callchain.XXXXXX")

  perf record -g -e task-clock:u -o "${data}" ${prog}
  perf inject --aslr -i "${data}" -o "${data2}"

  orig_addr=$(get_noploop_addr "${data}")
  new_addr=$(get_noploop_addr "${data2}")

  echo "Callchain ASLR: orig_addr=$orig_addr, new_addr=$new_addr"

  if [ -z "$orig_addr" ]; then
    echo "Callchain ASLR test [Failed - no noploop samples in original file]"
    err=1
  elif [ -z "$new_addr" ]; then
    echo "Callchain ASLR test [Failed - could not find remapped address]"
    err=1
  elif [ "$orig_addr" = "$new_addr" ]; then
    echo "Callchain ASLR test [Failed - addresses are not remapped]"
    err=1
  else
    # Extract callchain addresses (indented lines starting with hex addresses)
    orig_callchain=$(perf script -i "${data}" | awk '/^[[:space:]]+[0-9a-f]+/ {print $1}')
    new_callchain=$(perf script -i "${data2}" | awk '/^[[:space:]]+[0-9a-f]+/ {print $1}')

    if [ -z "$orig_callchain" ]; then
      echo "Callchain ASLR test [Failed - no callchain samples in original file]"
      err=1
    elif [ -z "$new_callchain" ]; then
      echo "Callchain ASLR test [Failed - callchain data was dropped]"
      err=1
    elif [ "$orig_callchain" = "$new_callchain" ]; then
      echo "Callchain ASLR test [Failed - callchain addresses were not remapped]"
      err=1
    else
      echo "Callchain ASLR test [Success]"
    fi
  fi
}

test_report_aslr() {
  echo "Test perf report consistency"
  local data
  data=$(mktemp "${temp_dir}/perf.data.report.XXXXXX")
  local data2
  data2=$(mktemp "${temp_dir}/perf.data2.report.XXXXXX")
  local data_clean
  data_clean=$(mktemp "${temp_dir}/perf.data.clean.XXXXXX")

  perf record -e task-clock:u -o "${data}" ${prog}
  # Use -b to inject build-ids and force ordered events processing in both
  perf inject -b -i "${data}" -o "${data_clean}"
  perf inject -v -b --aslr -i "${data}" -o "${data2}"

  local report1="${temp_dir}/report1_basic"
  local report2="${temp_dir}/report2_basic"
  local report1_clean="${temp_dir}/report1_basic.clean"
  local report2_clean="${temp_dir}/report2_basic.clean"
  local diff_file="${temp_dir}/diff_basic"

  perf report -i "${data_clean}" --stdio > "${report1}"
  perf report -i "${data2}" --stdio > "${report2}"

  # Strip headers and compare lines with percentages
  grep '%' "${report1}" | grep -v '^#' | \
    grep -v -E '0x[0-9a-f]{8,}|0000000000000000' | sort > "${report1_clean}" || true
  grep '%' "${report2}" | grep -v '^#' | \
    grep -v -E '0x[0-9a-f]{8,}|0000000000000000' | sort > "${report2_clean}" || true

  diff -u -w "${report1_clean}" "${report2_clean}" > "${diff_file}" || true

  if [ ! -s "${report1_clean}" ]; then
    echo "Report ASLR test [Failed - no samples captured]"
    err=1
  elif [ -s "${diff_file}" ]; then
    echo "Report ASLR test [Failed - reports differ]"
    echo "Showing first 20 lines of diff:"
    head -n 20 "${diff_file}"
    err=1
  else
    echo "Report ASLR test [Success]"
  fi
}

test_pipe_report_aslr() {
  echo "Test pipe mode perf report consistency"
  local data
  data=$(mktemp "${temp_dir}/perf.data.pipe_report.XXXXXX")
  local data2
  data2=$(mktemp "${temp_dir}/perf.data2.pipe_report.XXXXXX")
  local data_clean
  data_clean=$(mktemp "${temp_dir}/perf.data.clean.XXXXXX")

  # Use tee to save the original pipe data, then process it with inject -b
  perf record -e task-clock:u -o - ${prog} | \
    tee "${data}" | \
    perf inject -b --aslr -o "${data2}"
  perf inject -b -i "${data}" -o "${data_clean}"

  local report1="${temp_dir}/report1_pipe"
  local report2="${temp_dir}/report2_pipe"
  local report1_clean="${temp_dir}/report1_pipe.clean"
  local report2_clean="${temp_dir}/report2_pipe.clean"
  local diff_file="${temp_dir}/diff_pipe"

  perf report -i "${data_clean}" --stdio > "${report1}"
  perf report -i "${data2}" --stdio > "${report2}"

  # Strip headers and compare lines with percentages
  grep '%' "${report1}" | grep -v '^#' | \
    grep -v -E '0x[0-9a-f]{8,}|0000000000000000' | sort > "${report1_clean}" || true
  grep '%' "${report2}" | grep -v '^#' | \
    grep -v -E '0x[0-9a-f]{8,}|0000000000000000' | sort > "${report2_clean}" || true

  diff -u -w "${report1_clean}" "${report2_clean}" > "${diff_file}" || true

  if [ ! -s "${report1_clean}" ]; then
    echo "Pipe Report ASLR test [Failed - no samples captured]"
    err=1
  elif [ -s "${diff_file}" ]; then
    echo "Pipe Report ASLR test [Failed - reports differ]"
    echo "Showing first 20 lines of diff:"
    head -n 20 "${diff_file}"
    err=1
  else
    echo "Pipe Report ASLR test [Success]"
  fi
}

test_pipe_out_report_aslr() {
  echo "Test pipe output mode perf report consistency"
  local data
  data=$(mktemp "${temp_dir}/perf.data.pipe_out_report.XXXXXX")
  local data_clean
  data_clean=$(mktemp "${temp_dir}/perf.data.clean.XXXXXX")

  perf record -e task-clock:u -o "${data}" ${prog}
  perf inject -b -i "${data}" -o "${data_clean}"

  local report1="${temp_dir}/report1_pipe_out"
  local report2="${temp_dir}/report2_pipe_out"
  local report1_clean="${temp_dir}/report1_pipe_out.clean"
  local report2_clean="${temp_dir}/report2_pipe_out.clean"
  local diff_file="${temp_dir}/diff_pipe_out"

  perf report -i "${data_clean}" --stdio > "${report1}"
  perf inject -b --aslr -i "${data}" -o - | perf report -i - --stdio > "${report2}"

  # Strip headers and compare lines with percentages
  grep '%' "${report1}" | grep -v '^#' | \
    grep -v -E '0x[0-9a-f]{8,}|0000000000000000' | sort > "${report1_clean}" || true
  grep '%' "${report2}" | grep -v '^#' | \
    grep -v -E '0x[0-9a-f]{8,}|0000000000000000' | sort > "${report2_clean}" || true

  diff -u -w "${report1_clean}" "${report2_clean}" > "${diff_file}" || true

  if [ ! -s "${report1_clean}" ]; then
    echo "Pipe Output Report ASLR test [Failed - no samples captured]"
    err=1
  elif [ -s "${diff_file}" ]; then
    echo "Pipe Output Report ASLR test [Failed - reports differ]"
    echo "Showing first 20 lines of diff:"
    head -n 20 "${diff_file}"
    err=1
  else
    echo "Pipe Output Report ASLR test [Success]"
  fi
}

test_dropped_samples() {
  echo "Test dropped samples (phys-data)"
  local data
  data=$(mktemp "${temp_dir}/perf.data.dropped.XXXXXX")
  local data2
  data2=$(mktemp "${temp_dir}/perf.data2.dropped.XXXXXX")

  # Check if --phys-data is supported by recording a short run
  if ! perf record -e task-clock:u --phys-data -o "${data}" -- sleep 0.1 > /dev/null 2>&1; then
    echo "Skipping dropped samples test as --phys-data is not supported"
    return
  fi

  perf record -e task-clock:u --phys-data -o "${data}" ${prog}
  perf inject --aslr -i "${data}" -o "${data2}"

  # Verify that the original file actually contained samples!
  orig_samples=$(perf script -i "${data}" | wc -l)
  if [ "$orig_samples" -eq 0 ]; then
    echo "Dropped samples test [Failed - no samples in original file]"
    err=1
  else
    # Verify that samples are dropped.
    samples_count=$(perf script -i "${data2}" | wc -l)

    if [ "$samples_count" -gt 0 ]; then
      echo "Dropped samples test [Failed - samples were not dropped]"
      err=1
    else
      echo "Dropped samples test [Success]"
    fi
  fi
}

test_kernel_aslr() {
  echo "Test kernel ASLR remapping"
  local kdata
  kdata=$(mktemp "${temp_dir}/perf.data.kernel.XXXXXX")
  local kdata2
  kdata2=$(mktemp "${temp_dir}/perf.data2.kernel.XXXXXX")
  local log_file
  log_file=$(mktemp "${temp_dir}/kernel_record.log.XXXXXX")

  # Try to record kernel samples
  if ! perf record -e task-clock:k -o "${kdata}" ${kprog} > "${log_file}" 2>&1; then
    echo "Skipping kernel ASLR test as recording failed (maybe no permissions)"
    return
  fi

  # Check for warning about kernel map restriction
  if grep -q "Couldn't record kernel reference relocation symbol" "${log_file}"; then
    echo "Skipping kernel ASLR test as kernel map could not be recorded (permissions restricted)"
    return
  fi

  perf inject -v --aslr -i "${kdata}" -o "${kdata2}"

  # Check if kernel addresses are remapped.
  # Find the field that ends with :k: (the event name) and take the next field!
  orig_addr=$(perf script -i "${kdata}" | awk '
    BEGIN { found=0 }
    {
      for (i=1; i<NF; i++) {
        if ($i ~ /:[k]+:?$/) {
          if (!found) {
            print $(i+1)
            found=1
          }
        }
      }
    }')
  new_addr=$(perf script -i "${kdata2}" | awk '
    BEGIN { found=0 }
    {
      for (i=1; i<NF; i++) {
        if ($i ~ /:[k]+:?$/) {
          if (!found) {
            print $(i+1)
            found=1
          }
        }
      }
    }')

  echo "Kernel ASLR: orig_addr=$orig_addr, new_addr=$new_addr"

  if [ -z "$orig_addr" ]; then
    echo "Kernel ASLR test [Failed - no kernel samples in original file]"
    err=1
  elif [ -z "$new_addr" ]; then
    echo "Kernel ASLR test [Failed - could not find remapped address]"
    err=1
  elif [ "$orig_addr" = "$new_addr" ]; then
    echo "Kernel ASLR test [Failed - addresses are not remapped]"
    err=1
  else
    echo "Kernel ASLR test [Success]"
  fi
}

test_kernel_report_aslr() {
  echo "Test kernel perf report consistency"
  local kdata
  kdata=$(mktemp "${temp_dir}/perf.data.kernel_report.XXXXXX")
  local kdata2
  kdata2=$(mktemp "${temp_dir}/perf.data2.kernel_report.XXXXXX")
  local data_clean
  data_clean=$(mktemp "${temp_dir}/perf.data.clean.XXXXXX")
  local log_file
  log_file=$(mktemp "${temp_dir}/kernel_report_record.log.XXXXXX")

  # Try to record kernel samples
  if ! perf record -e task-clock:k -o "${kdata}" ${kprog} > "${log_file}" 2>&1; then
    echo "Skipping kernel report test as recording failed (maybe no permissions)"
    return
  fi

  # Check for warning about kernel map restriction
  if grep -q "Couldn't record kernel reference relocation symbol" "${log_file}"; then
    echo "Skipping kernel report test as kernel map could not be recorded (permissions restricted)"
    return
  fi

  # Use -b to inject build-ids and force ordered events processing in both
  perf inject -b -i "${kdata}" -o "${data_clean}"
  perf inject -v -b --aslr -i "${kdata}" -o "${kdata2}"

  local report1="${temp_dir}/report_kernel1"
  local report2="${temp_dir}/report_kernel2"
  local report1_clean="${temp_dir}/report_kernel1.clean"
  local report2_clean="${temp_dir}/report_kernel2.clean"

  perf report -i "${data_clean}" --stdio > "${report1}"
  perf report -i "${kdata2}" --stdio > "${report2}"

  # Strip headers and compare lines with percentages
  grep '%' "${report1}" | grep -v '^#' > "${report1_clean}" || true
  grep '%' "${report2}" | grep -v '^#' > "${report2_clean}" || true

  # Normalize kernel DSOs and addresses in clean reports
  # This allows kernel modules to be either a module or kernel.kallsyms
  local report1_norm="${temp_dir}/report_kernel1.norm"
  local report2_norm="${temp_dir}/report_kernel2.norm"
  local diff_file="${temp_dir}/diff_kernel"

  grep -v -E '0x[0-9a-f]{8,}|0000000000000000' "${report1_clean}" | \
    awk '{gsub(/\[[a-zA-Z0-9_.-]{2,}\](\.[a-zA-Z0-9_]+)?/, "[kernel]", $0); print}' | \
    sort > "${report1_norm}" || true
  grep -v -E '0x[0-9a-f]{8,}|0000000000000000' "${report2_clean}" | \
    awk '{gsub(/\[[a-zA-Z0-9_.-]{2,}\](\.[a-zA-Z0-9_]+)?/, "[kernel]", $0); print}' | \
    sort > "${report2_norm}" || true

  diff -u -w "${report1_norm}" "${report2_norm}" > "${diff_file}" || true

  if [ ! -s "${report1_norm}" ]; then
    echo "Kernel Report ASLR test [Failed - no samples captured]"
    err=1
  elif [ -s "${diff_file}" ]; then
    echo "Kernel Report ASLR test [Failed - reports differ]"
    echo "Showing first 20 lines of diff:"
    head -n 20 "${diff_file}"
    err=1
  else
    echo "Kernel Report ASLR test [Success]"
  fi
}

test_regs_stripping() {
  echo "Test user register stripping"
  local rdata="${temp_dir}/perf.data.regs"
  local rdata2="${temp_dir}/perf.data.regs.injected"
  local rdata_clean="${temp_dir}/perf.data.regs.clean"

  if ! perf record -e cycles:u --user-regs -o "${rdata}" ${prog} > /dev/null 2>&1; then
    echo "Skipping user registers test as recording failed (unsupported flag/platform)"
    return
  fi

  perf inject -b -i "${rdata}" -o "${rdata_clean}"
  perf inject -v -b --aslr -i "${rdata}" -o "${rdata2}"

  local report1="${temp_dir}/report_regs1"
  local report2="${temp_dir}/report_regs2"
  local report1_clean="${temp_dir}/report_regs1.clean"
  local report2_clean="${temp_dir}/report_regs2.clean"
  local diff_file="${temp_dir}/diff_regs"

  perf report -i "${rdata_clean}" --stdio > "${report1}" 2>/dev/null || true
  perf report -i "${rdata2}" --stdio > "${report2}" 2>/dev/null || true

  grep '%' "${report1}" | grep -v '^#' | \
    grep -v -E '0x[0-9a-f]{8,}|0000000000000000' | \
    sort > "${report1_clean}" || true
  grep '%' "${report2}" | grep -v '^#' | \
    grep -v -E '0x[0-9a-f]{8,}|0000000000000000' | \
    sort > "${report2_clean}" || true

  diff -u -w "${report1_clean}" "${report2_clean}" > "${diff_file}" || true

  if [ ! -s "${report1_clean}" ]; then
    echo "User registers stripping test [Failed - profile trace starved/empty]"
    err=1
    return
  elif [ -s "${diff_file}" ]; then
    echo "User registers stripping test [Failed - report parsing differs]"
    echo "Showing first 20 lines of diff:"
    head -n 20 "${diff_file}"
    err=1
    return
  fi

  local script_dump="${temp_dir}/script_regs_dump"
  perf script -D -i "${rdata2}" > "${script_dump}" 2>/dev/null || true
  if grep -q "user regs:" "${script_dump}"; then
    echo "User registers stripping test [Failed - register dumps still present]"
    err=1
  else
    echo "User registers stripping test [Success]"
  fi
}

test_basic_aslr
test_pipe_aslr
test_callchain_aslr
test_report_aslr
test_pipe_report_aslr
test_pipe_out_report_aslr
test_dropped_samples
case "$(uname -m)" in
  aarch64*|arm*)
    echo "Skipping kernel ASLR tests on ARM"
    ;;
  *)
    test_kernel_aslr
    test_kernel_report_aslr
    ;;
esac

test_regs_stripping

cleanup ${err}
exit $err
