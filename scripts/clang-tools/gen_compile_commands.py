#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) Google LLC, 2018
#
# Author: Tom Roeder <tmroeder@google.com>
#
"""A tool for generating compile_commands.json in the Linux kernel."""

import argparse
import json
import logging
import os
import re
import subprocess
import sys

_DEFAULT_OUTPUT = 'compile_commands.json'
_DEFAULT_LOG_LEVEL = 'WARNING'

_FILENAME_PATTERN = r'^\..*\.cmd$'
_LINE_PATTERN = r'^cmd_[^ ]*\.o := (.* )([^ ]*\.c) *(;|$)'
_VALID_LOG_LEVELS = ['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL']
# The tools/ directory adopts a different build system, and produces .cmd
# files in a different format. Do not support it.
_EXCLUDE_DIRS = ['.git', 'Documentation', 'include', 'tools']

def parse_arguments():
    """Sets up and parses command-line arguments.

    Returns:
        log_level: A logging level to filter log output.
        directory: The work directory where the objects were built.
        ar: Command used for parsing .a archives.
        output: Where to write the compile-commands JSON file.
        paths: The list of files/directories to handle to find .cmd files.
    """
    usage = 'Creates a compile_commands.json database from kernel .cmd files'
    parser = argparse.ArgumentParser(description=usage)

    directory_help = ('specify the output directory used for the kernel build '
                      '(defaults to the working directory)')
    parser.add_argument('-d', '--directory', type=str, default='.',
                        help=directory_help)

    output_help = ('path to the output command database (defaults to ' +
                   _DEFAULT_OUTPUT + ')')
    parser.add_argument('-o', '--output', type=str, default=_DEFAULT_OUTPUT,
                        help=output_help)

    log_level_help = ('the level of log messages to produce (defaults to ' +
                      _DEFAULT_LOG_LEVEL + ')')
    parser.add_argument('--log_level', choices=_VALID_LOG_LEVELS,
                        default=_DEFAULT_LOG_LEVEL, help=log_level_help)

    ar_help = 'command used for parsing .a archives'
    parser.add_argument('-a', '--ar', type=str, default='llvm-ar', help=ar_help)

    paths_help = ('directories to search or files to parse '
                  '(files should be *.o, *.a, or modules.order). '
                  'If nothing is specified, the current directory is searched')
    parser.add_argument('paths', type=str, nargs='*', help=paths_help)

    args = parser.parse_args()

    return (args.log_level,
            os.path.abspath(args.directory),
            args.output,
            args.ar,
            args.paths if len(args.paths) > 0 else [args.directory])


def cmdfiles_in_dir(directory):
    """Generate the iterator of .cmd files found under the directory.

    Walk under the given directory, and yield every .cmd file found.

    Args:
        directory: The directory to search for .cmd files.

    Yields:
        The path to a .cmd file.
    """

    filename_matcher = re.compile(_FILENAME_PATTERN)
    exclude_dirs = [ os.path.join(directory, d) for d in _EXCLUDE_DIRS ]

    for dirpath, dirnames, filenames in os.walk(directory, topdown=True):
        # Prune unwanted directories.
        if dirpath in exclude_dirs:
            dirnames[:] = []
            continue

        for filename in filenames:
            if filename_matcher.match(filename):
                yield os.path.join(dirpath, filename)


def to_cmdfile(path):
    """Return the path of .cmd file used for the given build artifact

    Args:
        Path: file path

    Returns:
        The path to .cmd file
    """
    dir, base = os.path.split(path)
    return os.path.join(dir, '.' + base + '.cmd')


def cmdfiles_for_a(archive, ar):
    """Generate the iterator of .cmd files associated with the archive.

    Parse the given archive, and yield every .cmd file used to build it.

    Args:
        archive: The archive to parse

    Yields:
        The path to every .cmd file found
    """
    for obj in subprocess.check_output([ar, '-t', archive]).decode().split():
        yield to_cmdfile(obj)


def cmdfiles_for_modorder(modorder):
    """Generate the iterator of .cmd files associated with the modules.order.

    Parse the given modules.order, and yield every .cmd file used to build the
    contained modules.

    Args:
        modorder: The modules.order file to parse

    Yields:
        The path to every .cmd file found
    """
    with open(modorder) as f:
        for line in f:
            ko = line.rstrip()
            base, ext = os.path.splitext(ko)
            if ext != '.ko':
                sys.exit('{}: module path must end with .ko'.format(ko))
            mod = base + '.mod'
            # Read from *.mod, to get a list of objects that compose the module.
            with open(mod) as m:
                for mod_line in m:
                    yield to_cmdfile(mod_line.rstrip())


def process_line(root_directory, command_prefix, file_path):
    """Extracts information from a .cmd line and creates an entry from it.

    Args:
        root_directory: The directory that was searched for .cmd files. Usually
            used directly in the "directory" entry in compile_commands.json.
        command_prefix: The extracted command line, up to the last element.
        file_path: The .c file from the end of the extracted command.
            Usually relative to root_directory, but sometimes absolute.

    Returns:
        An entry to append to compile_commands.

    Raises:
        ValueError: Could not find the extracted file based on file_path and
            root_directory or file_directory.
    """
    # The .cmd files are intended to be included directly by Make, so they
    # escape the pound sign '#', either as '\#' or '$(pound)' (depending on the
    # kernel version). The compile_commands.json file is not interepreted
    # by Make, so this code replaces the escaped version with '#'.
    prefix = command_prefix.replace('\#', '#').replace('$(pound)', '#')

    # Use os.path.abspath() to normalize the path resolving '.' and '..' .
    abs_path = os.path.abspath(os.path.join(root_directory, file_path))
    if not os.path.exists(abs_path):
        raise ValueError('File %s not found' % abs_path)
    return {
        'directory': root_directory,
        'file': abs_path,
        'command': prefix + file_path,
    }


def main():
    """Walks through the directory and finds and parses .cmd files."""
    log_level, directory, output, ar, paths = parse_arguments()

    level = getattr(logging, log_level)
    logging.basicConfig(format='%(levelname)s: %(message)s', level=level)

    line_matcher = re.compile(_LINE_PATTERN)

    compile_commands = []

    for path in paths:
        # If 'path' is a directory, handle all .cmd files under it.
        # Otherwise, handle .cmd files associated with the file.
        # built-in objects are linked via vmlinux.a
        # Modules are listed in modules.order.
        if os.path.isdir(path):
            cmdfiles = cmdfiles_in_dir(path)
        elif path.endswith('.a'):
            cmdfiles = cmdfiles_for_a(path, ar)
        elif path.endswith('modules.order'):
            cmdfiles = cmdfiles_for_modorder(path)
        else:
            sys.exit('{}: unknown file type'.format(path))

        for cmdfile in cmdfiles:
            with open(cmdfile, 'rt') as f:
                result = line_matcher.match(f.readline())
                if result:
                    try:
                        entry = process_line(directory, result.group(1),
                                             result.group(2))
                        compile_commands.append(entry)
                    except ValueError as err:
                        logging.info('Could not add line from %s: %s',
                                     cmdfile, err)

    with open(output, 'wt') as f:
        json.dump(compile_commands, f, indent=2, sort_keys=True)


if __name__ == '__main__':
    main()
