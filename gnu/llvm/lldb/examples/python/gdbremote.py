#!/usr/bin/env python

# ----------------------------------------------------------------------
# This module will enable GDB remote packet logging when the
# 'start_gdb_log' command is called with a filename to log to. When the
# 'stop_gdb_log' command is called, it will disable the logging and
# print out statistics about how long commands took to execute and also
# will primnt ou
# Be sure to add the python path that points to the LLDB shared library.
#
# To use this in the embedded python interpreter using "lldb" just
# import it with the full path using the "command script import"
# command. This can be done from the LLDB command line:
#   (lldb) command script import /path/to/gdbremote.py
# Or it can be added to your ~/.lldbinit file so this module is always
# available.
# ----------------------------------------------------------------------

import binascii
import subprocess
import json
import math
import optparse
import os
import re
import shlex
import string
import sys
import tempfile
import xml.etree.ElementTree as ET

# ----------------------------------------------------------------------
# Global variables
# ----------------------------------------------------------------------
g_log_file = ""
g_byte_order = "little"
g_number_regex = re.compile("^(0x[0-9a-fA-F]+|[0-9]+)")
g_thread_id_regex = re.compile("^(-1|[0-9a-fA-F]+|0)")


class TerminalColors:
    """Simple terminal colors class"""

    def __init__(self, enabled=True):
        # TODO: discover terminal type from "file" and disable if
        # it can't handle the color codes
        self.enabled = enabled

    def reset(self):
        """Reset all terminal colors and formatting."""
        if self.enabled:
            return "\x1b[0m"
        return ""

    def bold(self, on=True):
        """Enable or disable bold depending on the "on" parameter."""
        if self.enabled:
            if on:
                return "\x1b[1m"
            else:
                return "\x1b[22m"
        return ""

    def italics(self, on=True):
        """Enable or disable italics depending on the "on" parameter."""
        if self.enabled:
            if on:
                return "\x1b[3m"
            else:
                return "\x1b[23m"
        return ""

    def underline(self, on=True):
        """Enable or disable underline depending on the "on" parameter."""
        if self.enabled:
            if on:
                return "\x1b[4m"
            else:
                return "\x1b[24m"
        return ""

    def inverse(self, on=True):
        """Enable or disable inverse depending on the "on" parameter."""
        if self.enabled:
            if on:
                return "\x1b[7m"
            else:
                return "\x1b[27m"
        return ""

    def strike(self, on=True):
        """Enable or disable strike through depending on the "on" parameter."""
        if self.enabled:
            if on:
                return "\x1b[9m"
            else:
                return "\x1b[29m"
        return ""

    def black(self, fg=True):
        """Set the foreground or background color to black.
        The foreground color will be set if "fg" tests True. The background color will be set if "fg" tests False.
        """
        if self.enabled:
            if fg:
                return "\x1b[30m"
            else:
                return "\x1b[40m"
        return ""

    def red(self, fg=True):
        """Set the foreground or background color to red.
        The foreground color will be set if "fg" tests True. The background color will be set if "fg" tests False.
        """
        if self.enabled:
            if fg:
                return "\x1b[31m"
            else:
                return "\x1b[41m"
        return ""

    def green(self, fg=True):
        """Set the foreground or background color to green.
        The foreground color will be set if "fg" tests True. The background color will be set if "fg" tests False.
        """
        if self.enabled:
            if fg:
                return "\x1b[32m"
            else:
                return "\x1b[42m"
        return ""

    def yellow(self, fg=True):
        """Set the foreground or background color to yellow.
        The foreground color will be set if "fg" tests True. The background color will be set if "fg" tests False.
        """
        if self.enabled:
            if fg:
                return "\x1b[33m"
            else:
                return "\x1b[43m"
        return ""

    def blue(self, fg=True):
        """Set the foreground or background color to blue.
        The foreground color will be set if "fg" tests True. The background color will be set if "fg" tests False.
        """
        if self.enabled:
            if fg:
                return "\x1b[34m"
            else:
                return "\x1b[44m"
        return ""

    def magenta(self, fg=True):
        """Set the foreground or background color to magenta.
        The foreground color will be set if "fg" tests True. The background color will be set if "fg" tests False.
        """
        if self.enabled:
            if fg:
                return "\x1b[35m"
            else:
                return "\x1b[45m"
        return ""

    def cyan(self, fg=True):
        """Set the foreground or background color to cyan.
        The foreground color will be set if "fg" tests True. The background color will be set if "fg" tests False.
        """
        if self.enabled:
            if fg:
                return "\x1b[36m"
            else:
                return "\x1b[46m"
        return ""

    def white(self, fg=True):
        """Set the foreground or background color to white.
        The foreground color will be set if "fg" tests True. The background color will be set if "fg" tests False.
        """
        if self.enabled:
            if fg:
                return "\x1b[37m"
            else:
                return "\x1b[47m"
        return ""

    def default(self, fg=True):
        """Set the foreground or background color to the default.
        The foreground color will be set if "fg" tests True. The background color will be set if "fg" tests False.
        """
        if self.enabled:
            if fg:
                return "\x1b[39m"
            else:
                return "\x1b[49m"
        return ""


def start_gdb_log(debugger, command, result, dict):
    """Start logging GDB remote packets by enabling logging with timestamps and
    thread safe logging. Follow a call to this function with a call to "stop_gdb_log"
    in order to dump out the commands."""
    global g_log_file
    command_args = shlex.split(command)
    usage = "usage: start_gdb_log [options] [<LOGFILEPATH>]"
    description = """The command enables GDB remote packet logging with timestamps. The packets will be logged to <LOGFILEPATH> if supplied, or a temporary file will be used. Logging stops when stop_gdb_log is called and the packet times will
    be aggregated and displayed."""
    parser = optparse.OptionParser(
        description=description, prog="start_gdb_log", usage=usage
    )
    parser.add_option(
        "-v",
        "--verbose",
        action="store_true",
        dest="verbose",
        help="display verbose debug info",
        default=False,
    )
    try:
        (options, args) = parser.parse_args(command_args)
    except:
        return

    if g_log_file:
        result.PutCString(
            'error: logging is already in progress with file "%s"' % g_log_file
        )
    else:
        args_len = len(args)
        if args_len == 0:
            g_log_file = tempfile.mktemp()
        elif len(args) == 1:
            g_log_file = args[0]

        if g_log_file:
            debugger.HandleCommand(
                'log enable --threadsafe --timestamp --file "%s" gdb-remote packets'
                % g_log_file
            )
            result.PutCString(
                "GDB packet logging enable with log file '%s'\nUse the 'stop_gdb_log' command to stop logging and show packet statistics."
                % g_log_file
            )
            return

        result.PutCString("error: invalid log file path")
    result.PutCString(usage)


