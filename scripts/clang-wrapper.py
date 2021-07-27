#! /usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of The Linux Foundation nor
#       the names of its contributors may be used to endorse or promote
#       products derived from this software without specific prior written
#       permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Invoke clang, looking for warnings, and causing a failure if there are
# non-whitelisted warnings.

from __future__ import print_function
import errno
import re
import os
import sys
import subprocess

allowed_warnings = set([
    "atags_to_fdt.c:129", # arch/arm/boot/compressed/atags_to_fdt.c:129:5: warning: stack frame size of 2368 bytes in function 'atags_to_fdt' [-Wframe-larger-than=]
    "file.c:3010", # fs/f2fs/file.c:3010:12: warning: unused function 'f2fs_ioctl_check_project'
    "configfs.c:1488", # drivers/usb/gadget/configfs.c:1488:12: warning: unused function 'configfs_composite_setup'
    "configfs.c:1513", # drivers/usb/gadget/configfs.c:1513:13: warning: unused function 'configfs_composite_disconnect'
 ])

# Capture the name of the object file, can find it.
ofile = None

do_exit = False;

warning_re = re.compile(r'''(.*/|)([^/]+\.[a-z]+:\d+):(\d+:)? warning:''')
def interpret_warning(line):
    """Decode the message from clang.  The messages we care about have a filename, and a warning"""
    line = line.rstrip('\n')
    m = warning_re.match(line)
    if m and m.group(2) not in allowed_warnings:
        print ("error, forbidden warning:" + m.group(2))

        # If there is a warning, remove any object if it exists.
        if ofile:
            try:
                os.remove(ofile)
            except OSError:
                pass
        global do_exit
        do_exit = True;

def run_clang():
    args = sys.argv[1:]
    # Look for -o
    try:
        i = args.index('-o')
        global ofile
        ofile = args[i+1]
    except (ValueError, IndexError):
        pass

    try:
        env = os.environ.copy()
        env['LC_ALL'] = 'C'
        proc = subprocess.Popen(args, stderr=subprocess.PIPE, env=env)
        for line in proc.stderr:
            print (line.decode("utf-8"), end="")
            interpret_warning(line.decode("utf-8"))
        if do_exit:
            sys.exit(1)

        result = proc.wait()
    except OSError as e:
        result = e.errno
        if result == errno.ENOENT:
            print (args[0] + ':' + e.strerror)
            print ('Is your PATH set correctly?')
        else:
            print (' '.join(args) + str(e))

    return result

if __name__ == '__main__':
    status = run_clang()
    sys.exit(status)
