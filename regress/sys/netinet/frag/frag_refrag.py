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
packet=IP(src=LOCAL_ADDR, dst=REMOTE_ADDR)/ \
    ICMP(type='echo-request', id=eid)/payload
request_cksum=ICMP(bytes(packet.payload)).chksum
print("request cksum=%#x" % (request_cksum))
frag=[]
fid=pid & 0xffff
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
    flags='MF')/bytes(packet)[20:36])
offset=2
chunk=4
while 40+8*(offset+chunk) < len(payload):
	frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
	    frag=offset, flags='MF')/
	    bytes(packet)[20+(8*offset):20+8*(offset+chunk)])
	offset+=chunk
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
	frag=offset)/bytes(packet)[20+(8*offset):])
eth=[]
for f in frag:
	eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/f)

if os.fork() == 0:
	time.sleep(1)
	sendp(eth, iface=LOCAL_IF)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=3, filter=
    "ip and src "+REMOTE_ADDR+" and dst "+LOCAL_ADDR+" and icmp")
for a in ans:
	if a and a.type == ETH_P_IP and \
	    a.payload.proto == 1 and \
	    a.payload.frag == 0 and a.payload.flags == 1 and \
	    icmptypes[a.payload.payload.type] == 'echo-reply':
		id=a.payload.payload.id
		print("id=%#x" % (id))
		if id != eid:
			print("WRONG ECHO REPLY ID")
			exit(2)
		reply_cksum=a.payload.payload.chksum
		print("reply cksum=%#x" % (reply_cksum))
		# change request checksum incrementaly and check with reply
		diff_cksum=~(~reply_cksum+~(~request_cksum+~0x0800+0x0000))
		if diff_cksum & 0xffff != 0xffff and diff_cksum & 0xffff != 0:
			print("CHECKSUM ERROR diff cksum=%#x" % (diff_cksum))
			exit(1)
		exit(0)
print("NO ECHO REPLY")
exit(2)
