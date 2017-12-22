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
import subprocess
import time

logfile = None
log_level = 1
bpf_test_dir = os.path.dirname(os.path.realpath(__file__))
pp = pprint.PrettyPrinter()
devs = [] # devices we created for clean up
files = [] # files to be removed

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

def tool(name, args, flags, JSON=True, fail=True):
    params = ""
    if JSON:
        params += "%s " % (flags["json"])

    ret, out = cmd(name + " " + params + args, fail=fail)
    if JSON and len(out.strip()) != 0:
        return ret, json.loads(out)
    else:
        return ret, out

def bpftool(args, JSON=True, fail=True):
    return tool("bpftool", args, {"json":"-p"}, JSON=JSON, fail=fail)

def bpftool_prog_list(expected=None):
    _, progs = bpftool("prog show", JSON=True, fail=True)
    if expected is not None:
        if len(progs) != expected:
            fail(True, "%d BPF programs loaded, expected %d" %
                 (len(progs), expected))
    return progs

def bpftool_prog_list_wait(expected=0, n_retry=20):
    for i in range(n_retry):
        nprogs = len(bpftool_prog_list())
        if nprogs == expected:
            return
        time.sleep(0.05)
    raise Exception("Time out waiting for program counts to stabilize want %d, have %d" % (expected, nprogs))

def ip(args, force=False, JSON=True, fail=True):
    if force:
        args = "-force " + args
    return tool("ip", args, {"json":"-j"}, JSON=JSON, fail=fail)

def tc(args, JSON=True, fail=True):
    return tool("tc", args, {"json":"-p"}, JSON=JSON, fail=fail)

def ethtool(dev, opt, args, fail=True):
    return cmd("ethtool %s %s %s" % (opt, dev["ifname"], args), fail=fail)

def bpf_obj(name, sec=".text", path=bpf_test_dir,):
    return "obj %s sec %s" % (os.path.join(path, name), sec)

def bpf_pinned(name):
    return "pinned %s" % (name)

def bpf_bytecode(bytecode):
    return "bytecode \"%s\"" % (bytecode)

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
        ip("link del dev %s" % (self.dev["ifname"]))

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

    def set_mtu(self, mtu, fail=True):
        return ip("link set dev %s mtu %d" % (self.dev["ifname"], mtu),
                  fail=fail)

    def set_xdp(self, bpf, mode, force=False, fail=True):
        return ip("link set dev %s xdp%s %s" % (self.dev["ifname"], mode, bpf),
                  force=force, fail=fail)

    def unset_xdp(self, mode, force=False, fail=True):
        return ip("link set dev %s xdp%s off" % (self.dev["ifname"], mode),
                  force=force, fail=fail)

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

    def cls_bpf_add_filter(self, bpf, da=False, skip_sw=False, skip_hw=False,
                           fail=True):
        params = ""
        if da:
            params += " da"
        if skip_sw:
            params += " skip_sw"
        if skip_hw:
            params += " skip_hw"
        return tc("filter add dev %s ingress bpf %s %s" %
                  (self['ifname'], bpf, params), fail=fail)

    def set_ethtool_tc_offloads(self, enable, fail=True):
        args = "hw-tc-offload %s" % ("on" if enable else "off")
        return ethtool(self, "-K", args, fail=fail)

################################################################################
def clean_up():
    for dev in devs:
        dev.remove()
    for f in files:
        cmd("rm -f %s" % (f))

def pin_prog(file_name, idx=0):
    progs = bpftool_prog_list(expected=(idx + 1))
    prog = progs[idx]
    bpftool("prog pin id %d %s" % (prog["id"], file_name))
    files.append(file_name)

    return file_name, bpf_pinned(file_name)

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
samples = ["sample_ret0.o"]
for s in samples:
    ret, out = cmd("ls %s/%s" % (bpf_test_dir, s), fail=False)
    skip(ret != 0, "sample %s/%s not found, please compile it" %
         (bpf_test_dir, s))

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
    ret, _ = sim.cls_bpf_add_filter(obj, skip_sw=True, fail=False)
    fail(ret == 0, "TC filter loaded without enabling TC offloads")
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
    ret, _ = sim.cls_bpf_add_filter(bytecode, skip_sw=True, fail=False)
    fail(ret == 0, "TC bytecode loaded for offload")
    sim.wait_for_flush()

    start_test("Test TC offloads work...")
    ret, _ = sim.cls_bpf_add_filter(obj, skip_sw=True, fail=False)
    fail(ret != 0, "TC filter did not load with TC offloads enabled")

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

    start_test("Test XDP prog replace with bad flags...")
    ret, _ = sim.set_xdp(obj, "offload", force=True, fail=False)
    fail(ret == 0, "Replaced XDP program with a program in different mode")
    ret, _ = sim.set_xdp(obj, "", force=True, fail=False)
    fail(ret == 0, "Replaced XDP program with a program in different mode")

    start_test("Test XDP prog remove with bad flags...")
    ret, _ = sim.unset_xdp("offload", force=True, fail=False)
    fail(ret == 0, "Removed program with a bad mode mode")
    ret, _ = sim.unset_xdp("", force=True, fail=False)
    fail(ret == 0, "Removed program with a bad mode mode")

    start_test("Test MTU restrictions...")
    ret, _ = sim.set_mtu(9000, fail=False)
    fail(ret == 0,
         "Driver should refuse to increase MTU to 9000 with XDP loaded...")
    sim.unset_xdp("drv")
    bpftool_prog_list_wait(expected=0)
    sim.set_mtu(9000)
    ret, _ = sim.set_xdp(obj, "drv", fail=False)
    fail(ret == 0, "Driver should refuse to load program with MTU of 9000...")
    sim.set_mtu(1500)

    sim.wait_for_flush()
    start_test("Test XDP offload...")
    sim.set_xdp(obj, "offload")
    ipl = sim.ip_link_show(xdp=True)
    link_xdp = ipl["xdp"]["prog"]
    progs = bpftool_prog_list(expected=1)
    prog = progs[0]
    fail(link_xdp["id"] != prog["id"], "Loaded program has wrong ID")

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

    ret, _ = sim.set_xdp(pinned, "offload", fail=False)
    fail(ret == 0, "Pinned program loaded for a different device accepted")
    sim2.remove()
    ret, _ = sim.set_xdp(pinned, "offload", fail=False)
    fail(ret == 0, "Pinned program loaded for a removed device accepted")
    rm(pin_file)
    bpftool_prog_list_wait(expected=0)

    start_test("Test mixing of TC and XDP...")
    sim.tc_add_ingress()
    sim.set_xdp(obj, "offload")
    ret, _ = sim.cls_bpf_add_filter(obj, skip_sw=True, fail=False)
    fail(ret == 0, "Loading TC when XDP active should fail")
    sim.unset_xdp("offload")
    sim.wait_for_flush()

    sim.cls_bpf_add_filter(obj, skip_sw=True)
    ret, _ = sim.set_xdp(obj, "offload", fail=False)
    fail(ret == 0, "Loading XDP when TC active should fail")

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
    ret, _ = sim.cls_bpf_add_filter(obj, da=True, skip_sw=True, fail=False)
    fail(ret == 0, "Managed to offload two TC filters at the same time")

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

    print("%s: OK" % (os.path.basename(__file__)))

finally:
    log("Clean up...", "", level=1)
    log_level_inc()
    clean_up()