def stop_gdb_log(debugger, command, result, dict):
    """Stop logging GDB remote packets to the file that was specified in a call
    to "start_gdb_log" and normalize the timestamps to be relative to the first
    timestamp in the log file. Also print out statistics for how long each
    command took to allow performance bottlenecks to be determined."""
    global g_log_file
    # Any commands whose names might be followed by more valid C identifier
    # characters must be listed here
    command_args = shlex.split(command)
    usage = "usage: stop_gdb_log [options]"
    description = """The command stops a previously enabled GDB remote packet logging command. Packet logging must have been previously enabled with a call to start_gdb_log."""
    parser = optparse.OptionParser(
        description=description, prog="stop_gdb_log", usage=usage
    )
    parser.add_option(
        "-v",
        "--verbose",
        action="store_true",
        dest="verbose",
        help="display verbose debug info",
        default=False,
    )
    parser.add_option(
        "--plot",
        action="store_true",
        dest="plot",
        help="plot packet latencies by packet type",
        default=False,
    )
    parser.add_option(
        "-q",
        "--quiet",
        action="store_true",
        dest="quiet",
        help="display verbose debug info",
        default=False,
    )
    parser.add_option(
        "-C",
        "--color",
        action="store_true",
        dest="color",
        help="add terminal colors",
        default=False,
    )
    parser.add_option(
        "-c",
        "--sort-by-count",
        action="store_true",
        dest="sort_count",
        help="display verbose debug info",
        default=False,
    )
    parser.add_option(
        "-s",
        "--symbolicate",
        action="store_true",
        dest="symbolicate",
        help='symbolicate addresses in log using current "lldb.target"',
        default=False,
    )
    try:
        (options, args) = parser.parse_args(command_args)
    except:
        return
    options.colors = TerminalColors(options.color)
    options.symbolicator = None
    if options.symbolicate:
        if lldb.target:
            import lldb.utils.symbolication

            options.symbolicator = lldb.utils.symbolication.Symbolicator()
            options.symbolicator.target = lldb.target
        else:
            print("error: can't symbolicate without a target")

    if not g_log_file:
        result.PutCString(
            'error: logging must have been previously enabled with a call to "stop_gdb_log"'
        )
    elif os.path.exists(g_log_file):
        if len(args) == 0:
            debugger.HandleCommand("log disable gdb-remote packets")
            result.PutCString(
                "GDB packet logging disabled. Logged packets are in '%s'" % g_log_file
            )
            parse_gdb_log_file(g_log_file, options)
        else:
            result.PutCString(usage)
    else:
        print('error: the GDB packet log file "%s" does not exist' % g_log_file)


def is_hex_byte(str):
    if len(str) == 2:
        return str[0] in string.hexdigits and str[1] in string.hexdigits
    return False


def get_hex_string_if_all_printable(str):
    try:
        s = binascii.unhexlify(str).decode()
        if all(c in string.printable for c in s):
            return s
    except (TypeError, binascii.Error, UnicodeDecodeError):
        pass
    return None


# global register info list
g_register_infos = list()
g_max_register_info_name_len = 0


class RegisterInfo:
    """Class that represents register information"""

    def __init__(self, kvp):
        self.info = dict()
        for kv in kvp:
            key = kv[0]
            value = kv[1]
            self.info[key] = value

    def name(self):
        """Get the name of the register."""
        if self.info and "name" in self.info:
            return self.info["name"]
        return None

    def bit_size(self):
        """Get the size in bits of the register."""
        if self.info and "bitsize" in self.info:
            return int(self.info["bitsize"])
        return 0

    def byte_size(self):
        """Get the size in bytes of the register."""
        return self.bit_size() / 8

    def get_value_from_hex_string(self, hex_str):
        """Dump the register value given a native byte order encoded hex ASCII byte string."""
        encoding = self.info["encoding"]
        bit_size = self.bit_size()
        packet = Packet(hex_str)
        if encoding == "uint":
            uval = packet.get_hex_uint(g_byte_order)
            if bit_size == 8:
                return "0x%2.2x" % (uval)
            elif bit_size == 16:
                return "0x%4.4x" % (uval)
            elif bit_size == 32:
                return "0x%8.8x" % (uval)
            elif bit_size == 64:
                return "0x%16.16x" % (uval)
        bytes = list()
        uval = packet.get_hex_uint8()
        while uval is not None:
            bytes.append(uval)
            uval = packet.get_hex_uint8()
        value_str = "0x"
        if g_byte_order == "little":
            bytes.reverse()
        for byte in bytes:
            value_str += "%2.2x" % byte
        return "%s" % (value_str)

    def __str__(self):
        """Dump the register info key/value pairs"""
        s = ""
        for key in self.info.keys():
            if s:
                s += ", "
            s += "%s=%s " % (key, self.info[key])
        return s


class Packet:
    """Class that represents a packet that contains string data"""

    def __init__(self, packet_str):
        self.str = packet_str

    def peek_char(self):
        ch = 0
        if self.str:
            ch = self.str[0]
        return ch

    def get_char(self):
        ch = 0
        if self.str:
            ch = self.str[0]
            self.str = self.str[1:]
        return ch

    def skip_exact_string(self, s):
        if self.str and self.str.startswith(s):
            self.str = self.str[len(s) :]
            return True
        else:
            return False

    def get_thread_id(self, fail_value=-1):
        match = g_number_regex.match(self.str)
        if match:
            number_str = match.group(1)
            self.str = self.str[len(number_str) :]
            return int(number_str, 0)
        else:
            return fail_value

    def get_hex_uint8(self):
        if (
            self.str
            and len(self.str) >= 2
            and self.str[0] in string.hexdigits
            and self.str[1] in string.hexdigits
        ):
            uval = int(self.str[0:2], 16)
            self.str = self.str[2:]
            return uval
        return None

    def get_hex_uint16(self, byte_order):
        uval = 0
        if byte_order == "big":
            uval |= self.get_hex_uint8() << 8
            uval |= self.get_hex_uint8()
        else:
            uval |= self.get_hex_uint8()
            uval |= self.get_hex_uint8() << 8
        return uval

    def get_hex_uint32(self, byte_order):
        uval = 0
        if byte_order == "big":
            uval |= self.get_hex_uint8() << 24
            uval |= self.get_hex_uint8() << 16
            uval |= self.get_hex_uint8() << 8
            uval |= self.get_hex_uint8()
        else:
            uval |= self.get_hex_uint8()
            uval |= self.get_hex_uint8() << 8
            uval |= self.get_hex_uint8() << 16
            uval |= self.get_hex_uint8() << 24
        return uval

    def get_hex_uint64(self, byte_order):
        uval = 0
        if byte_order == "big":
            uval |= self.get_hex_uint8() << 56
            uval |= self.get_hex_uint8() << 48
            uval |= self.get_hex_uint8() << 40
            uval |= self.get_hex_uint8() << 32
            uval |= self.get_hex_uint8() << 24
            uval |= self.get_hex_uint8() << 16
            uval |= self.get_hex_uint8() << 8
            uval |= self.get_hex_uint8()
        else:
            uval |= self.get_hex_uint8()
            uval |= self.get_hex_uint8() << 8
            uval |= self.get_hex_uint8() << 16
            uval |= self.get_hex_uint8() << 24
            uval |= self.get_hex_uint8() << 32
            uval |= self.get_hex_uint8() << 40
            uval |= self.get_hex_uint8() << 48
            uval |= self.get_hex_uint8() << 56
        return uval

    def get_number(self, fail_value=-1):
        """Get a number from the packet. The number must be in big endian format and should be parsed
        according to its prefix (starts with "0x" means hex, starts with "0" means octal, starts with
        [1-9] means decimal, etc)"""
        match = g_number_regex.match(self.str)
        if match:
            number_str = match.group(1)
            self.str = self.str[len(number_str) :]
            return int(number_str, 0)
        else:
            return fail_value

    def get_hex_ascii_str(self, n=0):
        hex_chars = self.get_hex_chars(n)
        if hex_chars:
            return binascii.unhexlify(hex_chars)
        else:
            return None

    def get_hex_chars(self, n=0):
        str_len = len(self.str)
        if n == 0:
            # n was zero, so we need to determine all hex chars and
            # stop when we hit the end of the string of a non-hex character
            while n < str_len and self.str[n] in string.hexdigits:
                n = n + 1
        else:
            if n > str_len:
                return None  # Not enough chars
            # Verify all chars are hex if a length was specified
            for i in range(n):
                if self.str[i] not in string.hexdigits:
                    return None  # Not all hex digits
        if n == 0:
            return None
        hex_str = self.str[0:n]
        self.str = self.str[n:]
        return hex_str

    def get_hex_uint(self, byte_order, n=0):
        if byte_order == "big":
            hex_str = self.get_hex_chars(n)
            if hex_str is None:
                return None
            return int(hex_str, 16)
        else:
            uval = self.get_hex_uint8()
            if uval is None:
                return None
            uval_result = 0
            shift = 0
            while uval is not None:
                uval_result |= uval << shift
                shift += 8
                uval = self.get_hex_uint8()
            return uval_result

    def get_key_value_pairs(self):
        kvp = list()
        if ";" in self.str:
            key_value_pairs = self.str.split(";")
            for key_value_pair in key_value_pairs:
                if len(key_value_pair):
                    kvp.append(key_value_pair.split(":", 1))
        return kvp

    def split(self, ch):
        return self.str.split(ch)

    def split_hex(self, ch, byte_order):
        hex_values = list()
        strings = self.str.split(ch)
        for str in strings:
            hex_values.append(Packet(str).get_hex_uint(byte_order))
        return hex_values

    def __str__(self):
        return self.str

    def __len__(self):
        return len(self.str)


