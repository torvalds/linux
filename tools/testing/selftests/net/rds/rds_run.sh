#! /bin/bash
# SPDX-License-Identifier: GPL-2.0

set -e
set -u

unset KBUILD_OUTPUT

current_dir="$(realpath "$(dirname "$0")")"
build_dir="$current_dir"

build_include="$current_dir/include.sh"
if test -f "$build_include"; then
	# this include will define "$mk_build_dir" as the location the test was
	# built.  We will need this if the tests are installed in a location
	# other than the kernel source

	source "$build_include"
	build_dir="$mk_build_dir"
fi

# Source settings for timeout value (also used by ksft runner)
source "$current_dir"/settings

# This test requires kernel source and the *.gcda data therein
# Locate the top level of the kernel source, and the net/rds
# subfolder with the appropriate *.gcno object files
ksrc_dir="$(realpath "$build_dir"/../../../../../)"
kconfig="$ksrc_dir/.config"
obj_dir="$ksrc_dir/net/rds"

GCOV_CMD=gcov

#check to see if the host has the required packages to generate a gcov report
check_gcov_env()
{
	if ! which "$GCOV_CMD" > /dev/null 2>&1; then
		echo "# Warning: Could not find gcov. "
		GENERATE_GCOV_REPORT=0
		return
	fi

	# the gcov version must match the gcc version
	GCC_VER=$(gcc -dumpfullversion)
	GCOV_VER=$($GCOV_CMD -v | grep gcov | awk '{print $3}'| awk 'BEGIN {FS="-"}{print $1}')
	if [ "$GCOV_VER" != "$GCC_VER" ]; then
		#attempt to find a matching gcov version
		GCOV_CMD=gcov-$(gcc -dumpversion)

		if ! which "$GCOV_CMD" > /dev/null 2>&1; then
			echo "# Warning: Could not find an appropriate gcov installation. \
				gcov version must match gcc version"
			GENERATE_GCOV_REPORT=0
			return
		fi

		#recheck version number of found gcov executable
		GCOV_VER=$($GCOV_CMD -v | grep gcov | awk '{print $3}'| \
			awk 'BEGIN {FS="-"}{print $1}')
		if [ "$GCOV_VER" != "$GCC_VER" ]; then
			echo "# Warning: Could not find an appropriate gcov installation. \
				gcov version must match gcc version"
			GENERATE_GCOV_REPORT=0
		else
			echo "# Warning: Mismatched gcc and gcov detected.  Using $GCOV_CMD"
		fi
	fi
}

# Check to see if the kconfig has the required configs to generate a coverage report
check_gcov_conf()
{
	if ! grep -x "CONFIG_GCOV_PROFILE_RDS=y" "$kconfig" > /dev/null 2>&1; then
		echo "# INFO: CONFIG_GCOV_PROFILE_RDS should be enabled for coverage reports"
		GENERATE_GCOV_REPORT=0
	fi
	if ! grep -x "CONFIG_GCOV_KERNEL=y" "$kconfig" > /dev/null 2>&1; then
		echo "# INFO: CONFIG_GCOV_KERNEL should be enabled for coverage reports"
		GENERATE_GCOV_REPORT=0
	fi
	if grep -x "CONFIG_GCOV_PROFILE_ALL=y" "$kconfig" > /dev/null 2>&1; then
		echo "# INFO: CONFIG_GCOV_PROFILE_ALL should be disabled for coverage reports"
		GENERATE_GCOV_REPORT=0
	fi

	if [ "$GENERATE_GCOV_REPORT" -eq 0 ]; then
		echo "# To enable gcov reports, please run "\
			"\"tools/testing/selftests/net/rds/config.sh -g\" and rebuild the kernel"
	else
		# if we have the required kernel configs, proceed to check the environment to
		# ensure we have the required gcov packages
		check_gcov_env
	fi
}

