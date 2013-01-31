#
# Makefile for the linux networking.
#
# 2 Sep 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
#

obj-y	:= nonet.o

obj-$(CONFIG_NET)		:= socket.o core/

tmp-$(CONFIG_COMPAT) 		:= compat.o
obj-$(CONFIG_NET)		+= $(tmp-y)

# LLC has to be linked before the files in net/802/
obj-$(CONFIG_LLC)		+= llc/
obj-$(CONFIG_NET)		+= ethernet/ 802/ sched/ netlink/
obj-$(CONFIG_NETFILTER)		+= netfilter/
obj-$(CONFIG_INET)		+= ipv4/
obj-$(CONFIG_XFRM)		+= xfrm/
obj-$(CONFIG_UNIX)		+= unix/
obj-$(CONFIG_NET)		+= ipv6/
obj-$(CONFIG_PACKET)		+= packet/
obj-$(CONFIG_NET_KEY)		+= key/
obj-$(CONFIG_BRIDGE)		+= bridge/
obj-$(CONFIG_NET_DSA)		+= dsa/
obj-$(CONFIG_IPX)		+= ipx/
obj-$(CONFIG_ATALK)		+= appletalk/
obj-$(CONFIG_X25)		+= x25/
obj-$(CONFIG_LAPB)		+= lapb/
obj-$(CONFIG_NETROM)		+= netrom/
obj-$(CONFIG_ROSE)		+= rose/
obj-$(CONFIG_AX25)		+= ax25/
obj-$(CONFIG_CAN)		+= can/
obj-$(CONFIG_IRDA)		+= irda/
obj-$(CONFIG_BT)		+= bluetooth/
obj-$(CONFIG_SUNRPC)		+= sunrpc/
obj-$(CONFIG_AF_RXRPC)		+= rxrpc/
obj-$(CONFIG_ATM)		+= atm/
obj-$(CONFIG_L2TP)		+= l2tp/
obj-$(CONFIG_DECNET)		+= decnet/
obj-$(CONFIG_PHONET)		+= phonet/
ifneq ($(CONFIG_VLAN_8021Q),)
obj-y				+= 8021q/
endif
obj-$(CONFIG_IP_DCCP)		+= dccp/
obj-$(CONFIG_IP_SCTP)		+= sctp/
obj-$(CONFIG_RDS)		+= rds/
obj-$(CONFIG_WIRELESS)		+= wireless/
obj-$(CONFIG_MAC80211)		+= mac80211/
obj-$(CONFIG_TIPC)		+= tipc/
obj-$(CONFIG_NETLABEL)		+= netlabel/
obj-$(CONFIG_IUCV)		+= iucv/
obj-$(CONFIG_RFKILL)		+= rfkill/
obj-$(CONFIG_NET_9P)		+= 9p/
obj-$(CONFIG_CAIF)		+= caif/
ifneq ($(CONFIG_DCB),)
obj-y				+= dcb/
endif
obj-$(CONFIG_IEEE802154)	+= ieee802154/
obj-$(CONFIG_MAC802154)		+= mac802154/

ifeq ($(CONFIG_NET),y)
obj-$(CONFIG_SYSCTL)		+= sysctl_net.o
endif
obj-$(CONFIG_WIMAX)		+= wimax/
obj-$(CONFIG_DNS_RESOLVER)	+= dns_resolver/
obj-$(CONFIG_CEPH_LIB)		+= ceph/
obj-$(CONFIG_BATMAN_ADV)	+= batman-adv/
obj-$(CONFIG_NFC)		+= nfc/
obj-$(CONFIG_OPENVSWITCH)	+= openvswitch/