g_thread_suffix_regex = re.compile(";thread:([0-9a-fA-F]+);")


def get_thread_from_thread_suffix(str):
    if str:
        match = g_thread_suffix_regex.match(str)
        if match:
            return int(match.group(1), 16)
    return None


def cmd_qThreadStopInfo(options, cmd, args):
    packet = Packet(args)
    tid = packet.get_hex_uint("big")
    print("get_thread_stop_info  (tid = 0x%x)" % (tid))


def cmd_stop_reply(options, cmd, args):
    print("get_last_stop_info()")
    return False


def rsp_stop_reply(options, cmd, cmd_args, rsp):
    global g_byte_order
    packet = Packet(rsp)
    stop_type = packet.get_char()
    if stop_type == "T" or stop_type == "S":
        signo = packet.get_hex_uint8()
        key_value_pairs = packet.get_key_value_pairs()
        for key_value_pair in key_value_pairs:
            key = key_value_pair[0]
            if is_hex_byte(key):
                reg_num = Packet(key).get_hex_uint8()
                if reg_num < len(g_register_infos):
                    reg_info = g_register_infos[reg_num]
                    key_value_pair[0] = reg_info.name()
                    key_value_pair[1] = reg_info.get_value_from_hex_string(
                        key_value_pair[1]
                    )
            elif key == "jthreads" or key == "jstopinfo":
                key_value_pair[1] = binascii.unhexlify(key_value_pair[1])
        key_value_pairs.insert(0, ["signal", signo])
        print("stop_reply():")
        dump_key_value_pairs(key_value_pairs)
    elif stop_type == "W":
        exit_status = packet.get_hex_uint8()
        print("stop_reply(): exit (status=%i)" % exit_status)
    elif stop_type == "O":
        print('stop_reply(): stdout = "%s"' % packet.str)


def cmd_unknown_packet(options, cmd, args):
    if args:
        print("cmd: %s, args: %s", cmd, args)
    else:
        print("cmd: %s", cmd)
    return False


def cmd_qSymbol(options, cmd, args):
    if args == ":":
        print("ready to serve symbols")
    else:
        packet = Packet(args)
        symbol_addr = packet.get_hex_uint("big")
        if symbol_addr is None:
            if packet.skip_exact_string(":"):
                symbol_name = packet.get_hex_ascii_str()
                print('lookup_symbol("%s") -> symbol not available yet' % (symbol_name))
            else:
                print("error: bad command format")
        else:
            if packet.skip_exact_string(":"):
                symbol_name = packet.get_hex_ascii_str()
                print('lookup_symbol("%s") -> 0x%x' % (symbol_name, symbol_addr))
            else:
                print("error: bad command format")


def cmd_QSetWithHexString(options, cmd, args):
    print('%s("%s")' % (cmd[:-1], binascii.unhexlify(args)))


def cmd_QSetWithString(options, cmd, args):
    print('%s("%s")' % (cmd[:-1], args))


def cmd_QSetWithUnsigned(options, cmd, args):
    print("%s(%i)" % (cmd[:-1], int(args)))


def rsp_qSymbol(options, cmd, cmd_args, rsp):
    if len(rsp) == 0:
        print("Unsupported")
    else:
        if rsp == "OK":
            print("No more symbols to lookup")
        else:
            packet = Packet(rsp)
            if packet.skip_exact_string("qSymbol:"):
                symbol_name = packet.get_hex_ascii_str()
                print('lookup_symbol("%s")' % (symbol_name))
            else:
                print(
                    'error: response string should start with "qSymbol:": respnse is "%s"'
                    % (rsp)
                )


def cmd_qXfer(options, cmd, args):
    # $qXfer:features:read:target.xml:0,1ffff#14
    print("read target special data %s" % (args))
    return True


def rsp_qXfer(options, cmd, cmd_args, rsp):
    data = cmd_args.split(":")
    if data[0] == "features":
        if data[1] == "read":
            filename, extension = os.path.splitext(data[2])
            if extension == ".xml":
                response = Packet(rsp)
                xml_string = response.get_hex_ascii_str()
                if xml_string:
                    ch = xml_string[0]
                    if ch == "l":
                        xml_string = xml_string[1:]
                        xml_root = ET.fromstring(xml_string)
                        for reg_element in xml_root.findall("./feature/reg"):
                            if not "value_regnums" in reg_element.attrib:
                                reg_info = RegisterInfo([])
                                if "name" in reg_element.attrib:
                                    reg_info.info["name"] = reg_element.attrib["name"]
                                else:
                                    reg_info.info["name"] = "unspecified"
                                if "encoding" in reg_element.attrib:
                                    reg_info.info["encoding"] = reg_element.attrib[
                                        "encoding"
                                    ]
                                else:
                                    reg_info.info["encoding"] = "uint"
                                if "offset" in reg_element.attrib:
                                    reg_info.info["offset"] = reg_element.attrib[
                                        "offset"
                                    ]
                                if "bitsize" in reg_element.attrib:
                                    reg_info.info["bitsize"] = reg_element.attrib[
                                        "bitsize"
                                    ]
                                g_register_infos.append(reg_info)
                        print('XML for "%s":' % (data[2]))
                        ET.dump(xml_root)


