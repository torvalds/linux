#! /bin/bash
# SPDX-License-Identifier: GPL-2.0

set -e
set -u
set -x

unset KBUILD_OUTPUT

GENERATE_GCOV_REPORT=0
while getopts "g" opt; do
  case ${opt} in
    g)
      GENERATE_GCOV_REPORT=1
      ;;
    :)
      echo "USAGE: config.sh [-g]"
      exit 1
      ;;
    ?)
      echo "Invalid option: -${OPTARG}."
      exit 1
      ;;
  esac
done

CONF_FILE="tools/testing/selftests/net/config"

# no modules
scripts/config --file "$CONF_FILE" --disable CONFIG_MODULES

# enable RDS
scripts/config --file "$CONF_FILE" --enable CONFIG_RDS
scripts/config --file "$CONF_FILE" --enable CONFIG_RDS_TCP

if [ "$GENERATE_GCOV_REPORT" -eq 1 ]; then
	# instrument RDS and only RDS
	scripts/config --file "$CONF_FILE" --enable CONFIG_GCOV_KERNEL
	scripts/config --file "$CONF_FILE" --disable GCOV_PROFILE_ALL
	scripts/config --file "$CONF_FILE" --enable GCOV_PROFILE_RDS
else
	scripts/config --file "$CONF_FILE" --disable CONFIG_GCOV_KERNEL
	scripts/config --file "$CONF_FILE" --disable GCOV_PROFILE_ALL
	scripts/config --file "$CONF_FILE" --disable GCOV_PROFILE_RDS
fi

# need network namespaces to run tests with veth network interfaces
scripts/config --file "$CONF_FILE" --enable CONFIG_NET_NS
scripts/config --file "$CONF_FILE" --enable CONFIG_VETH

# simulate packet loss
scripts/config --file "$CONF_FILE" --enable CONFIG_NET_SCH_NETEM

