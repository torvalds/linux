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
