#
# gdb helper commands and functions for Linux kernel debugging
#
#  common utilities
#
# Copyright (c) Siemens AG, 2011-2013
#
# Authors:
#  Jan Kiszka <jan.kiszka@siemens.com>
#
# This work is licensed under the terms of the GNU GPL version 2.
#

import contextlib
import dataclasses
import re
import typing

import gdb


class CachedType:
    def __init__(self, name):
        self._type = None
        self._name = name

    def _new_objfile_handler(self, event):
        self._type = None
        gdb.events.new_objfile.disconnect(self._new_objfile_handler)

    def get_type(self):
        if self._type is None:
            self._type = gdb.lookup_type(self._name)
            if self._type is None:
                raise gdb.GdbError(
                    "cannot resolve type '{0}'".format(self._name))
            if hasattr(gdb, 'events') and hasattr(gdb.events, 'new_objfile'):
                gdb.events.new_objfile.connect(self._new_objfile_handler)
        return self._type


long_type = CachedType("long")
ulong_type = CachedType("unsigned long")
uint_type = CachedType("unsigned int")
atomic_long_type = CachedType("atomic_long_t")
size_t_type = CachedType("size_t")
struct_page_type = CachedType("struct page")

def get_uint_type():
    global uint_type
    return uint_type.get_type()

def get_page_type():
    global struct_page_type
    return struct_page_type.get_type()

def get_long_type():
    global long_type
    return long_type.get_type()

def get_ulong_type():
    global ulong_type
    return ulong_type.get_type()

def get_size_t_type():
    global size_t_type
    return size_t_type.get_type()

def offset_of(typeobj, field):
    element = gdb.Value(0).cast(typeobj)
    return int(str(element[field].address).split()[0], 16)


def container_of(ptr, typeobj, member):
    return (ptr.cast(get_long_type()) -
            offset_of(typeobj, member)).cast(typeobj)


class ContainerOf(gdb.Function):
    """Return pointer to containing data structure.

$container_of(PTR, "TYPE", "ELEMENT"): Given PTR, return a pointer to the
data structure of the type TYPE in which PTR is the address of ELEMENT.
Note that TYPE and ELEMENT have to be quoted as strings."""

    def __init__(self):
        super(ContainerOf, self).__init__("container_of")

    def invoke(self, ptr, typename, elementname):
        return container_of(ptr, gdb.lookup_type(typename.string()).pointer(),
                            elementname.string())


ContainerOf()


BIG_ENDIAN = 0
LITTLE_ENDIAN = 1
target_endianness = None


def get_target_endianness():
    global target_endianness
    if target_endianness is None:
        endian = gdb.execute("show endian", to_string=True)
        if "little endian" in endian:
            target_endianness = LITTLE_ENDIAN
        elif "big endian" in endian:
            target_endianness = BIG_ENDIAN
        else:
            raise gdb.GdbError("unknown endianness '{0}'".format(str(endian)))
    return target_endianness


def read_memoryview(inf, start, length):
    m = inf.read_memory(start, length)
    if type(m) is memoryview:
        return m
    return memoryview(m)


def read_u16(buffer, offset):
    buffer_val = buffer[offset:offset + 2]
    value = [0, 0]

    if type(buffer_val[0]) is str:
        value[0] = ord(buffer_val[0])
        value[1] = ord(buffer_val[1])
    else:
        value[0] = buffer_val[0]
        value[1] = buffer_val[1]

    if get_target_endianness() == LITTLE_ENDIAN:
        return value[0] + (value[1] << 8)
    else:
        return value[1] + (value[0] << 8)


def read_u32(buffer, offset):
    if get_target_endianness() == LITTLE_ENDIAN:
        return read_u16(buffer, offset) + (read_u16(buffer, offset + 2) << 16)
    else:
        return read_u16(buffer, offset + 2) + (read_u16(buffer, offset) << 16)


