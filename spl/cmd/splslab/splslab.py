#!/usr/bin/python

import sys
import time
import getopt
import re
import signal
from collections import defaultdict

class Stat:
    # flag definitions based on the kmem.h
    NOTOUCH = 1
    NODEBUG = 2
    KMEM = 32
    VMEM = 64
    SLAB = 128
    OFFSLAB = 256
    NOEMERGENCY = 512
    DEADLOCKED = 16384
    GROWING = 32768
    REAPING = 65536
    DESTROY = 131072

    fdefs = {
        NOTOUCH : "NTCH",
        NODEBUG : "NDBG", 
        KMEM : "KMEM",
        VMEM : "VMEM",
        SLAB : "SLAB",
        OFFSLAB : "OFSL",
        NOEMERGENCY : "NEMG",
        DEADLOCKED : "DDLK",
        GROWING : "GROW",
        REAPING : "REAP",
        DESTROY : "DSTR"
        }

    def __init__(self, name, flags, size, alloc, slabsize, objsize):
        self._name = name
        self._flags = self.f2str(flags)
        self._size = size
        self._alloc = alloc
        self._slabsize = slabsize
        self._objsize = objsize

    def f2str(self, flags):
        fstring = ''
        for k in Stat.fdefs.keys():
            if flags & k:
                fstring = fstring + Stat.fdefs[k] + '|'

        fstring = fstring[:-1]
        return fstring

class CumulativeStat:
    def __init__(self, skey="a"):
        self._size = 0
        self._alloc = 0
        self._pct = 0
        self._skey = skey
        self._regexp = \
            re.compile('(\w+)\s+(\w+)\s+(\w+)\s+(\w+)\s+(\w+)\s+(\w+)\s+');
        self._stats = defaultdict(list)

    # Add another stat to the dictionary and re-calculate the totals
    def add(self, s):
        key = 0
        if self._skey == "a":
            key = s._alloc
        else:
            key = s._size
        self._stats[key].append(s)
        self._size = self._size + s._size
        self._alloc = self._alloc + s._alloc
        if self._size:
            self._pct = self._alloc * 100 / self._size
        else:
            self._pct = 0

    # Parse the slab info in the procfs
    # Calculate cumulative stats
    def slab_update(self):
        k = [line.strip() for line in open('/proc/spl/kmem/slab')]

        if not k:
            sys.stderr.write("No SPL slab stats found\n")
            sys.exit(1)

        del k[0:2]

        for s in k:
            if not s:
                continue
            m = self._regexp.match(s)
            if m:
                self.add(Stat(m.group(1), int(m.group(2),16), int(m.group(3)),
                            int(m.group(4)), int(m.group(5)), int(m.group(6))))
            else:
                sys.stderr.write("Error: unexpected input format\n" % s)
                exit(-1)

    def show_header(self):
        sys.stdout.write("\n%25s %20s %15s %15s %15s %15s\n\n" % \
            ("cache name", "flags", "size", "alloc", "slabsize", "objsize"))

    # Show up to the number of 'rows' of output sorted in descending order
    # by the key specified earlier; if rows == 0, all rows are shown
    def show(self, rows):
        self.show_header()
        i = 1
        done = False
        for k in reversed(sorted(self._stats.keys())):
            for s in self._stats[k]:
                sys.stdout.write("%25s %20s %15d %15d %15d %15d\n" % \
                                     (s._name, s._flags, s._size, s._alloc, \
                                          s._slabsize, s._objsize))
                i = i + 1
                if rows != 0 and i > rows:
                    done = True
                    break
            if done:
                break
        sys.stdout.write("%25s %36d %15d (%d%%)\n\n" % \
            ("Totals:", self._size, self._alloc, self._pct))

def usage():
    cmd = "Usage: splslab.py [-n|--num-rows] number [-s|--sort-by] " + \
        "[interval] [count]";
    sys.stderr.write("%s\n" % cmd)
    sys.stderr.write("\t-h : print help\n")
    sys.stderr.write("\t-n : --num-rows N : limit output to N top " +
                     "largest slabs (default: all)\n")
    sys.stderr.write("\t-s : --sort-by key : sort output in descending " +
                     "order by total size (s)\n\t\tor allocated size (a) " +
                     "(default: a)\n")
    sys.stderr.write("\tinterval : repeat every interval seconds\n")
    sys.stderr.write("\tcount : output statistics count times and exit\n")
    

def main():

    rows = 0
    count = 0
    skey = "a"
    interval = 1

    signal.signal(signal.SIGINT, signal.SIG_DFL)

    try:
        opts, args = getopt.getopt(
            sys.argv[1:],
            "n:s:h",
            [
                "num-rows",
                "sort-by",
                "help"
            ]
        )
    except getopt.error as e:
        sys.stderr.write("Error: %s\n" % e.msg)
        usage()
        exit(-1)

    i = 1
    for opt, arg in opts:
        if opt in ('-n', '--num-rows'):
            rows = int(arg)
            i = i + 2
        elif opt in ('-s', '--sort-by'):
            if arg != "s" and arg != "a":
                sys.stderr.write("Error: invalid sorting key \"%s\"\n" % arg)
                usage()
                exit(-1)
            skey = arg
            i = i + 2
        elif opt in ('-h', '--help'):
            usage()
            exit(0)
        else:
            break

    args = sys.argv[i:]

    interval = int(args[0]) if len(args) else interval
    count = int(args[1]) if len(args) > 1 else count

    i = 0
    while True:
        cs = CumulativeStat(skey)
        cs.slab_update()
        cs.show(rows)

        i = i + 1
        if count and i >= count:
            break

        time.sleep(interval)

    return 0

if __name__ == '__main__':
    main()
