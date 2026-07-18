#! /usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
This module provides functional testing for the net/rds component.
"""

import argparse
import atexit
import ctypes
import errno
import hashlib
import os
import select
import re
import signal
import socket
import subprocess
import sys
import time

# Allow utils module to be imported from different directory
this_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.append(os.path.join(this_dir, "../"))
# pylint: disable-next=wrong-import-position,import-error,no-name-in-module
from lib.py.utils import ip, cmd # noqa: E402
# pylint: disable-next=wrong-import-position,import-error,no-name-in-module
from lib.py.ksft import ksft_pr # noqa: E402

libc = ctypes.cdll.LoadLibrary('libc.so.6')
setns = libc.setns

NET0 = 'net0'
NET1 = 'net1'

VETH0 = 'veth0'
VETH1 = 'veth1'

tcpdump_procs = []
tcp_addrs = [
    # we technically don't need different port numbers, but this will
    # help identify traffic in the network analyzer
    ('10.0.0.1', 10000),
    ('10.0.0.2', 20000),
]

# RDMA network configs
RXE_DEV0 = 'rxe0'
RXE_DEV1 = 'rxe1'

VETH_RDMA0 = 'veth_rdma0'
VETH_RDMA1 = 'veth_rdma1'

rdma_addrs = [
    ('10.0.0.3', 30000),
    ('10.0.0.4', 30000),
]

# send_packets flag space
OP_FLAG_TCP     = 0x1
OP_FLAG_RDMA    = 0x2

# from include/uapi/linux/rds.h: SO_RDS_TRANSPORT pins a socket to a
# specific RDS transport so connection setup cannot silently fall back
# to another (e.g. loopback) transport.
SOL_RDS          = 276
SO_RDS_TRANSPORT = 8
RDS_TRANS_TCP    = 2
RDS_TRANS_IB     = 0

signal_handler_label = ""

tap_idx = 0
nr_pass = 0
nr_fail = 0

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
        try:
            # change network namespace
            with open(f'/var/run/netns/{netns}', encoding='utf-8') as f:
                setns(f.fileno(), 0)
            # create socket in target namespace
            sock = socket.socket(*sock_args)

            # send resulting socket to parent
            socket.send_fds(u0, [], [sock.fileno()])

            os._exit(0)
        except BaseException:
            os._exit(1)

    # receive socket from child
    _, fds, _, _ = socket.recv_fds(u1, 0, 1)
    _, status = os.waitpid(child, 0)
    u0.close()
    u1.close()
    if not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
        raise RuntimeError(
            f"netns_socket child failed in netns {netns} (status={status})")
    return socket.fromfd(fds[0], *sock_args)

def send_burst(socks, ip_addrs, snd_hashes, nr_sent, nr_total):
    """Send until blocked or nr_total reached. Return updated nr_sent."""

    while nr_sent < nr_total:
        data = hashlib.sha256(
            f'packet {nr_sent}'.encode('utf-8')).hexdigest().encode('utf-8')
        # pseudo-random send/receive pattern
        snd_idx = nr_sent % 2
        rcv_idx = 1 - (nr_sent % 3) % 2

        snd = socks[snd_idx]
        rcv = socks[rcv_idx]
        try:
            snd.sendto(data, ip_addrs[rcv_idx])
        except BlockingIOError:
            return nr_sent
        except OSError as e:
            if e.errno in (errno.ENOBUFS, errno.ECONNRESET, errno.EPIPE):
                return nr_sent
            raise
        snd_hashes.setdefault((snd.fileno(), rcv.fileno()),
                hashlib.sha256()).update(f'<{data}>'.encode('utf-8'))
        nr_sent += 1
    return nr_sent

def recv_burst(epoll, socks, ip_addrs, rcv_hashes, nr_rcv):
    """Drain whatever's readable from epoll. Return updated nr_recv."""
    for filen, evntmask in epoll.poll():
        if not evntmask & select.EPOLLRDNORM:
            continue
        rcv = next(s for s in socks if s.fileno() == filen)
        while True:
            try:
                data, adr = rcv.recvfrom(1024)
            except BlockingIOError:
                break
            snd_idx = ip_addrs.index(adr)
            snd = socks[snd_idx]
            rcv_hashes.setdefault((snd.fileno(), rcv.fileno()),
                    hashlib.sha256()).update(f'<{data}>'.encode('utf-8'))
            nr_rcv += 1
    return nr_rcv

