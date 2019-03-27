#!/usr/bin/env python
"""
Copyright (c) 2017 Miles Fertel
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.

     $FreeBSD$
"""

from time import time
import os
import sys

WIKI = False
sorts=["heap", "merge", "quick"]
if (WIKI):
    sorts.append("wiki")
tests=["rand", "sort", "rev", "part"]
runs = 5
trials = 5
outdir = "stats"
datadir = '{}/data'.format(outdir)
progname = "sort_bench"
try:
    elts = sys.argv[1]
except:
    elts = 20

if (not os.path.isdir(datadir)):
    os.makedirs(datadir)

for test in tests:
    files = []
    for sort in sorts:
        filename = '{}/{}{}'.format(datadir, test, sort)
        files.append(filename)
        with open(filename, 'w+') as f:
            for _ in range(trials):
                start = time()
                ret = os.system('./{} {} {} {} {}'.format(progname, sort, test, runs, elts))
                total = time() - start
                if (ret):
                    sys.exit("Bench program failed. Did you remember to compile it?")
                f.write('{}\n'.format(str(total)))
            f.close()
    with open('{}/{}'.format(outdir, test), 'w+') as f:
        command = 'ministat -s -w 60 '
        for filename in files:
            command += '{} '.format(filename)
        command += '> {}/{}stats'.format(outdir, test)
        os.system(command)

