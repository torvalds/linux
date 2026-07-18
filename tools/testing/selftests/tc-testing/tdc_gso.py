#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
tdc_gso.py - send a UDP GSO datagram

Copyright (C) 2026 Xingquan Liu <b1n@b1n.io>
"""

import argparse
import socket
import struct
import sys

UDP_MAX_SEGMENTS = 1 << 7


parser = argparse.ArgumentParser(description="UDP GSO datagram sender")
parser.add_argument("src", help="source IPv4 address")
parser.add_argument("dst", help="destination IPv4 address")
parser.add_argument("port", type=int, help="destination UDP port")
parser.add_argument("gso_size", type=int, help="UDP GSO segment payload size")
parser.add_argument("payload_len", type=int, help="total UDP payload length")
args = parser.parse_args()

if args.gso_size <= 0 or args.gso_size > 0xFFFF:
    parser.error("gso_size must fit in an unsigned 16-bit integer")
if args.payload_len <= args.gso_size:
    parser.error("payload_len must be larger than gso_size")
if args.payload_len > args.gso_size * UDP_MAX_SEGMENTS:
    parser.error("payload_len exceeds UDP_MAX_SEGMENTS")

SOL_UDP = getattr(socket, "SOL_UDP", socket.IPPROTO_UDP)
UDP_SEGMENT = getattr(socket, "UDP_SEGMENT", 103)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((args.src, 0))

payload = b"b" * args.payload_len
cmsg = [(SOL_UDP, UDP_SEGMENT, struct.pack("=H", args.gso_size))]

sent = sock.sendmsg([payload], cmsg, 0, (args.dst, args.port))
sys.exit(sent != len(payload))
