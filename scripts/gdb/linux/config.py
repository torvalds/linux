# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2019 Google LLC.

import gdb
import zlib

from linux import utils


class LxConfigDump(gdb.Command):
    """Output kernel config to the filename specified as the command
       argument. Equivalent to 'zcat /proc/config.gz > config.txt' on
       a running target"""

    def __init__(self):
        super(LxConfigDump, self).__init__("lx-configdump", gdb.COMMAND_DATA,
                                           gdb.COMPLETE_FILENAME)

    def invoke(self, arg, from_tty):
        if len(arg) == 0:
            filename = "config.txt"
        else:
            filename = arg

        try:
            py_config_ptr = gdb.parse_and_eval("kernel_config_data + 8")
            py_config_size = gdb.parse_and_eval(
                    "sizeof(kernel_config_data) - 1 - 8 * 2")
        except gdb.error as e:
            raise gdb.GdbError("Can't find config, enable CONFIG_IKCONFIG?")

        inf = gdb.inferiors()[0]
        zconfig_buf = utils.read_memoryview(inf, py_config_ptr,
                                            py_config_size).tobytes()

        config_buf = zlib.decompress(zconfig_buf, 16)
        with open(filename, 'wb') as f:
            f.write(config_buf)

        gdb.write("Dumped config to " + filename + "\n")


LxConfigDump()