def read_u64(buffer, offset):
    if get_target_endianness() == LITTLE_ENDIAN:
        return read_u32(buffer, offset) + (read_u32(buffer, offset + 4) << 32)
    else:
        return read_u32(buffer, offset + 4) + (read_u32(buffer, offset) << 32)


def read_ulong(buffer, offset):
    if get_long_type().sizeof == 8:
        return read_u64(buffer, offset)
    else:
        return read_u32(buffer, offset)

atomic_long_counter_offset = atomic_long_type.get_type()['counter'].bitpos
atomic_long_counter_sizeof = atomic_long_type.get_type()['counter'].type.sizeof

def read_atomic_long(buffer, offset):
    global atomic_long_counter_offset
    global atomic_long_counter_sizeof

    if atomic_long_counter_sizeof == 8:
        return read_u64(buffer, offset + atomic_long_counter_offset)
    else:
        return read_u32(buffer, offset + atomic_long_counter_offset)

target_arch = None


def is_target_arch(arch):
    if hasattr(gdb.Frame, 'architecture'):
        return arch in gdb.newest_frame().architecture().name()
    else:
        global target_arch
        if target_arch is None:
            target_arch = gdb.execute("show architecture", to_string=True)
        return arch in target_arch


GDBSERVER_QEMU = 0
GDBSERVER_KGDB = 1
gdbserver_type = None


def get_gdbserver_type():
    def exit_handler(event):
        global gdbserver_type
        gdbserver_type = None
        gdb.events.exited.disconnect(exit_handler)

    def probe_qemu():
        try:
            return gdb.execute("monitor info version", to_string=True) != ""
        except gdb.error:
            return False

    def probe_kgdb():
        try:
            thread_info = gdb.execute("info thread 1", to_string=True)
            return "shadowCPU" in thread_info
        except gdb.error:
            return False

    global gdbserver_type
    if gdbserver_type is None:
        if probe_qemu():
            gdbserver_type = GDBSERVER_QEMU
        elif probe_kgdb():
            gdbserver_type = GDBSERVER_KGDB
        if gdbserver_type is not None and hasattr(gdb, 'events'):
            gdb.events.exited.connect(exit_handler)
    return gdbserver_type


def gdb_eval_or_none(expresssion):
    try:
        return gdb.parse_and_eval(expresssion)
    except gdb.error:
        return None


@contextlib.contextmanager
def qemu_phy_mem_mode():
    connection = gdb.selected_inferior().connection
    orig = connection.send_packet("qqemu.PhyMemMode")
    if orig not in b"01":
        raise gdb.error("Unexpected qemu.PhyMemMode")
    orig = orig.decode()
    if connection.send_packet("Qqemu.PhyMemMode:1") != b"OK":
        raise gdb.error("Failed to set qemu.PhyMemMode")
    try:
        yield
    finally:
        if connection.send_packet("Qqemu.PhyMemMode:" + orig) != b"OK":
            raise gdb.error("Failed to restore qemu.PhyMemMode")


@dataclasses.dataclass
class VmCore:
    kerneloffset: typing.Optional[int]


def parse_vmcore(s):
    match = re.search(r"KERNELOFFSET=([0-9a-f]+)", s)
    if match is None:
        kerneloffset = None
    else:
        kerneloffset = int(match.group(1), 16)
    return VmCore(kerneloffset=kerneloffset)


def get_vmlinux():
    vmlinux = 'vmlinux'
    for obj in gdb.objfiles():
        if (obj.filename.endswith('vmlinux') or
            obj.filename.endswith('vmlinux.debug')):
            vmlinux = obj.filename
    return vmlinux


@contextlib.contextmanager
def pagination_off():
    show_pagination = gdb.execute("show pagination", to_string=True)
    pagination = show_pagination.endswith("on.\n")
    gdb.execute("set pagination off")
    try:
        yield
    finally:
        gdb.execute("set pagination %s" % ("on" if pagination else "off"))
