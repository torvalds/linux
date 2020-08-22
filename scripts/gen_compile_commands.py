#!/usr/bin/env python
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

_DEFAULT_OUTPUT = 'compile_commands.json'
_DEFAULT_LOG_LEVEL = 'WARNING'

_FILENAME_PATTERN = r'^\..*\.cmd$'
_LINE_PATTERN = r'^cmd_[^ ]*\.o := (.* )([^ ]*\.c)$'
_VALID_LOG_LEVELS = ['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL']

# A kernel build generally has over 2000 entries in its compile_commands.json
# database. If this code finds 300 or fewer, then warn the user that they might
# not have all the .cmd files, and they might need to compile the kernel.
_LOW_COUNT_THRESHOLD = 300


def parse_arguments():
    """Sets up and parses command-line arguments.

    Returns:
        log_level: A logging level to filter log output.
        directory: The work directory where the objects were built.
        output: Where to write the compile-commands JSON file.
    """
    usage = 'Creates a compile_commands.json database from kernel .cmd files'
    parser = argparse.ArgumentParser(description=usage)

    directory_help = ('specify the output directory used for the kernel build '
                      '(defaults to the working directory)')
    parser.add_argument('-d', '--directory', type=str, help=directory_help)

    output_help = ('The location to write compile_commands.json (defaults to '
                   'compile_commands.json in the search directory)')
    parser.add_argument('-o', '--output', type=str, help=output_help)

    log_level_help = ('the level of log messages to produce (defaults to ' +
                      _DEFAULT_LOG_LEVEL + ')')
    parser.add_argument('--log_level', choices=_VALID_LOG_LEVELS,
                        default=_DEFAULT_LOG_LEVEL, help=log_level_help)

    args = parser.parse_args()

    directory = args.directory or os.getcwd()
    output = args.output or os.path.join(directory, _DEFAULT_OUTPUT)
    directory = os.path.abspath(directory)

    return args.log_level, directory, output


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
    log_level, directory, output = parse_arguments()

    level = getattr(logging, log_level)
    logging.basicConfig(format='%(levelname)s: %(message)s', level=level)

    filename_matcher = re.compile(_FILENAME_PATTERN)
    line_matcher = re.compile(_LINE_PATTERN)

    compile_commands = []
    for dirpath, _, filenames in os.walk(directory):
        for filename in filenames:
            if not filename_matcher.match(filename):
                continue
            filepath = os.path.join(dirpath, filename)

            with open(filepath, 'rt') as f:
                result = line_matcher.match(f.readline())
                if result:
                    try:
                        entry = process_line(directory,
                                             result.group(1), result.group(2))
                        compile_commands.append(entry)
                    except ValueError as err:
                        logging.info('Could not add line from %s: %s',
                                     filepath, err)

    with open(output, 'wt') as f:
        json.dump(compile_commands, f, indent=2, sort_keys=True)

    count = len(compile_commands)
    if count < _LOW_COUNT_THRESHOLD:
        logging.warning(
            'Found %s entries. Have you compiled the kernel?', count)


if __name__ == '__main__':
    main()
