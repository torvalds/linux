#!/usr/bin/env python

import string
import struct
import sys


class FileExtract:
    """Decode binary data from a file"""

    def __init__(self, f, b="="):
        """Initialize with an open binary file and optional byte order"""

        self.file = f
        self.byte_order = b
        self.offsets = list()

    def set_byte_order(self, b):
        '''Set the byte order, valid values are "big", "little", "swap", "native", "<", ">", "@", "="'''
        if b == "big":
            self.byte_order = ">"
        elif b == "little":
            self.byte_order = "<"
        elif b == "swap":
            # swap what ever the current byte order is
            self.byte_order = swap_unpack_char()
        elif b == "native":
            self.byte_order = "="
        elif b == "<" or b == ">" or b == "@" or b == "=":
            self.byte_order = b
        else:
            print("error: invalid byte order specified: '%s'" % b)

    def is_in_memory(self):
        return False

    def seek(self, offset, whence=0):
        if self.file:
            return self.file.seek(offset, whence)
        raise ValueError

    def tell(self):
        if self.file:
            return self.file.tell()
        raise ValueError

    def read_size(self, byte_size):
        s = self.file.read(byte_size)
        if len(s) != byte_size:
            return None
        return s

    def push_offset_and_seek(self, offset):
        '''Push the current file offset and seek to "offset"'''
        self.offsets.append(self.file.tell())
        self.file.seek(offset, 0)

    def pop_offset_and_seek(self):
        """Pop a previously pushed file offset, or do nothing if there were no previously pushed offsets"""
        if len(self.offsets) > 0:
            self.file.seek(self.offsets.pop())

    def get_sint8(self, fail_value=0):
        """Extract a single int8_t from the binary file at the current file position, returns a single integer"""
        s = self.read_size(1)
        if s:
            (v,) = struct.unpack(self.byte_order + "b", s)
            return v
        else:
            return fail_value

    def get_uint8(self, fail_value=0):
        """Extract a single uint8_t from the binary file at the current file position, returns a single integer"""
        s = self.read_size(1)
        if s:
            (v,) = struct.unpack(self.byte_order + "B", s)
            return v
        else:
            return fail_value

    def get_sint16(self, fail_value=0):
        """Extract a single int16_t from the binary file at the current file position, returns a single integer"""
        s = self.read_size(2)
        if s:
            (v,) = struct.unpack(self.byte_order + "h", s)
            return v
        else:
            return fail_value

    def get_uint16(self, fail_value=0):
        """Extract a single uint16_t from the binary file at the current file position, returns a single integer"""
        s = self.read_size(2)
        if s:
            (v,) = struct.unpack(self.byte_order + "H", s)
            return v
        else:
            return fail_value

    def get_sint32(self, fail_value=0):
        """Extract a single int32_t from the binary file at the current file position, returns a single integer"""
        s = self.read_size(4)
        if s:
            (v,) = struct.unpack(self.byte_order + "i", s)
            return v
        else:
            return fail_value

    def get_uint32(self, fail_value=0):
        """Extract a single uint32_t from the binary file at the current file position, returns a single integer"""
        s = self.read_size(4)
        if s:
            (v,) = struct.unpack(self.byte_order + "I", s)
            return v
        else:
            return fail_value

    def get_sint64(self, fail_value=0):
        """Extract a single int64_t from the binary file at the current file position, returns a single integer"""
        s = self.read_size(8)
        if s:
            (v,) = struct.unpack(self.byte_order + "q", s)
            return v
        else:
            return fail_value

    def get_uint64(self, fail_value=0):
        """Extract a single uint64_t from the binary file at the current file position, returns a single integer"""
        s = self.read_size(8)
        if s:
            (v,) = struct.unpack(self.byte_order + "Q", s)
            return v
        else:
            return fail_value

    def get_fixed_length_c_string(
        self, n, fail_value="", isprint_only_with_space_padding=False
    ):
        """Extract a single fixed length C string from the binary file at the current file position, returns a single C string"""
        s = self.read_size(n)
        if s:
            (cstr,) = struct.unpack(self.byte_order + ("%i" % n) + "s", s)
            # Strip trialing NULLs
            cstr = string.strip(cstr, "\0")
            if isprint_only_with_space_padding:
                for c in cstr:
                    if c in string.printable or ord(c) == 0:
                        continue
                    return fail_value
            return cstr
        else:
            return fail_value

    def get_c_string(self):
        """Extract a single NULL terminated C string from the binary file at the current file position, returns a single C string"""
        cstr = ""
        byte = self.get_uint8()
        while byte != 0:
            cstr += "%c" % byte
            byte = self.get_uint8()
        return cstr

    def get_n_sint8(self, n, fail_value=0):
        """Extract "n" int8_t integers from the binary file at the current file position, returns a list of integers"""
        s = self.read_size(n)
        if s:
            return struct.unpack(self.byte_order + ("%u" % n) + "b", s)
        else:
            return (fail_value,) * n

    def get_n_uint8(self, n, fail_value=0):
        """Extract "n" uint8_t integers from the binary file at the current file position, returns a list of integers"""
        s = self.read_size(n)
        if s:
            return struct.unpack(self.byte_order + ("%u" % n) + "B", s)
        else:
            return (fail_value,) * n

    def get_n_sint16(self, n, fail_value=0):
        """Extract "n" int16_t integers from the binary file at the current file position, returns a list of integers"""
        s = self.read_size(2 * n)
        if s:
            return struct.unpack(self.byte_order + ("%u" % n) + "h", s)
        else:
            return (fail_value,) * n

    def get_n_uint16(self, n, fail_value=0):
        """Extract "n" uint16_t integers from the binary file at the current file position, returns a list of integers"""
        s = self.read_size(2 * n)
        if s:
            return struct.unpack(self.byte_order + ("%u" % n) + "H", s)
        else:
            return (fail_value,) * n

    def get_n_sint32(self, n, fail_value=0):
        """Extract "n" int32_t integers from the binary file at the current file position, returns a list of integers"""
        s = self.read_size(4 * n)
        if s:
            return struct.unpack(self.byte_order + ("%u" % n) + "i", s)
        else:
            return (fail_value,) * n

    def get_n_uint32(self, n, fail_value=0):
        """Extract "n" uint32_t integers from the binary file at the current file position, returns a list of integers"""
        s = self.read_size(4 * n)
        if s:
            return struct.unpack(self.byte_order + ("%u" % n) + "I", s)
        else:
            return (fail_value,) * n

    def get_n_sint64(self, n, fail_value=0):
        """Extract "n" int64_t integers from the binary file at the current file position, returns a list of integers"""
        s = self.read_size(8 * n)
        if s:
            return struct.unpack(self.byte_order + ("%u" % n) + "q", s)
        else:
            return (fail_value,) * n

    def get_n_uint64(self, n, fail_value=0):
        """Extract "n" uint64_t integers from the binary file at the current file position, returns a list of integers"""
        s = self.read_size(8 * n)
        if s:
            return struct.unpack(self.byte_order + ("%u" % n) + "Q", s)
        else:
            return (fail_value,) * n
