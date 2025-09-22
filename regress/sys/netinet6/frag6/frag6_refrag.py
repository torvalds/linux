#!/usr/local/bin/python3

print("fragments of a large packet that has to be refragmented by reflector")

# |--------|
#          |------------------|
#                              ...
#                                 |------------------|
#                                                    |----|

import os
from addr import *
from scapy.all import *

pid=os.getpid()
eid=pid & 0xffff
payload=b"ABCDEFGHIJKLMNOP" * 100
packet=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/ \
    ICMPv6EchoRequest(id=eid, data=payload)
request_cksum=ICMPv6Unknown(bytes(packet.payload)).cksum
print("request cksum=%#x" % (request_cksum))
frag=[]
fid=pid & 0xffffffff
frag.append(IPv6ExtHdrFragment(nh=58, id=fid, m=1)/bytes(packet)[40:56])
offset=2
chunk=4
while 40+8*(offset+chunk) < len(payload):
	frag.append(IPv6ExtHdrFragment(nh=58, id=fid,
	    offset=offset, m=1)/
	    bytes(packet)[40+(8*offset):40+8*(offset+chunk)])
	offset+=chunk
frag.append(IPv6ExtHdrFragment(nh=58, id=fid,
    offset=offset)/bytes(packet)[40+(8*offset):])
eth=[]
for f in frag:
	pkt=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/f
	eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/pkt)

if os.fork() == 0:
	time.sleep(1)
	sendp(eth, iface=LOCAL_IF)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=3, filter=
    "ip6 and src "+REMOTE_ADDR6+" and dst "+LOCAL_ADDR6+" and proto ipv6-frag")
for a in ans:
	if a and a.type == ETH_P_IPV6 and \
	    ipv6nh[a.payload.nh] == 'Fragment Header' and \
	    a.payload.payload.offset == 0 and \
	    ipv6nh[a.payload.payload.nh] == 'ICMPv6' and \
	    icmp6types[a.payload.payload.payload.type] == 'Echo Reply':
		id=a.payload.payload.payload.id
		print("id=%#x" % (id))
		if id != eid:
			print("WRONG ECHO REPLY ID")
			exit(2)
		reply_cksum=a.payload.payload.payload.cksum
		print("reply cksum=%#x" % (reply_cksum))
		# change request checksum incrementaly and check with reply
		diff_cksum=~(~reply_cksum+~(~request_cksum+~0x8000+0x8100))
		if diff_cksum & 0xffff != 0xffff and diff_cksum & 0xffff != 0:
			print("CHECKSUM ERROR diff cksum=%#x" % (diff_cksum))
			exit(1)
		exit(0)
print("NO ECHO REPLY")
exit(2)