# Checks if a kconfig is enabled (set to =y or =m)
# $1: kconfig symbol to check
# $2: (optional) module name backing $1
#     Ex: check_conf_enabled CONFIG_RDS_TCP rds_tcp
#     Modules for configs set to  =m will be probed
#     If omitted, only a built-in (=y) config is accepted.
# Returns on success.  exits 4 on failure
# Kselftest framework requirement - SKIP code is 4.
check_conf_enabled() {
	if grep -x "$1=y" "$kconfig" > /dev/null 2>&1; then
		return
	fi
	if [ -n "${2:-}" ] && grep -x "$1=m" "$kconfig" > /dev/null 2>&1; then
		probe_module "$2"
		return
	fi
	echo "selftests: [SKIP] This test requires $1 enabled"
	echo "Please run" \
	     "tools/testing/selftests/net/rds/config.sh and rebuild the kernel"
	exit 4
}

check_rdma_conf_enabled() {
	if grep -x "$1=y" "$kconfig" > /dev/null 2>&1; then
		return
	fi
	if [ -n "${2:-}" ] && grep -x "$1=m" "$kconfig" > /dev/null 2>&1; then
		probe_module "$2"
		return
	fi
	echo "selftests: [XFAIL] rdma transport requires $1 enabled"
	echo "To enable, run" \
	     "tools/testing/selftests/net/rds/config.sh -r and rebuild"
	exit 2
}

# Load the module backing a config that is built as a loadable module
# (=m).  Built-in (=y) configs are already available and don't reach
# here.  Exits with the SKIP code if a required module cannot be loaded.
probe_module() {
	if ! modprobe -q "$1"; then
		echo "selftests: [SKIP] could not load required module $1"
		exit 4
	fi
}

check_conf() {
	check_conf_enabled CONFIG_NET_SCH_NETEM sch_netem
	check_conf_enabled CONFIG_VETH veth
	check_conf_enabled CONFIG_NET_NS
	check_conf_enabled CONFIG_RDS_TCP rds_tcp
	check_conf_enabled CONFIG_RDS rds
}

# Check kernel config and host environment for RDS-RDMA support.
# Exits with XFAIL (2) if the user requested rdma but prerequisites
# are not met.
check_rdma_conf()
{
	case "$TRANSPORT" in
	  *rdma*) ;;
	  *) return ;;
	esac

	# Kconfig will enforce CONFIG_INFINIBAND_* as dependencies
	# of CONFIG_RDMA_RXE
	check_rdma_conf_enabled CONFIG_RDMA_RXE rdma_rxe
	check_rdma_conf_enabled CONFIG_RDS_RDMA rds_rdma

	if ! which rdma > /dev/null 2>&1; then
		echo "selftests: [XFAIL] rdma transport requires the 'rdma'" \
		      "tool (iproute2)"
		exit 2
	fi
}

check_env()
{
	if ! test -d "$obj_dir"; then
		echo "selftests: [SKIP] This test requires a kernel source tree"
		exit 4
	fi
	if ! test -e "$kconfig"; then
		echo "selftests: [SKIP] This test requires a configured kernel source tree"
		exit 4
	fi
	if ! which strace > /dev/null 2>&1; then
		echo "selftests: [SKIP] Could not run test without strace"
		exit 4
	fi
	if ! which tcpdump > /dev/null 2>&1; then
		echo "selftests: [SKIP] Could not run test without tcpdump"
		exit 4
	fi

	if ! which python3 > /dev/null 2>&1; then
		echo "selftests: [SKIP] Could not run test without python3"
		exit 4
	fi

	python_major=$(python3 -c "import sys; print(sys.version_info[0])")
	python_minor=$(python3 -c "import sys; print(sys.version_info[1])")
	if [[ python_major -lt 3 || ( python_major -eq 3 && python_minor -lt 9 ) ]] ; then
		echo "selftests: [SKIP] Could not run test without at least python3.9"
		python3 -V
		exit 4
	fi
}

