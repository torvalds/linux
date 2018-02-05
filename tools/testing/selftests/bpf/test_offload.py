#!/usr/bin/python3

# Copyright (C) 2017 Netronome Systems, Inc.
#
# This software is licensed under the GNU General License Version 2,
# June 1991 as shown in the file COPYING in the top-level directory of this
# source tree.
#
# THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM "AS IS"
# WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING,
# BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE
# OF THE PROGRAM IS WITH YOU. SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME
# THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION.

from datetime import datetime
import argparse
import json
import os
import pprint
import random
import string
import struct
import subprocess
import time

logfile = None
log_level = 1
skip_extack = False
bpf_test_dir = os.path.dirname(os.path.realpath(__file__))
pp = pprint.PrettyPrinter()
devs = [] # devices we created for clean up
files = [] # files to be removed
netns = [] # net namespaces to be removed

def log_get_sec(level=0):
    return "*" * (log_level + level)

def log_level_inc(add=1):
    global log_level
    log_level += add

def log_level_dec(sub=1):
    global log_level
    log_level -= sub

def log_level_set(level):
    global log_level
    log_level = level

def log(header, data, level=None):
    """
    Output to an optional log.
    """
    if logfile is None:
        return
    if level is not None:
        log_level_set(level)

    if not isinstance(data, str):
        data = pp.pformat(data)

    if len(header):
        logfile.write("\n" + log_get_sec() + " ")
        logfile.write(header)
    if len(header) and len(data.strip()):
        logfile.write("\n")
    logfile.write(data)

def skip(cond, msg):
    if not cond:
        return
    print("SKIP: " + msg)
    log("SKIP: " + msg, "", level=1)
    os.sys.exit(0)

def fail(cond, msg):
    if not cond:
        return
    print("FAIL: " + msg)
    log("FAIL: " + msg, "", level=1)
    os.sys.exit(1)

def start_test(msg):
    log(msg, "", level=1)
    log_level_inc()
    print(msg)

