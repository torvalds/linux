#!/usr/bin/env python

"""A tool for looking for indirect jumps and calls in x86 binaries.

   Helpful to verify whether or not retpoline mitigations are catching
   all of the indirect branches in a binary and telling you which
   functions the remaining ones are in (assembly, etc).

   Depends on llvm-objdump being in your path and is tied to the
   dump format.
"""

from __future__ import print_function

import os
import sys
import re
import subprocess
import optparse

# Look for indirect calls/jmps in a binary. re: (call|jmp).*\*
def look_for_indirect(file):
    args = ["llvm-objdump"]
    args.extend(["-d"])
    args.extend([file])

    p = subprocess.Popen(
        args=args, stdin=None, stderr=subprocess.PIPE, stdout=subprocess.PIPE
    )
    (stdout, stderr) = p.communicate()

    function = ""
    for line in stdout.splitlines():
        if line.startswith(" ") == False:
            function = line
        result = re.search("(call|jmp).*\*", line)
        if result != None:
            # TODO: Perhaps use cxxfilt to demangle functions?
            print(function)
            print(line)
    return


def main(args):
    # No options currently other than the binary.
    parser = optparse.OptionParser("%prog [options] <binary>")
    (opts, args) = parser.parse_args(args)
    if len(args) != 2:
        parser.error("invalid number of arguments: %s" % len(args))
    look_for_indirect(args[1])


if __name__ == "__main__":
    main(sys.argv)
