#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2024 Google LLC
# Written by Simon Glass <sjg@chromium.org>
#

"""Build a FIT containing a lot of devicetree files

Usage:
    make_fit.py -A arm64 -n 'Linux-6.6' -O linux
        -o arch/arm64/boot/image.fit -k /tmp/kern/arch/arm64/boot/image.itk
        -r /boot/initrd.img-6.14.0-27-generic @arch/arm64/boot/dts/dtbs-list
        -E -c gzip

Creates a FIT containing the supplied kernel, an optional ramdisk, and a set of
devicetree files, either specified individually or listed in a file (with an
'@' prefix).

Use -r to specify an existing ramdisk/initrd file.

Use -E to generate an external FIT (where the data is placed after the
FIT data structure). This allows parsing of the data without loading
the entire FIT.

Use -c to compress the data, using bzip2, gzip, lz4, lzma, lzo and
zstd algorithms.

Use -D to decompose "composite" DTBs into their base components and
deduplicate the resulting base DTBs and DTB overlays. This requires the
DTBs to be sourced from the kernel build directory, as the implementation
looks at the .cmd files produced by the kernel build.

The resulting FIT can be booted by bootloaders which support FIT, such
as U-Boot, Linuxboot, Tianocore, etc.
"""

import argparse
import collections
import multiprocessing
import os
import subprocess
import sys
import tempfile
import time

import libfdt


# Tool extension and the name of the command-line tools
CompTool = collections.namedtuple('CompTool', 'ext,tools')

COMP_TOOLS = {
    'bzip2': CompTool('.bz2', 'pbzip2,bzip2'),
    'gzip': CompTool('.gz', 'pigz,gzip'),
    'lz4': CompTool('.lz4', 'lz4'),
    'lzma': CompTool('.lzma', 'plzip,lzma'),
    'lzo': CompTool('.lzo', 'lzop'),
    'xz': CompTool('.xz', 'xz'),
    'zstd': CompTool('.zstd', 'zstd'),
}


def parse_args():
    """Parse the program ArgumentParser

    Returns:
        Namespace object containing the arguments
    """
    epilog = 'Build a FIT from a directory tree containing .dtb files'
    parser = argparse.ArgumentParser(epilog=epilog, fromfile_prefix_chars='@')
    parser.add_argument('-A', '--arch', type=str, required=True,
          help='Specifies the architecture')
    parser.add_argument('-c', '--compress', type=str, default='none',
          help='Specifies the compression')
    parser.add_argument('-D', '--decompose-dtbs', action='store_true',
          help='Decompose composite DTBs into base DTB and overlays')
    parser.add_argument('-E', '--external', action='store_true',
          help='Convert the FIT to use external data')
    parser.add_argument('-n', '--name', type=str, required=True,
          help='Specifies the name')
    parser.add_argument('-o', '--output', type=str, required=True,
          help='Specifies the output file (.fit)')
    parser.add_argument('-O', '--os', type=str, required=True,
          help='Specifies the operating system')
    parser.add_argument('-k', '--kernel', type=str, required=True,
          help='Specifies the (uncompressed) kernel input file (.itk)')
    parser.add_argument('-r', '--ramdisk', type=str,
          help='Specifies the ramdisk/initrd input file')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Enable verbose output')
    parser.add_argument('dtbs', type=str, nargs='*',
          help='Specifies the devicetree files to process')

    return parser.parse_args()


def setup_fit(fsw, name):
    """Make a start on writing the FIT

    Outputs the root properties and the 'images' node

    Args:
        fsw (libfdt.FdtSw): Object to use for writing
        name (str): Name of kernel image
    """
    fsw.INC_SIZE = 16 << 20
    fsw.finish_reservemap()
    fsw.begin_node('')
    fsw.property_string('description', f'{name} with devicetree set')
    fsw.property_u32('#address-cells', 1)

    fsw.property_u32('timestamp', int(time.time()))
    fsw.begin_node('images')


def write_kernel(fsw, data, args):
    """Write out the kernel image

    Writes a kernel node along with the required properties

    Args:
        fsw (libfdt.FdtSw): Object to use for writing
        data (bytes): Data to write (possibly compressed)
        args (Namespace): Contains necessary strings:
            arch: FIT architecture, e.g. 'arm64'
            fit_os: Operating Systems, e.g. 'linux'
            name: Name of OS, e.g. 'Linux-6.6.0-rc7'
            compress: Compression algorithm to use, e.g. 'gzip'
    """
    with fsw.add_node('kernel'):
        fsw.property_string('description', args.name)
        fsw.property_string('type', 'kernel_noload')
        fsw.property_string('arch', args.arch)
        fsw.property_string('os', args.os)
        fsw.property_string('compression', args.compress)
        fsw.property('data', data)
        fsw.property_u32('load', 0)
        fsw.property_u32('entry', 0)