def check_info(socks):
    """
    Check all rds info pages for errors

    :param socks: list of sockets to check
    """

    # the Python socket module doesn't know these
    rds_info_first = 10000
    rds_info_last = 10017

    nr_success = 0
    nr_error = 0

    for sock in socks:
        for optname in range(rds_info_first, rds_info_last + 1):
            # Sigh, the Python socket module doesn't allow us to pass
            # buffer lengths greater than 1024 for some reason. RDS
            # wants multiple pages.
            try:
                sock.getsockopt(socket.SOL_RDS, optname, 1024)
                nr_success = nr_success + 1
            except OSError as e:
                nr_error = nr_error + 1
                if e.errno == errno.ENOSPC:
                    # ignore
                    pass

    ksft_pr(f"getsockopt(): {nr_success}/{nr_error}")

def verify_hashes(snd_hashes, rcv_hashes):
    """Compare send/recv hashes per (sender, receiver) pair."""
    for key, snd_hash in snd_hashes.items():
        rcv_hash = rcv_hashes.get(key)
        if rcv_hash is None:
            ksft_pr("FAIL: No data received")
            return 1
        if snd_hash.hexdigest() != rcv_hash.hexdigest():
            ksft_pr("FAIL: Send/recv mismatch")
            ksft_pr("hash expected:", snd_hash.hexdigest())
            ksft_pr("hash received:", rcv_hash.hexdigest())
            return 1
        ksft_pr(f"{key[0]}/{key[1]}: ok")
    return 0

def snd_rcv_packets(env):
    """
    Send packets on the given network interfaces

    :param env: transport-environment dict for setup_tcp() / setup_rdma().
                "addrs": list of (ip, port) tuples matching the sockets
                "netns": list of netns names for TCP or None for RDMA
                "flags": OP_FLAG_TCP or OP_FLAG_RDMA, selects sockets
    """

    addrs = env["addrs"]
    netns_list = env["netns"]
    flags = env.get("flags", 0)

    if (flags & OP_FLAG_TCP) and (flags & OP_FLAG_RDMA):
        raise RuntimeError(f"Invalid transport flag sets multiple transports: {flags}")

    if flags & OP_FLAG_TCP:
        sockets = [
            netns_socket(netns_list[0], socket.AF_RDS, socket.SOCK_SEQPACKET),
            netns_socket(netns_list[1], socket.AF_RDS, socket.SOCK_SEQPACKET),
        ]

        # Pin the sockets to the TCP transport so it doesn't fail over to a
        # different transport during this test
        for s in sockets:
            s.setsockopt(SOL_RDS, SO_RDS_TRANSPORT, RDS_TRANS_TCP)
    elif flags & OP_FLAG_RDMA:
        sockets = [
            socket.socket(socket.AF_RDS, socket.SOCK_SEQPACKET),
            socket.socket(socket.AF_RDS, socket.SOCK_SEQPACKET),
        ]

        # Pin the sockets to the RDMA transport so it doesn't fail over to a
        # different transport during this test
        for s in sockets:
            s.setsockopt(SOL_RDS, SO_RDS_TRANSPORT, RDS_TRANS_IB)
    else:
        raise RuntimeError(f"Invalid transport flag sets no transports: {flags}")

    for s, addr in zip(sockets, addrs):
        s.bind(addr)
        s.setblocking(0)

    send_hashes = {}
    recv_hashes = {}

    ep = select.epoll()

    for s in sockets:
        ep.register(s, select.EPOLLRDNORM)

    num_packets = 50000
    nr_send = 0
    nr_recv = 0

    while nr_send < num_packets:

        # Send as much as we can without blocking
        ksft_pr("sending...", nr_send, nr_recv)
        nr_send = send_burst(sockets, addrs, send_hashes, nr_send, num_packets)

        # Receive as much as we can without blocking
        ksft_pr("receiving...", nr_send, nr_recv)
        while nr_recv < nr_send:
            nr_recv = recv_burst(ep, sockets, addrs, recv_hashes, nr_recv)

        # exercise net/rds/tcp.c:rds_tcp_sysctl_reset()
        if netns_list:
            for net in netns_list:
                ip(f"netns exec {net} /usr/sbin/sysctl net.rds.tcp.rds_tcp_rcvbuf=10000")
                ip(f"netns exec {net} /usr/sbin/sysctl net.rds.tcp.rds_tcp_sndbuf=10000")

    ksft_pr("done", nr_send, nr_recv)

    check_info(sockets)

    # We're done sending and receiving stuff, now let's check if what
    # we received is what we sent.
    rc = verify_hashes(send_hashes, recv_hashes)

    ep.close()
    for s in sockets:
        s.close()

    return rc

def stop_pcaps():
    """Stop tcpdump processes.

    We use pop() here to drain the list in the event that the test
    completes after the signal handler is fired.  List will be empty
    if logdir is not set
    """

    if not tcpdump_procs:
        return

    ksft_pr("Stopping network packet captures")
    while tcpdump_procs:
        proc = tcpdump_procs.pop()
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

