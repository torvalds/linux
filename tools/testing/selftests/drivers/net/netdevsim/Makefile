# SPDX-License-Identifier: GPL-2.0+ OR MIT

TEST_PROGS := \
	devlink.sh \
	devlink_in_netns.sh \
	devlink_trap.sh \
	ethtool-coalesce.sh \
	ethtool-features.sh \
	ethtool-fec.sh \
	ethtool-pause.sh \
	fib.sh \
	fib_notifications.sh \
	hw_stats_l3.sh \
	nexthop.sh \
	peer.sh \
	psample.sh \
	tc-mq-visibility.sh \
	udp_tunnel_nic.sh \
# end of TEST_PROGS

TEST_FILES := \
	ethtool-common.sh
# end of TEST_FILES

TEST_INCLUDES := \
	$(wildcard ../../../net/lib/sh/*.sh) \
	../../../net/forwarding/devlink_lib.sh \
	../../../net/forwarding/fib_offload_lib.sh \
	../../../net/forwarding/lib.sh \
	../../../net/lib.sh \
# end of TEST_INCLUDES

include ../../../lib.mk
