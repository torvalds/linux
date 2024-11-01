#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2018-2019 Netronome Systems, Inc.
# Copyright (C) 2021 Isovalent, Inc.

# In case user attempts to run with Python 2.
from __future__ import print_function

import argparse
import re
import sys, os
import subprocess

helpersDocStart = 'Start of BPF helper function descriptions:'

class NoHelperFound(BaseException):
    pass

class NoSyscallCommandFound(BaseException):
    pass

class ParsingError(BaseException):
    def __init__(self, line='<line not provided>', reader=None):
        if reader:
            BaseException.__init__(self,
                                   'Error at file offset %d, parsing line: %s' %
                                   (reader.tell(), line))
        else:
            BaseException.__init__(self, 'Error parsing line: %s' % line)


class APIElement(object):
    """
    An object representing the description of an aspect of the eBPF API.
    @proto: prototype of the API symbol
    @desc: textual description of the symbol
    @ret: (optional) description of any associated return value
    """
    def __init__(self, proto='', desc='', ret=''):
        self.proto = proto
        self.desc = desc
        self.ret = ret


class Helper(APIElement):
    """
    An object representing the description of an eBPF helper function.
    @proto: function prototype of the helper function
    @desc: textual description of the helper function
    @ret: description of the return value of the helper function
    """
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.enum_val = None

    def proto_break_down(self):
        """
        Break down helper function protocol into smaller chunks: return type,
        name, distincts arguments.
        """
        arg_re = re.compile('((\w+ )*?(\w+|...))( (\**)(\w+))?$')
        res = {}
        proto_re = re.compile('(.+) (\**)(\w+)\(((([^,]+)(, )?){1,5})\)$')

        capture = proto_re.match(self.proto)
        res['ret_type'] = capture.group(1)
        res['ret_star'] = capture.group(2)
        res['name']     = capture.group(3)
        res['args'] = []

        args    = capture.group(4).split(', ')
        for a in args:
            capture = arg_re.match(a)
            res['args'].append({
                'type' : capture.group(1),
                'star' : capture.group(5),
                'name' : capture.group(6)
            })

        return res


