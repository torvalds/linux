#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test enslavement to LAG with a clean slate.
# See $lib_dir/router_bridge_lag.sh for further details.

ALL_TESTS="
	config_devlink_reload
	config_enslave_h1
	config_enslave_h2
	config_enslave_h3
	config_enslave_h4
	config_enslave_swp1
	config_enslave_swp2
	config_enslave_swp3
	config_enslave_swp4
	config_wait
	ping_ipv4
	ping_ipv6
"

config_devlink_reload()
{
	log_info "Devlink reload"
	devlink_reload
}

config_enslave_h1()
{
	config_enslave $h1 lag1
}

config_enslave_h2()
{
	config_enslave $h2 lag4
}

config_enslave_h3()
{
	config_enslave $h3 lag4
}

config_enslave_h4()
{
	config_enslave $h4 lag1
}

lib_dir=$(dirname $0)/../../../net/forwarding
EXTRA_SOURCE="source $lib_dir/devlink_lib.sh"
source $lib_dir/router_bridge_lag.sh
