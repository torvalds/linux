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
_LINE_PATTERN = r'^(saved)?cmd_[^ ]*\.o := (?P<command_prefix>.* )(?P<file_path>[^ ]*\.[cS]) *(;|$)'
_VALID_LOG_LEVELS = ['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL']

# Pre-compiled regexes for better performance
_INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*[<"]([^>"]*)[>"]')
_C_INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*"([^"]*\.c)"\s*$')
_FILENAME_MATCHER = re.compile(_FILENAME_PATTERN)

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
            os.path.realpath(args.directory),
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

    exclude_dirs = [ os.path.join(directory, d) for d in _EXCLUDE_DIRS ]

    for dirpath, dirnames, filenames in os.walk(directory, topdown=True):
        # Prune unwanted directories.
        if dirpath in exclude_dirs:
            dirnames[:] = []
            continue

        for filename in filenames:
            if _FILENAME_MATCHER.match(filename):
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
            obj = line.rstrip()
            base, ext = os.path.splitext(obj)
            if ext != '.o':
                sys.exit('{}: module path must end with .o'.format(obj))
            mod = base + '.mod'
            # Read from *.mod, to get a list of objects that compose the module.
            with open(mod) as m:
                for mod_line in m:
                    yield to_cmdfile(mod_line.rstrip())


def extract_includes_from_file(source_file, root_directory):
    """Extract #include statements from a C file.

    Args:
        source_file: Path to the source .c file to analyze
        root_directory: Root directory for resolving relative paths

    Returns:
        List of header files that should be included (without quotes/brackets)
    """
    includes = []
    if not os.path.exists(source_file):
        return includes

    try:
        with open(source_file, 'r') as f:
            for line in f:
                line = line.strip()
                # Look for #include statements.
                # Match both #include "header.h" and #include <header.h>.
                match = _INCLUDE_PATTERN.match(line)
                if match:
                    header = match.group(1)
                    # Skip including other .c files to avoid circular includes.
                    if not header.endswith('.c'):
                        # For relative includes (quoted), resolve path relative to source file.
                        if '"' in line:
                            src_dir = os.path.dirname(source_file)
                            header_path = os.path.join(src_dir, header)
                            if os.path.exists(header_path):
                                rel_header = os.path.relpath(header_path, root_directory)
                                includes.append(rel_header)
                            else:
                                includes.append(header)
                        else:
                            # System include like <linux/sched.h>.
                            includes.append(header)
    except IOError:
        pass

    return includes


def find_included_c_files(source_file, root_directory):
    """Find .c files that are included by the given source file.

    Args:
        source_file: Path to the source .c file
        root_directory: Root directory for resolving relative paths

    Yields:
        Full paths to included .c files
    """
    if not os.path.exists(source_file):
        return

    try:
        with open(source_file, 'r') as f:
            for line in f:
                line = line.strip()
                # Look for #include "*.c" patterns.
                match = _C_INCLUDE_PATTERN.match(line)
                if match:
                    included_file = match.group(1)
                    # Handle relative paths.
                    if not os.path.isabs(included_file):
                        src_dir = os.path.dirname(source_file)
                        included_file = os.path.join(src_dir, included_file)

                    # Normalize the path.
                    included_file = os.path.normpath(included_file)

                    # Check if the file exists.
                    if os.path.exists(included_file):
                        yield included_file
    except IOError:
        pass


def process_line(root_directory, command_prefix, file_path):
    """Extracts information from a .cmd line and creates entries from it.

    Args:
        root_directory: The directory that was searched for .cmd files. Usually
            used directly in the "directory" entry in compile_commands.json.
        command_prefix: The extracted command line, up to the last element.
        file_path: The .c file from the end of the extracted command.
            Usually relative to root_directory, but sometimes absolute.

    Returns:
        A list of entries to append to compile_commands (may include multiple
        entries if the source file includes other .c files).

    Raises:
        ValueError: Could not find the extracted file based on file_path and
            root_directory or file_directory.
    """
    # The .cmd files are intended to be included directly by Make, so they
    # escape the pound sign '#' as '$(pound)'. The compile_commands.json file
    # is not interepreted by Make, so this code replaces the escaped version
    # with '#'.
    prefix = command_prefix.replace('$(pound)', '#')

    # Return the canonical path, eliminating any symbolic links encountered in the path.
    abs_path = os.path.realpath(os.path.join(root_directory, file_path))
    if not os.path.exists(abs_path):
        raise ValueError('File %s not found' % abs_path)

    entries = []

    # Create entry for the main source file.
    main_entry = {
        'directory': root_directory,
        'file': abs_path,
        'command': prefix + file_path,
    }
    entries.append(main_entry)

    # Find and create entries for included .c files.
    for included_c_file in find_included_c_files(abs_path, root_directory):
        # For included .c files, create a compilation command that:
        # 1. Uses the same compilation flags as the parent file
        # 2. But compiles the included file directly (not the parent)
        # 3. Includes necessary headers from the parent file for proper macro resolution

        # Convert absolute path to relative for the command.
        rel_path = os.path.relpath(included_c_file, root_directory)

        # Extract includes from the parent file to provide proper compilation context.
        extra_includes = ''
        try:
            parent_includes = extract_includes_from_file(abs_path, root_directory)
            if parent_includes:
                extra_includes = ' ' + ' '.join('-include ' + inc for inc in parent_includes)
        except IOError:
            pass

        included_entry = {
            'directory': root_directory,
            'file': included_c_file,
            # Use the same compilation prefix but target the included file directly.
            # Add extra headers for proper macro resolution.
            'command': prefix + extra_includes + ' ' + rel_path,
        }
        entries.append(included_entry)
        logging.debug('Added entry for included file: %s', included_c_file)

    return entries


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
                        entries = process_line(directory, result.group('command_prefix'),
                                             result.group('file_path'))
                        compile_commands.extend(entries)
                    except ValueError as err:
                        logging.info('Could not add line from %s: %s',
                                     cmdfile, err)

    with open(output, 'wt') as f:
        json.dump(sorted(compile_commands, key=lambda x: x["file"]), f, indent=2, sort_keys=True)


if __name__ == '__main__':
    main()