def write_ramdisk(fsw, data, args):
    """Write out the ramdisk image

    Writes a ramdisk node along with the required properties

    Args:
        fsw (libfdt.FdtSw): Object to use for writing
        data (bytes): Data to write (possibly compressed)
        args (Namespace): Contains necessary strings:
            arch: FIT architecture, e.g. 'arm64'
            fit_os: Operating Systems, e.g. 'linux'
    """
    with fsw.add_node('ramdisk'):
        fsw.property_string('description', 'Ramdisk')
        fsw.property_string('type', 'ramdisk')
        fsw.property_string('arch', args.arch)
        fsw.property_string('compression', 'none')
        fsw.property_string('os', args.os)
        fsw.property('data', data)


def finish_fit(fsw, entries, has_ramdisk=False):
    """Finish the FIT ready for use

    Writes the /configurations node and subnodes

    Args:
        fsw (libfdt.FdtSw): Object to use for writing
        entries (list of tuple): List of configurations:
            str: Description of model
            str: Compatible stringlist
        has_ramdisk (bool): True if a ramdisk is included in the FIT
    """
    fsw.end_node()
    seq = 0
    with fsw.add_node('configurations'):
        for model, compat, files in entries:
            seq += 1
            with fsw.add_node(f'conf-{seq}'):
                fsw.property('compatible', bytes(compat))
                fsw.property_string('description', model)
                fsw.property('fdt', bytes(''.join(f'fdt-{x}\x00' for x in files), "ascii"))
                fsw.property_string('kernel', 'kernel')
                if has_ramdisk:
                    fsw.property_string('ramdisk', 'ramdisk')
    fsw.end_node()


def compress_data(inf, compress):
    """Compress data using a selected algorithm

    Args:
        inf (IOBase): Filename containing the data to compress
        compress (str): Compression algorithm, e.g. 'gzip'

    Return:
        bytes: Compressed data
    """
    if compress == 'none':
        return inf.read()

    comp = COMP_TOOLS.get(compress)
    if not comp:
        raise ValueError(f"Unknown compression algorithm '{compress}'")

    with tempfile.NamedTemporaryFile() as comp_fname:
        with open(comp_fname.name, 'wb') as outf:
            done = False
            for tool in comp.tools.split(','):
                try:
                    # Add parallel flags for tools that support them
                    cmd = [tool]
                    if tool in ('zstd', 'xz'):
                        cmd.extend(['-T0'])  # Use all available cores
                    cmd.append('-c')
                    subprocess.call(cmd, stdin=inf, stdout=outf)
                    done = True
                    break
                except FileNotFoundError:
                    pass
            if not done:
                raise ValueError(f'Missing tool(s): {comp.tools}\n')
            with open(comp_fname.name, 'rb') as compf:
                comp_data = compf.read()
    return comp_data


def compress_dtb(fname, compress):
    """Compress a single DTB file

    Args:
        fname (str): Filename containing the DTB
        compress (str): Compression algorithm, e.g. 'gzip'

    Returns:
        tuple: (str: fname, bytes: compressed_data)
    """
    with open(fname, 'rb') as inf:
        compressed = compress_data(inf, compress)
    return fname, compressed


def output_dtb(fsw, seq, fname, arch, compress, data=None):
    """Write out a single devicetree to the FIT

    Args:
        fsw (libfdt.FdtSw): Object to use for writing
        seq (int): Sequence number (1 for first)
        fname (str): Filename containing the DTB
        arch (str): FIT architecture, e.g. 'arm64'
        compress (str): Compressed algorithm, e.g. 'gzip'
        data (bytes): Pre-compressed data (optional)
    """
    with fsw.add_node(f'fdt-{seq}'):
        fsw.property_string('description', os.path.basename(fname))
        fsw.property_string('type', 'flat_dt')
        fsw.property_string('arch', arch)
        fsw.property_string('compression', compress)

        if data is None:
            with open(fname, 'rb') as inf:
                data = compress_data(inf, compress)
        fsw.property('data', data)