def cmd_A(options, cmd, args):
    print("launch process:")
    packet = Packet(args)
    while True:
        arg_len = packet.get_number()
        if arg_len == -1:
            break
        if not packet.skip_exact_string(","):
            break
        arg_idx = packet.get_number()
        if arg_idx == -1:
            break
        if not packet.skip_exact_string(","):
            break
        arg_value = packet.get_hex_ascii_str(arg_len)
        print('argv[%u] = "%s"' % (arg_idx, arg_value))


def cmd_qC(options, cmd, args):
    print("query_current_thread_id()")


def rsp_qC(options, cmd, cmd_args, rsp):
    packet = Packet(rsp)
    if packet.skip_exact_string("QC"):
        tid = packet.get_thread_id()
        print("current_thread_id = %#x" % (tid))
    else:
        print("current_thread_id = old thread ID")


def cmd_query_packet(options, cmd, args):
    if args:
        print("%s%s" % (cmd, args))
    else:
        print("%s" % (cmd))
    return False


def rsp_ok_error(rsp):
    print("rsp: ", rsp)


def rsp_ok_means_supported(options, cmd, cmd_args, rsp):
    if rsp == "OK":
        print("%s%s is supported" % (cmd, cmd_args))
    elif rsp == "":
        print("%s%s is not supported" % (cmd, cmd_args))
    else:
        print("%s%s -> %s" % (cmd, cmd_args, rsp))


def rsp_ok_means_success(options, cmd, cmd_args, rsp):
    if rsp == "OK":
        print("success")
    elif rsp == "":
        print("%s%s is not supported" % (cmd, cmd_args))
    else:
        print("%s%s -> %s" % (cmd, cmd_args, rsp))


def dump_key_value_pairs(key_value_pairs):
    max_key_len = 0
    for key_value_pair in key_value_pairs:
        key_len = len(key_value_pair[0])
        if max_key_len < key_len:
            max_key_len = key_len
    for key_value_pair in key_value_pairs:
        key = key_value_pair[0]
        value = key_value_pair[1]
        unhex_value = get_hex_string_if_all_printable(value)
        if unhex_value:
            print("%*s = %s (%s)" % (max_key_len, key, value, unhex_value))
        else:
            print("%*s = %s" % (max_key_len, key, value))


def rsp_dump_key_value_pairs(options, cmd, cmd_args, rsp):
    if rsp:
        print("%s response:" % (cmd))
        packet = Packet(rsp)
        key_value_pairs = packet.get_key_value_pairs()
        dump_key_value_pairs(key_value_pairs)
    else:
        print("not supported")


def cmd_c(options, cmd, args):
    print("continue()")
    return False


def cmd_s(options, cmd, args):
    print("step()")
    return False


def cmd_qSpeedTest(options, cmd, args):
    print(("qSpeedTest: cmd='%s', args='%s'" % (cmd, args)))


def rsp_qSpeedTest(options, cmd, cmd_args, rsp):
    print(("qSpeedTest: rsp='%s' cmd='%s', args='%s'" % (rsp, cmd, args)))


def cmd_vCont(options, cmd, args):
    if args == "?":
        print("%s: get supported extended continue modes" % (cmd))
    else:
        got_other_threads = 0
        s = ""
        for thread_action in args[1:].split(";"):
            (short_action, thread) = thread_action.split(":", 1)
            tid = int(thread, 16)
            if short_action == "c":
                action = "continue"
            elif short_action == "s":
                action = "step"
            elif short_action[0] == "C":
                action = "continue with signal 0x%s" % (short_action[1:])
            elif short_action == "S":
                action = "step with signal 0x%s" % (short_action[1:])
            else:
                action = short_action
            if s:
                s += ", "
            if tid == -1:
                got_other_threads = 1
                s += "other-threads:"
            else:
                s += "thread 0x%4.4x: %s" % (tid, action)
        if got_other_threads:
            print("extended_continue (%s)" % (s))
        else:
            print("extended_continue (%s, other-threads: suspend)" % (s))
    return False


def rsp_vCont(options, cmd, cmd_args, rsp):
    if cmd_args == "?":
        # Skip the leading 'vCont;'
        rsp = rsp[6:]
        modes = rsp.split(";")
        s = "%s: supported extended continue modes include: " % (cmd)

        for i, mode in enumerate(modes):
            if i:
                s += ", "
            if mode == "c":
                s += "continue"
            elif mode == "C":
                s += "continue with signal"
            elif mode == "s":
                s += "step"
            elif mode == "S":
                s += "step with signal"
            elif mode == "t":
                s += "stop"
            # else:
            #     s += 'unrecognized vCont mode: ', str(mode)
        print(s)
    elif rsp:
        if rsp[0] == "T" or rsp[0] == "S" or rsp[0] == "W" or rsp[0] == "X":
            rsp_stop_reply(options, cmd, cmd_args, rsp)
            return
        if rsp[0] == "O":
            print("stdout: %s" % (rsp))
            return
    else:
        print(
            "not supported (cmd = '%s', args = '%s', rsp = '%s')" % (cmd, cmd_args, rsp)
        )


def cmd_vAttach(options, cmd, args):
    (extra_command, args) = args.split(";")
    if extra_command:
        print("%s%s(%s)" % (cmd, extra_command, args))
    else:
        print("attach(pid = %u)" % int(args, 16))
    return False


def cmd_qRegisterInfo(options, cmd, args):
    print("query_register_info(reg_num=%i)" % (int(args, 16)))
    return False


def rsp_qRegisterInfo(options, cmd, cmd_args, rsp):
    global g_max_register_info_name_len
    print("query_register_info(reg_num=%i):" % (int(cmd_args, 16)), end=" ")
    if len(rsp) == 3 and rsp[0] == "E":
        g_max_register_info_name_len = 0
        for reg_info in g_register_infos:
            name_len = len(reg_info.name())
            if g_max_register_info_name_len < name_len:
                g_max_register_info_name_len = name_len
        print(" DONE")
    else:
        packet = Packet(rsp)
        reg_info = RegisterInfo(packet.get_key_value_pairs())
        g_register_infos.append(reg_info)
        print(reg_info)
    return False


def cmd_qThreadInfo(options, cmd, args):
    if cmd == "qfThreadInfo":
        query_type = "first"
    else:
        query_type = "subsequent"
    print("get_current_thread_list(type=%s)" % (query_type))
    return False


