#!/usr/bin/env python3
# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
#
# Copyright (C) 2021 Isovalent, Inc.

import argparse
import re
import os, sys

LINUX_ROOT = os.path.abspath(os.path.join(__file__,
    os.pardir, os.pardir, os.pardir, os.pardir, os.pardir))
BPFTOOL_DIR = os.getenv('BPFTOOL_DIR',
    os.path.join(LINUX_ROOT, 'tools/bpf/bpftool'))
BPFTOOL_BASHCOMP_DIR = os.getenv('BPFTOOL_BASHCOMP_DIR',
    os.path.join(BPFTOOL_DIR, 'bash-completion'))
BPFTOOL_DOC_DIR = os.getenv('BPFTOOL_DOC_DIR',
    os.path.join(BPFTOOL_DIR, 'Documentation'))
INCLUDE_DIR = os.getenv('INCLUDE_DIR',
    os.path.join(LINUX_ROOT, 'tools/include'))

retval = 0

class BlockParser(object):
    """
    A parser for extracting set of values from blocks such as enums.
    @reader: a pointer to the open file to parse
    """
    def __init__(self, reader):
        self.reader = reader

    def search_block(self, start_marker):
        """
        Search for a given structure in a file.
        @start_marker: regex marking the beginning of a structure to parse
        """
        offset = self.reader.tell()
        array_start = re.search(start_marker, self.reader.read())
        if array_start is None:
            raise Exception('Failed to find start of block')
        self.reader.seek(offset + array_start.start())

    def parse(self, pattern, end_marker):
        """
        Parse a block and return a set of values. Values to extract must be
        on separate lines in the file.
        @pattern: pattern used to identify the values to extract
        @end_marker: regex marking the end of the block to parse
        """
        entries = set()
        while True:
            line = self.reader.readline()
            if not line or re.match(end_marker, line):
                break
            capture = pattern.search(line)
            if capture and pattern.groups >= 1:
                entries.add(capture.group(1))
        return entries

class ArrayParser(BlockParser):
    """
    A parser for extracting a set of values from some BPF-related arrays.
    @reader: a pointer to the open file to parse
    @array_name: name of the array to parse
    """
    end_marker = re.compile('^};')

    def __init__(self, reader, array_name):
        self.array_name = array_name
        self.start_marker = re.compile(f'(static )?const bool {self.array_name}\[.*\] = {{\n')
        super().__init__(reader)

    def search_block(self):
        """
        Search for the given array in a file.
        """
        super().search_block(self.start_marker);

    def parse(self):
        """
        Parse a block and return data as a dictionary. Items to extract must be
        on separate lines in the file.
        """
        pattern = re.compile('\[(BPF_\w*)\]\s*= (true|false),?$')
        entries = set()
        while True:
            line = self.reader.readline()
            if line == '' or re.match(self.end_marker, line):
                break
            capture = pattern.search(line)
            if capture:
                entries |= {capture.group(1)}
        return entries

class InlineListParser(BlockParser):
    """
    A parser for extracting set of values from inline lists.
    """
    def parse(self, pattern, end_marker):
        """
        Parse a block and return a set of values. Multiple values to extract
        can be on a same line in the file.
        @pattern: pattern used to identify the values to extract
        @end_marker: regex marking the end of the block to parse
        """
        entries = set()
        while True:
            line = self.reader.readline()
            if not line:
                break
            entries.update(pattern.findall(line))
            if re.search(end_marker, line):
                break
        return entries