def signal_handler(_sig, _frame):
    """
    Test timed out signal handler
    """
    ksft_pr(f"Test timed out: {signal_handler_label}")
    print(f"not ok {tap_idx} rds selftest {signal_handler_label}")
    sys.exit(1)

def setup_tcp():
    """
    Configure tcp network
    """

    # clean up any leftovers from a previously interrupted run
    teardown_tcp()

    ip(f"netns add {NET0}")
    ip(f"netns add {NET1}")
    ip("link add type veth")

    # Move TCP interfaces into separate namespaces so they can no longer be
    # bound directly; this prevents rds from switching over from the tcp
    # transport to the loop transport.
    ip(f"link set {VETH0} netns {NET0} up")
    ip(f"link set {VETH1} netns {NET1} up")

    # add addresses
    ip(f"-n {NET0} addr add {tcp_addrs[0][0]}/32 dev {VETH0}")
    ip(f"-n {NET1} addr add {tcp_addrs[1][0]}/32 dev {VETH1}")

    # add routes
    ip(f"-n {NET0} route add {tcp_addrs[1][0]}/32 dev {VETH0}")
    ip(f"-n {NET1} route add {tcp_addrs[0][0]}/32 dev {VETH1}")

    # sanity check that our two interfaces/addresses are correctly set up
    # and communicating by doing a single ping
    ip(f"netns exec {NET0} ping -c 1 {tcp_addrs[1][0]}")

    # Start a packet capture on each network
    if logdir is not None:
        for netn in [NET0, NET1]:
            pcap = logdir+'/rds-'+netn+'.pcap'

            tcpdump_cmd = ['ip', 'netns', 'exec', netn, '/usr/sbin/tcpdump']
            sudo_user = os.environ.get('SUDO_USER')
            if sudo_user:
                tcpdump_cmd.extend(['-Z', sudo_user])
            tcpdump_cmd.extend(['-i', 'any', '-w', pcap])

            # pylint: disable-next=consider-using-with
            p = subprocess.Popen(tcpdump_cmd,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            tcpdump_procs.append(p)

    # simulate packet loss, duplication and corruption
    for netn, iface in [(NET0, VETH0), (NET1, VETH1)]:
        ip(f"netns exec {netn} /usr/sbin/tc qdisc add dev {iface} root netem  \
             corrupt {PACKET_CORRUPTION} loss {PACKET_LOSS} duplicate  \
             {PACKET_DUPLICATE}")

def teardown_tcp():
    """
    Tear down the tcp network configured by setup_tcp().

    Removing the namespaces also removes the veth pair, addresses,
    routes, and netem qdisc that live inside them.  fail=False so
    this is safe to call in error paths after a partial or complete setup.
    """
    cmd(f"ip netns del {NET0}", fail=False)
    cmd(f"ip netns del {NET1}", fail=False)

def get_iface_mac(iface):
    """Return the MAC address of a local network interface."""
    out = subprocess.check_output(['ip', 'link', 'show', iface], text=True)
    mac = re.search(r'link/ether\s+([0-9a-f:]+)', out)
    if not mac:
        raise RuntimeError(f"Cannot determine MAC address of {iface}")
    return mac.group(1)

def setup_rdma():
    """
    Configure rdma network
    """

    # remove links left over by previously interrupted run.
    teardown_rdma()

    # use call here since modprobe may fail if the rdma_rxe
    # module is built-in
    subprocess.call(['modprobe', 'rdma_rxe'],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    ip(f"link add {VETH_RDMA0} type veth peer name {VETH_RDMA1}")

    ip(f"link set {VETH_RDMA0} up")
    ip(f"link set {VETH_RDMA1} up")

    # Since both addresses are in the same namespace, the source address
    # is always local, so enable accept_local
    cmd(f"/usr/sbin/sysctl -q net.ipv4.conf.{VETH_RDMA0}.accept_local=1")
    cmd(f"/usr/sbin/sysctl -q net.ipv4.conf.{VETH_RDMA1}.accept_local=1")

    # Reverse path filters must be disabled so that the local routes don't
    # cause RPF failures.
    cmd(f"/usr/sbin/sysctl -q net.ipv4.conf.{VETH_RDMA0}.rp_filter=0")
    cmd(f"/usr/sbin/sysctl -q net.ipv4.conf.{VETH_RDMA1}.rp_filter=0")

    # add addresses
    ip(f"addr add {rdma_addrs[0][0]}/32 dev {VETH_RDMA0}")
    ip(f"addr add {rdma_addrs[1][0]}/32 dev {VETH_RDMA1}")

    # add routes
    ip(f"route add {rdma_addrs[1][0]}/32 dev {VETH_RDMA0}")
    ip(f"route add {rdma_addrs[0][0]}/32 dev {VETH_RDMA1}")

    # ARP will not resolve neighbor IPs on /32 routes without a subnet.
    # Avoid this by adding neighbors directly so RDMA CM can populate path
    # records with correct mac addrs without waiting for the ARP.
    mac0 = get_iface_mac(VETH_RDMA0)
    mac1 = get_iface_mac(VETH_RDMA1)
    ip(f"neigh add {rdma_addrs[1][0]} lladdr {mac1} dev {VETH_RDMA0} nud permanent")
    ip(f"neigh add {rdma_addrs[0][0]} lladdr {mac0} dev {VETH_RDMA1} nud permanent")

    cmd(f'rdma link add {RXE_DEV0} type rxe netdev {VETH_RDMA0}')
    cmd(f'rdma link add {RXE_DEV1} type rxe netdev {VETH_RDMA1}')

    time.sleep(1)  # allow RXE devices to initialise

    # Start a packet capture on each network
    if logdir is not None:
        for iface in [VETH_RDMA0, VETH_RDMA1]:
            pcap = logdir+'/rds-roce-'+iface+'.pcap'

            tcpdump_cmd = ['/usr/sbin/tcpdump']
            sudo_user = os.environ.get('SUDO_USER')
            if sudo_user:
                tcpdump_cmd.extend(['-Z', sudo_user])
            tcpdump_cmd.extend(['-i', iface, '-w', pcap])

            # pylint: disable-next=consider-using-with
            p = subprocess.Popen(tcpdump_cmd,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            tcpdump_procs.append(p)

    # simulate packet loss, duplication and corruption
    for iface in [VETH_RDMA0, VETH_RDMA1]:
        cmd(f"/usr/sbin/tc qdisc add dev {iface} root netem  \
             corrupt {PACKET_CORRUPTION} loss {PACKET_LOSS} duplicate  \
             {PACKET_DUPLICATE}")

def teardown_rdma():
    """
    Tear down the rdma network configured by setup_rdma().
    """

    # remove links left over by previously interrupted run.
    cmd(f'rdma link del {RXE_DEV0}', fail=False)
    cmd(f'rdma link del {RXE_DEV1}', fail=False)
    cmd(f'ip link del {VETH_RDMA0}', fail=False)


#Parse out command line arguments.  We take an optional
# timeout parameter and an optional log output folder
parser = argparse.ArgumentParser(description="init script args",
                  formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument("-d", "--logdir", action="store",
                    help="directory to store logs", default=None)
parser.add_argument("-T", "--transport", default="tcp",
                    help="Comma-separated list of transports to test: "
                         "tcp, rdma, or tcp,rdma.  Each matching test "
                         "is run once per transport.  "
                         "'rdma' requires CONFIG_RDS_RDMA and rdma_rxe.")
parser.add_argument('-t', '--timeout', help="timeout to terminate hung test",
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

# check transport is either tcp or rdma
transports = [t.strip() for t in args.transport.split(',')]
for t in transports:
    if t not in ('tcp', 'rdma'):
        raise SystemExit(f"test.py: unknown transport: {t!r}")

# Register stop_pcaps before any network setups so that any partially setup
# tcpdumps are still cleaned up on error
atexit.register(stop_pcaps)

# Set up all requested transports upfront so network plumbing is
# ready before any test runs.
transport_envs = {}
FLAGS = 0
if 'tcp' in transports:
    # Register cleanups before setups to handle partial setups that error'd out
    atexit.register(teardown_tcp)
    setup_tcp()
    transport_envs['tcp'] = {
        'addrs': tcp_addrs,
        'netns': [NET0, NET1],
        'flags': FLAGS | OP_FLAG_TCP,
    }

if 'rdma' in transports:
    atexit.register(teardown_rdma)
    setup_rdma()
    transport_envs['rdma'] = {
        'addrs': rdma_addrs,
        'netns': None,
        'flags': FLAGS | OP_FLAG_RDMA,
    }

print("TAP version 13")
print(f"1..{len(transport_envs)}")

for transport, tenv in transport_envs.items():
    tap_idx += 1

    # add a timeout
    if args.timeout > 0:
        signal_handler_label = transport
        signal.alarm(args.timeout)
        signal.signal(signal.SIGALRM, signal_handler)

    ret = snd_rcv_packets(tenv)

    # cancel timeout
    signal.alarm(0)

    if ret == 0:
        ksft_pr("Success")
        print(f"ok {tap_idx} rds selftest {transport}")
        nr_pass += 1
    else:
        print(f"not ok {tap_idx} rds selftest {transport}")
        nr_fail += 1

ksft_pr(f"Totals: pass:{nr_pass} fail:{nr_fail} skip:0")
sys.exit(1 if nr_fail else 0)