def rsp_qThreadInfo(options, cmd, cmd_args, rsp):
    packet = Packet(rsp)
    response_type = packet.get_char()
    if response_type == "m":
        tids = packet.split_hex(";", "big")
        for i, tid in enumerate(tids):
            if i:
                print(",", end=" ")
            print("0x%x" % (tid), end=" ")
        print()
    elif response_type == "l":
        print("END")


def rsp_hex_big_endian(options, cmd, cmd_args, rsp):
    if rsp == "":
        print("%s%s is not supported" % (cmd, cmd_args))
    else:
        packet = Packet(rsp)
        uval = packet.get_hex_uint("big")
        print("%s: 0x%x" % (cmd, uval))


def cmd_read_mem_bin(options, cmd, args):
    # x0x7fff5fc39200,0x200
    packet = Packet(args)
    addr = packet.get_hex_uint("big")
    comma = packet.get_char()
    size = packet.get_hex_uint("big")
    print("binary_read_memory (addr = 0x%16.16x, size = %u)" % (addr, size))
    return False


def rsp_mem_bin_bytes(options, cmd, cmd_args, rsp):
    packet = Packet(cmd_args)
    addr = packet.get_hex_uint("big")
    comma = packet.get_char()
    size = packet.get_hex_uint("big")
    print("memory:")
    if size > 0:
        dump_hex_memory_buffer(addr, rsp)


def cmd_read_memory(options, cmd, args):
    packet = Packet(args)
    addr = packet.get_hex_uint("big")
    comma = packet.get_char()
    size = packet.get_hex_uint("big")
    print("read_memory (addr = 0x%16.16x, size = %u)" % (addr, size))
    return False


def dump_hex_memory_buffer(addr, hex_byte_str):
    packet = Packet(hex_byte_str)
    idx = 0
    ascii = ""
    uval = packet.get_hex_uint8()
    while uval is not None:
        if (idx % 16) == 0:
            if ascii:
                print("  ", ascii)
                ascii = ""
            print("0x%x:" % (addr + idx), end=" ")
        print("%2.2x" % (uval), end=" ")
        if 0x20 <= uval and uval < 0x7F:
            ascii += "%c" % uval
        else:
            ascii += "."
        uval = packet.get_hex_uint8()
        idx = idx + 1
    if ascii:
        print("  ", ascii)
        ascii = ""


def cmd_write_memory(options, cmd, args):
    packet = Packet(args)
    addr = packet.get_hex_uint("big")
    if packet.get_char() != ",":
        print("error: invalid write memory command (missing comma after address)")
        return
    size = packet.get_hex_uint("big")
    if packet.get_char() != ":":
        print("error: invalid write memory command (missing colon after size)")
        return
    print("write_memory (addr = 0x%16.16x, size = %u, data:" % (addr, size))
    dump_hex_memory_buffer(addr, packet.str)
    return False


def cmd_alloc_memory(options, cmd, args):
    packet = Packet(args)
    byte_size = packet.get_hex_uint("big")
    if packet.get_char() != ",":
        print("error: invalid allocate memory command (missing comma after address)")
        return
    print(
        "allocate_memory (byte-size = %u (0x%x), permissions = %s)"
        % (byte_size, byte_size, packet.str)
    )
    return False


def rsp_alloc_memory(options, cmd, cmd_args, rsp):
    packet = Packet(rsp)
    addr = packet.get_hex_uint("big")
    print("addr = 0x%x" % addr)


def cmd_dealloc_memory(options, cmd, args):
    packet = Packet(args)
    addr = packet.get_hex_uint("big")
    if packet.get_char() != ",":
        print("error: invalid allocate memory command (missing comma after address)")
    else:
        print("deallocate_memory (addr = 0x%x, permissions = %s)" % (addr, packet.str))
    return False


def rsp_memory_bytes(options, cmd, cmd_args, rsp):
    addr = Packet(cmd_args).get_hex_uint("big")
    dump_hex_memory_buffer(addr, rsp)


def get_register_name_equal_value(options, reg_num, hex_value_str):
    if reg_num < len(g_register_infos):
        reg_info = g_register_infos[reg_num]
        value_str = reg_info.get_value_from_hex_string(hex_value_str)
        s = reg_info.name() + " = "
        if options.symbolicator:
            symbolicated_addresses = options.symbolicator.symbolicate(int(value_str, 0))
            if symbolicated_addresses:
                s += options.colors.magenta()
                s += "%s" % symbolicated_addresses[0]
                s += options.colors.reset()
                return s
        s += value_str
        return s
    else:
        reg_value = Packet(hex_value_str).get_hex_uint(g_byte_order)
        return "reg(%u) = 0x%x" % (reg_num, reg_value)


def cmd_read_one_reg(options, cmd, args):
    packet = Packet(args)
    reg_num = packet.get_hex_uint("big")
    tid = get_thread_from_thread_suffix(packet.str)
    name = None
    if reg_num < len(g_register_infos):
        name = g_register_infos[reg_num].name()
    if packet.str:
        packet.get_char()  # skip ;
        thread_info = packet.get_key_value_pairs()
        tid = int(thread_info[0][1], 16)
    s = "read_register (reg_num=%u" % reg_num
    if name:
        s += " (%s)" % (name)
    if tid is not None:
        s += ", tid = 0x%4.4x" % (tid)
    s += ")"
    print(s)
    return False


def rsp_read_one_reg(options, cmd, cmd_args, rsp):
    packet = Packet(cmd_args)
    reg_num = packet.get_hex_uint("big")
    print(get_register_name_equal_value(options, reg_num, rsp))


def cmd_write_one_reg(options, cmd, args):
    packet = Packet(args)
    reg_num = packet.get_hex_uint("big")
    if packet.get_char() != "=":
        print("error: invalid register write packet")
    else:
        name = None
        hex_value_str = packet.get_hex_chars()
        tid = get_thread_from_thread_suffix(packet.str)
        s = "write_register (reg_num=%u" % reg_num
        if name:
            s += " (%s)" % (name)
        s += ", value = "
        s += get_register_name_equal_value(options, reg_num, hex_value_str)
        if tid is not None:
            s += ", tid = 0x%4.4x" % (tid)
        s += ")"
        print(s)
    return False


def dump_all_regs(packet):
    for reg_info in g_register_infos:
        nibble_size = reg_info.bit_size() / 4
        hex_value_str = packet.get_hex_chars(nibble_size)
        if hex_value_str is not None:
            value = reg_info.get_value_from_hex_string(hex_value_str)
            print("%*s = %s" % (g_max_register_info_name_len, reg_info.name(), value))
        else:
            return


def cmd_read_all_regs(cmd, cmd_args):
    packet = Packet(cmd_args)
    packet.get_char()  # toss the 'g' command character
    tid = get_thread_from_thread_suffix(packet.str)
    if tid is not None:
        print("read_all_register(thread = 0x%4.4x)" % tid)
    else:
        print("read_all_register()")
    return False


def rsp_read_all_regs(options, cmd, cmd_args, rsp):
    packet = Packet(rsp)
    dump_all_regs(packet)