class HeaderParser(object):
    """
    An object used to parse a file in order to extract the documentation of a
    list of eBPF helper functions. All the helpers that can be retrieved are
    stored as Helper object, in the self.helpers() array.
    @filename: name of file to parse, usually include/uapi/linux/bpf.h in the
               kernel tree
    """
    def __init__(self, filename):
        self.reader = open(filename, 'r')
        self.line = ''
        self.helpers = []
        self.commands = []
        self.desc_unique_helpers = set()
        self.define_unique_helpers = []
        self.helper_enum_vals = {}
        self.desc_syscalls = []
        self.enum_syscalls = []

    def parse_element(self):
        proto    = self.parse_symbol()
        desc     = self.parse_desc(proto)
        ret      = self.parse_ret(proto)
        return APIElement(proto=proto, desc=desc, ret=ret)

    def parse_helper(self):
        proto    = self.parse_proto()
        desc     = self.parse_desc(proto)
        ret      = self.parse_ret(proto)
        return Helper(proto=proto, desc=desc, ret=ret)

    def parse_symbol(self):
        p = re.compile(' \* ?(BPF\w+)$')
        capture = p.match(self.line)
        if not capture:
            raise NoSyscallCommandFound
        end_re = re.compile(' \* ?NOTES$')
        end = end_re.match(self.line)
        if end:
            raise NoSyscallCommandFound
        self.line = self.reader.readline()
        return capture.group(1)

    def parse_proto(self):
        # Argument can be of shape:
        #   - "void"
        #   - "type  name"
        #   - "type *name"
        #   - Same as above, with "const" and/or "struct" in front of type
        #   - "..." (undefined number of arguments, for bpf_trace_printk())
        # There is at least one term ("void"), and at most five arguments.
        p = re.compile(' \* ?((.+) \**\w+\((((const )?(struct )?(\w+|\.\.\.)( \**\w+)?)(, )?){1,5}\))$')
        capture = p.match(self.line)
        if not capture:
            raise NoHelperFound
        self.line = self.reader.readline()
        return capture.group(1)

    def parse_desc(self, proto):
        p = re.compile(' \* ?(?:\t| {5,8})Description$')
        capture = p.match(self.line)
        if not capture:
            raise Exception("No description section found for " + proto)
        # Description can be several lines, some of them possibly empty, and it
        # stops when another subsection title is met.
        desc = ''
        desc_present = False
        while True:
            self.line = self.reader.readline()
            if self.line == ' *\n':
                desc += '\n'
            else:
                p = re.compile(' \* ?(?:\t| {5,8})(?:\t| {8})(.*)')
                capture = p.match(self.line)
                if capture:
                    desc_present = True
                    desc += capture.group(1) + '\n'
                else:
                    break

        if not desc_present:
            raise Exception("No description found for " + proto)
        return desc

    def parse_ret(self, proto):
        p = re.compile(' \* ?(?:\t| {5,8})Return$')
        capture = p.match(self.line)
        if not capture:
            raise Exception("No return section found for " + proto)
        # Return value description can be several lines, some of them possibly
        # empty, and it stops when another subsection title is met.
        ret = ''
        ret_present = False
        while True:
            self.line = self.reader.readline()
            if self.line == ' *\n':
                ret += '\n'
            else:
                p = re.compile(' \* ?(?:\t| {5,8})(?:\t| {8})(.*)')
                capture = p.match(self.line)
                if capture:
                    ret_present = True
                    ret += capture.group(1) + '\n'
                else:
                    break

        if not ret_present:
            raise Exception("No return found for " + proto)
        return ret

    def seek_to(self, target, help_message, discard_lines = 1):
        self.reader.seek(0)
        offset = self.reader.read().find(target)
        if offset == -1:
            raise Exception(help_message)
        self.reader.seek(offset)
        self.reader.readline()
        for _ in range(discard_lines):
            self.reader.readline()
        self.line = self.reader.readline()

    def parse_desc_syscall(self):
        self.seek_to('* DOC: eBPF Syscall Commands',
                     'Could not find start of eBPF syscall descriptions list')
        while True:
            try:
                command = self.parse_element()
                self.commands.append(command)
                self.desc_syscalls.append(command.proto)

            except NoSyscallCommandFound:
                break

    def parse_enum_syscall(self):
        self.seek_to('enum bpf_cmd {',
                     'Could not find start of bpf_cmd enum', 0)
        # Searches for either one or more BPF\w+ enums
        bpf_p = re.compile('\s*(BPF\w+)+')
        # Searches for an enum entry assigned to another entry,
        # for e.g. BPF_PROG_RUN = BPF_PROG_TEST_RUN, which is
        # not documented hence should be skipped in check to
        # determine if the right number of syscalls are documented
        assign_p = re.compile('\s*(BPF\w+)\s*=\s*(BPF\w+)')
        bpf_cmd_str = ''
        while True:
            capture = assign_p.match(self.line)
            if capture:
                # Skip line if an enum entry is assigned to another entry
                self.line = self.reader.readline()
                continue
            capture = bpf_p.match(self.line)
            if capture:
                bpf_cmd_str += self.line
            else:
                break
            self.line = self.reader.readline()
        # Find the number of occurences of BPF\w+
        self.enum_syscalls = re.findall('(BPF\w+)+', bpf_cmd_str)

    def parse_desc_helpers(self):
        self.seek_to(helpersDocStart,
                     'Could not find start of eBPF helper descriptions list')
        while True:
            try:
                helper = self.parse_helper()
                self.helpers.append(helper)
                proto = helper.proto_break_down()
                self.desc_unique_helpers.add(proto['name'])
            except NoHelperFound:
                break

    def parse_define_helpers(self):
        # Parse FN(...) in #define __BPF_FUNC_MAPPER to compare later with the
        # number of unique function names present in description and use the
        # correct enumeration value.
        # Note: seek_to(..) discards the first line below the target search text,
        # resulting in FN(unspec) being skipped and not added to self.define_unique_helpers.
        self.seek_to('#define __BPF_FUNC_MAPPER(FN)',
                     'Could not find start of eBPF helper definition list')
        # Searches for one FN(\w+) define or a backslash for newline
        p = re.compile('\s*FN\((\w+)\)|\\\\')
        fn_defines_str = ''
        i = 1  # 'unspec' is skipped as mentioned above
        while True:
            capture = p.match(self.line)
            if capture:
                fn_defines_str += self.line
                self.helper_enum_vals[capture.expand(r'bpf_\1')] = i
                i += 1
            else:
                break
            self.line = self.reader.readline()
        # Find the number of occurences of FN(\w+)
        self.define_unique_helpers = re.findall('FN\(\w+\)', fn_defines_str)

    def assign_helper_values(self):
        seen_helpers = set()
        for helper in self.helpers:
            proto = helper.proto_break_down()
            name = proto['name']
            try:
                enum_val = self.helper_enum_vals[name]
            except KeyError:
                raise Exception("Helper %s is missing from enum bpf_func_id" % name)

            # Enforce current practice of having the descriptions ordered
            # by enum value.
            seen_helpers.add(name)
            desc_val = len(seen_helpers)
            if desc_val != enum_val:
                raise Exception("Helper %s comment order (#%d) must be aligned with its position (#%d) in enum bpf_func_id" % (name, desc_val, enum_val))

            helper.enum_val = enum_val

    def run(self):
        self.parse_desc_syscall()
        self.parse_enum_syscall()
        self.parse_desc_helpers()
        self.parse_define_helpers()
        self.assign_helper_values()
        self.reader.close()