LOG_DIR="${RDS_LOG_DIR:-}"
TIMEOUT=$timeout
GENERATE_GCOV_REPORT=1
TRANSPORT=tcp
FLAGS=()

while getopts "d:l:c:u:t:T:" opt; do
  case ${opt} in
    d)
      LOG_DIR=${OPTARG}
      ;;
    l)
      FLAGS+=("-l" "${OPTARG}")
      ;;
    c)
      FLAGS+=("-c" "${OPTARG}")
      ;;
    t)
      TIMEOUT=${OPTARG}
      ;;
    u)
      FLAGS+=("-u" "${OPTARG}")
      ;;
    T)
      TRANSPORT=${OPTARG}
      ;;
    :)
      echo "USAGE: rds_run.sh [-d logdir] [-l packet_loss]" \
           "[-c packet_corruption] [-u packet_duplicate] [-t timeout]" \
           "[-T tcp|rdma|tcp,rdma]"
      exit 1
      ;;
    ?)
      echo "Invalid option: -${OPTARG}."
      exit 1
      ;;
  esac
done

# Validate transport tokens
IFS=',' read -ra transports <<< "$TRANSPORT"
for t in "${transports[@]}"; do
    if [ "$t" != "tcp" ] && [ "$t" != "rdma" ]; then
        echo "rds_run.sh: unknown transport '$t' (expected tcp or rdma)"
        exit 1
    fi
done

FLAGS+=("--transport" "${TRANSPORT}")

check_env
check_conf
check_gcov_conf
check_rdma_conf

TRACE_CMD=()
if [[ -n "$LOG_DIR" ]]; then
   FLAGS+=("-d" "$LOG_DIR")

   TRACE_FILE="${LOG_DIR}/rds-strace.txt"
   COVR_DIR="${LOG_DIR}/coverage/"
   DMESG_FILE="${LOG_DIR}/rds-dmesg.out"

   mkdir -p  "$LOG_DIR"
   mkdir -p "$COVR_DIR"

   rm -f "$TRACE_FILE"
   rm -f "$DMESG_FILE"
   rm -f "$LOG_DIR"/rds-*.pcap
   rm -f "$COVR_DIR"/gcovr*

   echo "# Traces will be logged to ${TRACE_FILE}"
   TRACE_CMD=(strace -T -tt -o "${TRACE_FILE}")
fi

set +e
echo "# running RDS tests..."
"${TRACE_CMD[@]}" python3 "$(dirname "$0")/test.py" "${FLAGS[@]}" -t "$TIMEOUT"

test_rc=$?

if [[ -n "$LOG_DIR" ]]; then
   dmesg > "${DMESG_FILE}"
fi

if [[ -n "$LOG_DIR" ]] && [ "$GENERATE_GCOV_REPORT" -eq 1 ]; then
       echo "# saving coverage data..."

       # Ensure debugfs is mounted before reading gcov data.
       if ! mountpoint -q /sys/kernel/debug 2>/dev/null; then
               mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null || true
       fi

       (set +x; cd /sys/kernel/debug/gcov; find ./* -name '*.gcda' | \
       while read -r f
       do
               cat < "/sys/kernel/debug/gcov/$f" > "/$f"
       done)

       echo "# running gcovr..."
       gcovr -s --html-details --gcov-executable "$GCOV_CMD" --gcov-ignore-parse-errors \
             --root "${ksrc_dir}" -o "${COVR_DIR}/gcovr" "${ksrc_dir}/net/rds/" \
             > "${LOG_DIR}/gcovr.log" 2>&1
       echo "# gcovr log: ${LOG_DIR}/gcovr.log"
else
       echo "# Coverage report will be skipped"
fi

if [ "$test_rc" -eq 0 ]; then
	echo "# PASS: Test completed successfully"
else
	echo "# FAIL: Test failed"
fi

exit "$test_rc"
