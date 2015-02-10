#
# Copyright (c) 2013-2014, NVIDIA Corporation. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#

tlk_driver-objs += ote_device.o
tlk_driver-objs += ote_comms.o
tlk_driver-objs += ote_fs.o
tlk_driver-objs += ote_asm.o
tlk_driver-objs += ote_log.o

ifeq ($(CONFIG_ARM),y)
plus_sec := $(call as-instr,.arch_extension sec,+sec)
AFLAGS_ote_asm.o :=-Wa,-march=armv7-a$(plus_sec)
endif

obj-$(CONFIG_TRUSTED_LITTLE_KERNEL) += tlk_driver.o
