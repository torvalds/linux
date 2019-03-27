#!/usr/bin/env python
#
# Copyright (c) 2014 Marcel Moolenaar
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#

'''
Simple diagnostics program fo the AMD Am89c900 series ILACC.
This ethernet controller is emulated by VMware Fusion among
possibly other virtualization platforms.

The datasheet can be found here:
    http://support.amd.com/TechDocs/18219.pdf

This example program sends a single DHCP discovery packet,
waits 2 seconds and then iterates over the receive ring for
a targeted packet.

For this program to function, connect the network interface
to a network with a DHCP server. In VMware Fusion this can
best be done by configuring the interface as a NAT interface
using the "Share with my Mac" setting.
'''

import ctypes
import logging
import os
import sys
import time

sys.path.append('/usr/lib')

import bus
import busdma


# ILACC initialization block definition
class initblock(ctypes.LittleEndianStructure):
    _fields_ = [('mode', ctypes.c_uint32),
                ('hwaddr', ctypes.c_uint8 * 6),
                ('_pad1_', ctypes.c_uint16),
                ('filter', ctypes.c_uint16 * 4),
                ('rxdesc', ctypes.c_uint32),
                ('txdesc', ctypes.c_uint32),
                ('_pad2_', ctypes.c_uint32)]


# ILACC ring buffer descriptor
class bufdesc(ctypes.LittleEndianStructure):
    _fields_ = [('buffer', ctypes.c_uint32),
                ('flags', ctypes.c_uint32),
                ('length', ctypes.c_uint32),
                ('_pad_', ctypes.c_uint32)]


# The DHCP packet definition (incl. all headers)
class packet(ctypes.BigEndianStructure):
    _pack_ = 1
    _fields_ = [('eth_dest', ctypes.c_uint8 * 6),
                ('eth_src', ctypes.c_uint8 * 6),
                ('eth_type', ctypes.c_uint16),
                ('ip_vl', ctypes.c_uint8),
                ('ip_de', ctypes.c_uint8),
                ('ip_len', ctypes.c_uint16),
                ('ip_id', ctypes.c_uint16),
                ('ip_ff', ctypes.c_uint16),
                ('ip_ttl', ctypes.c_uint8),
                ('ip_proto', ctypes.c_uint8),
                ('ip_cksum', ctypes.c_uint16),
                ('ip_src', ctypes.c_uint32),
                ('ip_dest', ctypes.c_uint32),
                ('udp_src', ctypes.c_uint16),
                ('udp_dest', ctypes.c_uint16),
                ('udp_len', ctypes.c_uint16),
                ('udp_cksum', ctypes.c_uint16),
                ('bootp_op', ctypes.c_uint8),
                ('bootp_htype', ctypes.c_uint8),
                ('bootp_hlen', ctypes.c_uint8),
                ('bootp_hops', ctypes.c_uint8),
                ('bootp_xid', ctypes.c_uint32),
                ('bootp_secs', ctypes.c_uint16),
                ('bootp_flags', ctypes.c_uint16),
                ('bootp_ciaddr', ctypes.c_uint32),
                ('bootp_yiaddr', ctypes.c_uint32),
                ('bootp_siaddr', ctypes.c_uint32),
                ('bootp_giaddr', ctypes.c_uint32),
                ('bootp_chaddr', ctypes.c_uint8 * 16),
                ('bootp_sname', ctypes.c_uint8 * 64),
                ('bootp_file', ctypes.c_uint8 * 128),
                ('dhcp_magic', ctypes.c_uint32),
                ('dhcp_options', ctypes.c_uint8 * 60)]

MACFMT = '%02x:%02x:%02x:%02x:%02x:%02x'

dev = 'pci0:2:1:0'

logging.basicConfig(level=logging.DEBUG)

pcicfg = bus.map(dev, 'pcicfg')
logging.debug('pcicfg=%s (%s)' % (pcicfg, dev))

vendor = bus.read_2(pcicfg, 0)
device = bus.read_2(pcicfg, 2)
if vendor != 0x1022 or device != 0x2000:
    logging.error('Not an AMD PCnet-PCI (vendor=%x, device=%x)' %
                  (vendor, device))
    sys.exit(1)