###############################################################################

class Printer(object):
    """
    A generic class for printers. Printers should be created with an array of
    Helper objects, and implement a way to print them in the desired fashion.
    @parser: A HeaderParser with objects to print to standard output
    """
    def __init__(self, parser):
        self.parser = parser
        self.elements = []

    def print_header(self):
        pass

    def print_footer(self):
        pass

    def print_one(self, helper):
        pass

    def print_all(self):
        self.print_header()
        for elem in self.elements:
            self.print_one(elem)
        self.print_footer()

    def elem_number_check(self, desc_unique_elem, define_unique_elem, type, instance):
        """
        Checks the number of helpers/syscalls documented within the header file
        description with those defined as part of enum/macro and raise an
        Exception if they don't match.
        """
        nr_desc_unique_elem = len(desc_unique_elem)
        nr_define_unique_elem = len(define_unique_elem)
        if nr_desc_unique_elem != nr_define_unique_elem:
            exception_msg = '''
The number of unique %s in description (%d) doesn\'t match the number of unique %s defined in %s (%d)
''' % (type, nr_desc_unique_elem, type, instance, nr_define_unique_elem)
            if nr_desc_unique_elem < nr_define_unique_elem:
                # Function description is parsed until no helper is found (which can be due to
                # misformatting). Hence, only print the first missing/misformatted helper/enum.
                exception_msg += '''
The description for %s is not present or formatted correctly.
''' % (define_unique_elem[nr_desc_unique_elem])
            raise Exception(exception_msg)