class FileExtractor(object):
    """
    A generic reader for extracting data from a given file. This class contains
    several helper methods that wrap around parser objects to extract values
    from different structures.
    This class does not offer a way to set a filename, which is expected to be
    defined in children classes.
    """
    def __init__(self):
        self.reader = open(self.filename, 'r')

    def close(self):
        """
        Close the file used by the parser.
        """
        self.reader.close()

    def reset_read(self):
        """
        Reset the file position indicator for this parser. This is useful when
        parsing several structures in the file without respecting the order in
        which those structures appear in the file.
        """
        self.reader.seek(0)

    def get_types_from_array(self, array_name):
        """
        Search for and parse a list of allowed BPF_* enum members, for example:

            const bool prog_type_name[] = {
                    [BPF_PROG_TYPE_UNSPEC]                  = true,
                    [BPF_PROG_TYPE_SOCKET_FILTER]           = true,
                    [BPF_PROG_TYPE_KPROBE]                  = true,
            };

        Return a set of the enum members, for example:

            {'BPF_PROG_TYPE_UNSPEC',
             'BPF_PROG_TYPE_SOCKET_FILTER',
             'BPF_PROG_TYPE_KPROBE'}

        @array_name: name of the array to parse
        """
        array_parser = ArrayParser(self.reader, array_name)
        array_parser.search_block()
        return array_parser.parse()

    def get_enum(self, enum_name):
        """
        Search for and parse an enum containing BPF_* members, for example:

            enum bpf_prog_type {
                    BPF_PROG_TYPE_UNSPEC,
                    BPF_PROG_TYPE_SOCKET_FILTER,
                    BPF_PROG_TYPE_KPROBE,
            };

        Return a set containing all member names, for example:

            {'BPF_PROG_TYPE_UNSPEC',
             'BPF_PROG_TYPE_SOCKET_FILTER',
             'BPF_PROG_TYPE_KPROBE'}

        @enum_name: name of the enum to parse
        """
        start_marker = re.compile(f'enum {enum_name} {{\n')
        pattern = re.compile('^\s*(BPF_\w+),?(\s+/\*.*\*/)?$')
        end_marker = re.compile('^};')
        parser = BlockParser(self.reader)
        parser.search_block(start_marker)
        return parser.parse(pattern, end_marker)

    def make_enum_map(self, names, enum_prefix):
        """
        Search for and parse an enum containing BPF_* members, just as get_enum
        does. However, instead of just returning a set of the variant names,
        also generate a textual representation from them by (assuming and)
        removing a provided prefix and lowercasing the remainder. Then return a
        dict mapping from name to textual representation.

        @enum_values: a set of enum values; e.g., as retrieved by get_enum
        @enum_prefix: the prefix to remove from each of the variants to infer
        textual representation
        """
        mapping = {}
        for name in names:
            if not name.startswith(enum_prefix):
                raise Exception(f"enum variant {name} does not start with {enum_prefix}")
            text = name[len(enum_prefix):].lower()
            mapping[name] = text

        return mapping

    def __get_description_list(self, start_marker, pattern, end_marker):
        parser = InlineListParser(self.reader)
        parser.search_block(start_marker)
        return parser.parse(pattern, end_marker)

    def get_rst_list(self, block_name):
        """
        Search for and parse a list of type names from RST documentation, for
        example:

             |       *TYPE* := {
             |               **socket** | **kprobe** |
             |               **kretprobe**
             |       }

        Return a set containing all type names, for example:

            {'socket', 'kprobe', 'kretprobe'}

        @block_name: name of the blog to parse, 'TYPE' in the example
        """
        start_marker = re.compile(f'\*{block_name}\* := {{')
        pattern = re.compile('\*\*([\w/-]+)\*\*')
        end_marker = re.compile('}\n')
        return self.__get_description_list(start_marker, pattern, end_marker)

    def get_help_list(self, block_name):
        """
        Search for and parse a list of type names from a help message in
        bpftool, for example:

            "       TYPE := { socket | kprobe |\\n"
            "               kretprobe }\\n"

        Return a set containing all type names, for example:

            {'socket', 'kprobe', 'kretprobe'}

        @block_name: name of the blog to parse, 'TYPE' in the example
        """
        start_marker = re.compile(f'"\s*{block_name} := {{')
        pattern = re.compile('([\w/]+) [|}]')
        end_marker = re.compile('}')
        return self.__get_description_list(start_marker, pattern, end_marker)

    def get_help_list_macro(self, macro):
        """
        Search for and parse a list of values from a help message starting with
        a macro in bpftool, for example:

            "       " HELP_SPEC_OPTIONS " |\\n"
            "                    {-f|--bpffs} | {-m|--mapcompat} | {-n|--nomount} }\\n"

        Return a set containing all item names, for example:

            {'-f', '--bpffs', '-m', '--mapcompat', '-n', '--nomount'}

        @macro: macro starting the block, 'HELP_SPEC_OPTIONS' in the example
        """
        start_marker = re.compile(f'"\s*{macro}\s*" [|}}]')
        pattern = re.compile('([\w-]+) ?(?:\||}[ }\]])')
        end_marker = re.compile('}\\\\n')
        return self.__get_description_list(start_marker, pattern, end_marker)

    def get_bashcomp_list(self, block_name):
        """
        Search for and parse a list of type names from a variable in bash
        completion file, for example:

            local BPFTOOL_PROG_LOAD_TYPES='socket kprobe \\
                kretprobe'

        Return a set containing all type names, for example:

            {'socket', 'kprobe', 'kretprobe'}

        @block_name: name of the blog to parse, 'TYPE' in the example
        """
        start_marker = re.compile(f'local {block_name}=\'')
        pattern = re.compile('(?:.*=\')?([\w/]+)')
        end_marker = re.compile('\'$')
        return self.__get_description_list(start_marker, pattern, end_marker)

