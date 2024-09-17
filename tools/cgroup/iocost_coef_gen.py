#!/usr/bin/env python3
#
# Copyright (C) 2019 Tejun Heo <tj@kernel.org>
# Copyright (C) 2019 Andy Newell <newella@fb.com>
# Copyright (C) 2019 Facebook

desc = """
Generate linear IO cost model coefficients used by the blk-iocost
controller.  If the target raw testdev is specified, destructive tests
are performed against the whole device; otherwise, on
./iocost-coef-fio.testfile.  The result can be written directly to
/sys/fs/cgroup/io.cost.model.

On high performance devices, --numjobs > 1 is needed to achieve
saturation.

See Documentation/admin-guide/cgroup-v2.rst and block/blk-iocost.c
for more details.
"""

import argparse
import re
import json
import glob
import os
import sys
import atexit
import shutil
import tempfile
import subprocess

parser = argparse.ArgumentParser(description=desc,
                                 formatter_class=argparse.RawTextHelpFormatter)
parser.add_argument('--testdev', metavar='DEV',
                    help='Raw block device to use for testing, ignores --testfile-size')
parser.add_argument('--testfile-size-gb', type=float, metavar='GIGABYTES', default=16,
                    help='Testfile size in gigabytes (default: %(default)s)')
parser.add_argument('--duration', type=int, metavar='SECONDS', default=120,
                    help='Individual test run duration in seconds (default: %(default)s)')
parser.add_argument('--seqio-block-mb', metavar='MEGABYTES', type=int, default=128,
                    help='Sequential test block size in megabytes (default: %(default)s)')
parser.add_argument('--seq-depth', type=int, metavar='DEPTH', default=64,
                    help='Sequential test queue depth (default: %(default)s)')
parser.add_argument('--rand-depth', type=int, metavar='DEPTH', default=64,
                    help='Random test queue depth (default: %(default)s)')
parser.add_argument('--numjobs', type=int, metavar='JOBS', default=1,
                    help='Number of parallel fio jobs to run (default: %(default)s)')
parser.add_argument('--quiet', action='store_true')
parser.add_argument('--verbose', action='store_true')

def info(msg):
    if not args.quiet:
        print(msg)

def dbg(msg):
    if args.verbose and not args.quiet:
        print(msg)

# determine ('DEVNAME', 'MAJ:MIN') for @path
def dir_to_dev(path):
    # find the block device the current directory is on
    devname = subprocess.run(f'findmnt -nvo SOURCE -T{path}',
                             stdout=subprocess.PIPE, shell=True).stdout
    devname = os.path.basename(devname).decode('utf-8').strip()

    # partition -> whole device
    parents = glob.glob('/sys/block/*/' + devname)
    if len(parents):
        devname = os.path.basename(os.path.dirname(parents[0]))
    rdev = os.stat(f'/dev/{devname}').st_rdev
    return (devname, f'{os.major(rdev)}:{os.minor(rdev)}')

def create_testfile(path, size):
    global args

    if os.path.isfile(path) and os.stat(path).st_size == size:
        return

    info(f'Creating testfile {path}')
    subprocess.check_call(f'rm -f {path}', shell=True)
    subprocess.check_call(f'touch {path}', shell=True)
    subprocess.call(f'chattr +C {path}', shell=True)
    subprocess.check_call(
        f'pv -s {size} -pr /dev/urandom {"-q" if args.quiet else ""} | '
        f'dd of={path} count={size} '
        f'iflag=count_bytes,fullblock oflag=direct bs=16M status=none',
        shell=True)

def run_fio(testfile, duration, iotype, iodepth, blocksize, jobs):
    global args

    eta = 'never' if args.quiet else 'always'
    outfile = tempfile.NamedTemporaryFile()
    cmd = (f'fio --direct=1 --ioengine=libaio --name=coef '
           f'--filename={testfile} --runtime={round(duration)} '
           f'--readwrite={iotype} --iodepth={iodepth} --blocksize={blocksize} '
           f'--eta={eta} --output-format json --output={outfile.name} '
           f'--time_based --numjobs={jobs}')
    if args.verbose:
        dbg(f'Running {cmd}')
    subprocess.check_call(cmd, shell=True)
    with open(outfile.name, 'r') as f:
        d = json.loads(f.read())
    return sum(j['read']['bw_bytes'] + j['write']['bw_bytes'] for j in d['jobs'])

def restore_elevator_nomerges():
    global elevator_path, nomerges_path, elevator, nomerges

    info(f'Restoring elevator to {elevator} and nomerges to {nomerges}')
    with open(elevator_path, 'w') as f:
        f.write(elevator)
    with open(nomerges_path, 'w') as f:
        f.write(nomerges)


args = parser.parse_args()

missing = False
for cmd in [ 'findmnt', 'pv', 'dd', 'fio' ]:
    if not shutil.which(cmd):
        print(f'Required command "{cmd}" is missing', file=sys.stderr)
        missing = True
if missing:
    sys.exit(1)

if args.testdev:
    devname = os.path.basename(args.testdev)
    rdev = os.stat(f'/dev/{devname}').st_rdev
    devno = f'{os.major(rdev)}:{os.minor(rdev)}'
    testfile = f'/dev/{devname}'
    info(f'Test target: {devname}({devno})')
else:
    devname, devno = dir_to_dev('.')
    testfile = 'iocost-coef-fio.testfile'
    testfile_size = int(args.testfile_size_gb * 2 ** 30)
    create_testfile(testfile, testfile_size)
    info(f'Test target: {testfile} on {devname}({devno})')

elevator_path = f'/sys/block/{devname}/queue/scheduler'
nomerges_path = f'/sys/block/{devname}/queue/nomerges'

with open(elevator_path, 'r') as f:
    elevator = re.sub(r'.*\[(.*)\].*', r'\1', f.read().strip())
with open(nomerges_path, 'r') as f:
    nomerges = f.read().strip()

info(f'Temporarily disabling elevator and merges')
atexit.register(restore_elevator_nomerges)
with open(elevator_path, 'w') as f:
    f.write('none')
with open(nomerges_path, 'w') as f:
    f.write('1')

info('Determining rbps...')
rbps = run_fio(testfile, args.duration, 'read',
               1, args.seqio_block_mb * (2 ** 20), args.numjobs)
info(f'\nrbps={rbps}, determining rseqiops...')
rseqiops = round(run_fio(testfile, args.duration, 'read',
                         args.seq_depth, 4096, args.numjobs) / 4096)
info(f'\nrseqiops={rseqiops}, determining rrandiops...')
rrandiops = round(run_fio(testfile, args.duration, 'randread',
                          args.rand_depth, 4096, args.numjobs) / 4096)
info(f'\nrrandiops={rrandiops}, determining wbps...')
wbps = run_fio(testfile, args.duration, 'write',
               1, args.seqio_block_mb * (2 ** 20), args.numjobs)
info(f'\nwbps={wbps}, determining wseqiops...')
wseqiops = round(run_fio(testfile, args.duration, 'write',
                         args.seq_depth, 4096, args.numjobs) / 4096)
info(f'\nwseqiops={wseqiops}, determining wrandiops...')
wrandiops = round(run_fio(testfile, args.duration, 'randwrite',
                          args.rand_depth, 4096, args.numjobs) / 4096)
info(f'\nwrandiops={wrandiops}')
restore_elevator_nomerges()
atexit.unregister(restore_elevator_nomerges)
info('')

print(f'{devno} rbps={rbps} rseqiops={rseqiops} rrandiops={rrandiops} '
      f'wbps={wbps} wseqiops={wseqiops} wrandiops={wrandiops}')