def cmd(cmd, shell=True, include_stderr=False, background=False, fail=True):
    """
    Run a command in subprocess and return tuple of (retval, stdout);
    optionally return stderr as well as third value.
    """
    proc = subprocess.Popen(cmd, shell=shell, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    if background:
        msg = "%s START: %s" % (log_get_sec(1),
                                datetime.now().strftime("%H:%M:%S.%f"))
        log("BKG " + proc.args, msg)
        return proc

    return cmd_result(proc, include_stderr=include_stderr, fail=fail)

def cmd_result(proc, include_stderr=False, fail=False):
    stdout, stderr = proc.communicate()
    stdout = stdout.decode("utf-8")
    stderr = stderr.decode("utf-8")
    proc.stdout.close()
    proc.stderr.close()

    stderr = "\n" + stderr
    if stderr[-1] == "\n":
        stderr = stderr[:-1]

    sec = log_get_sec(1)
    log("CMD " + proc.args,
        "RETCODE: %d\n%s STDOUT:\n%s%s STDERR:%s\n%s END: %s" %
        (proc.returncode, sec, stdout, sec, stderr,
         sec, datetime.now().strftime("%H:%M:%S.%f")))

    if proc.returncode != 0 and fail:
        if len(stderr) > 0 and stderr[-1] == "\n":
            stderr = stderr[:-1]
        raise Exception("Command failed: %s\n%s" % (proc.args, stderr))

    if include_stderr:
        return proc.returncode, stdout, stderr
    else:
        return proc.returncode, stdout

def rm(f):
    cmd("rm -f %s" % (f))
    if f in files:
        files.remove(f)

def tool(name, args, flags, JSON=True, ns="", fail=True, include_stderr=False):
    params = ""
    if JSON:
        params += "%s " % (flags["json"])

    if ns != "":
        ns = "ip netns exec %s " % (ns)

    if include_stderr:
        ret, stdout, stderr = cmd(ns + name + " " + params + args,
                                  fail=fail, include_stderr=True)
    else:
        ret, stdout = cmd(ns + name + " " + params + args,
                          fail=fail, include_stderr=False)

    if JSON and len(stdout.strip()) != 0:
        out = json.loads(stdout)
    else:
        out = stdout

    if include_stderr:
        return ret, out, stderr
    else:
        return ret, out

def bpftool(args, JSON=True, ns="", fail=True):
    return tool("bpftool", args, {"json":"-p"}, JSON=JSON, ns=ns, fail=fail)

def bpftool_prog_list(expected=None, ns=""):
    _, progs = bpftool("prog show", JSON=True, ns=ns, fail=True)
    if expected is not None:
        if len(progs) != expected:
            fail(True, "%d BPF programs loaded, expected %d" %
                 (len(progs), expected))
    return progs

def bpftool_map_list(expected=None, ns=""):
    _, maps = bpftool("map show", JSON=True, ns=ns, fail=True)
    if expected is not None:
        if len(maps) != expected:
            fail(True, "%d BPF maps loaded, expected %d" %
                 (len(maps), expected))
    return maps

def bpftool_prog_list_wait(expected=0, n_retry=20):
    for i in range(n_retry):
        nprogs = len(bpftool_prog_list())
        if nprogs == expected:
            return
        time.sleep(0.05)
    raise Exception("Time out waiting for program counts to stabilize want %d, have %d" % (expected, nprogs))

def bpftool_map_list_wait(expected=0, n_retry=20):
    for i in range(n_retry):
        nmaps = len(bpftool_map_list())
        if nmaps == expected:
            return
        time.sleep(0.05)
    raise Exception("Time out waiting for map counts to stabilize want %d, have %d" % (expected, nmaps))

def ip(args, force=False, JSON=True, ns="", fail=True, include_stderr=False):
    if force:
        args = "-force " + args
    return tool("ip", args, {"json":"-j"}, JSON=JSON, ns=ns,
                fail=fail, include_stderr=include_stderr)

def tc(args, JSON=True, ns="", fail=True, include_stderr=False):
    return tool("tc", args, {"json":"-p"}, JSON=JSON, ns=ns,
                fail=fail, include_stderr=include_stderr)

def ethtool(dev, opt, args, fail=True):
    return cmd("ethtool %s %s %s" % (opt, dev["ifname"], args), fail=fail)

def bpf_obj(name, sec=".text", path=bpf_test_dir,):
    return "obj %s sec %s" % (os.path.join(path, name), sec)

def bpf_pinned(name):
    return "pinned %s" % (name)

def bpf_bytecode(bytecode):
    return "bytecode \"%s\"" % (bytecode)

def mknetns(n_retry=10):
    for i in range(n_retry):
        name = ''.join([random.choice(string.ascii_letters) for i in range(8)])
        ret, _ = ip("netns add %s" % (name), fail=False)
        if ret == 0:
            netns.append(name)
            return name
    return None

def int2str(fmt, val):
    ret = []
    for b in struct.pack(fmt, val):
        ret.append(int(b))
    return " ".join(map(lambda x: str(x), ret))

def str2int(strtab):
    inttab = []
    for i in strtab:
        inttab.append(int(i, 16))
    ba = bytearray(inttab)
    if len(strtab) == 4:
        fmt = "I"
    elif len(strtab) == 8:
        fmt = "Q"
    else:
        raise Exception("String array of len %d can't be unpacked to an int" %
                        (len(strtab)))
    return struct.unpack(fmt, ba)[0]

class DebugfsDir:
    """
    Class for accessing DebugFS directories as a dictionary.
    """

    def __init__(self, path):
        self.path = path
        self._dict = self._debugfs_dir_read(path)

    def __len__(self):
        return len(self._dict.keys())

    def __getitem__(self, key):
        if type(key) is int:
            key = list(self._dict.keys())[key]
        return self._dict[key]

    def __setitem__(self, key, value):
        log("DebugFS set %s = %s" % (key, value), "")
        log_level_inc()

        cmd("echo '%s' > %s/%s" % (value, self.path, key))
        log_level_dec()

        _, out = cmd('cat %s/%s' % (self.path, key))
        self._dict[key] = out.strip()

    def _debugfs_dir_read(self, path):
        dfs = {}

        log("DebugFS state for %s" % (path), "")
        log_level_inc(add=2)

        _, out = cmd('ls ' + path)
        for f in out.split():
            p = os.path.join(path, f)
            if os.path.isfile(p):
                _, out = cmd('cat %s/%s' % (path, f))
                dfs[f] = out.strip()
            elif os.path.isdir(p):
                dfs[f] = DebugfsDir(p)
            else:
                raise Exception("%s is neither file nor directory" % (p))

        log_level_dec()
        log("DebugFS state", dfs)
        log_level_dec()

        return dfs

class NetdevSim:
    """
    Class for netdevsim netdevice and its attributes.
    """

    def __init__(self):
        self.dev = self._netdevsim_create()
        devs.append(self)

        self.ns = ""

        self.dfs_dir = '/sys/kernel/debug/netdevsim/%s' % (self.dev['ifname'])
        self.dfs_refresh()

    def __getitem__(self, key):
        return self.dev[key]

    def _netdevsim_create(self):
        _, old  = ip("link show")
        ip("link add sim%d type netdevsim")
        _, new  = ip("link show")

        for dev in new:
            f = filter(lambda x: x["ifname"] == dev["ifname"], old)
            if len(list(f)) == 0:
                return dev

        raise Exception("failed to create netdevsim device")

    def remove(self):
        devs.remove(self)
        ip("link del dev %s" % (self.dev["ifname"]), ns=self.ns)

    def dfs_refresh(self):
        self.dfs = DebugfsDir(self.dfs_dir)
        return self.dfs

    def dfs_num_bound_progs(self):
        path = os.path.join(self.dfs_dir, "bpf_bound_progs")
        _, progs = cmd('ls %s' % (path))
        return len(progs.split())

    def dfs_get_bound_progs(self, expected):
        progs = DebugfsDir(os.path.join(self.dfs_dir, "bpf_bound_progs"))
        if expected is not None:
            if len(progs) != expected:
                fail(True, "%d BPF programs bound, expected %d" %
                     (len(progs), expected))
        return progs

    def wait_for_flush(self, bound=0, total=0, n_retry=20):
        for i in range(n_retry):
            nbound = self.dfs_num_bound_progs()
            nprogs = len(bpftool_prog_list())
            if nbound == bound and nprogs == total:
                return
            time.sleep(0.05)
        raise Exception("Time out waiting for program counts to stabilize want %d/%d, have %d bound, %d loaded" % (bound, total, nbound, nprogs))

    def set_ns(self, ns):
        name = "1" if ns == "" else ns
        ip("link set dev %s netns %s" % (self.dev["ifname"], name), ns=self.ns)
        self.ns = ns

    def set_mtu(self, mtu, fail=True):
        return ip("link set dev %s mtu %d" % (self.dev["ifname"], mtu),
                  fail=fail)

    def set_xdp(self, bpf, mode, force=False, JSON=True, verbose=False,
                fail=True, include_stderr=False):
        if verbose:
            bpf += " verbose"
        return ip("link set dev %s xdp%s %s" % (self.dev["ifname"], mode, bpf),
                  force=force, JSON=JSON,
                  fail=fail, include_stderr=include_stderr)

    def unset_xdp(self, mode, force=False, JSON=True,
                  fail=True, include_stderr=False):
        return ip("link set dev %s xdp%s off" % (self.dev["ifname"], mode),
                  force=force, JSON=JSON,
                  fail=fail, include_stderr=include_stderr)

    def ip_link_show(self, xdp):
        _, link = ip("link show dev %s" % (self['ifname']))
        if len(link) > 1:
            raise Exception("Multiple objects on ip link show")
        if len(link) < 1:
            return {}
        fail(xdp != "xdp" in link,
             "XDP program not reporting in iplink (reported %s, expected %s)" %
             ("xdp" in link, xdp))
        return link[0]

    def tc_add_ingress(self):
        tc("qdisc add dev %s ingress" % (self['ifname']))

    def tc_del_ingress(self):
        tc("qdisc del dev %s ingress" % (self['ifname']))

    def tc_flush_filters(self, bound=0, total=0):
        self.tc_del_ingress()
        self.tc_add_ingress()
        self.wait_for_flush(bound=bound, total=total)

    def tc_show_ingress(self, expected=None):
        # No JSON support, oh well...
        flags = ["skip_sw", "skip_hw", "in_hw"]
        named = ["protocol", "pref", "chain", "handle", "id", "tag"]

        args = "-s filter show dev %s ingress" % (self['ifname'])
        _, out = tc(args, JSON=False)

        filters = []
        lines = out.split('\n')
        for line in lines:
            words = line.split()
            if "handle" not in words:
                continue
            fltr = {}
            for flag in flags:
                fltr[flag] = flag in words
            for name in named:
                try:
                    idx = words.index(name)
                    fltr[name] = words[idx + 1]
                except ValueError:
                    pass
            filters.append(fltr)

        if expected is not None:
            fail(len(filters) != expected,
                 "%d ingress filters loaded, expected %d" %
                 (len(filters), expected))
        return filters

    def cls_filter_op(self, op, qdisc="ingress", prio=None, handle=None,
                      chain=None, cls="", params="",
                      fail=True, include_stderr=False):
        spec = ""
        if prio is not None:
            spec += " prio %d" % (prio)
        if handle:
            spec += " handle %s" % (handle)
        if chain is not None:
            spec += " chain %d" % (chain)

        return tc("filter {op} dev {dev} {qdisc} {spec} {cls} {params}"\
                  .format(op=op, dev=self['ifname'], qdisc=qdisc, spec=spec,
                          cls=cls, params=params),
                  fail=fail, include_stderr=include_stderr)

    def cls_bpf_add_filter(self, bpf, op="add", prio=None, handle=None,
                           chain=None, da=False, verbose=False,
                           skip_sw=False, skip_hw=False,
                           fail=True, include_stderr=False):
        cls = "bpf " + bpf

        params = ""
        if da:
            params += " da"
        if verbose:
            params += " verbose"
        if skip_sw:
            params += " skip_sw"
        if skip_hw:
            params += " skip_hw"

        return self.cls_filter_op(op=op, prio=prio, handle=handle, cls=cls,
                                  chain=chain, params=params,
                                  fail=fail, include_stderr=include_stderr)

    def set_ethtool_tc_offloads(self, enable, fail=True):
        args = "hw-tc-offload %s" % ("on" if enable else "off")
        return ethtool(self, "-K", args, fail=fail)

################################################################################
def clean_up():
    global files, netns, devs

    for dev in devs:
        dev.remove()
    for f in files:
        cmd("rm -f %s" % (f))
    for ns in netns:
        cmd("ip netns delete %s" % (ns))
    files = []
    netns = []

def pin_prog(file_name, idx=0):
    progs = bpftool_prog_list(expected=(idx + 1))
    prog = progs[idx]
    bpftool("prog pin id %d %s" % (prog["id"], file_name))
    files.append(file_name)

    return file_name, bpf_pinned(file_name)

def pin_map(file_name, idx=0, expected=1):
    maps = bpftool_map_list(expected=expected)
    m = maps[idx]
    bpftool("map pin id %d %s" % (m["id"], file_name))
    files.append(file_name)

    return file_name, bpf_pinned(file_name)

def check_dev_info_removed(prog_file=None, map_file=None):
    bpftool_prog_list(expected=0)
    ret, err = bpftool("prog show pin %s" % (prog_file), fail=False)
    fail(ret == 0, "Showing prog with removed device did not fail")
    fail(err["error"].find("No such device") == -1,
         "Showing prog with removed device expected ENODEV, error is %s" %
         (err["error"]))

    bpftool_map_list(expected=0)
    ret, err = bpftool("map show pin %s" % (map_file), fail=False)
    fail(ret == 0, "Showing map with removed device did not fail")
    fail(err["error"].find("No such device") == -1,
         "Showing map with removed device expected ENODEV, error is %s" %
         (err["error"]))

def check_dev_info(other_ns, ns, prog_file=None, map_file=None, removed=False):
    progs = bpftool_prog_list(expected=1, ns=ns)
    prog = progs[0]

    fail("dev" not in prog.keys(), "Device parameters not reported")
    dev = prog["dev"]
    fail("ifindex" not in dev.keys(), "Device parameters not reported")
    fail("ns_dev" not in dev.keys(), "Device parameters not reported")
    fail("ns_inode" not in dev.keys(), "Device parameters not reported")

    if not other_ns:
        fail("ifname" not in dev.keys(), "Ifname not reported")
        fail(dev["ifname"] != sim["ifname"],
             "Ifname incorrect %s vs %s" % (dev["ifname"], sim["ifname"]))
    else:
        fail("ifname" in dev.keys(), "Ifname is reported for other ns")

    maps = bpftool_map_list(expected=2, ns=ns)
    for m in maps:
        fail("dev" not in m.keys(), "Device parameters not reported")
        fail(dev != m["dev"], "Map's device different than program's")

def check_extack(output, reference, args):
    if skip_extack:
        return
    lines = output.split("\n")
    comp = len(lines) >= 2 and lines[1] == reference
    fail(not comp, "Missing or incorrect netlink extack message")

def check_extack_nsim(output, reference, args):
    check_extack(output, "Error: netdevsim: " + reference, args)

def check_no_extack(res, needle):
    fail((res[1] + res[2]).count(needle) or (res[1] + res[2]).count("Warning:"),
         "Found '%s' in command output, leaky extack?" % (needle))

def check_verifier_log(output, reference):
    lines = output.split("\n")
    for l in reversed(lines):
        if l == reference:
            return
    fail(True, "Missing or incorrect message from netdevsim in verifier log")

def test_spurios_extack(sim, obj, skip_hw, needle):
    res = sim.cls_bpf_add_filter(obj, prio=1, handle=1, skip_hw=skip_hw,
                                 include_stderr=True)
    check_no_extack(res, needle)
    res = sim.cls_bpf_add_filter(obj, op="replace", prio=1, handle=1,
                                 skip_hw=skip_hw, include_stderr=True)
    check_no_extack(res, needle)
    res = sim.cls_filter_op(op="delete", prio=1, handle=1, cls="bpf",
                            include_stderr=True)
    check_no_extack(res, needle)


# Parse command line
parser = argparse.ArgumentParser()
parser.add_argument("--log", help="output verbose log to given file")
args = parser.parse_args()
if args.log:
    logfile = open(args.log, 'w+')
    logfile.write("# -*-Org-*-")

log("Prepare...", "", level=1)
log_level_inc()

# Check permissions
skip(os.getuid() != 0, "test must be run as root")

# Check tools
ret, progs = bpftool("prog", fail=False)
skip(ret != 0, "bpftool not installed")
# Check no BPF programs are loaded
skip(len(progs) != 0, "BPF programs already loaded on the system")

# Check netdevsim
ret, out = cmd("modprobe netdevsim", fail=False)
skip(ret != 0, "netdevsim module could not be loaded")

# Check debugfs
_, out = cmd("mount")
if out.find("/sys/kernel/debug type debugfs") == -1:
    cmd("mount -t debugfs none /sys/kernel/debug")

# Check samples are compiled
samples = ["sample_ret0.o", "sample_map_ret0.o"]
for s in samples:
    ret, out = cmd("ls %s/%s" % (bpf_test_dir, s), fail=False)
    skip(ret != 0, "sample %s/%s not found, please compile it" %
         (bpf_test_dir, s))

# Check if iproute2 is built with libmnl (needed by extack support)
_, _, err = cmd("tc qdisc delete dev lo handle 0",
                fail=False, include_stderr=True)
if err.find("Error: Failed to find qdisc with specified handle.") == -1:
    print("Warning: no extack message in iproute2 output, libmnl missing?")
    log("Warning: no extack message in iproute2 output, libmnl missing?", "")
    skip_extack = True

# Check if net namespaces seem to work
ns = mknetns()
skip(ns is None, "Could not create a net namespace")
cmd("ip netns delete %s" % (ns))
netns = []

try:
    obj = bpf_obj("sample_ret0.o")
    bytecode = bpf_bytecode("1,6 0 0 4294967295,")

    start_test("Test destruction of generic XDP...")
    sim = NetdevSim()
    sim.set_xdp(obj, "generic")
    sim.remove()
    bpftool_prog_list_wait(expected=0)

    sim = NetdevSim()
    sim.tc_add_ingress()

    start_test("Test TC non-offloaded...")
    ret, _ = sim.cls_bpf_add_filter(obj, skip_hw=True, fail=False)
    fail(ret != 0, "Software TC filter did not load")

    start_test("Test TC non-offloaded isn't getting bound...")
    ret, _ = sim.cls_bpf_add_filter(obj, fail=False)
    fail(ret != 0, "Software TC filter did not load")
    sim.dfs_get_bound_progs(expected=0)

    sim.tc_flush_filters()

    start_test("Test TC offloads are off by default...")
    ret, _, err = sim.cls_bpf_add_filter(obj, skip_sw=True,
                                         fail=False, include_stderr=True)
    fail(ret == 0, "TC filter loaded without enabling TC offloads")
    check_extack(err, "Error: TC offload is disabled on net device.", args)
    sim.wait_for_flush()

    sim.set_ethtool_tc_offloads(True)
    sim.dfs["bpf_tc_non_bound_accept"] = "Y"

    start_test("Test TC offload by default...")
    ret, _ = sim.cls_bpf_add_filter(obj, fail=False)
    fail(ret != 0, "Software TC filter did not load")
    sim.dfs_get_bound_progs(expected=0)
    ingress = sim.tc_show_ingress(expected=1)
    fltr = ingress[0]
    fail(not fltr["in_hw"], "Filter not offloaded by default")

    sim.tc_flush_filters()

    start_test("Test TC cBPF bytcode tries offload by default...")
    ret, _ = sim.cls_bpf_add_filter(bytecode, fail=False)
    fail(ret != 0, "Software TC filter did not load")
    sim.dfs_get_bound_progs(expected=0)
    ingress = sim.tc_show_ingress(expected=1)
    fltr = ingress[0]
    fail(not fltr["in_hw"], "Bytecode not offloaded by default")

    sim.tc_flush_filters()
    sim.dfs["bpf_tc_non_bound_accept"] = "N"

    start_test("Test TC cBPF unbound bytecode doesn't offload...")
    ret, _, err = sim.cls_bpf_add_filter(bytecode, skip_sw=True,
                                         fail=False, include_stderr=True)
    fail(ret == 0, "TC bytecode loaded for offload")
    check_extack_nsim(err, "netdevsim configured to reject unbound programs.",
                      args)
    sim.wait_for_flush()

    start_test("Test non-0 chain offload...")
    ret, _, err = sim.cls_bpf_add_filter(obj, chain=1, prio=1, handle=1,
                                         skip_sw=True,
                                         fail=False, include_stderr=True)
    fail(ret == 0, "Offloaded a filter to chain other than 0")
    check_extack(err, "Error: Driver supports only offload of chain 0.", args)
    sim.tc_flush_filters()

    start_test("Test TC replace...")
    sim.cls_bpf_add_filter(obj, prio=1, handle=1)
    sim.cls_bpf_add_filter(obj, op="replace", prio=1, handle=1)
    sim.cls_filter_op(op="delete", prio=1, handle=1, cls="bpf")

    sim.cls_bpf_add_filter(obj, prio=1, handle=1, skip_sw=True)
    sim.cls_bpf_add_filter(obj, op="replace", prio=1, handle=1, skip_sw=True)
    sim.cls_filter_op(op="delete", prio=1, handle=1, cls="bpf")

    sim.cls_bpf_add_filter(obj, prio=1, handle=1, skip_hw=True)
    sim.cls_bpf_add_filter(obj, op="replace", prio=1, handle=1, skip_hw=True)
    sim.cls_filter_op(op="delete", prio=1, handle=1, cls="bpf")

    start_test("Test TC replace bad flags...")
    for i in range(3):
        for j in range(3):
            ret, _ = sim.cls_bpf_add_filter(obj, op="replace", prio=1, handle=1,
                                            skip_sw=(j == 1), skip_hw=(j == 2),
                                            fail=False)
            fail(bool(ret) != bool(j),
                 "Software TC incorrect load in replace test, iteration %d" %
                 (j))
        sim.cls_filter_op(op="delete", prio=1, handle=1, cls="bpf")

    start_test("Test spurious extack from the driver...")
    test_spurios_extack(sim, obj, False, "netdevsim")
    test_spurios_extack(sim, obj, True, "netdevsim")

    sim.set_ethtool_tc_offloads(False)

    test_spurios_extack(sim, obj, False, "TC offload is disabled")
    test_spurios_extack(sim, obj, True, "TC offload is disabled")

    sim.set_ethtool_tc_offloads(True)

    sim.tc_flush_filters()

    start_test("Test TC offloads work...")
    ret, _, err = sim.cls_bpf_add_filter(obj, verbose=True, skip_sw=True,
                                         fail=False, include_stderr=True)
    fail(ret != 0, "TC filter did not load with TC offloads enabled")
    check_verifier_log(err, "[netdevsim] Hello from netdevsim!")

    start_test("Test TC offload basics...")
    dfs = sim.dfs_get_bound_progs(expected=1)
    progs = bpftool_prog_list(expected=1)
    ingress = sim.tc_show_ingress(expected=1)

    dprog = dfs[0]
    prog = progs[0]
    fltr = ingress[0]
    fail(fltr["skip_hw"], "TC does reports 'skip_hw' on offloaded filter")
    fail(not fltr["in_hw"], "TC does not report 'in_hw' for offloaded filter")
    fail(not fltr["skip_sw"], "TC does not report 'skip_sw' back")

    start_test("Test TC offload is device-bound...")
    fail(str(prog["id"]) != fltr["id"], "Program IDs don't match")
    fail(prog["tag"] != fltr["tag"], "Program tags don't match")
    fail(fltr["id"] != dprog["id"], "Program IDs don't match")
    fail(dprog["state"] != "xlated", "Offloaded program state not translated")
    fail(dprog["loaded"] != "Y", "Offloaded program is not loaded")

    start_test("Test disabling TC offloads is rejected while filters installed...")
    ret, _ = sim.set_ethtool_tc_offloads(False, fail=False)
    fail(ret == 0, "Driver should refuse to disable TC offloads with filters installed...")

    start_test("Test qdisc removal frees things...")
    sim.tc_flush_filters()
    sim.tc_show_ingress(expected=0)

    start_test("Test disabling TC offloads is OK without filters...")
    ret, _ = sim.set_ethtool_tc_offloads(False, fail=False)
    fail(ret != 0,
         "Driver refused to disable TC offloads without filters installed...")

    sim.set_ethtool_tc_offloads(True)

    start_test("Test destroying device gets rid of TC filters...")
    sim.cls_bpf_add_filter(obj, skip_sw=True)
    sim.remove()
    bpftool_prog_list_wait(expected=0)

    sim = NetdevSim()
    sim.set_ethtool_tc_offloads(True)

    start_test("Test destroying device gets rid of XDP...")
    sim.set_xdp(obj, "offload")
    sim.remove()
    bpftool_prog_list_wait(expected=0)

    sim = NetdevSim()
    sim.set_ethtool_tc_offloads(True)

    start_test("Test XDP prog reporting...")
    sim.set_xdp(obj, "drv")
    ipl = sim.ip_link_show(xdp=True)
    progs = bpftool_prog_list(expected=1)
    fail(ipl["xdp"]["prog"]["id"] != progs[0]["id"],
         "Loaded program has wrong ID")

    start_test("Test XDP prog replace without force...")
    ret, _ = sim.set_xdp(obj, "drv", fail=False)
    fail(ret == 0, "Replaced XDP program without -force")
    sim.wait_for_flush(total=1)

    start_test("Test XDP prog replace with force...")
    ret, _ = sim.set_xdp(obj, "drv", force=True, fail=False)
    fail(ret != 0, "Could not replace XDP program with -force")
    bpftool_prog_list_wait(expected=1)
    ipl = sim.ip_link_show(xdp=True)
    progs = bpftool_prog_list(expected=1)
    fail(ipl["xdp"]["prog"]["id"] != progs[0]["id"],
         "Loaded program has wrong ID")
    fail("dev" in progs[0].keys(),
         "Device parameters reported for non-offloaded program")

    start_test("Test XDP prog replace with bad flags...")
    ret, _, err = sim.set_xdp(obj, "offload", force=True,
                              fail=False, include_stderr=True)
    fail(ret == 0, "Replaced XDP program with a program in different mode")
    check_extack_nsim(err, "program loaded with different flags.", args)
    ret, _, err = sim.set_xdp(obj, "", force=True,
                              fail=False, include_stderr=True)
    fail(ret == 0, "Replaced XDP program with a program in different mode")
    check_extack_nsim(err, "program loaded with different flags.", args)

    start_test("Test XDP prog remove with bad flags...")
    ret, _, err = sim.unset_xdp("offload", force=True,
                                fail=False, include_stderr=True)
    fail(ret == 0, "Removed program with a bad mode mode")
    check_extack_nsim(err, "program loaded with different flags.", args)
    ret, _, err = sim.unset_xdp("", force=True,
                                fail=False, include_stderr=True)
    fail(ret == 0, "Removed program with a bad mode mode")
    check_extack_nsim(err, "program loaded with different flags.", args)

    start_test("Test MTU restrictions...")
    ret, _ = sim.set_mtu(9000, fail=False)
    fail(ret == 0,
         "Driver should refuse to increase MTU to 9000 with XDP loaded...")
    sim.unset_xdp("drv")
    bpftool_prog_list_wait(expected=0)
    sim.set_mtu(9000)
    ret, _, err = sim.set_xdp(obj, "drv", fail=False, include_stderr=True)
    fail(ret == 0, "Driver should refuse to load program with MTU of 9000...")
    check_extack_nsim(err, "MTU too large w/ XDP enabled.", args)
    sim.set_mtu(1500)

    sim.wait_for_flush()
    start_test("Test XDP offload...")
    _, _, err = sim.set_xdp(obj, "offload", verbose=True, include_stderr=True)
    ipl = sim.ip_link_show(xdp=True)
    link_xdp = ipl["xdp"]["prog"]
    progs = bpftool_prog_list(expected=1)
    prog = progs[0]
    fail(link_xdp["id"] != prog["id"], "Loaded program has wrong ID")
    check_verifier_log(err, "[netdevsim] Hello from netdevsim!")

    start_test("Test XDP offload is device bound...")
    dfs = sim.dfs_get_bound_progs(expected=1)
    dprog = dfs[0]

    fail(prog["id"] != link_xdp["id"], "Program IDs don't match")
    fail(prog["tag"] != link_xdp["tag"], "Program tags don't match")
    fail(str(link_xdp["id"]) != dprog["id"], "Program IDs don't match")
    fail(dprog["state"] != "xlated", "Offloaded program state not translated")
    fail(dprog["loaded"] != "Y", "Offloaded program is not loaded")

    start_test("Test removing XDP program many times...")
    sim.unset_xdp("offload")
    sim.unset_xdp("offload")
    sim.unset_xdp("drv")
    sim.unset_xdp("drv")
    sim.unset_xdp("")
    sim.unset_xdp("")
    bpftool_prog_list_wait(expected=0)

    start_test("Test attempt to use a program for a wrong device...")
    sim2 = NetdevSim()
    sim2.set_xdp(obj, "offload")
    pin_file, pinned = pin_prog("/sys/fs/bpf/tmp")

    ret, _, err = sim.set_xdp(pinned, "offload",
                              fail=False, include_stderr=True)
    fail(ret == 0, "Pinned program loaded for a different device accepted")
    check_extack_nsim(err, "program bound to different dev.", args)
    sim2.remove()
    ret, _, err = sim.set_xdp(pinned, "offload",
                              fail=False, include_stderr=True)
    fail(ret == 0, "Pinned program loaded for a removed device accepted")
    check_extack_nsim(err, "xdpoffload of non-bound program.", args)
    rm(pin_file)
    bpftool_prog_list_wait(expected=0)

    start_test("Test mixing of TC and XDP...")
    sim.tc_add_ingress()
    sim.set_xdp(obj, "offload")
    ret, _, err = sim.cls_bpf_add_filter(obj, skip_sw=True,
                                         fail=False, include_stderr=True)
    fail(ret == 0, "Loading TC when XDP active should fail")
    check_extack_nsim(err, "driver and netdev offload states mismatch.", args)
    sim.unset_xdp("offload")
    sim.wait_for_flush()

    sim.cls_bpf_add_filter(obj, skip_sw=True)
    ret, _, err = sim.set_xdp(obj, "offload", fail=False, include_stderr=True)
    fail(ret == 0, "Loading XDP when TC active should fail")
    check_extack_nsim(err, "TC program is already loaded.", args)

    start_test("Test binding TC from pinned...")
    pin_file, pinned = pin_prog("/sys/fs/bpf/tmp")
    sim.tc_flush_filters(bound=1, total=1)
    sim.cls_bpf_add_filter(pinned, da=True, skip_sw=True)
    sim.tc_flush_filters(bound=1, total=1)

    start_test("Test binding XDP from pinned...")
    sim.set_xdp(obj, "offload")
    pin_file, pinned = pin_prog("/sys/fs/bpf/tmp2", idx=1)

    sim.set_xdp(pinned, "offload", force=True)
    sim.unset_xdp("offload")
    sim.set_xdp(pinned, "offload", force=True)
    sim.unset_xdp("offload")

    start_test("Test offload of wrong type fails...")
    ret, _ = sim.cls_bpf_add_filter(pinned, da=True, skip_sw=True, fail=False)
    fail(ret == 0, "Managed to attach XDP program to TC")

    start_test("Test asking for TC offload of two filters...")
    sim.cls_bpf_add_filter(obj, da=True, skip_sw=True)
    ret, _, err = sim.cls_bpf_add_filter(obj, da=True, skip_sw=True,
                                         fail=False, include_stderr=True)
    fail(ret == 0, "Managed to offload two TC filters at the same time")
    check_extack_nsim(err, "driver and netdev offload states mismatch.", args)

    sim.tc_flush_filters(bound=2, total=2)

    start_test("Test if netdev removal waits for translation...")
    delay_msec = 500
    sim.dfs["bpf_bind_verifier_delay"] = delay_msec
    start = time.time()
    cmd_line = "tc filter add dev %s ingress bpf %s da skip_sw" % \
               (sim['ifname'], obj)
    tc_proc = cmd(cmd_line, background=True, fail=False)
    # Wait for the verifier to start
    while sim.dfs_num_bound_progs() <= 2:
        pass
    sim.remove()
    end = time.time()
    ret, _ = cmd_result(tc_proc, fail=False)
    time_diff = end - start
    log("Time", "start:\t%s\nend:\t%s\ndiff:\t%s" % (start, end, time_diff))

    fail(ret == 0, "Managed to load TC filter on a unregistering device")
    delay_sec = delay_msec * 0.001
    fail(time_diff < delay_sec, "Removal process took %s, expected %s" %
         (time_diff, delay_sec))

    # Remove all pinned files and reinstantiate the netdev
    clean_up()
    bpftool_prog_list_wait(expected=0)

    sim = NetdevSim()
    map_obj = bpf_obj("sample_map_ret0.o")
    start_test("Test loading program with maps...")
    sim.set_xdp(map_obj, "offload", JSON=False) # map fixup msg breaks JSON

    start_test("Test bpftool bound info reporting (own ns)...")
    check_dev_info(False, "")

    start_test("Test bpftool bound info reporting (other ns)...")
    ns = mknetns()
    sim.set_ns(ns)
    check_dev_info(True, "")

    start_test("Test bpftool bound info reporting (remote ns)...")
    check_dev_info(False, ns)

    start_test("Test bpftool bound info reporting (back to own ns)...")
    sim.set_ns("")
    check_dev_info(False, "")

    prog_file, _ = pin_prog("/sys/fs/bpf/tmp_prog")
    map_file, _ = pin_map("/sys/fs/bpf/tmp_map", idx=1, expected=2)
    sim.remove()

    start_test("Test bpftool bound info reporting (removed dev)...")
    check_dev_info_removed(prog_file=prog_file, map_file=map_file)

    # Remove all pinned files and reinstantiate the netdev
    clean_up()
    bpftool_prog_list_wait(expected=0)

    sim = NetdevSim()

    start_test("Test map update (no flags)...")
    sim.set_xdp(map_obj, "offload", JSON=False) # map fixup msg breaks JSON
    maps = bpftool_map_list(expected=2)
    array = maps[0] if maps[0]["type"] == "array" else maps[1]
    htab = maps[0] if maps[0]["type"] == "hash" else maps[1]
    for m in maps:
        for i in range(2):
            bpftool("map update id %d key %s value %s" %
                    (m["id"], int2str("I", i), int2str("Q", i * 3)))

    for m in maps:
        ret, _ = bpftool("map update id %d key %s value %s" %
                         (m["id"], int2str("I", 3), int2str("Q", 3 * 3)),
                         fail=False)
        fail(ret == 0, "added too many entries")

    start_test("Test map update (exists)...")
    for m in maps:
        for i in range(2):
            bpftool("map update id %d key %s value %s exist" %
                    (m["id"], int2str("I", i), int2str("Q", i * 3)))

    for m in maps:
        ret, err = bpftool("map update id %d key %s value %s exist" %
                           (m["id"], int2str("I", 3), int2str("Q", 3 * 3)),
                           fail=False)
        fail(ret == 0, "updated non-existing key")
        fail(err["error"].find("No such file or directory") == -1,
             "expected ENOENT, error is '%s'" % (err["error"]))

    start_test("Test map update (noexist)...")
    for m in maps:
        for i in range(2):
            ret, err = bpftool("map update id %d key %s value %s noexist" %
                               (m["id"], int2str("I", i), int2str("Q", i * 3)),
                               fail=False)
        fail(ret == 0, "updated existing key")
        fail(err["error"].find("File exists") == -1,
             "expected EEXIST, error is '%s'" % (err["error"]))

    start_test("Test map dump...")
    for m in maps:
        _, entries = bpftool("map dump id %d" % (m["id"]))
        for i in range(2):
            key = str2int(entries[i]["key"])
            fail(key != i, "expected key %d, got %d" % (key, i))
            val = str2int(entries[i]["value"])
            fail(val != i * 3, "expected value %d, got %d" % (val, i * 3))

    start_test("Test map getnext...")
    for m in maps:
        _, entry = bpftool("map getnext id %d" % (m["id"]))
        key = str2int(entry["next_key"])
        fail(key != 0, "next key %d, expected %d" % (key, 0))
        _, entry = bpftool("map getnext id %d key %s" %
                           (m["id"], int2str("I", 0)))
        key = str2int(entry["next_key"])
        fail(key != 1, "next key %d, expected %d" % (key, 1))
        ret, err = bpftool("map getnext id %d key %s" %
                           (m["id"], int2str("I", 1)), fail=False)
        fail(ret == 0, "got next key past the end of map")
        fail(err["error"].find("No such file or directory") == -1,
             "expected ENOENT, error is '%s'" % (err["error"]))

    start_test("Test map delete (htab)...")
    for i in range(2):
        bpftool("map delete id %d key %s" % (htab["id"], int2str("I", i)))

    start_test("Test map delete (array)...")
    for i in range(2):
        ret, err = bpftool("map delete id %d key %s" %
                           (htab["id"], int2str("I", i)), fail=False)
        fail(ret == 0, "removed entry from an array")
        fail(err["error"].find("No such file or directory") == -1,
             "expected ENOENT, error is '%s'" % (err["error"]))

    start_test("Test map remove...")
    sim.unset_xdp("offload")
    bpftool_map_list_wait(expected=0)
    sim.remove()

    sim = NetdevSim()
    sim.set_xdp(map_obj, "offload", JSON=False) # map fixup msg breaks JSON
    sim.remove()
    bpftool_map_list_wait(expected=0)

    start_test("Test map creation fail path...")
    sim = NetdevSim()
    sim.dfs["bpf_map_accept"] = "N"
    ret, _ = sim.set_xdp(map_obj, "offload", JSON=False, fail=False)
    fail(ret == 0,
         "netdevsim didn't refuse to create a map with offload disabled")

    print("%s: OK" % (os.path.basename(__file__)))

finally:
    log("Clean up...", "", level=1)
    log_level_inc()
    clean_up()