class PrinterRST(Printer):
    """
    A generic class for printers that print ReStructured Text. Printers should
    be created with a HeaderParser object, and implement a way to print API
    elements in the desired fashion.
    @parser: A HeaderParser with objects to print to standard output
    """
    def __init__(self, parser):
        self.parser = parser

    def print_license(self):
        license = '''\
.. Copyright (C) All BPF authors and contributors from 2014 to present.
.. See git log include/uapi/linux/bpf.h in kernel tree for details.
.. 
.. SPDX-License-Identifier:  Linux-man-pages-copyleft
.. 
.. Please do not edit this file. It was generated from the documentation
.. located in file include/uapi/linux/bpf.h of the Linux kernel sources
.. (helpers description), and from scripts/bpf_doc.py in the same
.. repository (header and footer).
'''
        print(license)

    def print_elem(self, elem):
        if (elem.desc):
            print('\tDescription')
            # Do not strip all newline characters: formatted code at the end of
            # a section must be followed by a blank line.
            for line in re.sub('\n$', '', elem.desc, count=1).split('\n'):
                print('{}{}'.format('\t\t' if line else '', line))

        if (elem.ret):
            print('\tReturn')
            for line in elem.ret.rstrip().split('\n'):
                print('{}{}'.format('\t\t' if line else '', line))

        print('')

    def get_kernel_version(self):
        try:
            version = subprocess.run(['git', 'describe'], cwd=linuxRoot,
                                     capture_output=True, check=True)
            version = version.stdout.decode().rstrip()
        except:
            try:
                version = subprocess.run(['make', '-s', '--no-print-directory', 'kernelversion'],
                                         cwd=linuxRoot, capture_output=True, check=True)
                version = version.stdout.decode().rstrip()
            except:
                return 'Linux'
        return 'Linux {version}'.format(version=version)

    def get_last_doc_update(self, delimiter):
        try:
            cmd = ['git', 'log', '-1', '--pretty=format:%cs', '--no-patch',
                   '-L',
                   '/{}/,/\*\//:include/uapi/linux/bpf.h'.format(delimiter)]
            date = subprocess.run(cmd, cwd=linuxRoot,
                                  capture_output=True, check=True)
            return date.stdout.decode().rstrip()
        except:
            return ''