def cmd_write_all_regs(options, cmd, args):
    packet = Packet(args)
    print("write_all_registers()")
    dump_all_regs(packet)
    return False


g_bp_types = ["software_bp", "hardware_bp", "write_wp", "read_wp", "access_wp"]


def cmd_bp(options, cmd, args):
    if cmd == "Z":
        s = "set_"
    else:
        s = "clear_"
    packet = Packet(args)
    bp_type = packet.get_hex_uint("big")
    packet.get_char()  # Skip ,
    bp_addr = packet.get_hex_uint("big")
    packet.get_char()  # Skip ,
    bp_size = packet.get_hex_uint("big")
    s += g_bp_types[bp_type]
    s += " (addr = 0x%x, size = %u)" % (bp_addr, bp_size)
    print(s)
    return False


def cmd_mem_rgn_info(options, cmd, args):
    packet = Packet(args)
    packet.get_char()  # skip ':' character
    addr = packet.get_hex_uint("big")
    print("get_memory_region_info (addr=0x%x)" % (addr))
    return False


def cmd_kill(options, cmd, args):
    print("kill_process()")
    return False


def cmd_jThreadsInfo(options, cmd, args):
    print("jThreadsInfo()")
    return False


def cmd_jGetLoadedDynamicLibrariesInfos(options, cmd, args):
    print("jGetLoadedDynamicLibrariesInfos()")
    return False


def decode_packet(s, start_index=0):
    # print '\ndecode_packet("%s")' % (s[start_index:])
    index = s.find("}", start_index)
    have_escapes = index != -1
    if have_escapes:
        normal_s = s[start_index:index]
    else:
        normal_s = s[start_index:]
    # print 'normal_s = "%s"' % (normal_s)
    if have_escapes:
        escape_char = "%c" % (ord(s[index + 1]) ^ 0x20)
        # print 'escape_char for "%s" = %c' % (s[index:index+2], escape_char)
        return normal_s + escape_char + decode_packet(s, index + 2)
    else:
        return normal_s


def rsp_json(options, cmd, cmd_args, rsp):
    print("%s() reply:" % (cmd))
    if not rsp:
        return
    try:
        json_tree = json.loads(rsp)
        print(json.dumps(json_tree, indent=4, separators=(",", ": ")))
    except json.JSONDecodeError:
        return


def rsp_jGetLoadedDynamicLibrariesInfos(options, cmd, cmd_args, rsp):
    if cmd_args:
        rsp_json(options, cmd, cmd_args, rsp)
    else:
        rsp_ok_means_supported(options, cmd, cmd_args, rsp)


gdb_remote_commands = {
    "\\?": {"cmd": cmd_stop_reply, "rsp": rsp_stop_reply, "name": "stop reply pacpket"},
    "qThreadStopInfo": {
        "cmd": cmd_qThreadStopInfo,
        "rsp": rsp_stop_reply,
        "name": "stop reply pacpket",
    },
    "QStartNoAckMode": {
        "cmd": cmd_query_packet,
        "rsp": rsp_ok_means_supported,
        "name": "query if no ack mode is supported",
    },
    "QThreadSuffixSupported": {
        "cmd": cmd_query_packet,
        "rsp": rsp_ok_means_supported,
        "name": "query if thread suffix is supported",
    },
    "QListThreadsInStopReply": {
        "cmd": cmd_query_packet,
        "rsp": rsp_ok_means_supported,
        "name": "query if threads in stop reply packets are supported",
    },
    "QSetDetachOnError:": {
        "cmd": cmd_QSetWithUnsigned,
        "rsp": rsp_ok_means_success,
        "name": "set if we should detach on error",
    },
    "QSetDisableASLR:": {
        "cmd": cmd_QSetWithUnsigned,
        "rsp": rsp_ok_means_success,
        "name": "set if we should disable ASLR",
    },
    "qLaunchSuccess": {
        "cmd": cmd_query_packet,
        "rsp": rsp_ok_means_success,
        "name": "check on launch success for the A packet",
    },
    "A": {"cmd": cmd_A, "rsp": rsp_ok_means_success, "name": "launch process"},
    "QLaunchArch:": {
        "cmd": cmd_QSetWithString,
        "rsp": rsp_ok_means_supported,
        "name": "set the arch to launch in case the file contains multiple architectures",
    },
    "qVAttachOrWaitSupported": {
        "cmd": cmd_query_packet,
        "rsp": rsp_ok_means_supported,
        "name": "set the launch architecture",
    },
    "qHostInfo": {
        "cmd": cmd_query_packet,
        "rsp": rsp_dump_key_value_pairs,
        "name": "get host information",
    },
    "qC": {"cmd": cmd_qC, "rsp": rsp_qC, "name": "return the current thread ID"},
    "vCont": {"cmd": cmd_vCont, "rsp": rsp_vCont, "name": "extended continue command"},
    "qSpeedTest": {
        "cmd": cmd_qSpeedTest,
        "rsp": rsp_qSpeedTest,
        "name": "speed test packdet",
    },
    "vAttach": {"cmd": cmd_vAttach, "rsp": rsp_stop_reply, "name": "attach to process"},
    "c": {"cmd": cmd_c, "rsp": rsp_stop_reply, "name": "continue"},
    "s": {"cmd": cmd_s, "rsp": rsp_stop_reply, "name": "step"},
    "qRegisterInfo": {
        "cmd": cmd_qRegisterInfo,
        "rsp": rsp_qRegisterInfo,
        "name": "query register info",
    },
    "qfThreadInfo": {
        "cmd": cmd_qThreadInfo,
        "rsp": rsp_qThreadInfo,
        "name": "get current thread list",
    },
    "qsThreadInfo": {
        "cmd": cmd_qThreadInfo,
        "rsp": rsp_qThreadInfo,
        "name": "get current thread list",
    },
    "qShlibInfoAddr": {
        "cmd": cmd_query_packet,
        "rsp": rsp_hex_big_endian,
        "name": "get shared library info address",
    },
    "qMemoryRegionInfo": {
        "cmd": cmd_mem_rgn_info,
        "rsp": rsp_dump_key_value_pairs,
        "name": "get memory region information",
    },
    "qProcessInfo": {
        "cmd": cmd_query_packet,
        "rsp": rsp_dump_key_value_pairs,
        "name": "get process info",
    },
    "qSupported": {
        "cmd": cmd_query_packet,
        "rsp": rsp_ok_means_supported,
        "name": "query supported",
    },
    "qXfer:": {"cmd": cmd_qXfer, "rsp": rsp_qXfer, "name": "qXfer"},
    "qSymbol:": {"cmd": cmd_qSymbol, "rsp": rsp_qSymbol, "name": "qSymbol"},
    "QSetSTDIN:": {
        "cmd": cmd_QSetWithHexString,
        "rsp": rsp_ok_means_success,
        "name": "set STDIN prior to launching with A packet",
    },
    "QSetSTDOUT:": {
        "cmd": cmd_QSetWithHexString,
        "rsp": rsp_ok_means_success,
        "name": "set STDOUT prior to launching with A packet",
    },
    "QSetSTDERR:": {
        "cmd": cmd_QSetWithHexString,
        "rsp": rsp_ok_means_success,
        "name": "set STDERR prior to launching with A packet",
    },
    "QEnvironment:": {
        "cmd": cmd_QSetWithString,
        "rsp": rsp_ok_means_success,
        "name": "set an environment variable prior to launching with A packet",
    },
    "QEnvironmentHexEncoded:": {
        "cmd": cmd_QSetWithHexString,
        "rsp": rsp_ok_means_success,
        "name": "set an environment variable prior to launching with A packet",
    },
    "x": {
        "cmd": cmd_read_mem_bin,
        "rsp": rsp_mem_bin_bytes,
        "name": "read memory binary",
    },
    "X": {
        "cmd": cmd_write_memory,
        "rsp": rsp_ok_means_success,
        "name": "write memory binary",
    },
    "m": {"cmd": cmd_read_memory, "rsp": rsp_memory_bytes, "name": "read memory"},
    "M": {"cmd": cmd_write_memory, "rsp": rsp_ok_means_success, "name": "write memory"},
    "_M": {"cmd": cmd_alloc_memory, "rsp": rsp_alloc_memory, "name": "allocate memory"},
    "_m": {
        "cmd": cmd_dealloc_memory,
        "rsp": rsp_ok_means_success,
        "name": "deallocate memory",
    },
    "p": {
        "cmd": cmd_read_one_reg,
        "rsp": rsp_read_one_reg,
        "name": "read single register",
    },
    "P": {
        "cmd": cmd_write_one_reg,
        "rsp": rsp_ok_means_success,
        "name": "write single register",
    },
    "g": {
        "cmd": cmd_read_all_regs,
        "rsp": rsp_read_all_regs,
        "name": "read all registers",
    },
    "G": {
        "cmd": cmd_write_all_regs,
        "rsp": rsp_ok_means_success,
        "name": "write all registers",
    },
    "z": {
        "cmd": cmd_bp,
        "rsp": rsp_ok_means_success,
        "name": "clear breakpoint or watchpoint",
    },
    "Z": {
        "cmd": cmd_bp,
        "rsp": rsp_ok_means_success,
        "name": "set breakpoint or watchpoint",
    },
    "k": {"cmd": cmd_kill, "rsp": rsp_stop_reply, "name": "kill process"},
    "jThreadsInfo": {
        "cmd": cmd_jThreadsInfo,
        "rsp": rsp_json,
        "name": "JSON get all threads info",
    },
    "jGetLoadedDynamicLibrariesInfos:": {
        "cmd": cmd_jGetLoadedDynamicLibrariesInfos,
        "rsp": rsp_jGetLoadedDynamicLibrariesInfos,
        "name": "JSON get loaded dynamic libraries",
    },
}


