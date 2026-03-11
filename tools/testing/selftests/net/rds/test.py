#! /usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import argparse
import ctypes
import errno
import hashlib
import os
import select
import signal
import socket
import subprocess
import sys
import tempfile
import shutil

# Allow utils module to be imported from different directory
this_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.append(os.path.join(this_dir, "../"))
from lib.py.utils import ip

libc = ctypes.cdll.LoadLibrary('libc.so.6')
setns = libc.setns

NET0 = 'net0'
NET1 = 'net1'

VETH0 = 'veth0'
VETH1 = 'veth1'

# Helper function for creating a socket inside a network namespace.
# We need this because otherwise RDS will detect that the two TCP
# sockets are on the same interface and use the loop transport instead
# of the TCP transport.
def netns_socket(netns, *sock_args):
    """
    Creates sockets inside of network namespace

    :param netns: the name of the network namespace
    :param sock_args: socket family and type
    """
    u0, u1 = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)

    child = os.fork()
    if child == 0:
        # change network namespace
        with open(f'/var/run/netns/{netns}', encoding='utf-8') as f:
            try:
                setns(f.fileno(), 0)
            except IOError as e:
                print(e.errno)
                print(e)

        # create socket in target namespace
        sock = socket.socket(*sock_args)

        # send resulting socket to parent
        socket.send_fds(u0, [], [sock.fileno()])

        sys.exit(0)

    # receive socket from child
    _, fds, _, _ = socket.recv_fds(u1, 0, 1)
    os.waitpid(child, 0)
    u0.close()
    u1.close()
    return socket.fromfd(fds[0], *sock_args)

def signal_handler(_sig, _frame):
    """
    Test timed out signal handler
    """
    print('Test timed out')
    sys.exit(1)

