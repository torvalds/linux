#
# Makefile for the Linux kernel ubuntu supplied third-party device drivers.
#

#
# NOTE: to allow drivers to be added and removed without causing merge
# collisions you should add new entries in the middle of the six lines
# of ## at the bottom of the list.  Always add three lines of ## above
# your new entry and maintain the six lines below.
#

##
##
##
##
##
##
ifeq ($(ARCH),x86)
obj-y                         += vbox/
endif
##
##
##
obj-$(CONFIG_HIO)             += hio/
##
##
##
ifeq ($(ARCH),x86)
obj-y				+= xr-usb-serial/
endif
##
##
##
##
##
##

# This is a stupid trick to get kbuild to create ubuntu/built-in.o
obj- += foo.o