class PrinterHelpersRST(PrinterRST):
    """
    A printer for dumping collected information about helpers as a ReStructured
    Text page compatible with the rst2man program, which can be used to
    generate a manual page for the helpers.
    @parser: A HeaderParser with Helper objects to print to standard output
    """
    def __init__(self, parser):
        self.elements = parser.helpers
        self.elem_number_check(parser.desc_unique_helpers, parser.define_unique_helpers, 'helper', '__BPF_FUNC_MAPPER')

    def print_header(self):
        header = '''\
===========
BPF-HELPERS
===========
-------------------------------------------------------------------------------
list of eBPF helper functions
-------------------------------------------------------------------------------

:Manual section: 7
:Version: {version}
{date_field}{date}

DESCRIPTION
===========

The extended Berkeley Packet Filter (eBPF) subsystem consists in programs
written in a pseudo-assembly language, then attached to one of the several
kernel hooks and run in reaction of specific events. This framework differs
from the older, "classic" BPF (or "cBPF") in several aspects, one of them being
the ability to call special functions (or "helpers") from within a program.
These functions are restricted to a white-list of helpers defined in the
kernel.

These helpers are used by eBPF programs to interact with the system, or with
the context in which they work. For instance, they can be used to print
debugging messages, to get the time since the system was booted, to interact
with eBPF maps, or to manipulate network packets. Since there are several eBPF
program types, and that they do not run in the same context, each program type
can only call a subset of those helpers.

Due to eBPF conventions, a helper can not have more than five arguments.

Internally, eBPF programs call directly into the compiled helper functions
without requiring any foreign-function interface. As a result, calling helpers
introduces no overhead, thus offering excellent performance.

This document is an attempt to list and document the helpers available to eBPF
developers. They are sorted by chronological order (the oldest helpers in the
kernel at the top).

HELPERS
=======
'''
        kernelVersion = self.get_kernel_version()
        lastUpdate = self.get_last_doc_update(helpersDocStart)

        PrinterRST.print_license(self)
        print(header.format(version=kernelVersion,
                            date_field = ':Date: ' if lastUpdate else '',
                            date=lastUpdate))

    def print_footer(self):
        footer = '''
EXAMPLES
========

Example usage for most of the eBPF helpers listed in this manual page are
available within the Linux kernel sources, at the following locations:

* *samples/bpf/*
* *tools/testing/selftests/bpf/*

LICENSE
=======

eBPF programs can have an associated license, passed along with the bytecode
instructions to the kernel when the programs are loaded. The format for that
string is identical to the one in use for kernel modules (Dual licenses, such
as "Dual BSD/GPL", may be used). Some helper functions are only accessible to
programs that are compatible with the GNU General Public License (GNU GPL).

In order to use such helpers, the eBPF program must be loaded with the correct
license string passed (via **attr**) to the **bpf**\ () system call, and this
generally translates into the C source code of the program containing a line
similar to the following:

::

	char ____license[] __attribute__((section("license"), used)) = "GPL";

IMPLEMENTATION
==============

This manual page is an effort to document the existing eBPF helper functions.
But as of this writing, the BPF sub-system is under heavy development. New eBPF
program or map types are added, along with new helper functions. Some helpers
are occasionally made available for additional program types. So in spite of
the efforts of the community, this page might not be up-to-date. If you want to
check by yourself what helper functions exist in your kernel, or what types of
programs they can support, here are some files among the kernel tree that you
may be interested in:

* *include/uapi/linux/bpf.h* is the main BPF header. It contains the full list
  of all helper functions, as well as many other BPF definitions including most
  of the flags, structs or constants used by the helpers.
* *net/core/filter.c* contains the definition of most network-related helper
  functions, and the list of program types from which they can be used.
* *kernel/trace/bpf_trace.c* is the equivalent for most tracing program-related
  helpers.
* *kernel/bpf/verifier.c* contains the functions used to check that valid types
  of eBPF maps are used with a given helper function.
* *kernel/bpf/* directory contains other files in which additional helpers are
  defined (for cgroups, sockmaps, etc.).
* The bpftool utility can be used to probe the availability of helper functions
  on the system (as well as supported program and map types, and a number of
  other parameters). To do so, run **bpftool feature probe** (see
  **bpftool-feature**\ (8) for details). Add the **unprivileged** keyword to
  list features available to unprivileged users.

Compatibility between helper functions and program types can generally be found
in the files where helper functions are defined. Look for the **struct
bpf_func_proto** objects and for functions returning them: these functions
contain a list of helpers that a given program type can call. Note that the
**default:** label of the **switch ... case** used to filter helpers can call
other functions, themselves allowing access to additional helpers. The
requirement for GPL license is also in those **struct bpf_func_proto**.

Compatibility between helper functions and map types can be found in the
**check_map_func_compatibility**\ () function in file *kernel/bpf/verifier.c*.

Helper functions that invalidate the checks on **data** and **data_end**
pointers for network processing are listed in function
**bpf_helper_changes_pkt_data**\ () in file *net/core/filter.c*.

SEE ALSO
========

**bpf**\ (2),
**bpftool**\ (8),
**cgroups**\ (7),
**ip**\ (8),
**perf_event_open**\ (2),
**sendmsg**\ (2),
**socket**\ (7),
**tc-bpf**\ (8)'''
        print(footer)

    def print_proto(self, helper):
        """
        Format function protocol with bold and italics markers. This makes RST
        file less readable, but gives nice results in the manual page.
        """
        proto = helper.proto_break_down()

        print('**%s %s%s(' % (proto['ret_type'],
                              proto['ret_star'].replace('*', '\\*'),
                              proto['name']),
              end='')

        comma = ''
        for a in proto['args']:
            one_arg = '{}{}'.format(comma, a['type'])
            if a['name']:
                if a['star']:
                    one_arg += ' {}**\ '.format(a['star'].replace('*', '\\*'))
                else:
                    one_arg += '** '
                one_arg += '*{}*\\ **'.format(a['name'])
            comma = ', '
            print(one_arg, end='')

        print(')**')

    def print_one(self, helper):
        self.print_proto(helper)
        self.print_elem(helper)