#Parse out command line arguments.  We take an optional
# timeout parameter and an optional log output folder
parser = argparse.ArgumentParser(description="init script args",
                  formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument("-d", "--logdir", action="store",
                    help="directory to store logs", default="/tmp")
parser.add_argument('--timeout', help="timeout to terminate hung test",
                    type=int, default=0)
parser.add_argument('-l', '--loss', help="Simulate tcp packet loss",
                    type=int, default=0)
parser.add_argument('-c', '--corruption', help="Simulate tcp packet corruption",
                    type=int, default=0)
parser.add_argument('-u', '--duplicate', help="Simulate tcp packet duplication",
                    type=int, default=0)
args = parser.parse_args()
logdir=args.logdir
PACKET_LOSS=str(args.loss)+'%'
PACKET_CORRUPTION=str(args.corruption)+'%'
PACKET_DUPLICATE=str(args.duplicate)+'%'

ip(f"netns add {NET0}")
ip(f"netns add {NET1}")
ip("link add type veth")

addrs = [
    # we technically don't need different port numbers, but this will
    # help identify traffic in the network analyzer
    ('10.0.0.1', 10000),
    ('10.0.0.2', 20000),
]

# move interfaces to separate namespaces so they can no longer be
# bound directly; this prevents rds from switching over from the tcp
# transport to the loop transport.
ip(f"link set {VETH0} netns {NET0} up")
ip(f"link set {VETH1} netns {NET1} up")



# add addresses
ip(f"-n {NET0} addr add {addrs[0][0]}/32 dev {VETH0}")
ip(f"-n {NET1} addr add {addrs[1][0]}/32 dev {VETH1}")

# add routes
ip(f"-n {NET0} route add {addrs[1][0]}/32 dev {VETH0}")
ip(f"-n {NET1} route add {addrs[0][0]}/32 dev {VETH1}")

# sanity check that our two interfaces/addresses are correctly set up
# and communicating by doing a single ping
ip(f"netns exec {NET0} ping -c 1 {addrs[1][0]}")

# Start a packet capture on each network
tcpdump_procs = []
for net in [NET0, NET1]:
    pcap = logdir+'/'+net+'.pcap'
    fd, pcap_tmp = tempfile.mkstemp(suffix=".pcap", prefix=f"{net}-", dir="/tmp")
    p = subprocess.Popen(
        ['ip', 'netns', 'exec', net,
         '/usr/sbin/tcpdump', '-i', 'any', '-w', pcap_tmp])
    tcpdump_procs.append((p, pcap_tmp, pcap, fd))

# simulate packet loss, duplication and corruption
for net, iface in [(NET0, VETH0), (NET1, VETH1)]:
    ip(f"netns exec {net} /usr/sbin/tc qdisc add dev {iface} root netem  \
         corrupt {PACKET_CORRUPTION} loss {PACKET_LOSS} duplicate  \
         {PACKET_DUPLICATE}")

# add a timeout
if args.timeout > 0:
    signal.alarm(args.timeout)
    signal.signal(signal.SIGALRM, signal_handler)

sockets = [
    netns_socket(NET0, socket.AF_RDS, socket.SOCK_SEQPACKET),
    netns_socket(NET1, socket.AF_RDS, socket.SOCK_SEQPACKET),
]

for s, addr in zip(sockets, addrs):
    s.bind(addr)
    s.setblocking(0)

fileno_to_socket = {
    s.fileno(): s for s in sockets
}

addr_to_socket = dict(zip(addrs, sockets))

socket_to_addr = {
    s: addr for addr, s in zip(addrs, sockets)
}

send_hashes = {}
recv_hashes = {}

ep = select.epoll()

for s in sockets:
    ep.register(s, select.EPOLLRDNORM)

NUM_PACKETS = 50000
nr_send = 0
nr_recv = 0

while nr_send < NUM_PACKETS:
    # Send as much as we can without blocking
    print("sending...", nr_send, nr_recv)
    while nr_send < NUM_PACKETS:
        send_data = hashlib.sha256(
            f'packet {nr_send}'.encode('utf-8')).hexdigest().encode('utf-8')

        # pseudo-random send/receive pattern
        sender = sockets[nr_send % 2]
        receiver = sockets[1 - (nr_send % 3) % 2]

        try:
            sender.sendto(send_data, socket_to_addr[receiver])
            send_hashes.setdefault((sender.fileno(), receiver.fileno()),
                    hashlib.sha256()).update(f'<{send_data}>'.encode('utf-8'))
            nr_send = nr_send + 1
        except BlockingIOError as e:
            break
        except OSError as e:
            if e.errno in [errno.ENOBUFS, errno.ECONNRESET, errno.EPIPE]:
                break
            raise

    # Receive as much as we can without blocking
    print("receiving...", nr_send, nr_recv)
    while nr_recv < nr_send:
        for fileno, eventmask in ep.poll():
            receiver = fileno_to_socket[fileno]

            if eventmask & select.EPOLLRDNORM:
                while True:
                    try:
                        recv_data, address = receiver.recvfrom(1024)
                        sender = addr_to_socket[address]
                        recv_hashes.setdefault((sender.fileno(),
                            receiver.fileno()), hashlib.sha256()).update(
                                    f'<{recv_data}>'.encode('utf-8'))
                        nr_recv = nr_recv + 1
                    except BlockingIOError as e:
                        break

    # exercise net/rds/tcp.c:rds_tcp_sysctl_reset()
    for net in [NET0, NET1]:
        ip(f"netns exec {net} /usr/sbin/sysctl net.rds.tcp.rds_tcp_rcvbuf=10000")
        ip(f"netns exec {net} /usr/sbin/sysctl net.rds.tcp.rds_tcp_sndbuf=10000")

print("done", nr_send, nr_recv)

# the Python socket module doesn't know these
RDS_INFO_FIRST = 10000
RDS_INFO_LAST = 10017

nr_success = 0
nr_error = 0

for s in sockets:
    for optname in range(RDS_INFO_FIRST, RDS_INFO_LAST + 1):
        # Sigh, the Python socket module doesn't allow us to pass
        # buffer lengths greater than 1024 for some reason. RDS
        # wants multiple pages.
        try:
            s.getsockopt(socket.SOL_RDS, optname, 1024)
            nr_success = nr_success + 1
        except OSError as e:
            nr_error = nr_error + 1
            if e.errno == errno.ENOSPC:
                # ignore
                pass

print(f"getsockopt(): {nr_success}/{nr_error}")

print("Stopping network packet captures")
for p, pcap_tmp, pcap, fd in tcpdump_procs:
    p.terminate()
    p.wait()
    os.close(fd)
    shutil.move(pcap_tmp, pcap)

# We're done sending and receiving stuff, now let's check if what
# we received is what we sent.
for (sender, receiver), send_hash in send_hashes.items():
    recv_hash = recv_hashes.get((sender, receiver))

    if recv_hash is None:
        print("FAIL: No data received")
        sys.exit(1)

    if send_hash.hexdigest() != recv_hash.hexdigest():
        print("FAIL: Send/recv mismatch")
        print("hash expected:", send_hash.hexdigest())
        print("hash received:", recv_hash.hexdigest())
        sys.exit(1)

    print(f"{sender}/{receiver}: ok")

print("Success")
sys.exit(0)
