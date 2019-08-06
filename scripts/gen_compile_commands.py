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
# database. If this code finds 500 or fewer, then warn the user that they might
# not have all the .cmd files, and they might need to compile the kernel.
_LOW_COUNT_THRESHOLD = 500


def parse_arguments():
    """Sets up and parses command-line arguments.

    Returns:
        log_level: A logging level to filter log output.
        directory: The directory to search for .cmd files.
        output: Where to write the compile-commands JSON file.
    """
    usage = 'Creates a compile_commands.json database from kernel .cmd files'
    parser = argparse.ArgumentParser(description=usage)

    directory_help = ('Path to the kernel source directory to search '
                      '(defaults to the working directory)')
    parser.add_argument('-d', '--directory', type=str, help=directory_help)

    output_help = ('The location to write compile_commands.json (defaults to '
                   'compile_commands.json in the search directory)')
    parser.add_argument('-o', '--output', type=str, help=output_help)

    log_level_help = ('The level of log messages to produce (one of ' +
                      ', '.join(_VALID_LOG_LEVELS) + '; defaults to ' +
                      _DEFAULT_LOG_LEVEL + ')')
    parser.add_argument(
        '--log_level', type=str, default=_DEFAULT_LOG_LEVEL,
        help=log_level_help)

    args = parser.parse_args()

    log_level = args.log_level
    if log_level not in _VALID_LOG_LEVELS:
        raise ValueError('%s is not a valid log level' % log_level)

    directory = args.directory or os.getcwd()
    output = args.output or os.path.join(directory, _DEFAULT_OUTPUT)
    directory = os.path.abspath(directory)

    return log_level, directory, output


def process_line(root_directory, file_directory, command_prefix, relative_path):
    """Extracts information from a .cmd line and creates an entry from it.

    Args:
        root_directory: The directory that was searched for .cmd files. Usually
            used directly in the "directory" entry in compile_commands.json.
        file_directory: The path to the directory the .cmd file was found in.
        command_prefix: The extracted command line, up to the last element.
        relative_path: The .c file from the end of the extracted command.
            Usually relative to root_directory, but sometimes relative to
            file_directory and sometimes neither.

    Returns:
        An entry to append to compile_commands.

    Raises:
        ValueError: Could not find the extracted file based on relative_path and
            root_directory or file_directory.
    """
    # The .cmd files are intended to be included directly by Make, so they
    # escape the pound sign '#', either as '\#' or '$(pound)' (depending on the
    # kernel version). The compile_commands.json file is not interepreted
    # by Make, so this code replaces the escaped version with '#'.
    prefix = command_prefix.replace('\#', '#').replace('$(pound)', '#')

    cur_dir = root_directory
    expected_path = os.path.join(cur_dir, relative_path)
    if not os.path.exists(expected_path):
        # Try using file_directory instead. Some of the tools have a different
        # style of .cmd file than the kernel.
        cur_dir = file_directory
        expected_path = os.path.join(cur_dir, relative_path)
        if not os.path.exists(expected_path):
            raise ValueError('File %s not in %s or %s' %
                             (relative_path, root_directory, file_directory))
    return {
        'directory': cur_dir,
        'file': relative_path,
        'command': prefix + relative_path,
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
                for line in f:
                    result = line_matcher.match(line)
                    if not result:
                        continue

                    try:
                        entry = process_line(directory, dirpath,
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