class PrinterSyscallRST(PrinterRST):
    """
    A printer for dumping collected information about the syscall API as a
    ReStructured Text page compatible with the rst2man program, which can be
    used to generate a manual page for the syscall.
    @parser: A HeaderParser with APIElement objects to print to standard
             output
    """
    def __init__(self, parser):
        self.elements = parser.commands
        self.elem_number_check(parser.desc_syscalls, parser.enum_syscalls, 'syscall', 'bpf_cmd')

    def print_header(self):
        header = '''\
===
bpf
===
-------------------------------------------------------------------------------
Perform a command on an extended BPF object
-------------------------------------------------------------------------------

:Manual section: 2

COMMANDS
========
'''
        PrinterRST.print_license(self)
        print(header)

    def print_one(self, command):
        print('**%s**' % (command.proto))
        self.print_elem(command)


class PrinterHelpers(Printer):
    """
    A printer for dumping collected information about helpers as C header to
    be included from BPF program.
    @parser: A HeaderParser with Helper objects to print to standard output
    """
    def __init__(self, parser):
        self.elements = parser.helpers
        self.elem_number_check(parser.desc_unique_helpers, parser.define_unique_helpers, 'helper', '__BPF_FUNC_MAPPER')

    type_fwds = [
            'struct bpf_fib_lookup',
            'struct bpf_sk_lookup',
            'struct bpf_perf_event_data',
            'struct bpf_perf_event_value',
            'struct bpf_pidns_info',
            'struct bpf_redir_neigh',
            'struct bpf_sock',
            'struct bpf_sock_addr',
            'struct bpf_sock_ops',
            'struct bpf_sock_tuple',
            'struct bpf_spin_lock',
            'struct bpf_sysctl',
            'struct bpf_tcp_sock',
            'struct bpf_tunnel_key',
            'struct bpf_xfrm_state',
            'struct linux_binprm',
            'struct pt_regs',
            'struct sk_reuseport_md',
            'struct sockaddr',
            'struct tcphdr',
            'struct seq_file',
            'struct tcp6_sock',
            'struct tcp_sock',
            'struct tcp_timewait_sock',
            'struct tcp_request_sock',
            'struct udp6_sock',
            'struct unix_sock',
            'struct task_struct',

            'struct __sk_buff',
            'struct sk_msg_md',
            'struct xdp_md',
            'struct path',
            'struct btf_ptr',
            'struct inode',
            'struct socket',
            'struct file',
            'struct bpf_timer',
            'struct mptcp_sock',
            'struct bpf_dynptr',
            'struct iphdr',
            'struct ipv6hdr',
    ]
    known_types = {
            '...',
            'void',
            'const void',
            'char',
            'const char',
            'int',
            'long',
            'unsigned long',

            '__be16',
            '__be32',
            '__wsum',

            'struct bpf_fib_lookup',
            'struct bpf_perf_event_data',
            'struct bpf_perf_event_value',
            'struct bpf_pidns_info',
            'struct bpf_redir_neigh',
            'struct bpf_sk_lookup',
            'struct bpf_sock',
            'struct bpf_sock_addr',
            'struct bpf_sock_ops',
            'struct bpf_sock_tuple',
            'struct bpf_spin_lock',
            'struct bpf_sysctl',
            'struct bpf_tcp_sock',
            'struct bpf_tunnel_key',
            'struct bpf_xfrm_state',
            'struct linux_binprm',
            'struct pt_regs',
            'struct sk_reuseport_md',
            'struct sockaddr',
            'struct tcphdr',
            'struct seq_file',
            'struct tcp6_sock',
            'struct tcp_sock',
            'struct tcp_timewait_sock',
            'struct tcp_request_sock',
            'struct udp6_sock',
            'struct unix_sock',
            'struct task_struct',
            'struct path',
            'struct btf_ptr',
            'struct inode',
            'struct socket',
            'struct file',
            'struct bpf_timer',
            'struct mptcp_sock',
            'struct bpf_dynptr',
            'struct iphdr',
            'struct ipv6hdr',
    }
    mapped_types = {
            'u8': '__u8',
            'u16': '__u16',
            'u32': '__u32',
            'u64': '__u64',
            's8': '__s8',
            's16': '__s16',
            's32': '__s32',
            's64': '__s64',
            'size_t': 'unsigned long',
            'struct bpf_map': 'void',
            'struct sk_buff': 'struct __sk_buff',
            'const struct sk_buff': 'const struct __sk_buff',
            'struct sk_msg_buff': 'struct sk_msg_md',
            'struct xdp_buff': 'struct xdp_md',
    }
    # Helpers overloaded for different context types.
    overloaded_helpers = [
        'bpf_get_socket_cookie',
        'bpf_sk_assign',
    ]

    def print_header(self):
        header = '''\
/* This is auto-generated file. See bpf_doc.py for details. */

/* Forward declarations of BPF structs */'''

        print(header)
        for fwd in self.type_fwds:
            print('%s;' % fwd)
        print('')

    def print_footer(self):
        footer = ''
        print(footer)

    def map_type(self, t):
        if t in self.known_types:
            return t
        if t in self.mapped_types:
            return self.mapped_types[t]
        print("Unrecognized type '%s', please add it to known types!" % t,
              file=sys.stderr)
        sys.exit(1)

    seen_helpers = set()

    def print_one(self, helper):
        proto = helper.proto_break_down()

        if proto['name'] in self.seen_helpers:
            return
        self.seen_helpers.add(proto['name'])

        print('/*')
        print(" * %s" % proto['name'])
        print(" *")
        if (helper.desc):
            # Do not strip all newline characters: formatted code at the end of
            # a section must be followed by a blank line.
            for line in re.sub('\n$', '', helper.desc, count=1).split('\n'):
                print(' *{}{}'.format(' \t' if line else '', line))

        if (helper.ret):
            print(' *')
            print(' * Returns')
            for line in helper.ret.rstrip().split('\n'):
                print(' *{}{}'.format(' \t' if line else '', line))

        print(' */')
        print('static %s %s(*%s)(' % (self.map_type(proto['ret_type']),
                                      proto['ret_star'], proto['name']), end='')
        comma = ''
        for i, a in enumerate(proto['args']):
            t = a['type']
            n = a['name']
            if proto['name'] in self.overloaded_helpers and i == 0:
                    t = 'void'
                    n = 'ctx'
            one_arg = '{}{}'.format(comma, self.map_type(t))
            if n:
                if a['star']:
                    one_arg += ' {}'.format(a['star'])
                else:
                    one_arg += ' '
                one_arg += '{}'.format(n)
            comma = ', '
            print(one_arg, end='')

        print(') = (void *) %d;' % helper.enum_val)
        print('')

