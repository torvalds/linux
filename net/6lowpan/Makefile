# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_6LOWPAN) += 6lowpan.o

6lowpan-y := core.o iphc.o nhc.o ndisc.o
6lowpan-$(CONFIG_6LOWPAN_DEBUGFS) += debugfs.o

#rfc6282 nhcs
obj-$(CONFIG_6LOWPAN_NHC_DEST) += nhc_dest.o
obj-$(CONFIG_6LOWPAN_NHC_FRAGMENT) += nhc_fragment.o
obj-$(CONFIG_6LOWPAN_NHC_HOP) += nhc_hop.o
obj-$(CONFIG_6LOWPAN_NHC_IPV6) += nhc_ipv6.o
obj-$(CONFIG_6LOWPAN_NHC_MOBILITY) += nhc_mobility.o
obj-$(CONFIG_6LOWPAN_NHC_ROUTING) += nhc_routing.o
obj-$(CONFIG_6LOWPAN_NHC_UDP) += nhc_udp.o

#rfc7400 ghcs
obj-$(CONFIG_6LOWPAN_GHC_EXT_HDR_HOP) += nhc_ghc_ext_hop.o
obj-$(CONFIG_6LOWPAN_GHC_UDP) += nhc_ghc_udp.o
obj-$(CONFIG_6LOWPAN_GHC_ICMPV6) += nhc_ghc_icmpv6.o
obj-$(CONFIG_6LOWPAN_GHC_EXT_HDR_DEST) += nhc_ghc_ext_dest.o
obj-$(CONFIG_6LOWPAN_GHC_EXT_HDR_FRAG) += nhc_ghc_ext_frag.o
obj-$(CONFIG_6LOWPAN_GHC_EXT_HDR_ROUTE) += nhc_ghc_ext_route.o