class SourceFileExtractor(FileExtractor):
    """
    An abstract extractor for a source file with usage message.
    This class does not offer a way to set a filename, which is expected to be
    defined in children classes.
    """
    def get_options(self):
        return self.get_help_list_macro('HELP_SPEC_OPTIONS')

class MainHeaderFileExtractor(SourceFileExtractor):
    """
    An extractor for bpftool's main.h
    """
    filename = os.path.join(BPFTOOL_DIR, 'main.h')

    def get_common_options(self):
        """
        Parse the list of common options in main.h (options that apply to all
        commands), which looks to the lists of options in other source files
        but has different start and end markers:

            "OPTIONS := { {-j|--json} [{-p|--pretty}] | {-d|--debug}"

        Return a set containing all options, such as:

            {'-p', '-d', '--pretty', '--debug', '--json', '-j'}
        """
        start_marker = re.compile(f'"OPTIONS :=')
        pattern = re.compile('([\w-]+) ?(?:\||}[ }\]"])')
        end_marker = re.compile('#define')

        parser = InlineListParser(self.reader)
        parser.search_block(start_marker)
        return parser.parse(pattern, end_marker)

class ManSubstitutionsExtractor(SourceFileExtractor):
    """
    An extractor for substitutions.rst
    """
    filename = os.path.join(BPFTOOL_DOC_DIR, 'substitutions.rst')

    def get_common_options(self):
        """
        Parse the list of common options in substitutions.rst (options that
        apply to all commands).

        Return a set containing all options, such as:

            {'-p', '-d', '--pretty', '--debug', '--json', '-j'}
        """
        start_marker = re.compile('\|COMMON_OPTIONS\| replace:: {')
        pattern = re.compile('\*\*([\w/-]+)\*\*')
        end_marker = re.compile('}$')

        parser = InlineListParser(self.reader)
        parser.search_block(start_marker)
        return parser.parse(pattern, end_marker)

class ProgFileExtractor(SourceFileExtractor):
    """
    An extractor for bpftool's prog.c.
    """
    filename = os.path.join(BPFTOOL_DIR, 'prog.c')

    def get_attach_types(self):
        types = self.get_types_from_array('attach_types')
        return self.make_enum_map(types, 'BPF_')

    def get_prog_attach_help(self):
        return self.get_help_list('ATTACH_TYPE')

class MapFileExtractor(SourceFileExtractor):
    """
    An extractor for bpftool's map.c.
    """
    filename = os.path.join(BPFTOOL_DIR, 'map.c')

    def get_map_help(self):
        return self.get_help_list('TYPE')

class CgroupFileExtractor(SourceFileExtractor):
    """
    An extractor for bpftool's cgroup.c.
    """
    filename = os.path.join(BPFTOOL_DIR, 'cgroup.c')

    def get_prog_attach_help(self):
        return self.get_help_list('ATTACH_TYPE')

class GenericSourceExtractor(SourceFileExtractor):
    """
    An extractor for generic source code files.
    """
    filename = ""

    def __init__(self, filename):
        self.filename = os.path.join(BPFTOOL_DIR, filename)
        super().__init__()

class BpfHeaderExtractor(FileExtractor):
    """
    An extractor for the UAPI BPF header.
    """
    filename = os.path.join(INCLUDE_DIR, 'uapi/linux/bpf.h')

    def __init__(self):
        super().__init__()
        self.attach_types = {}

    def get_prog_types(self):
        return self.get_enum('bpf_prog_type')

    def get_map_type_map(self):
        names = self.get_enum('bpf_map_type')
        return self.make_enum_map(names, 'BPF_MAP_TYPE_')

    def get_attach_type_map(self):
        if not self.attach_types:
          names = self.get_enum('bpf_attach_type')
          self.attach_types = self.make_enum_map(names, 'BPF_')
        return self.attach_types

    def get_cgroup_attach_type_map(self):
        if not self.attach_types:
            self.get_attach_type_map()
        return {name: text for name, text in self.attach_types.items()
            if name.startswith('BPF_CGROUP')}