###############################################################################

# If script is launched from scripts/ from kernel tree and can access
# ../include/uapi/linux/bpf.h, use it as a default name for the file to parse,
# otherwise the --filename argument will be required from the command line.
script = os.path.abspath(sys.argv[0])
linuxRoot = os.path.dirname(os.path.dirname(script))
bpfh = os.path.join(linuxRoot, 'include/uapi/linux/bpf.h')

printers = {
        'helpers': PrinterHelpersRST,
        'syscall': PrinterSyscallRST,
}

argParser = argparse.ArgumentParser(description="""
Parse eBPF header file and generate documentation for the eBPF API.
The RST-formatted output produced can be turned into a manual page with the
rst2man utility.
""")
argParser.add_argument('--header', action='store_true',
                       help='generate C header file')
if (os.path.isfile(bpfh)):
    argParser.add_argument('--filename', help='path to include/uapi/linux/bpf.h',
                           default=bpfh)
else:
    argParser.add_argument('--filename', help='path to include/uapi/linux/bpf.h')
argParser.add_argument('target', nargs='?', default='helpers',
                       choices=printers.keys(), help='eBPF API target')
args = argParser.parse_args()

# Parse file.
headerParser = HeaderParser(args.filename)
headerParser.run()

# Print formatted output to standard output.
if args.header:
    if args.target != 'helpers':
        raise NotImplementedError('Only helpers header generation is supported')
    printer = PrinterHelpers(headerParser)
else:
    printer = printers[args.target](headerParser)
printer.print_all()