command = bus.read_2(pcicfg, 4)
if not (command & 1):
    logging.info('enabling I/O port decoding')
    command |= 1
    bus.write_2(pcicfg, 4, command)

if not (command & 4):
    logging.info('enabling bus mastering')
    command |= 4
    bus.write_2(pcicfg, 4, command)

bus.unmap(pcicfg)

io = bus.map(dev, '10.io')
logging.debug('io=%s (%s)' % (io, dev))


def delay(msec):
    time.sleep(msec / 1000.0)


def ffs(x):
    y = (1 + (x ^ (x-1))) >> 1
    return y.bit_length()


def ip_str(a):
    return '%d.%d.%d.%d' % ((a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255,
                            a & 255)


def mac_is(l, r):
    for i in xrange(6):
        if l[i] != r[i]:
            return False
    return True


def mac_str(m):
    return MACFMT % (m[0], m[1], m[2], m[3], m[4], m[5])


def rdbcr(reg):
    bus.write_2(io, 0x12, reg & 0xffff)
    return bus.read_2(io, 0x16)


def wrbcr(reg, val):
    bus.write_2(io, 0x12, reg & 0xffff)
    bus.write_2(io, 0x16, val & 0xffff)


def rdcsr(reg):
    bus.write_2(io, 0x12, reg & 0xffff)
    return bus.read_2(io, 0x10)


def wrcsr(reg, val):
    bus.write_2(io, 0x12, reg & 0xffff)
    bus.write_2(io, 0x10, val & 0xffff)


def start():
    wrcsr(0, 0x42)
    delay(100)


def stop():
    wrcsr(0, 4)
    delay(100)


mac = ()
bcast = ()
for o in xrange(6):
    mac += (bus.read_1(io, o),)
    bcast += (0xff,)
logging.info('ethernet address = ' + MACFMT % mac)

stop()
wrbcr(20, 2)            # reset
wrcsr(3, 0)             # byte swapping mode
wrbcr(2, rdbcr(2) | 2)  # Autoneg

memsize = 32*1024
bufsize = 1536
nrxbufs = 16
ntxbufs = 4
logging.debug("DMA memory: size = %#x (TX buffers: %u, RX buffers: %u)" %
              (memsize, ntxbufs, nrxbufs))

mem_tag = busdma.tag_create(dev, 16, 0, 0xffffffff, memsize, 1, memsize, 0, 0)
dmamem = busdma.mem_alloc(mem_tag, 0)
busseg = busdma.md_first_seg(dmamem, busdma.MD_BUS_SPACE)
cpuseg = busdma.md_first_seg(dmamem, busdma.MD_VIRT_SPACE)
busaddr = busdma.seg_get_addr(busseg)
cpuaddr = busdma.seg_get_addr(cpuseg)
logging.debug("DMA memory: CPU address: %#x, device address: %#x" %
              (cpuaddr, busaddr))

addr_initblock = cpuaddr
addr_rxdesc = addr_initblock + ctypes.sizeof(initblock)
addr_txdesc = addr_rxdesc + ctypes.sizeof(bufdesc) * nrxbufs
addr_rxbufs = addr_txdesc + ctypes.sizeof(bufdesc) * ntxbufs
addr_txbufs = addr_rxbufs + bufsize * nrxbufs

ib = initblock.from_address(addr_initblock)
ib.mode = ((ffs(ntxbufs) - 1) << 28) | ((ffs(nrxbufs) - 1) << 20)
for i in xrange(len(mac)):
    ib.hwaddr[i] = mac[i]
for i in xrange(4):
    ib.filter[i] = 0xffff
ib.rxdesc = busaddr + (addr_rxdesc - cpuaddr)
ib.txdesc = busaddr + (addr_txdesc - cpuaddr)
ib._pad1_ = 0
ib._pad2_ = 0

for i in xrange(nrxbufs):
    bd = bufdesc.from_address(addr_rxdesc + ctypes.sizeof(bufdesc) * i)
    bd.buffer = busaddr + (addr_rxbufs - cpuaddr) + bufsize * i
    bd.flags = (1 << 31) | (15 << 12) | (-bufsize & 0xfff)
    bd.length = 0
    bd._pad_ = 0

for i in xrange(ntxbufs):
    bd = bufdesc.from_address(addr_txdesc + ctypes.sizeof(bufdesc) * i)
    bd.buffer = busaddr + (addr_txbufs - cpuaddr) + bufsize * i
    bd.flags = (15 << 12)
    bd.length = 0
    bd._pad_ = 0

busdma.sync_range(dmamem, busdma.SYNC_PREWRITE, 0, addr_rxbufs - cpuaddr)

# Program address of DMA memory
wrcsr(1, busaddr)
wrcsr(2, busaddr >> 16)
delay(100)

# Initialize hardware
wrcsr(0, 1)
logging.debug('Waiting for initialization to complete')
csr = rdcsr(0)
while (csr & 0x100) == 0:
    logging.debug('CSR=%#x' % (csr))
    csr = rdcsr(0)

start()

pkt = packet.from_address(addr_txbufs)
ctypes.memset(addr_txbufs, 0, ctypes.sizeof(pkt))
options = [53, 1, 1]
for i in xrange(len(options)):
    pkt.dhcp_options[i] = options[i]
pkt.dhcp_magic = 0x63825363
for i in xrange(6):
    pkt.bootp_chaddr[i] = mac[i]
pkt.bootp_hlen = 6
pkt.bootp_htype = 1
pkt.bootp_op = 1
pkt.udp_len = ctypes.sizeof(pkt) - 34
pkt.udp_dest = 67
pkt.udp_src = 68
pkt.ip_dest = 0xffffffff
pkt.ip_cksum = 0x79a6
pkt.ip_proto = 17
pkt.ip_ttl = 64
pkt.ip_len = ctypes.sizeof(pkt) - 14
pkt.ip_vl = 0x45
pkt.eth_type = 0x0800
for i in xrange(6):
    pkt.eth_src[i] = mac[i]
    pkt.eth_dest[i] = bcast[i]
pktlen = ctypes.sizeof(pkt)

busdma.sync_range(dmamem, busdma.SYNC_PREWRITE, addr_txbufs - cpuaddr, bufsize)

bd = bufdesc.from_address(addr_txdesc)
bd.length = 0
bd.flags = (1 << 31) | (1 << 25) | (1 << 24) | (0xf << 12) | (-pktlen & 0xfff)

busdma.sync_range(dmamem, busdma.SYNC_PREWRITE, addr_txdesc - cpuaddr,
                  ctypes.sizeof(bufdesc))

wrcsr(0, 0x48)

logging.info('DHCP discovery packet sent')

# Now wait 2 seconds for a DHCP offer to be received.
logging.debug('Waiting 2 seconds for an offer to be received')
time.sleep(2)

stop()

busdma.sync_range(dmamem, busdma.SYNC_PREWRITE, addr_rxdesc - cpuaddr,
                  ctypes.sizeof(bufdesc) * nrxbufs)

for i in xrange(nrxbufs):
    bd = bufdesc.from_address(addr_rxdesc + ctypes.sizeof(bufdesc) * i)
    if (bd.flags & 0x80000000):
        continue
    pkt = packet.from_address(addr_rxbufs + i * bufsize)
    if mac_is(pkt.eth_dest, bcast):
        logging.debug('RX #%d: broadcast packet: length %u' % (i, bd.length))
        continue
    if not mac_is(pkt.eth_dest, mac):
        logging.debug('RX #%d: packet for %s?' % (i, mac_str(pkt.eth_dest)))
        continue
    logging.debug('RX %d: packet from %s!' % (i, mac_str(pkt.eth_src)))
    logging.info('Our IP address = %s' % (ip_str(pkt.ip_dest)))

busdma.mem_free(dmamem)
busdma.tag_destroy(mem_tag)
bus.unmap(io)