class ManPageExtractor(FileExtractor):
    """
    An abstract extractor for an RST documentation page.
    This class does not offer a way to set a filename, which is expected to be
    defined in children classes.
    """
    def get_options(self):
        return self.get_rst_list('OPTIONS')

class ManProgExtractor(ManPageExtractor):
    """
    An extractor for bpftool-prog.rst.
    """
    filename = os.path.join(BPFTOOL_DOC_DIR, 'bpftool-prog.rst')

    def get_attach_types(self):
        return self.get_rst_list('ATTACH_TYPE')

class ManMapExtractor(ManPageExtractor):
    """
    An extractor for bpftool-map.rst.
    """
    filename = os.path.join(BPFTOOL_DOC_DIR, 'bpftool-map.rst')

    def get_map_types(self):
        return self.get_rst_list('TYPE')

class ManCgroupExtractor(ManPageExtractor):
    """
    An extractor for bpftool-cgroup.rst.
    """
    filename = os.path.join(BPFTOOL_DOC_DIR, 'bpftool-cgroup.rst')

    def get_attach_types(self):
        return self.get_rst_list('ATTACH_TYPE')

class ManGenericExtractor(ManPageExtractor):
    """
    An extractor for generic RST documentation pages.
    """
    filename = ""

    def __init__(self, filename):
        self.filename = os.path.join(BPFTOOL_DIR, filename)
        super().__init__()

class BashcompExtractor(FileExtractor):
    """
    An extractor for bpftool's bash completion file.
    """
    filename = os.path.join(BPFTOOL_BASHCOMP_DIR, 'bpftool')

    def get_prog_attach_types(self):
        return self.get_bashcomp_list('BPFTOOL_PROG_ATTACH_TYPES')

def verify(first_set, second_set, message):
    """
    Print all values that differ between two sets.
    @first_set: one set to compare
    @second_set: another set to compare
    @message: message to print for values belonging to only one of the sets
    """
    global retval
    diff = first_set.symmetric_difference(second_set)
    if diff:
        print(message, diff)
        retval = 1