def calculate_mean_and_standard_deviation(floats):
    sum = 0.0
    count = len(floats)
    if count == 0:
        return (0.0, 0.0)
    for f in floats:
        sum += f
    mean = sum / count
    accum = 0.0
    for f in floats:
        delta = f - mean
        accum += delta * delta

    std_dev = math.sqrt(accum / (count - 1))
    return (mean, std_dev)


def parse_gdb_log_file(path, options):
    f = open(path)
    parse_gdb_log(f, options)
    f.close()


def round_up(n, incr):
    return float(((int(n) + incr) / incr) * incr)


def plot_latencies(sec_times):
    # import numpy as np
    import matplotlib.pyplot as plt

    for i, name in enumerate(sec_times.keys()):
        times = sec_times[name]
        if len(times) <= 1:
            continue
        plt.subplot(2, 1, 1)
        plt.title('Packet "%s" Times' % (name))
        plt.xlabel("Packet")
        units = "ms"
        adj_times = []
        max_time = 0.0
        for time in times:
            time = time * 1000.0
            adj_times.append(time)
            if time > max_time:
                max_time = time
        if max_time < 1.0:
            units = "us"
            max_time = 0.0
            for i in range(len(adj_times)):
                adj_times[i] *= 1000.0
                if adj_times[i] > max_time:
                    max_time = adj_times[i]
        plt.ylabel("Time (%s)" % (units))
        max_y = None
        for i in [5.0, 10.0, 25.0, 50.0]:
            if max_time < i:
                max_y = round_up(max_time, i)
                break
        if max_y is None:
            max_y = round_up(max_time, 100.0)
        plt.ylim(0.0, max_y)
        plt.plot(adj_times, "o-")
        plt.show()


