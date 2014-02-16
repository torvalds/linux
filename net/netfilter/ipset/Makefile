#
# Makefile for the ipset modules
#

ip_set-y := ip_set_core.o ip_set_getport.o pfxlen.o

# ipset core
obj-$(CONFIG_IP_SET) += ip_set.o

# bitmap types
obj-$(CONFIG_IP_SET_BITMAP_IP) += ip_set_bitmap_ip.o
obj-$(CONFIG_IP_SET_BITMAP_IPMAC) += ip_set_bitmap_ipmac.o
obj-$(CONFIG_IP_SET_BITMAP_PORT) += ip_set_bitmap_port.o

# hash types
obj-$(CONFIG_IP_SET_HASH_IP) += ip_set_hash_ip.o
obj-$(CONFIG_IP_SET_HASH_IPPORT) += ip_set_hash_ipport.o
obj-$(CONFIG_IP_SET_HASH_IPPORTIP) += ip_set_hash_ipportip.o
obj-$(CONFIG_IP_SET_HASH_IPPORTNET) += ip_set_hash_ipportnet.o
obj-$(CONFIG_IP_SET_HASH_NET) += ip_set_hash_net.o
obj-$(CONFIG_IP_SET_HASH_NETPORT) += ip_set_hash_netport.o
obj-$(CONFIG_IP_SET_HASH_NETIFACE) += ip_set_hash_netiface.o
obj-$(CONFIG_IP_SET_HASH_NETNET) += ip_set_hash_netnet.o
obj-$(CONFIG_IP_SET_HASH_NETPORTNET) += ip_set_hash_netportnet.o

# list types
obj-$(CONFIG_IP_SET_LIST_SET) += ip_set_list_set.o