def main():
    # No arguments supported at this time, but print usage for -h|--help
    argParser = argparse.ArgumentParser(description="""
    Verify that bpftool's code, help messages, documentation and bash
    completion are all in sync on program types, map types, attach types, and
    options. Also check that bpftool is in sync with the UAPI BPF header.
    """)
    args = argParser.parse_args()

    bpf_info = BpfHeaderExtractor()

    # Map types (names)

    map_info = MapFileExtractor()
    source_map_types = set(bpf_info.get_map_type_map().values())
    source_map_types.discard('unspec')

    # BPF_MAP_TYPE_CGROUP_STORAGE_DEPRECATED and BPF_MAP_TYPE_CGROUP_STORAGE
    # share the same enum value and source_map_types picks
    # BPF_MAP_TYPE_CGROUP_STORAGE_DEPRECATED/cgroup_storage_deprecated.
    # Replace 'cgroup_storage_deprecated' with 'cgroup_storage'
    # so it aligns with what `bpftool map help` shows.
    source_map_types.remove('cgroup_storage_deprecated')
    source_map_types.add('cgroup_storage')

    # The same applied to BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE_DEPRECATED and
    # BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE which share the same enum value
    # and source_map_types picks
    # BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE_DEPRECATED/percpu_cgroup_storage_deprecated.
    # Replace 'percpu_cgroup_storage_deprecated' with 'percpu_cgroup_storage'
    # so it aligns with what `bpftool map help` shows.
    source_map_types.remove('percpu_cgroup_storage_deprecated')
    source_map_types.add('percpu_cgroup_storage')

    help_map_types = map_info.get_map_help()
    help_map_options = map_info.get_options()
    map_info.close()

    man_map_info = ManMapExtractor()
    man_map_options = man_map_info.get_options()
    man_map_types = man_map_info.get_map_types()
    man_map_info.close()

    verify(source_map_types, help_map_types,
            f'Comparing {BpfHeaderExtractor.filename} (bpf_map_type) and {MapFileExtractor.filename} (do_help() TYPE):')
    verify(source_map_types, man_map_types,
            f'Comparing {BpfHeaderExtractor.filename} (bpf_map_type) and {ManMapExtractor.filename} (TYPE):')
    verify(help_map_options, man_map_options,
            f'Comparing {MapFileExtractor.filename} (do_help() OPTIONS) and {ManMapExtractor.filename} (OPTIONS):')

    # Attach types (names)

    prog_info = ProgFileExtractor()
    source_prog_attach_types = set(prog_info.get_attach_types().values())

    help_prog_attach_types = prog_info.get_prog_attach_help()
    help_prog_options = prog_info.get_options()
    prog_info.close()

    man_prog_info = ManProgExtractor()
    man_prog_options = man_prog_info.get_options()
    man_prog_attach_types = man_prog_info.get_attach_types()
    man_prog_info.close()


    bashcomp_info = BashcompExtractor()
    bashcomp_prog_attach_types = bashcomp_info.get_prog_attach_types()
    bashcomp_info.close()

    verify(source_prog_attach_types, help_prog_attach_types,
            f'Comparing {ProgFileExtractor.filename} (bpf_attach_type) and {ProgFileExtractor.filename} (do_help() ATTACH_TYPE):')
    verify(source_prog_attach_types, man_prog_attach_types,
            f'Comparing {ProgFileExtractor.filename} (bpf_attach_type) and {ManProgExtractor.filename} (ATTACH_TYPE):')
    verify(help_prog_options, man_prog_options,
            f'Comparing {ProgFileExtractor.filename} (do_help() OPTIONS) and {ManProgExtractor.filename} (OPTIONS):')
    verify(source_prog_attach_types, bashcomp_prog_attach_types,
            f'Comparing {ProgFileExtractor.filename} (bpf_attach_type) and {BashcompExtractor.filename} (BPFTOOL_PROG_ATTACH_TYPES):')

    # Cgroup attach types
    source_cgroup_attach_types = set(bpf_info.get_cgroup_attach_type_map().values())
    bpf_info.close()

    cgroup_info = CgroupFileExtractor()
    help_cgroup_attach_types = cgroup_info.get_prog_attach_help()
    help_cgroup_options = cgroup_info.get_options()
    cgroup_info.close()

    man_cgroup_info = ManCgroupExtractor()
    man_cgroup_options = man_cgroup_info.get_options()
    man_cgroup_attach_types = man_cgroup_info.get_attach_types()
    man_cgroup_info.close()

    verify(source_cgroup_attach_types, help_cgroup_attach_types,
            f'Comparing {BpfHeaderExtractor.filename} (bpf_attach_type) and {CgroupFileExtractor.filename} (do_help() ATTACH_TYPE):')
    verify(source_cgroup_attach_types, man_cgroup_attach_types,
            f'Comparing {BpfHeaderExtractor.filename} (bpf_attach_type) and {ManCgroupExtractor.filename} (ATTACH_TYPE):')
    verify(help_cgroup_options, man_cgroup_options,
            f'Comparing {CgroupFileExtractor.filename} (do_help() OPTIONS) and {ManCgroupExtractor.filename} (OPTIONS):')

    # Options for remaining commands

    for cmd in [ 'btf', 'feature', 'gen', 'iter', 'link', 'net', 'perf', 'struct_ops', ]:
        source_info = GenericSourceExtractor(cmd + '.c')
        help_cmd_options = source_info.get_options()
        source_info.close()

        man_cmd_info = ManGenericExtractor(os.path.join(BPFTOOL_DOC_DIR, 'bpftool-' + cmd + '.rst'))
        man_cmd_options = man_cmd_info.get_options()
        man_cmd_info.close()

        verify(help_cmd_options, man_cmd_options,
                f'Comparing {source_info.filename} (do_help() OPTIONS) and {man_cmd_info.filename} (OPTIONS):')

    source_main_info = GenericSourceExtractor('main.c')
    help_main_options = source_main_info.get_options()
    source_main_info.close()

    man_main_info = ManGenericExtractor(os.path.join(BPFTOOL_DOC_DIR, 'bpftool.rst'))
    man_main_options = man_main_info.get_options()
    man_main_info.close()

    verify(help_main_options, man_main_options,
            f'Comparing {source_main_info.filename} (do_help() OPTIONS) and {man_main_info.filename} (OPTIONS):')

    # Compare common options (options that apply to all commands)

    main_hdr_info = MainHeaderFileExtractor()
    source_common_options = main_hdr_info.get_common_options()
    main_hdr_info.close()

    man_substitutions = ManSubstitutionsExtractor()
    man_common_options = man_substitutions.get_common_options()
    man_substitutions.close()

    verify(source_common_options, man_common_options,
            f'Comparing common options from {main_hdr_info.filename} (HELP_SPEC_OPTIONS) and {man_substitutions.filename}:')

    sys.exit(retval)

if __name__ == "__main__":
    main()
