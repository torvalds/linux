#!/usr/local/bin/python2.7

import random
import scapy.all as sp
import sys

UDP_PROTO  = 17
AH_PROTO   = 51
FRAG_PROTO = 44

def main():
    intf = sys.argv[1]
    ipv6_src = sys.argv[2]
    ipv6_dst = sys.argv[3]

    ipv6_main = sp.IPv6(dst=ipv6_dst, src=ipv6_src)

    padding = 8
    fid = random.randint(0,100000)
    frag_0 = sp.IPv6ExtHdrFragment(id=fid, nh=UDP_PROTO, m=1, offset=0)
    frag_1 = sp.IPv6ExtHdrFragment(id=fid, nh=UDP_PROTO, m=0, offset=padding/8)
    
    pkt1_opts = sp.AH(nh=AH_PROTO, payloadlen=200) \
            / sp.Raw('XXXX' * 199) \
            / sp.AH(nh=FRAG_PROTO, payloadlen=1) \
            / frag_1

    pkt0 = sp.Ether() / ipv6_main / frag_0 / sp.Raw('A' * padding)
    pkt1 = sp.Ether() / ipv6_main / pkt1_opts / sp.Raw('B' * padding)

    sp.sendp(pkt0, iface=intf, verbose=False)
    sp.sendp(pkt1, iface=intf, verbose=False)

if __name__ == '__main__':
	main()
