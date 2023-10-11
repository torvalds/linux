#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
This script helps generate fragmented UDP packets.

While it is technically possible to dynamically generate
fragmented packets in C, it is much harder to read and write
said code. `scapy` is relatively industry standard and really
easy to read / write.

So we choose to write this script that generates a valid C
header. Rerun script and commit generated file after any
modifications.
"""

import argparse
import os

from scapy.all import *


# These constants must stay in sync with `ip_check_defrag.c`
VETH1_ADDR = "172.16.1.200"
VETH0_ADDR6 = "fc00::100"
VETH1_ADDR6 = "fc00::200"
CLIENT_PORT = 48878
SERVER_PORT = 48879
MAGIC_MESSAGE = "THIS IS THE ORIGINAL MESSAGE, PLEASE REASSEMBLE ME"


def print_header(f):
    f.write("// SPDX-License-Identifier: GPL-2.0\n")
    f.write("/* DO NOT EDIT -- this file is generated */\n")
    f.write("\n")
    f.write("#ifndef _IP_CHECK_DEFRAG_FRAGS_H\n")
    f.write("#define _IP_CHECK_DEFRAG_FRAGS_H\n")
    f.write("\n")
    f.write("#include <stdint.h>\n")
    f.write("\n")


def print_frags(f, frags, v6):
    for idx, frag in enumerate(frags):
        # 10 bytes per line to keep width in check
        chunks = [frag[i : i + 10] for i in range(0, len(frag), 10)]
        chunks_fmted = [", ".join([str(hex(b)) for b in chunk]) for chunk in chunks]
        suffix = "6" if v6 else ""

        f.write(f"static uint8_t frag{suffix}_{idx}[] = {{\n")
        for chunk in chunks_fmted:
            f.write(f"\t{chunk},\n")
        f.write(f"}};\n")


def print_trailer(f):
    f.write("\n")
    f.write("#endif /* _IP_CHECK_DEFRAG_FRAGS_H */\n")


def main(f):
    # srcip of 0 is filled in by IP_HDRINCL
    sip = "0.0.0.0"
    sip6 = VETH0_ADDR6
    dip = VETH1_ADDR
    dip6 = VETH1_ADDR6
    sport = CLIENT_PORT
    dport = SERVER_PORT
    payload = MAGIC_MESSAGE.encode()

    # Disable UDPv4 checksums to keep code simpler
    pkt = IP(src=sip,dst=dip) / UDP(sport=sport,dport=dport,chksum=0) / Raw(load=payload)
    # UDPv6 requires a checksum
    # Also pin the ipv6 fragment header ID, otherwise it's a random value
    pkt6 = IPv6(src=sip6,dst=dip6) / IPv6ExtHdrFragment(id=0xBEEF) / UDP(sport=sport,dport=dport) / Raw(load=payload)

    frags = [f.build() for f in pkt.fragment(24)]
    frags6 = [f.build() for f in fragment6(pkt6, 72)]

    print_header(f)
    print_frags(f, frags, False)
    print_frags(f, frags6, True)
    print_trailer(f)


if __name__ == "__main__":
    dir = os.path.dirname(os.path.realpath(__file__))
    header = f"{dir}/ip_check_defrag_frags.h"
    with open(header, "w") as f:
        main(f)
