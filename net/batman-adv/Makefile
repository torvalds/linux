# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2007-2019  B.A.T.M.A.N. contributors:
#
# Marek Lindner, Simon Wunderlich
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of version 2 of the GNU General Public
# License as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#

obj-$(CONFIG_BATMAN_ADV) += batman-adv.o
batman-adv-y += bat_algo.o
batman-adv-y += bat_iv_ogm.o
batman-adv-$(CONFIG_BATMAN_ADV_BATMAN_V) += bat_v.o
batman-adv-$(CONFIG_BATMAN_ADV_BATMAN_V) += bat_v_elp.o
batman-adv-$(CONFIG_BATMAN_ADV_BATMAN_V) += bat_v_ogm.o
batman-adv-y += bitarray.o
batman-adv-$(CONFIG_BATMAN_ADV_BLA) += bridge_loop_avoidance.o
batman-adv-$(CONFIG_BATMAN_ADV_DEBUGFS) += debugfs.o
batman-adv-$(CONFIG_BATMAN_ADV_DAT) += distributed-arp-table.o
batman-adv-y += fragmentation.o
batman-adv-y += gateway_client.o
batman-adv-y += gateway_common.o
batman-adv-y += hard-interface.o
batman-adv-y += hash.o
batman-adv-$(CONFIG_BATMAN_ADV_DEBUGFS) += icmp_socket.o
batman-adv-$(CONFIG_BATMAN_ADV_DEBUG) += log.o
batman-adv-y += main.o
batman-adv-$(CONFIG_BATMAN_ADV_MCAST) += multicast.o
batman-adv-y += netlink.o
batman-adv-$(CONFIG_BATMAN_ADV_NC) += network-coding.o
batman-adv-y += originator.o
batman-adv-y += routing.o
batman-adv-y += send.o
batman-adv-y += soft-interface.o
batman-adv-y += sysfs.o
batman-adv-$(CONFIG_BATMAN_ADV_TRACING) += trace.o
batman-adv-y += tp_meter.o
batman-adv-y += translation-table.o
batman-adv-y += tvlv.o

CFLAGS_trace.o := -I$(src)
