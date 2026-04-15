#! /bin/bash
# SPDX-License-Identifier: GPL-2.0

set -e
set -u
set -x

unset KBUILD_OUTPUT
CONF_FILE=""
FLAGS=()

GENERATE_GCOV_REPORT=0
while getopts "gc:" opt; do
  case ${opt} in
    g)
      GENERATE_GCOV_REPORT=1
      ;;
    c)
      CONF_FILE=$OPTARG
      ;;
    :)
      echo "USAGE: config.sh [-g] [-c config]"
      exit 1
      ;;
    ?)
      echo "Invalid option: -${OPTARG}."
      exit 1
      ;;
  esac
done

if [[ "$CONF_FILE" != "" ]]; then
	FLAGS=(--file "$CONF_FILE")
fi

# no modules
scripts/config "${FLAGS[@]}" --disable CONFIG_MODULES

# enable RDS
scripts/config "${FLAGS[@]}" --enable CONFIG_RDS
scripts/config "${FLAGS[@]}" --enable CONFIG_RDS_TCP

if [ "$GENERATE_GCOV_REPORT" -eq 1 ]; then
	# instrument RDS and only RDS
	scripts/config "${FLAGS[@]}" --enable CONFIG_GCOV_KERNEL
	scripts/config "${FLAGS[@]}" --disable GCOV_PROFILE_ALL
	scripts/config "${FLAGS[@]}" --enable GCOV_PROFILE_RDS
else
	scripts/config "${FLAGS[@]}" --disable CONFIG_GCOV_KERNEL
	scripts/config "${FLAGS[@]}" --disable GCOV_PROFILE_ALL
	scripts/config "${FLAGS[@]}" --disable GCOV_PROFILE_RDS
fi

# need network namespaces to run tests with veth network interfaces
scripts/config "${FLAGS[@]}" --enable CONFIG_NET_NS
scripts/config "${FLAGS[@]}" --enable CONFIG_VETH

# simulate packet loss
scripts/config "${FLAGS[@]}" --enable CONFIG_NET_SCH_NETEM