def parse_gdb_log(file, options):
    """Parse a GDB log file that was generated by enabling logging with:
    (lldb) log enable --threadsafe --timestamp --file <FILE> gdb-remote packets
    This log file will contain timestamps and this function will then normalize
    those packets to be relative to the first value timestamp that is found and
    show delta times between log lines and also keep track of how long it takes
    for GDB remote commands to make a send/receive round trip. This can be
    handy when trying to figure out why some operation in the debugger is taking
    a long time during a preset set of debugger commands."""

    tricky_commands = ["qRegisterInfo"]
    timestamp_regex = re.compile("(\s*)([1-9][0-9]+\.[0-9]+)([^0-9].*)$")
    packet_name_regex = re.compile("([A-Za-z_]+)[^a-z]")
    packet_transmit_name_regex = re.compile(
        "(?P<direction>send|read) packet: (?P<packet>.*)"
    )
    packet_contents_name_regex = re.compile("\$([^#]*)#[0-9a-fA-F]{2}")
    packet_checksum_regex = re.compile(".*#[0-9a-fA-F]{2}$")
    packet_names_regex_str = "(" + "|".join(gdb_remote_commands.keys()) + ")(.*)"
    packet_names_regex = re.compile(packet_names_regex_str)

    base_time = 0.0
    last_time = 0.0
    min_time = 100000000.0
    packet_total_times = {}
    all_packet_times = []
    packet_times = {}
    packet_counts = {}
    lines = file.read().splitlines()
    last_command = None
    last_command_args = None
    last_command_packet = None
    hide_next_response = False
    num_lines = len(lines)
    skip_count = 0
    for line_index, line in enumerate(lines):
        # See if we need to skip any lines
        if skip_count > 0:
            skip_count -= 1
            continue
        m = packet_transmit_name_regex.search(line)
        is_command = False
        direction = None
        if m:
            direction = m.group("direction")
            is_command = direction == "send"
            packet = m.group("packet")
            sys.stdout.write(options.colors.green())
            if not options.quiet and not hide_next_response:
                print("#  ", line)
            sys.stdout.write(options.colors.reset())

            # print 'direction = "%s", packet = "%s"' % (direction, packet)

            if packet[0] == "+":
                if is_command:
                    print("-->", end=" ")
                else:
                    print("<--", end=" ")
                if not options.quiet:
                    print("ACK")
                continue
            elif packet[0] == "-":
                if is_command:
                    print("-->", end=" ")
                else:
                    print("<--", end=" ")
                if not options.quiet:
                    print("NACK")
                continue
            elif packet[0] == "$":
                m = packet_contents_name_regex.match(packet)
                if not m and packet[0] == "$":
                    multiline_packet = packet
                    idx = line_index + 1
                    while idx < num_lines:
                        if not options.quiet and not hide_next_response:
                            print("#  ", lines[idx])
                        multiline_packet += lines[idx]
                        m = packet_contents_name_regex.match(multiline_packet)
                        if m:
                            packet = multiline_packet
                            skip_count = idx - line_index
                            break
                        else:
                            idx += 1
                if m:
                    if is_command:
                        print("-->", end=" ")
                    else:
                        print("<--", end=" ")
                    contents = decode_packet(m.group(1))
                    if is_command:
                        hide_next_response = False
                        m = packet_names_regex.match(contents)
                        if m:
                            last_command = m.group(1)
                            if last_command == "?":
                                last_command = "\\?"
                            packet_name = last_command
                            last_command_args = m.group(2)
                            last_command_packet = contents
                            hide_next_response = gdb_remote_commands[last_command][
                                "cmd"
                            ](options, last_command, last_command_args)
                        else:
                            packet_match = packet_name_regex.match(contents)
                            if packet_match:
                                packet_name = packet_match.group(1)
                                for tricky_cmd in tricky_commands:
                                    if packet_name.find(tricky_cmd) == 0:
                                        packet_name = tricky_cmd
                            else:
                                packet_name = contents
                            last_command = None
                            last_command_args = None
                            last_command_packet = None
                    elif last_command:
                        gdb_remote_commands[last_command]["rsp"](
                            options, last_command, last_command_args, contents
                        )
                else:
                    print('error: invalid packet: "', packet, '"')
            else:
                print("???")
        else:
            print("## ", line)
        match = timestamp_regex.match(line)
        if match:
            curr_time = float(match.group(2))
            if last_time and not is_command:
                delta = curr_time - last_time
                all_packet_times.append(delta)
            delta = 0.0
            if base_time:
                delta = curr_time - last_time
            else:
                base_time = curr_time

            if not is_command:
                if line.find("read packet: $") >= 0 and packet_name:
                    if packet_name in packet_total_times:
                        packet_total_times[packet_name] += delta
                        packet_counts[packet_name] += 1
                    else:
                        packet_total_times[packet_name] = delta
                        packet_counts[packet_name] = 1
                    if packet_name not in packet_times:
                        packet_times[packet_name] = []
                    packet_times[packet_name].append(delta)
                    packet_name = None
                if min_time > delta:
                    min_time = delta

            if not options or not options.quiet:
                print(
                    "%s%.6f %+.6f%s"
                    % (match.group(1), curr_time - base_time, delta, match.group(3))
                )
            last_time = curr_time
        # else:
        #     print line
    (average, std_dev) = calculate_mean_and_standard_deviation(all_packet_times)
    if average and std_dev:
        print(
            "%u packets with average packet time of %f and standard deviation of %f"
            % (len(all_packet_times), average, std_dev)
        )
    if packet_total_times:
        total_packet_time = 0.0
        total_packet_count = 0
        for key, vvv in packet_total_times.items():
            # print '  key = (%s) "%s"' % (type(key), key)
            # print 'value = (%s) %s' % (type(vvv), vvv)
            # if type(vvv) == 'float':
            total_packet_time += vvv
        for key, vvv in packet_counts.items():
            total_packet_count += vvv

        print("#------------------------------------------------------------")
        print("# Packet timing summary:")
        print(
            "# Totals: time = %6f, count = %6d"
            % (total_packet_time, total_packet_count)
        )
        print("# Min packet time: time = %6f" % (min_time))
        print("#------------------------------------------------------------")
        print("# Packet                   Time (sec)  Percent Count  Latency")
        print("#------------------------- ----------- ------- ------ -------")
        if options and options.sort_count:
            res = sorted(packet_counts, key=packet_counts.__getitem__, reverse=True)
        else:
            res = sorted(
                packet_total_times, key=packet_total_times.__getitem__, reverse=True
            )

        if last_time > 0.0:
            for item in res:
                packet_total_time = packet_total_times[item]
                packet_percent = (packet_total_time / total_packet_time) * 100.0
                packet_count = packet_counts[item]
                print(
                    "  %24s %11.6f  %5.2f%% %6d %9.6f"
                    % (
                        item,
                        packet_total_time,
                        packet_percent,
                        packet_count,
                        float(packet_total_time) / float(packet_count),
                    )
                )
        if options and options.plot:
            plot_latencies(packet_times)


if __name__ == "__main__":
    usage = "usage: gdbremote [options]"
    description = """The command disassembles a GDB remote packet log."""
    parser = optparse.OptionParser(
        description=description, prog="gdbremote", usage=usage
    )
    parser.add_option(
        "-v",
        "--verbose",
        action="store_true",
        dest="verbose",
        help="display verbose debug info",
        default=False,
    )
    parser.add_option(
        "-q",
        "--quiet",
        action="store_true",
        dest="quiet",
        help="display verbose debug info",
        default=False,
    )
    parser.add_option(
        "-C",
        "--color",
        action="store_true",
        dest="color",
        help="add terminal colors",
        default=False,
    )
    parser.add_option(
        "-c",
        "--sort-by-count",
        action="store_true",
        dest="sort_count",
        help="display verbose debug info",
        default=False,
    )
    parser.add_option(
        "--crashlog",
        type="string",
        dest="crashlog",
        help="symbolicate using a darwin crash log file",
        default=False,
    )
    try:
        (options, args) = parser.parse_args(sys.argv[1:])
    except:
        print("error: argument error")
        sys.exit(1)

    options.colors = TerminalColors(options.color)
    options.symbolicator = None
    if options.crashlog:
        import lldb

        lldb.debugger = lldb.SBDebugger.Create()
        import lldb.macosx.crashlog

        options.symbolicator = lldb.macosx.crashlog.CrashLog(options.crashlog)
        print("%s" % (options.symbolicator))

    # This script is being run from the command line, create a debugger in case we are
    # going to use any debugger functions in our function.
    if len(args):
        for file in args:
            print(
                "#----------------------------------------------------------------------"
            )
            print("# GDB remote log file: '%s'" % file)
            print(
                "#----------------------------------------------------------------------"
            )
            parse_gdb_log_file(file, options)
        if options.symbolicator:
            print("%s" % (options.symbolicator))
    else:
        parse_gdb_log(sys.stdin, options)


def __lldb_init_module(debugger, internal_dict):
    # This initializer is being run from LLDB in the embedded command interpreter
    # Add any commands contained in this module to LLDB
    debugger.HandleCommand(
        "command script add -o -f gdbremote.start_gdb_log start_gdb_log"
    )
    debugger.HandleCommand(
        "command script add -o -f gdbremote.stop_gdb_log stop_gdb_log"
    )
    print(
        'The "start_gdb_log" and "stop_gdb_log" commands are now installed and ready for use, type "start_gdb_log --help" or "stop_gdb_log --help" for more information'
    )
