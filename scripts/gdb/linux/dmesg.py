#
# gdb helper commands and functions for Linux kernel debugging
#
#  kernel log buffer dump
#
# Copyright (c) Siemens AG, 2011, 2012
#
# Authors:
#  Jan Kiszka <jan.kiszka@siemens.com>
#
# This work is licensed under the terms of the GNU GPL version 2.
#

import gdb
import sys

from linux import utils


class LxDmesg(gdb.Command):
    """Print Linux kernel log buffer."""

    def __init__(self):
        super(LxDmesg, self).__init__("lx-dmesg", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        log_buf_addr = int(str(gdb.parse_and_eval(
            "(void *)'printk.c'::log_buf")).split()[0], 16)
        log_first_idx = int(gdb.parse_and_eval("'printk.c'::log_first_idx"))
        log_next_idx = int(gdb.parse_and_eval("'printk.c'::log_next_idx"))
        log_buf_len = int(gdb.parse_and_eval("'printk.c'::log_buf_len"))

        inf = gdb.inferiors()[0]
        start = log_buf_addr + log_first_idx
        if log_first_idx < log_next_idx:
            log_buf_2nd_half = -1
            length = log_next_idx - log_first_idx
            log_buf = utils.read_memoryview(inf, start, length).tobytes()
        else:
            log_buf_2nd_half = log_buf_len - log_first_idx
            a = utils.read_memoryview(inf, start, log_buf_2nd_half)
            b = utils.read_memoryview(inf, log_buf_addr, log_next_idx)
            log_buf = a.tobytes() + b.tobytes()

        pos = 0
        while pos < log_buf.__len__():
            length = utils.read_u16(log_buf[pos + 8:pos + 10])
            if length == 0:
                if log_buf_2nd_half == -1:
                    gdb.write("Corrupted log buffer!\n")
                    break
                pos = log_buf_2nd_half
                continue

            text_len = utils.read_u16(log_buf[pos + 10:pos + 12])
            text = log_buf[pos + 16:pos + 16 + text_len].decode(
                encoding='utf8', errors='replace')
            time_stamp = utils.read_u64(log_buf[pos:pos + 8])

            for line in text.splitlines():
                msg = u"[{time:12.6f}] {line}\n".format(
                    time=time_stamp / 1000000000.0,
                    line=line)
                # With python2 gdb.write will attempt to convert unicode to
                # ascii and might fail so pass an utf8-encoded str instead.
                if sys.hexversion < 0x03000000:
                    msg = msg.encode(encoding='utf8', errors='replace')
                gdb.write(msg)

            pos += length


LxDmesg()