def process_dtb(fname, args):
    """Process an input DTB, decomposing it if requested and is possible

    Args:
        fname (str): Filename containing the DTB
        args (Namespace): Program arguments
    Returns:
        tuple:
            str: Model name string
            str: Root compatible string
            files: list of filenames corresponding to the DTB
    """
    # Get the compatible / model information
    with open(fname, 'rb') as inf:
        data = inf.read()
    fdt = libfdt.FdtRo(data)
    model = fdt.getprop(0, 'model').as_str()
    compat = fdt.getprop(0, 'compatible')

    if args.decompose_dtbs:
        # Check if the DTB needs to be decomposed
        path, basename = os.path.split(fname)
        cmd_fname = os.path.join(path, f'.{basename}.cmd')
        with open(cmd_fname, 'r', encoding='ascii') as inf:
            cmd = inf.read()

        if 'scripts/dtc/fdtoverlay' in cmd:
            # This depends on the structure of the composite DTB command
            files = cmd.split()
            files = files[files.index('-i') + 1:]
        else:
            files = [fname]
    else:
        files = [fname]

    return (model, compat, files)


def _process_dtbs(args, fsw, entries, fdts):
    """Process all DTB files and add them to the FIT

    Args:
        args: Program arguments
        fsw: FIT writer object
        entries: List to append entries to
        fdts: Dictionary of processed DTBs

    Returns:
        tuple:
            Number of files processed
            Total size of files processed
    """
    seq = 0
    size = 0

    # First figure out the unique DTB files that need compression
    todo = []
    file_info = []  # List of (fname, model, compat, files) tuples

    for fname in args.dtbs:
        # Ignore non-DTB (*.dtb) files
        if os.path.splitext(fname)[1] != '.dtb':
            continue

        try:
            (model, compat, files) = process_dtb(fname, args)
        except Exception as e:
            sys.stderr.write(f'Error processing {fname}:\n')
            raise e

        file_info.append((fname, model, compat, files))
        for fn in files:
            if fn not in fdts and fn not in todo:
                todo.append(fn)

    # Compress all DTBs in parallel
    cache = {}
    if todo and args.compress != 'none':
        if args.verbose:
            print(f'Compressing {len(todo)} DTBs...')

        with multiprocessing.Pool() as pool:
            compress_args = [(fn, args.compress) for fn in todo]
            # unpacks each tuple, calls compress_dtb(fn, compress) in parallel
            results = pool.starmap(compress_dtb, compress_args)

        cache = dict(results)

    # Now write all DTBs to the FIT using pre-compressed data
    for fname, model, compat, files in file_info:
        for fn in files:
            if fn not in fdts:
                seq += 1
                size += os.path.getsize(fn)
                output_dtb(fsw, seq, fn, args.arch, args.compress,
                           cache.get(fn))
                fdts[fn] = seq

        files_seq = [fdts[fn] for fn in files]
        entries.append([model, compat, files_seq])

    return seq, size


def build_fit(args):
    """Build the FIT from the provided files and arguments

    Args:
        args (Namespace): Program arguments

    Returns:
        tuple:
            bytes: FIT data
            int: Number of configurations generated
            size: Total uncompressed size of data
    """
    size = 0
    fsw = libfdt.FdtSw()
    setup_fit(fsw, args.name)
    entries = []
    fdts = {}

    # Handle the kernel
    with open(args.kernel, 'rb') as inf:
        comp_data = compress_data(inf, args.compress)
    size += os.path.getsize(args.kernel)
    write_kernel(fsw, comp_data, args)

    # Handle the ramdisk if provided. Compression is not supported as it is
    # already compressed.
    if args.ramdisk:
        with open(args.ramdisk, 'rb') as inf:
            data = inf.read()
        size += len(data)
        write_ramdisk(fsw, data, args)

    count, fdt_size = _process_dtbs(args, fsw, entries, fdts)
    size += fdt_size

    finish_fit(fsw, entries, bool(args.ramdisk))

    # Include the kernel itself in the returned file count
    fdt = fsw.as_fdt()
    fdt.pack()
    return fdt.as_bytearray(), count + 1 + bool(args.ramdisk), size


def run_make_fit():
    """Run the tool's main logic"""
    args = parse_args()

    out_data, count, size = build_fit(args)
    with open(args.output, 'wb') as outf:
        outf.write(out_data)

    ext_fit_size = None
    if args.external:
        mkimage = os.environ.get('MKIMAGE', 'mkimage')
        subprocess.check_call([mkimage, '-E', '-F', args.output],
                              stdout=subprocess.DEVNULL)

        with open(args.output, 'rb') as inf:
            data = inf.read()
        ext_fit = libfdt.FdtRo(data)
        ext_fit_size = ext_fit.totalsize()

    if args.verbose:
        comp_size = len(out_data)
        print(f'FIT size {comp_size:#x}/{comp_size / 1024 / 1024:.1f} MB',
              end='')
        if ext_fit_size:
            print(f', header {ext_fit_size:#x}/{ext_fit_size / 1024:.1f} KB',
                  end='')
        print(f', {count} files, uncompressed {size / 1024 / 1024:.1f} MB')


if __name__ == "__main__":
    sys.exit(run_make_fit())
