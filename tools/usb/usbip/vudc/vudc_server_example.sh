#!/bin/bash

################################################################################
# This is free and unencumbered software released into the public domain.
#
# Anyone is free to copy, modify, publish, use, compile, sell, or
# distribute this software, either in source code form or as a compiled
# binary, for any purpose, commercial or non-commercial, and by any
# means.
#
# In jurisdictions that recognize copyright laws, the author or authors
# of this software dedicate any and all copyright interest in the
# software to the public domain. We make this dedication for the benefit
# of the public at large and to the detriment of our heirs and
# successors. We intend this dedication to be an overt act of
# relinquishment in perpetuity of all present and future rights to this
# software under copyright law.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
# For more information, please refer to <https://unlicense.org/>
################################################################################

################################################################################
# This is a sample script which shows how to use vUDC with ConfigFS gadgets
################################################################################

# Stop script on error
set -e

################################################################################
# Create your USB gadget
# You may use bare ConfigFS interface (as below)
# or libusbgx or gt toool
# Instead of ConfigFS gadgets you may use any of legacy gadgets.
################################################################################
CONFIGFS_MOUNT_POINT="/sys/kernel/config"
GADGET_NAME="g1"
ID_VENDOR="0x1d6b"
ID_PRODUCT="0x0104"

cd ${CONFIGFS_MOUNT_POINT}/usb_gadget
# Create a new USB gadget
mkdir ${GADGET_NAME}
cd ${GADGET_NAME}

# This gadget contains one function - ACM (serial port over USB)
FUNC_DIR="functions/acm.ser0"
mkdir ${FUNC_DIR}

# Just one configuration
mkdir configs/c.1
ln -s ${FUNC_DIR} configs/c.1

# Set our gadget identity
echo ${ID_VENDOR} > idVendor
echo ${ID_PRODUCT} > idProduct

################################################################################
# Load vudc-module if vudc is not available
# You may change value of num param to get more than one vUDC instance
################################################################################
[[ -d /sys/class/udc/usbip-vudc.0 ]] || modprobe usbip-vudc num=1

################################################################################
# Bind gadget to our vUDC
# By default we bind to first one but you may change this if you would like
# to use more than one instance
################################################################################
echo "usbip-vudc.0" > UDC

################################################################################
# Let's now run our usbip daemon in a USB device mode
################################################################################
usbipd --device &

################################################################################
# Now your USB gadget is available using USB/IP protocol.
# To prepare your client, you should ensure that usbip-vhci module is inside
# your kernel. If it's not then you can load it:
#
# $ modprobe usbip-vhci
#
# To check availability of your gadget you may try to list devices exported
# on a remote server:
#
# $ modprobe usbip-vhci
# $ usbip list -r $SERVER_IP
# Exportable USB devices
# ======================
# usbipd: info: request 0x8005(6): complete
#  - 127.0.0.1
# usbip-vudc.0: Linux Foundation : unknown product (1d6b:0104)
#            : /sys/devices/platform/usbip-vudc.0
#            : (Defined at Interface level) (00/00/00)
#
# To attach this device to your client you may use:
#
# $ usbip attach -r $SERVER_IP -d usbip-vudc.0
#
################################################################################
