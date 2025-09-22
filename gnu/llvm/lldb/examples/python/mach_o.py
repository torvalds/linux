#!/usr/bin/env python

import cmd
import dict_utils
import file_extract
import optparse
import re
import struct
import string
import io
import sys
import uuid

# Mach header "magic" constants
MH_MAGIC = 0xFEEDFACE
MH_CIGAM = 0xCEFAEDFE
MH_MAGIC_64 = 0xFEEDFACF
MH_CIGAM_64 = 0xCFFAEDFE
FAT_MAGIC = 0xCAFEBABE
FAT_CIGAM = 0xBEBAFECA

# Mach haeder "filetype" constants
MH_OBJECT = 0x00000001
MH_EXECUTE = 0x00000002
MH_FVMLIB = 0x00000003
MH_CORE = 0x00000004
MH_PRELOAD = 0x00000005
MH_DYLIB = 0x00000006
MH_DYLINKER = 0x00000007
MH_BUNDLE = 0x00000008
MH_DYLIB_STUB = 0x00000009
MH_DSYM = 0x0000000A
MH_KEXT_BUNDLE = 0x0000000B

# Mach haeder "flag" constant bits
MH_NOUNDEFS = 0x00000001
MH_INCRLINK = 0x00000002
MH_DYLDLINK = 0x00000004
MH_BINDATLOAD = 0x00000008
MH_PREBOUND = 0x00000010
MH_SPLIT_SEGS = 0x00000020
MH_LAZY_INIT = 0x00000040
MH_TWOLEVEL = 0x00000080
MH_FORCE_FLAT = 0x00000100
MH_NOMULTIDEFS = 0x00000200
MH_NOFIXPREBINDING = 0x00000400
MH_PREBINDABLE = 0x00000800
MH_ALLMODSBOUND = 0x00001000
MH_SUBSECTIONS_VIA_SYMBOLS = 0x00002000
MH_CANONICAL = 0x00004000
MH_WEAK_DEFINES = 0x00008000
MH_BINDS_TO_WEAK = 0x00010000
MH_ALLOW_STACK_EXECUTION = 0x00020000
MH_ROOT_SAFE = 0x00040000
MH_SETUID_SAFE = 0x00080000
MH_NO_REEXPORTED_DYLIBS = 0x00100000
MH_PIE = 0x00200000
MH_DEAD_STRIPPABLE_DYLIB = 0x00400000
MH_HAS_TLV_DESCRIPTORS = 0x00800000
MH_NO_HEAP_EXECUTION = 0x01000000

# Mach load command constants
LC_REQ_DYLD = 0x80000000
LC_SEGMENT = 0x00000001
LC_SYMTAB = 0x00000002
LC_SYMSEG = 0x00000003
LC_THREAD = 0x00000004
LC_UNIXTHREAD = 0x00000005
LC_LOADFVMLIB = 0x00000006
LC_IDFVMLIB = 0x00000007
LC_IDENT = 0x00000008
LC_FVMFILE = 0x00000009
LC_PREPAGE = 0x0000000A
LC_DYSYMTAB = 0x0000000B
LC_LOAD_DYLIB = 0x0000000C
LC_ID_DYLIB = 0x0000000D
LC_LOAD_DYLINKER = 0x0000000E
LC_ID_DYLINKER = 0x0000000F
LC_PREBOUND_DYLIB = 0x00000010
LC_ROUTINES = 0x00000011
LC_SUB_FRAMEWORK = 0x00000012
LC_SUB_UMBRELLA = 0x00000013
LC_SUB_CLIENT = 0x00000014
LC_SUB_LIBRARY = 0x00000015
LC_TWOLEVEL_HINTS = 0x00000016
LC_PREBIND_CKSUM = 0x00000017
LC_LOAD_WEAK_DYLIB = 0x00000018 | LC_REQ_DYLD
LC_SEGMENT_64 = 0x00000019
LC_ROUTINES_64 = 0x0000001A
LC_UUID = 0x0000001B
LC_RPATH = 0x0000001C | LC_REQ_DYLD
LC_CODE_SIGNATURE = 0x0000001D
LC_SEGMENT_SPLIT_INFO = 0x0000001E
LC_REEXPORT_DYLIB = 0x0000001F | LC_REQ_DYLD
LC_LAZY_LOAD_DYLIB = 0x00000020
LC_ENCRYPTION_INFO = 0x00000021
LC_DYLD_INFO = 0x00000022
LC_DYLD_INFO_ONLY = 0x00000022 | LC_REQ_DYLD
LC_LOAD_UPWARD_DYLIB = 0x00000023 | LC_REQ_DYLD
LC_VERSION_MIN_MACOSX = 0x00000024
LC_VERSION_MIN_IPHONEOS = 0x00000025
LC_FUNCTION_STARTS = 0x00000026
LC_DYLD_ENVIRONMENT = 0x00000027

# Mach CPU constants
CPU_ARCH_MASK = 0xFF000000
CPU_ARCH_ABI64 = 0x01000000
CPU_TYPE_ANY = 0xFFFFFFFF
CPU_TYPE_VAX = 1
CPU_TYPE_MC680x0 = 6
CPU_TYPE_I386 = 7
CPU_TYPE_X86_64 = CPU_TYPE_I386 | CPU_ARCH_ABI64
CPU_TYPE_MIPS = 8
CPU_TYPE_MC98000 = 10
CPU_TYPE_HPPA = 11
CPU_TYPE_ARM = 12
CPU_TYPE_MC88000 = 13
CPU_TYPE_SPARC = 14
CPU_TYPE_I860 = 15
CPU_TYPE_ALPHA = 16
CPU_TYPE_POWERPC = 18
CPU_TYPE_POWERPC64 = CPU_TYPE_POWERPC | CPU_ARCH_ABI64

# VM protection constants
VM_PROT_READ = 1
VM_PROT_WRITE = 2
VM_PROT_EXECUTE = 4

# VM protection constants
N_STAB = 0xE0
N_PEXT = 0x10
N_TYPE = 0x0E
N_EXT = 0x01

# Values for nlist N_TYPE bits of the "Mach.NList.type" field.
N_UNDF = 0x0
N_ABS = 0x2
N_SECT = 0xE
N_PBUD = 0xC
N_INDR = 0xA

# Section indexes for the "Mach.NList.sect_idx" fields
NO_SECT = 0
MAX_SECT = 255

# Stab defines
N_GSYM = 0x20
N_FNAME = 0x22
N_FUN = 0x24
N_STSYM = 0x26
N_LCSYM = 0x28
N_BNSYM = 0x2E
N_OPT = 0x3C
N_RSYM = 0x40
N_SLINE = 0x44
N_ENSYM = 0x4E
N_SSYM = 0x60
N_SO = 0x64
N_OSO = 0x66
N_LSYM = 0x80
N_BINCL = 0x82
N_SOL = 0x84
N_PARAMS = 0x86
N_VERSION = 0x88
N_OLEVEL = 0x8A
N_PSYM = 0xA0
N_EINCL = 0xA2
N_ENTRY = 0xA4
N_LBRAC = 0xC0
N_EXCL = 0xC2
N_RBRAC = 0xE0
N_BCOMM = 0xE2
N_ECOMM = 0xE4
N_ECOML = 0xE8
N_LENG = 0xFE

vm_prot_names = ["---", "r--", "-w-", "rw-", "--x", "r-x", "-wx", "rwx"]


def dump_memory(base_addr, data, hex_bytes_len, num_per_line):
    hex_bytes = data.encode("hex")
    if hex_bytes_len == -1:
        hex_bytes_len = len(hex_bytes)
    addr = base_addr
    ascii_str = ""
    i = 0
    while i < hex_bytes_len:
        if ((i / 2) % num_per_line) == 0:
            if i > 0:
                print(" %s" % (ascii_str))
                ascii_str = ""
            print("0x%8.8x:" % (addr + i), end=" ")
        hex_byte = hex_bytes[i : i + 2]
        print(hex_byte, end=" ")
        int_byte = int(hex_byte, 16)
        ascii_char = "%c" % (int_byte)
        if int_byte >= 32 and int_byte < 127:
            ascii_str += ascii_char
        else:
            ascii_str += "."
        i = i + 2
    if ascii_str:
        if (i / 2) % num_per_line:
            padding = num_per_line - ((i / 2) % num_per_line)
        else:
            padding = 0
        print("%*s%s" % (padding * 3 + 1, "", ascii_str))
    print()


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
                return "\x1b[43m"
            else:
                return "\x1b[33m"
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


def swap_unpack_char():
    """Returns the unpack prefix that will for non-native endian-ness."""
    if struct.pack("H", 1).startswith("\x00"):
        return "<"
    return ">"


def dump_hex_bytes(addr, s, bytes_per_line=16):
    i = 0
    line = ""
    for ch in s:
        if (i % bytes_per_line) == 0:
            if line:
                print(line)
            line = "%#8.8x: " % (addr + i)
        line += "%02X " % ord(ch)
        i += 1
    print(line)


def dump_hex_byte_string_diff(addr, a, b, bytes_per_line=16):
    i = 0
    line = ""
    a_len = len(a)
    b_len = len(b)
    if a_len < b_len:
        max_len = b_len
    else:
        max_len = a_len
    tty_colors = TerminalColors(True)
    for i in range(max_len):
        ch = None
        if i < a_len:
            ch_a = a[i]
            ch = ch_a
        else:
            ch_a = None
        if i < b_len:
            ch_b = b[i]
            if not ch:
                ch = ch_b
        else:
            ch_b = None
        mismatch = ch_a != ch_b
        if (i % bytes_per_line) == 0:
            if line:
                print(line)
            line = "%#8.8x: " % (addr + i)
        if mismatch:
            line += tty_colors.red()
        line += "%02X " % ord(ch)
        if mismatch:
            line += tty_colors.default()
        i += 1

    print(line)


class Mach:
    """Class that does everything mach-o related"""

    class Arch:
        """Class that implements mach-o architectures"""

        def __init__(self, c=0, s=0):
            self.cpu = c
            self.sub = s

        def set_cpu_type(self, c):
            self.cpu = c

        def set_cpu_subtype(self, s):
            self.sub = s

        def set_arch(self, c, s):
            self.cpu = c
            self.sub = s

        def is_64_bit(self):
            return (self.cpu & CPU_ARCH_ABI64) != 0

        cpu_infos = [
            ["arm", CPU_TYPE_ARM, CPU_TYPE_ANY],
            ["arm", CPU_TYPE_ARM, 0],
            ["armv4", CPU_TYPE_ARM, 5],
            ["armv6", CPU_TYPE_ARM, 6],
            ["armv5", CPU_TYPE_ARM, 7],
            ["xscale", CPU_TYPE_ARM, 8],
            ["armv7", CPU_TYPE_ARM, 9],
            ["armv7f", CPU_TYPE_ARM, 10],
            ["armv7s", CPU_TYPE_ARM, 11],
            ["armv7k", CPU_TYPE_ARM, 12],
            ["armv7m", CPU_TYPE_ARM, 15],
            ["armv7em", CPU_TYPE_ARM, 16],
            ["ppc", CPU_TYPE_POWERPC, CPU_TYPE_ANY],
            ["ppc", CPU_TYPE_POWERPC, 0],
            ["ppc601", CPU_TYPE_POWERPC, 1],
            ["ppc602", CPU_TYPE_POWERPC, 2],
            ["ppc603", CPU_TYPE_POWERPC, 3],
            ["ppc603e", CPU_TYPE_POWERPC, 4],
            ["ppc603ev", CPU_TYPE_POWERPC, 5],
            ["ppc604", CPU_TYPE_POWERPC, 6],
            ["ppc604e", CPU_TYPE_POWERPC, 7],
            ["ppc620", CPU_TYPE_POWERPC, 8],
            ["ppc750", CPU_TYPE_POWERPC, 9],
            ["ppc7400", CPU_TYPE_POWERPC, 10],
            ["ppc7450", CPU_TYPE_POWERPC, 11],
            ["ppc970", CPU_TYPE_POWERPC, 100],
            ["ppc64", CPU_TYPE_POWERPC64, 0],
            ["ppc970-64", CPU_TYPE_POWERPC64, 100],
            ["i386", CPU_TYPE_I386, 3],
            ["i486", CPU_TYPE_I386, 4],
            ["i486sx", CPU_TYPE_I386, 0x84],
            ["i386", CPU_TYPE_I386, CPU_TYPE_ANY],
            ["x86_64", CPU_TYPE_X86_64, 3],
            ["x86_64", CPU_TYPE_X86_64, CPU_TYPE_ANY],
        ]

        def __str__(self):
            for info in self.cpu_infos:
                if self.cpu == info[1] and (self.sub & 0x00FFFFFF) == info[2]:
                    return info[0]
            return "{0}.{1}".format(self.cpu, self.sub)

    class Magic(dict_utils.Enum):
        enum = {
            "MH_MAGIC": MH_MAGIC,
            "MH_CIGAM": MH_CIGAM,
            "MH_MAGIC_64": MH_MAGIC_64,
            "MH_CIGAM_64": MH_CIGAM_64,
            "FAT_MAGIC": FAT_MAGIC,
            "FAT_CIGAM": FAT_CIGAM,
        }

        def __init__(self, initial_value=0):
            dict_utils.Enum.__init__(self, initial_value, self.enum)

        def is_skinny_mach_file(self):
            return (
                self.value == MH_MAGIC
                or self.value == MH_CIGAM
                or self.value == MH_MAGIC_64
                or self.value == MH_CIGAM_64
            )

        def is_universal_mach_file(self):
            return self.value == FAT_MAGIC or self.value == FAT_CIGAM

        def unpack(self, data):
            data.set_byte_order("native")
            self.value = data.get_uint32()

        def get_byte_order(self):
            if (
                self.value == MH_CIGAM
                or self.value == MH_CIGAM_64
                or self.value == FAT_CIGAM
            ):
                return swap_unpack_char()
            else:
                return "="

        def is_64_bit(self):
            return self.value == MH_MAGIC_64 or self.value == MH_CIGAM_64

    def __init__(self):
        self.magic = Mach.Magic()
        self.content = None
        self.path = None

    def extract(self, path, extractor):
        self.path = path
        self.unpack(extractor)

    def parse(self, path):
        self.path = path
        try:
            f = open(self.path)
            file_extractor = file_extract.FileExtract(f, "=")
            self.unpack(file_extractor)
            # f.close()
        except IOError as xxx_todo_changeme:
            (errno, strerror) = xxx_todo_changeme.args
            print("I/O error({0}): {1}".format(errno, strerror))
        except ValueError:
            print("Could not convert data to an integer.")
        except:
            print("Unexpected error:", sys.exc_info()[0])
            raise

    def compare(self, rhs):
        self.content.compare(rhs.content)

    def dump(self, options=None):
        self.content.dump(options)

    def dump_header(self, dump_description=True, options=None):
        self.content.dump_header(dump_description, options)

    def dump_load_commands(self, dump_description=True, options=None):
        self.content.dump_load_commands(dump_description, options)

    def dump_sections(self, dump_description=True, options=None):
        self.content.dump_sections(dump_description, options)

    def dump_section_contents(self, options):
        self.content.dump_section_contents(options)

    def dump_symtab(self, dump_description=True, options=None):
        self.content.dump_symtab(dump_description, options)

    def dump_symbol_names_matching_regex(self, regex, file=None):
        self.content.dump_symbol_names_matching_regex(regex, file)

    def description(self):
        return self.content.description()

    def unpack(self, data):
        self.magic.unpack(data)
        if self.magic.is_skinny_mach_file():
            self.content = Mach.Skinny(self.path)
        elif self.magic.is_universal_mach_file():
            self.content = Mach.Universal(self.path)
        else:
            self.content = None

        if self.content is not None:
            self.content.unpack(data, self.magic)

    def is_valid(self):
        return self.content is not None

    class Universal:
        def __init__(self, path):
            self.path = path
            self.type = "universal"
            self.file_off = 0
            self.magic = None
            self.nfat_arch = 0
            self.archs = list()

        def description(self):
            s = "%#8.8x: %s (" % (self.file_off, self.path)
            archs_string = ""
            for arch in self.archs:
                if len(archs_string):
                    archs_string += ", "
                archs_string += "%s" % arch.arch
            s += archs_string
            s += ")"
            return s

        def unpack(self, data, magic=None):
            self.file_off = data.tell()
            if magic is None:
                self.magic = Mach.Magic()
                self.magic.unpack(data)
            else:
                self.magic = magic
                self.file_off = self.file_off - 4
            # Universal headers are always in big endian
            data.set_byte_order("big")
            self.nfat_arch = data.get_uint32()
            for i in range(self.nfat_arch):
                self.archs.append(Mach.Universal.ArchInfo())
                self.archs[i].unpack(data)
            for i in range(self.nfat_arch):
                self.archs[i].mach = Mach.Skinny(self.path)
                data.seek(self.archs[i].offset, 0)
                skinny_magic = Mach.Magic()
                skinny_magic.unpack(data)
                self.archs[i].mach.unpack(data, skinny_magic)

        def compare(self, rhs):
            print("error: comparing two universal files is not supported yet")
            return False

        def dump(self, options):
            if options.dump_header:
                print()
                print(
                    "Universal Mach File: magic = %s, nfat_arch = %u"
                    % (self.magic, self.nfat_arch)
                )
                print()
            if self.nfat_arch > 0:
                if options.dump_header:
                    self.archs[0].dump_header(True, options)
                    for i in range(self.nfat_arch):
                        self.archs[i].dump_flat(options)
                if options.dump_header:
                    print()
                for i in range(self.nfat_arch):
                    self.archs[i].mach.dump(options)

        def dump_header(self, dump_description=True, options=None):
            if dump_description:
                print(self.description())
            for i in range(self.nfat_arch):
                self.archs[i].mach.dump_header(True, options)
                print()

        def dump_load_commands(self, dump_description=True, options=None):
            if dump_description:
                print(self.description())
            for i in range(self.nfat_arch):
                self.archs[i].mach.dump_load_commands(True, options)
                print()

        def dump_sections(self, dump_description=True, options=None):
            if dump_description:
                print(self.description())
            for i in range(self.nfat_arch):
                self.archs[i].mach.dump_sections(True, options)
                print()

        def dump_section_contents(self, options):
            for i in range(self.nfat_arch):
                self.archs[i].mach.dump_section_contents(options)
                print()

        def dump_symtab(self, dump_description=True, options=None):
            if dump_description:
                print(self.description())
            for i in range(self.nfat_arch):
                self.archs[i].mach.dump_symtab(True, options)
                print()

        def dump_symbol_names_matching_regex(self, regex, file=None):
            for i in range(self.nfat_arch):
                self.archs[i].mach.dump_symbol_names_matching_regex(regex, file)

        class ArchInfo:
            def __init__(self):
                self.arch = Mach.Arch(0, 0)
                self.offset = 0
                self.size = 0
                self.align = 0
                self.mach = None

            def unpack(self, data):
                # Universal headers are always in big endian
                data.set_byte_order("big")
                (
                    self.arch.cpu,
                    self.arch.sub,
                    self.offset,
                    self.size,
                    self.align,
                ) = data.get_n_uint32(5)

            def dump_header(self, dump_description=True, options=None):
                if options.verbose:
                    print("CPU        SUBTYPE    OFFSET     SIZE       ALIGN")
                    print("---------- ---------- ---------- ---------- ----------")
                else:
                    print("ARCH       FILEOFFSET FILESIZE   ALIGN")
                    print("---------- ---------- ---------- ----------")

            def dump_flat(self, options):
                if options.verbose:
                    print(
                        "%#8.8x %#8.8x %#8.8x %#8.8x %#8.8x"
                        % (
                            self.arch.cpu,
                            self.arch.sub,
                            self.offset,
                            self.size,
                            self.align,
                        )
                    )
                else:
                    print(
                        "%-10s %#8.8x %#8.8x %#8.8x"
                        % (self.arch, self.offset, self.size, self.align)
                    )

            def dump(self):
                print("   cputype: %#8.8x" % self.arch.cpu)
                print("cpusubtype: %#8.8x" % self.arch.sub)
                print("    offset: %#8.8x" % self.offset)
                print("      size: %#8.8x" % self.size)
                print("     align: %#8.8x" % self.align)

            def __str__(self):
                return "Mach.Universal.ArchInfo: %#8.8x %#8.8x %#8.8x %#8.8x %#8.8x" % (
                    self.arch.cpu,
                    self.arch.sub,
                    self.offset,
                    self.size,
                    self.align,
                )

            def __repr__(self):
                return "Mach.Universal.ArchInfo: %#8.8x %#8.8x %#8.8x %#8.8x %#8.8x" % (
                    self.arch.cpu,
                    self.arch.sub,
                    self.offset,
                    self.size,
                    self.align,
                )

    class Flags:
        def __init__(self, b):
            self.bits = b

        def __str__(self):
            s = ""
            if self.bits & MH_NOUNDEFS:
                s += "MH_NOUNDEFS | "
            if self.bits & MH_INCRLINK:
                s += "MH_INCRLINK | "
            if self.bits & MH_DYLDLINK:
                s += "MH_DYLDLINK | "
            if self.bits & MH_BINDATLOAD:
                s += "MH_BINDATLOAD | "
            if self.bits & MH_PREBOUND:
                s += "MH_PREBOUND | "
            if self.bits & MH_SPLIT_SEGS:
                s += "MH_SPLIT_SEGS | "
            if self.bits & MH_LAZY_INIT:
                s += "MH_LAZY_INIT | "
            if self.bits & MH_TWOLEVEL:
                s += "MH_TWOLEVEL | "
            if self.bits & MH_FORCE_FLAT:
                s += "MH_FORCE_FLAT | "
            if self.bits & MH_NOMULTIDEFS:
                s += "MH_NOMULTIDEFS | "
            if self.bits & MH_NOFIXPREBINDING:
                s += "MH_NOFIXPREBINDING | "
            if self.bits & MH_PREBINDABLE:
                s += "MH_PREBINDABLE | "
            if self.bits & MH_ALLMODSBOUND:
                s += "MH_ALLMODSBOUND | "
            if self.bits & MH_SUBSECTIONS_VIA_SYMBOLS:
                s += "MH_SUBSECTIONS_VIA_SYMBOLS | "
            if self.bits & MH_CANONICAL:
                s += "MH_CANONICAL | "
            if self.bits & MH_WEAK_DEFINES:
                s += "MH_WEAK_DEFINES | "
            if self.bits & MH_BINDS_TO_WEAK:
                s += "MH_BINDS_TO_WEAK | "
            if self.bits & MH_ALLOW_STACK_EXECUTION:
                s += "MH_ALLOW_STACK_EXECUTION | "
            if self.bits & MH_ROOT_SAFE:
                s += "MH_ROOT_SAFE | "
            if self.bits & MH_SETUID_SAFE:
                s += "MH_SETUID_SAFE | "
            if self.bits & MH_NO_REEXPORTED_DYLIBS:
                s += "MH_NO_REEXPORTED_DYLIBS | "
            if self.bits & MH_PIE:
                s += "MH_PIE | "
            if self.bits & MH_DEAD_STRIPPABLE_DYLIB:
                s += "MH_DEAD_STRIPPABLE_DYLIB | "
            if self.bits & MH_HAS_TLV_DESCRIPTORS:
                s += "MH_HAS_TLV_DESCRIPTORS | "
            if self.bits & MH_NO_HEAP_EXECUTION:
                s += "MH_NO_HEAP_EXECUTION | "
            # Strip the trailing " |" if we have any flags
            if len(s) > 0:
                s = s[0:-2]
            return s

    class FileType(dict_utils.Enum):
        enum = {
            "MH_OBJECT": MH_OBJECT,
            "MH_EXECUTE": MH_EXECUTE,
            "MH_FVMLIB": MH_FVMLIB,
            "MH_CORE": MH_CORE,
            "MH_PRELOAD": MH_PRELOAD,
            "MH_DYLIB": MH_DYLIB,
            "MH_DYLINKER": MH_DYLINKER,
            "MH_BUNDLE": MH_BUNDLE,
            "MH_DYLIB_STUB": MH_DYLIB_STUB,
            "MH_DSYM": MH_DSYM,
            "MH_KEXT_BUNDLE": MH_KEXT_BUNDLE,
        }

        def __init__(self, initial_value=0):
            dict_utils.Enum.__init__(self, initial_value, self.enum)

    class Skinny:
        def __init__(self, path):
            self.path = path
            self.type = "skinny"
            self.data = None
            self.file_off = 0
            self.magic = 0
            self.arch = Mach.Arch(0, 0)
            self.filetype = Mach.FileType(0)
            self.ncmds = 0
            self.sizeofcmds = 0
            self.flags = Mach.Flags(0)
            self.uuid = None
            self.commands = list()
            self.segments = list()
            self.sections = list()
            self.symbols = list()
            self.sections.append(Mach.Section())

        def description(self):
            return "%#8.8x: %s (%s)" % (self.file_off, self.path, self.arch)

        def unpack(self, data, magic=None):
            self.data = data
            self.file_off = data.tell()
            if magic is None:
                self.magic = Mach.Magic()
                self.magic.unpack(data)
            else:
                self.magic = magic
                self.file_off = self.file_off - 4
            data.set_byte_order(self.magic.get_byte_order())
            (
                self.arch.cpu,
                self.arch.sub,
                self.filetype.value,
                self.ncmds,
                self.sizeofcmds,
                bits,
            ) = data.get_n_uint32(6)
            self.flags.bits = bits

            if self.is_64_bit():
                data.get_uint32()  # Skip reserved word in mach_header_64

            for i in range(0, self.ncmds):
                lc = self.unpack_load_command(data)
                self.commands.append(lc)

        def get_data(self):
            if self.data:
                self.data.set_byte_order(self.magic.get_byte_order())
                return self.data
            return None

        def unpack_load_command(self, data):
            lc = Mach.LoadCommand()
            lc.unpack(self, data)
            lc_command = lc.command.get_enum_value()
            if lc_command == LC_SEGMENT or lc_command == LC_SEGMENT_64:
                lc = Mach.SegmentLoadCommand(lc)
                lc.unpack(self, data)
            elif (
                lc_command == LC_LOAD_DYLIB
                or lc_command == LC_ID_DYLIB
                or lc_command == LC_LOAD_WEAK_DYLIB
                or lc_command == LC_REEXPORT_DYLIB
            ):
                lc = Mach.DylibLoadCommand(lc)
                lc.unpack(self, data)
            elif (
                lc_command == LC_LOAD_DYLINKER
                or lc_command == LC_SUB_FRAMEWORK
                or lc_command == LC_SUB_CLIENT
                or lc_command == LC_SUB_UMBRELLA
                or lc_command == LC_SUB_LIBRARY
                or lc_command == LC_ID_DYLINKER
                or lc_command == LC_RPATH
            ):
                lc = Mach.LoadDYLDLoadCommand(lc)
                lc.unpack(self, data)
            elif lc_command == LC_DYLD_INFO_ONLY:
                lc = Mach.DYLDInfoOnlyLoadCommand(lc)
                lc.unpack(self, data)
            elif lc_command == LC_SYMTAB:
                lc = Mach.SymtabLoadCommand(lc)
                lc.unpack(self, data)
            elif lc_command == LC_DYSYMTAB:
                lc = Mach.DYLDSymtabLoadCommand(lc)
                lc.unpack(self, data)
            elif lc_command == LC_UUID:
                lc = Mach.UUIDLoadCommand(lc)
                lc.unpack(self, data)
            elif (
                lc_command == LC_CODE_SIGNATURE
                or lc_command == LC_SEGMENT_SPLIT_INFO
                or lc_command == LC_FUNCTION_STARTS
            ):
                lc = Mach.DataBlobLoadCommand(lc)
                lc.unpack(self, data)
            elif lc_command == LC_UNIXTHREAD:
                lc = Mach.UnixThreadLoadCommand(lc)
                lc.unpack(self, data)
            elif lc_command == LC_ENCRYPTION_INFO:
                lc = Mach.EncryptionInfoLoadCommand(lc)
                lc.unpack(self, data)
            lc.skip(data)
            return lc

        def compare(self, rhs):
            print("\nComparing:")
            print("a) %s %s" % (self.arch, self.path))
            print("b) %s %s" % (rhs.arch, rhs.path))
            result = True
            if self.type == rhs.type:
                for lhs_section in self.sections[1:]:
                    rhs_section = rhs.get_section_by_section(lhs_section)
                    if rhs_section:
                        print(
                            "comparing %s.%s..."
                            % (lhs_section.segname, lhs_section.sectname),
                            end=" ",
                        )
                        sys.stdout.flush()
                        lhs_data = lhs_section.get_contents(self)
                        rhs_data = rhs_section.get_contents(rhs)
                        if lhs_data and rhs_data:
                            if lhs_data == rhs_data:
                                print("ok")
                            else:
                                lhs_data_len = len(lhs_data)
                                rhs_data_len = len(rhs_data)
                                # if lhs_data_len < rhs_data_len:
                                #     if lhs_data == rhs_data[0:lhs_data_len]:
                                #         print 'section data for %s matches the first %u bytes' % (lhs_section.sectname, lhs_data_len)
                                #     else:
                                #         # TODO: check padding
                                #         result = False
                                # elif lhs_data_len > rhs_data_len:
                                #     if lhs_data[0:rhs_data_len] == rhs_data:
                                #         print 'section data for %s matches the first %u bytes' % (lhs_section.sectname, lhs_data_len)
                                #     else:
                                #         # TODO: check padding
                                #         result = False
                                # else:
                                result = False
                                print("error: sections differ")
                                # print 'a) %s' % (lhs_section)
                                # dump_hex_byte_string_diff(0, lhs_data, rhs_data)
                                # print 'b) %s' % (rhs_section)
                                # dump_hex_byte_string_diff(0, rhs_data, lhs_data)
                        elif lhs_data and not rhs_data:
                            print("error: section data missing from b:")
                            print("a) %s" % (lhs_section))
                            print("b) %s" % (rhs_section))
                            result = False
                        elif not lhs_data and rhs_data:
                            print("error: section data missing from a:")
                            print("a) %s" % (lhs_section))
                            print("b) %s" % (rhs_section))
                            result = False
                        elif lhs_section.offset or rhs_section.offset:
                            print("error: section data missing for both a and b:")
                            print("a) %s" % (lhs_section))
                            print("b) %s" % (rhs_section))
                            result = False
                        else:
                            print("ok")
                    else:
                        result = False
                        print(
                            "error: section %s is missing in %s"
                            % (lhs_section.sectname, rhs.path)
                        )
            else:
                print(
                    "error: comparing a %s mach-o file with a %s mach-o file is not supported"
                    % (self.type, rhs.type)
                )
                result = False
            if not result:
                print("error: mach files differ")
            return result

        def dump_header(self, dump_description=True, options=None):
            if options.verbose:
                print(
                    "MAGIC      CPU        SUBTYPE    FILETYPE   NUM CMDS SIZE CMDS  FLAGS"
                )
                print(
                    "---------- ---------- ---------- ---------- -------- ---------- ----------"
                )
            else:
                print(
                    "MAGIC        ARCH       FILETYPE       NUM CMDS SIZE CMDS  FLAGS"
                )
                print(
                    "------------ ---------- -------------- -------- ---------- ----------"
                )

        def dump_flat(self, options):
            if options.verbose:
                print(
                    "%#8.8x %#8.8x %#8.8x %#8.8x %#8u %#8.8x %#8.8x"
                    % (
                        self.magic,
                        self.arch.cpu,
                        self.arch.sub,
                        self.filetype.value,
                        self.ncmds,
                        self.sizeofcmds,
                        self.flags.bits,
                    )
                )
            else:
                print(
                    "%-12s %-10s %-14s %#8u %#8.8x %s"
                    % (
                        self.magic,
                        self.arch,
                        self.filetype,
                        self.ncmds,
                        self.sizeofcmds,
                        self.flags,
                    )
                )

        def dump(self, options):
            if options.dump_header:
                self.dump_header(True, options)
            if options.dump_load_commands:
                self.dump_load_commands(False, options)
            if options.dump_sections:
                self.dump_sections(False, options)
            if options.section_names:
                self.dump_section_contents(options)
            if options.dump_symtab:
                self.get_symtab()
                if len(self.symbols):
                    self.dump_sections(False, options)
                else:
                    print("No symbols")
            if options.find_mangled:
                self.dump_symbol_names_matching_regex(re.compile("^_?_Z"))

        def dump_header(self, dump_description=True, options=None):
            if dump_description:
                print(self.description())
            print("Mach Header")
            print("       magic: %#8.8x %s" % (self.magic.value, self.magic))
            print("     cputype: %#8.8x %s" % (self.arch.cpu, self.arch))
            print("  cpusubtype: %#8.8x" % self.arch.sub)
            print(
                "    filetype: %#8.8x %s"
                % (self.filetype.get_enum_value(), self.filetype.get_enum_name())
            )
            print("       ncmds: %#8.8x %u" % (self.ncmds, self.ncmds))
            print("  sizeofcmds: %#8.8x" % self.sizeofcmds)
            print("       flags: %#8.8x %s" % (self.flags.bits, self.flags))

        def dump_load_commands(self, dump_description=True, options=None):
            if dump_description:
                print(self.description())
            for lc in self.commands:
                print(lc)

        def get_section_by_name(self, name):
            for section in self.sections:
                if section.sectname and section.sectname == name:
                    return section
            return None

        def get_section_by_section(self, other_section):
            for section in self.sections:
                if (
                    section.sectname == other_section.sectname
                    and section.segname == other_section.segname
                ):
                    return section
            return None

        def dump_sections(self, dump_description=True, options=None):
            if dump_description:
                print(self.description())
            num_sections = len(self.sections)
            if num_sections > 1:
                self.sections[1].dump_header()
                for sect_idx in range(1, num_sections):
                    print("%s" % self.sections[sect_idx])

        def dump_section_contents(self, options):
            saved_section_to_disk = False
            for sectname in options.section_names:
                section = self.get_section_by_name(sectname)
                if section:
                    sect_bytes = section.get_contents(self)
                    if options.outfile:
                        if not saved_section_to_disk:
                            outfile = open(options.outfile, "w")
                            if options.extract_modules:
                                # print "Extracting modules from mach file..."
                                data = file_extract.FileExtract(
                                    io.BytesIO(sect_bytes), self.data.byte_order
                                )
                                version = data.get_uint32()
                                num_modules = data.get_uint32()
                                # print "version = %u, num_modules = %u" %
                                # (version, num_modules)
                                for i in range(num_modules):
                                    data_offset = data.get_uint64()
                                    data_size = data.get_uint64()
                                    name_offset = data.get_uint32()
                                    language = data.get_uint32()
                                    flags = data.get_uint32()
                                    data.seek(name_offset)
                                    module_name = data.get_c_string()
                                    # print "module[%u] data_offset = %#16.16x,
                                    # data_size = %#16.16x, name_offset =
                                    # %#16.16x (%s), language = %u, flags =
                                    # %#x" % (i, data_offset, data_size,
                                    # name_offset, module_name, language,
                                    # flags)
                                    data.seek(data_offset)
                                    outfile.write(data.read_size(data_size))
                            else:
                                print(
                                    "Saving section %s to '%s'"
                                    % (sectname, options.outfile)
                                )
                                outfile.write(sect_bytes)
                            outfile.close()
                            saved_section_to_disk = True
                        else:
                            print(
                                "error: you can only save a single section to disk at a time, skipping section '%s'"
                                % (sectname)
                            )
                    else:
                        print("section %s:\n" % (sectname))
                        section.dump_header()
                        print("%s\n" % (section))
                        dump_memory(0, sect_bytes, options.max_count, 16)
                else:
                    print('error: no section named "%s" was found' % (sectname))

        def get_segment(self, segname):
            if len(self.segments) == 1 and self.segments[0].segname == "":
                return self.segments[0]
            for segment in self.segments:
                if segment.segname == segname:
                    return segment
            return None

        def get_first_load_command(self, lc_enum_value):
            for lc in self.commands:
                if lc.command.value == lc_enum_value:
                    return lc
            return None

        def get_symtab(self):
            if self.data and not self.symbols:
                lc_symtab = self.get_first_load_command(LC_SYMTAB)
                if lc_symtab:
                    symtab_offset = self.file_off
                    if self.data.is_in_memory():
                        linkedit_segment = self.get_segment("__LINKEDIT")
                        if linkedit_segment:
                            linkedit_vmaddr = linkedit_segment.vmaddr
                            linkedit_fileoff = linkedit_segment.fileoff
                            symtab_offset = (
                                linkedit_vmaddr + lc_symtab.symoff - linkedit_fileoff
                            )
                            symtab_offset = (
                                linkedit_vmaddr + lc_symtab.stroff - linkedit_fileoff
                            )
                    else:
                        symtab_offset += lc_symtab.symoff

                    self.data.seek(symtab_offset)
                    is_64 = self.is_64_bit()
                    for i in range(lc_symtab.nsyms):
                        nlist = Mach.NList()
                        nlist.unpack(self, self.data, lc_symtab)
                        self.symbols.append(nlist)
                else:
                    print("no LC_SYMTAB")

        def dump_symtab(self, dump_description=True, options=None):
            self.get_symtab()
            if dump_description:
                print(self.description())
            for i, symbol in enumerate(self.symbols):
                print("[%5u] %s" % (i, symbol))

        def dump_symbol_names_matching_regex(self, regex, file=None):
            self.get_symtab()
            for symbol in self.symbols:
                if symbol.name and regex.search(symbol.name):
                    print(symbol.name)
                    if file:
                        file.write("%s\n" % (symbol.name))

        def is_64_bit(self):
            return self.magic.is_64_bit()

    class LoadCommand:
        class Command(dict_utils.Enum):
            enum = {
                "LC_SEGMENT": LC_SEGMENT,
                "LC_SYMTAB": LC_SYMTAB,
                "LC_SYMSEG": LC_SYMSEG,
                "LC_THREAD": LC_THREAD,
                "LC_UNIXTHREAD": LC_UNIXTHREAD,
                "LC_LOADFVMLIB": LC_LOADFVMLIB,
                "LC_IDFVMLIB": LC_IDFVMLIB,
                "LC_IDENT": LC_IDENT,
                "LC_FVMFILE": LC_FVMFILE,
                "LC_PREPAGE": LC_PREPAGE,
                "LC_DYSYMTAB": LC_DYSYMTAB,
                "LC_LOAD_DYLIB": LC_LOAD_DYLIB,
                "LC_ID_DYLIB": LC_ID_DYLIB,
                "LC_LOAD_DYLINKER": LC_LOAD_DYLINKER,
                "LC_ID_DYLINKER": LC_ID_DYLINKER,
                "LC_PREBOUND_DYLIB": LC_PREBOUND_DYLIB,
                "LC_ROUTINES": LC_ROUTINES,
                "LC_SUB_FRAMEWORK": LC_SUB_FRAMEWORK,
                "LC_SUB_UMBRELLA": LC_SUB_UMBRELLA,
                "LC_SUB_CLIENT": LC_SUB_CLIENT,
                "LC_SUB_LIBRARY": LC_SUB_LIBRARY,
                "LC_TWOLEVEL_HINTS": LC_TWOLEVEL_HINTS,
                "LC_PREBIND_CKSUM": LC_PREBIND_CKSUM,
                "LC_LOAD_WEAK_DYLIB": LC_LOAD_WEAK_DYLIB,
                "LC_SEGMENT_64": LC_SEGMENT_64,
                "LC_ROUTINES_64": LC_ROUTINES_64,
                "LC_UUID": LC_UUID,
                "LC_RPATH": LC_RPATH,
                "LC_CODE_SIGNATURE": LC_CODE_SIGNATURE,
                "LC_SEGMENT_SPLIT_INFO": LC_SEGMENT_SPLIT_INFO,
                "LC_REEXPORT_DYLIB": LC_REEXPORT_DYLIB,
                "LC_LAZY_LOAD_DYLIB": LC_LAZY_LOAD_DYLIB,
                "LC_ENCRYPTION_INFO": LC_ENCRYPTION_INFO,
                "LC_DYLD_INFO": LC_DYLD_INFO,
                "LC_DYLD_INFO_ONLY": LC_DYLD_INFO_ONLY,
                "LC_LOAD_UPWARD_DYLIB": LC_LOAD_UPWARD_DYLIB,
                "LC_VERSION_MIN_MACOSX": LC_VERSION_MIN_MACOSX,
                "LC_VERSION_MIN_IPHONEOS": LC_VERSION_MIN_IPHONEOS,
                "LC_FUNCTION_STARTS": LC_FUNCTION_STARTS,
                "LC_DYLD_ENVIRONMENT": LC_DYLD_ENVIRONMENT,
            }

            def __init__(self, initial_value=0):
                dict_utils.Enum.__init__(self, initial_value, self.enum)

        def __init__(self, c=None, l=0, o=0):
            if c is not None:
                self.command = c
            else:
                self.command = Mach.LoadCommand.Command(0)
            self.length = l
            self.file_off = o

        def unpack(self, mach_file, data):
            self.file_off = data.tell()
            self.command.value, self.length = data.get_n_uint32(2)

        def skip(self, data):
            data.seek(self.file_off + self.length, 0)

        def __str__(self):
            lc_name = self.command.get_enum_name()
            return "%#8.8x: <%#4.4x> %-24s" % (self.file_off, self.length, lc_name)

    class Section:
        def __init__(self):
            self.index = 0
            self.is_64 = False
            self.sectname = None
            self.segname = None
            self.addr = 0
            self.size = 0
            self.offset = 0
            self.align = 0
            self.reloff = 0
            self.nreloc = 0
            self.flags = 0
            self.reserved1 = 0
            self.reserved2 = 0
            self.reserved3 = 0

        def unpack(self, is_64, data):
            self.is_64 = is_64
            self.sectname = data.get_fixed_length_c_string(16, "", True)
            self.segname = data.get_fixed_length_c_string(16, "", True)
            if self.is_64:
                self.addr, self.size = data.get_n_uint64(2)
                (
                    self.offset,
                    self.align,
                    self.reloff,
                    self.nreloc,
                    self.flags,
                    self.reserved1,
                    self.reserved2,
                    self.reserved3,
                ) = data.get_n_uint32(8)
            else:
                self.addr, self.size = data.get_n_uint32(2)
                (
                    self.offset,
                    self.align,
                    self.reloff,
                    self.nreloc,
                    self.flags,
                    self.reserved1,
                    self.reserved2,
                ) = data.get_n_uint32(7)

        def dump_header(self):
            if self.is_64:
                print(
                    "INDEX ADDRESS            SIZE               OFFSET     ALIGN      RELOFF     NRELOC     FLAGS      RESERVED1  RESERVED2  RESERVED3  NAME"
                )
                print(
                    "===== ------------------ ------------------ ---------- ---------- ---------- ---------- ---------- ---------- ---------- ---------- ----------------------"
                )
            else:
                print(
                    "INDEX ADDRESS    SIZE       OFFSET     ALIGN      RELOFF     NRELOC     FLAGS      RESERVED1  RESERVED2  NAME"
                )
                print(
                    "===== ---------- ---------- ---------- ---------- ---------- ---------- ---------- ---------- ---------- ----------------------"
                )

        def __str__(self):
            if self.is_64:
                return (
                    "[%3u] %#16.16x %#16.16x %#8.8x %#8.8x %#8.8x %#8.8x %#8.8x %#8.8x %#8.8x %#8.8x %s.%s"
                    % (
                        self.index,
                        self.addr,
                        self.size,
                        self.offset,
                        self.align,
                        self.reloff,
                        self.nreloc,
                        self.flags,
                        self.reserved1,
                        self.reserved2,
                        self.reserved3,
                        self.segname,
                        self.sectname,
                    )
                )
            else:
                return (
                    "[%3u] %#8.8x %#8.8x %#8.8x %#8.8x %#8.8x %#8.8x %#8.8x %#8.8x %#8.8x %s.%s"
                    % (
                        self.index,
                        self.addr,
                        self.size,
                        self.offset,
                        self.align,
                        self.reloff,
                        self.nreloc,
                        self.flags,
                        self.reserved1,
                        self.reserved2,
                        self.segname,
                        self.sectname,
                    )
                )

        def get_contents(self, mach_file):
            """Get the section contents as a python string"""
            if self.size > 0 and mach_file.get_segment(self.segname).filesize > 0:
                data = mach_file.get_data()
                if data:
                    section_data_offset = mach_file.file_off + self.offset
                    # print '%s.%s is at offset 0x%x with size 0x%x' %
                    # (self.segname, self.sectname, section_data_offset,
                    # self.size)
                    data.push_offset_and_seek(section_data_offset)
                    bytes = data.read_size(self.size)
                    data.pop_offset_and_seek()
                    return bytes
            return None

    class DylibLoadCommand(LoadCommand):
        def __init__(self, lc):
            Mach.LoadCommand.__init__(self, lc.command, lc.length, lc.file_off)
            self.name = None
            self.timestamp = 0
            self.current_version = 0
            self.compatibility_version = 0

        def unpack(self, mach_file, data):
            byte_order_char = mach_file.magic.get_byte_order()
            (
                name_offset,
                self.timestamp,
                self.current_version,
                self.compatibility_version,
            ) = data.get_n_uint32(4)
            data.seek(self.file_off + name_offset, 0)
            self.name = data.get_fixed_length_c_string(self.length - 24)

        def __str__(self):
            s = Mach.LoadCommand.__str__(self)
            s += "%#8.8x %#8.8x %#8.8x " % (
                self.timestamp,
                self.current_version,
                self.compatibility_version,
            )
            s += self.name
            return s

    class LoadDYLDLoadCommand(LoadCommand):
        def __init__(self, lc):
            Mach.LoadCommand.__init__(self, lc.command, lc.length, lc.file_off)
            self.name = None

        def unpack(self, mach_file, data):
            data.get_uint32()
            self.name = data.get_fixed_length_c_string(self.length - 12)

        def __str__(self):
            s = Mach.LoadCommand.__str__(self)
            s += "%s" % self.name
            return s

    class UnixThreadLoadCommand(LoadCommand):
        class ThreadState:
            def __init__(self):
                self.flavor = 0
                self.count = 0
                self.register_values = list()

            def unpack(self, data):
                self.flavor, self.count = data.get_n_uint32(2)
                self.register_values = data.get_n_uint32(self.count)

            def __str__(self):
                s = "flavor = %u, count = %u, regs =" % (self.flavor, self.count)
                i = 0
                for register_value in self.register_values:
                    if i % 8 == 0:
                        s += "\n                                            "
                    s += " %#8.8x" % register_value
                    i += 1
                return s

        def __init__(self, lc):
            Mach.LoadCommand.__init__(self, lc.command, lc.length, lc.file_off)
            self.reg_sets = list()

        def unpack(self, mach_file, data):
            reg_set = Mach.UnixThreadLoadCommand.ThreadState()
            reg_set.unpack(data)
            self.reg_sets.append(reg_set)

        def __str__(self):
            s = Mach.LoadCommand.__str__(self)
            for reg_set in self.reg_sets:
                s += "%s" % reg_set
            return s

    class DYLDInfoOnlyLoadCommand(LoadCommand):
        def __init__(self, lc):
            Mach.LoadCommand.__init__(self, lc.command, lc.length, lc.file_off)
            self.rebase_off = 0
            self.rebase_size = 0
            self.bind_off = 0
            self.bind_size = 0
            self.weak_bind_off = 0
            self.weak_bind_size = 0
            self.lazy_bind_off = 0
            self.lazy_bind_size = 0
            self.export_off = 0
            self.export_size = 0

        def unpack(self, mach_file, data):
            byte_order_char = mach_file.magic.get_byte_order()
            (
                self.rebase_off,
                self.rebase_size,
                self.bind_off,
                self.bind_size,
                self.weak_bind_off,
                self.weak_bind_size,
                self.lazy_bind_off,
                self.lazy_bind_size,
                self.export_off,
                self.export_size,
            ) = data.get_n_uint32(10)

        def __str__(self):
            s = Mach.LoadCommand.__str__(self)
            s += "rebase_off = %#8.8x, rebase_size = %u, " % (
                self.rebase_off,
                self.rebase_size,
            )
            s += "bind_off = %#8.8x, bind_size = %u, " % (self.bind_off, self.bind_size)
            s += "weak_bind_off = %#8.8x, weak_bind_size = %u, " % (
                self.weak_bind_off,
                self.weak_bind_size,
            )
            s += "lazy_bind_off = %#8.8x, lazy_bind_size = %u, " % (
                self.lazy_bind_off,
                self.lazy_bind_size,
            )
            s += "export_off = %#8.8x, export_size = %u, " % (
                self.export_off,
                self.export_size,
            )
            return s

    class DYLDSymtabLoadCommand(LoadCommand):
        def __init__(self, lc):
            Mach.LoadCommand.__init__(self, lc.command, lc.length, lc.file_off)
            self.ilocalsym = 0
            self.nlocalsym = 0
            self.iextdefsym = 0
            self.nextdefsym = 0
            self.iundefsym = 0
            self.nundefsym = 0
            self.tocoff = 0
            self.ntoc = 0
            self.modtaboff = 0
            self.nmodtab = 0
            self.extrefsymoff = 0
            self.nextrefsyms = 0
            self.indirectsymoff = 0
            self.nindirectsyms = 0
            self.extreloff = 0
            self.nextrel = 0
            self.locreloff = 0
            self.nlocrel = 0

        def unpack(self, mach_file, data):
            byte_order_char = mach_file.magic.get_byte_order()
            (
                self.ilocalsym,
                self.nlocalsym,
                self.iextdefsym,
                self.nextdefsym,
                self.iundefsym,
                self.nundefsym,
                self.tocoff,
                self.ntoc,
                self.modtaboff,
                self.nmodtab,
                self.extrefsymoff,
                self.nextrefsyms,
                self.indirectsymoff,
                self.nindirectsyms,
                self.extreloff,
                self.nextrel,
                self.locreloff,
                self.nlocrel,
            ) = data.get_n_uint32(18)

        def __str__(self):
            s = Mach.LoadCommand.__str__(self)
            # s += "ilocalsym = %u, nlocalsym = %u, " % (self.ilocalsym, self.nlocalsym)
            # s += "iextdefsym = %u, nextdefsym = %u, " % (self.iextdefsym, self.nextdefsym)
            # s += "iundefsym %u, nundefsym = %u, " % (self.iundefsym, self.nundefsym)
            # s += "tocoff = %#8.8x, ntoc = %u, " % (self.tocoff, self.ntoc)
            # s += "modtaboff = %#8.8x, nmodtab = %u, " % (self.modtaboff, self.nmodtab)
            # s += "extrefsymoff = %#8.8x, nextrefsyms = %u, " % (self.extrefsymoff, self.nextrefsyms)
            # s += "indirectsymoff = %#8.8x, nindirectsyms = %u, " % (self.indirectsymoff, self.nindirectsyms)
            # s += "extreloff = %#8.8x, nextrel = %u, " % (self.extreloff, self.nextrel)
            # s += "locreloff = %#8.8x, nlocrel = %u" % (self.locreloff,
            # self.nlocrel)
            s += "ilocalsym      = %-10u, nlocalsym     = %u\n" % (
                self.ilocalsym,
                self.nlocalsym,
            )
            s += (
                "                                             iextdefsym     = %-10u, nextdefsym    = %u\n"
                % (self.iextdefsym, self.nextdefsym)
            )
            s += (
                "                                             iundefsym      = %-10u, nundefsym     = %u\n"
                % (self.iundefsym, self.nundefsym)
            )
            s += (
                "                                             tocoff         = %#8.8x, ntoc          = %u\n"
                % (self.tocoff, self.ntoc)
            )
            s += (
                "                                             modtaboff      = %#8.8x, nmodtab       = %u\n"
                % (self.modtaboff, self.nmodtab)
            )
            s += (
                "                                             extrefsymoff   = %#8.8x, nextrefsyms   = %u\n"
                % (self.extrefsymoff, self.nextrefsyms)
            )
            s += (
                "                                             indirectsymoff = %#8.8x, nindirectsyms = %u\n"
                % (self.indirectsymoff, self.nindirectsyms)
            )
            s += (
                "                                             extreloff      = %#8.8x, nextrel       = %u\n"
                % (self.extreloff, self.nextrel)
            )
            s += (
                "                                             locreloff      = %#8.8x, nlocrel       = %u"
                % (self.locreloff, self.nlocrel)
            )
            return s

    class SymtabLoadCommand(LoadCommand):
        def __init__(self, lc):
            Mach.LoadCommand.__init__(self, lc.command, lc.length, lc.file_off)
            self.symoff = 0
            self.nsyms = 0
            self.stroff = 0
            self.strsize = 0

        def unpack(self, mach_file, data):
            byte_order_char = mach_file.magic.get_byte_order()
            self.symoff, self.nsyms, self.stroff, self.strsize = data.get_n_uint32(4)

        def __str__(self):
            s = Mach.LoadCommand.__str__(self)
            s += "symoff = %#8.8x, nsyms = %u, stroff = %#8.8x, strsize = %u" % (
                self.symoff,
                self.nsyms,
                self.stroff,
                self.strsize,
            )
            return s

    class UUIDLoadCommand(LoadCommand):
        def __init__(self, lc):
            Mach.LoadCommand.__init__(self, lc.command, lc.length, lc.file_off)
            self.uuid = None

        def unpack(self, mach_file, data):
            uuid_data = data.get_n_uint8(16)
            uuid_str = ""
            for byte in uuid_data:
                uuid_str += "%2.2x" % byte
            self.uuid = uuid.UUID(uuid_str)
            mach_file.uuid = self.uuid

        def __str__(self):
            s = Mach.LoadCommand.__str__(self)
            s += self.uuid.__str__()
            return s

    class DataBlobLoadCommand(LoadCommand):
        def __init__(self, lc):
            Mach.LoadCommand.__init__(self, lc.command, lc.length, lc.file_off)
            self.dataoff = 0
            self.datasize = 0

        def unpack(self, mach_file, data):
            byte_order_char = mach_file.magic.get_byte_order()
            self.dataoff, self.datasize = data.get_n_uint32(2)

        def __str__(self):
            s = Mach.LoadCommand.__str__(self)
            s += "dataoff = %#8.8x, datasize = %u" % (self.dataoff, self.datasize)
            return s

    class EncryptionInfoLoadCommand(LoadCommand):
        def __init__(self, lc):
            Mach.LoadCommand.__init__(self, lc.command, lc.length, lc.file_off)
            self.cryptoff = 0
            self.cryptsize = 0
            self.cryptid = 0

        def unpack(self, mach_file, data):
            byte_order_char = mach_file.magic.get_byte_order()
            self.cryptoff, self.cryptsize, self.cryptid = data.get_n_uint32(3)

        def __str__(self):
            s = Mach.LoadCommand.__str__(self)
            s += "file-range = [%#8.8x - %#8.8x), cryptsize = %u, cryptid = %u" % (
                self.cryptoff,
                self.cryptoff + self.cryptsize,
                self.cryptsize,
                self.cryptid,
            )
            return s

    class SegmentLoadCommand(LoadCommand):
        def __init__(self, lc):
            Mach.LoadCommand.__init__(self, lc.command, lc.length, lc.file_off)
            self.segname = None
            self.vmaddr = 0
            self.vmsize = 0
            self.fileoff = 0
            self.filesize = 0
            self.maxprot = 0
            self.initprot = 0
            self.nsects = 0
            self.flags = 0

        def unpack(self, mach_file, data):
            is_64 = self.command.get_enum_value() == LC_SEGMENT_64
            self.segname = data.get_fixed_length_c_string(16, "", True)
            if is_64:
                (
                    self.vmaddr,
                    self.vmsize,
                    self.fileoff,
                    self.filesize,
                ) = data.get_n_uint64(4)
            else:
                (
                    self.vmaddr,
                    self.vmsize,
                    self.fileoff,
                    self.filesize,
                ) = data.get_n_uint32(4)
            self.maxprot, self.initprot, self.nsects, self.flags = data.get_n_uint32(4)
            mach_file.segments.append(self)
            for i in range(self.nsects):
                section = Mach.Section()
                section.unpack(is_64, data)
                section.index = len(mach_file.sections)
                mach_file.sections.append(section)

        def __str__(self):
            s = Mach.LoadCommand.__str__(self)
            if self.command.get_enum_value() == LC_SEGMENT:
                s += "%#8.8x %#8.8x %#8.8x %#8.8x " % (
                    self.vmaddr,
                    self.vmsize,
                    self.fileoff,
                    self.filesize,
                )
            else:
                s += "%#16.16x %#16.16x %#16.16x %#16.16x " % (
                    self.vmaddr,
                    self.vmsize,
                    self.fileoff,
                    self.filesize,
                )
            s += "%s %s %3u %#8.8x" % (
                vm_prot_names[self.maxprot],
                vm_prot_names[self.initprot],
                self.nsects,
                self.flags,
            )
            s += " " + self.segname
            return s

    class NList:
        class Type:
            class Stab(dict_utils.Enum):
                enum = {
                    "N_GSYM": N_GSYM,
                    "N_FNAME": N_FNAME,
                    "N_FUN": N_FUN,
                    "N_STSYM": N_STSYM,
                    "N_LCSYM": N_LCSYM,
                    "N_BNSYM": N_BNSYM,
                    "N_OPT": N_OPT,
                    "N_RSYM": N_RSYM,
                    "N_SLINE": N_SLINE,
                    "N_ENSYM": N_ENSYM,
                    "N_SSYM": N_SSYM,
                    "N_SO": N_SO,
                    "N_OSO": N_OSO,
                    "N_LSYM": N_LSYM,
                    "N_BINCL": N_BINCL,
                    "N_SOL": N_SOL,
                    "N_PARAMS": N_PARAMS,
                    "N_VERSION": N_VERSION,
                    "N_OLEVEL": N_OLEVEL,
                    "N_PSYM": N_PSYM,
                    "N_EINCL": N_EINCL,
                    "N_ENTRY": N_ENTRY,
                    "N_LBRAC": N_LBRAC,
                    "N_EXCL": N_EXCL,
                    "N_RBRAC": N_RBRAC,
                    "N_BCOMM": N_BCOMM,
                    "N_ECOMM": N_ECOMM,
                    "N_ECOML": N_ECOML,
                    "N_LENG": N_LENG,
                }

                def __init__(self, magic=0):
                    dict_utils.Enum.__init__(self, magic, self.enum)

            def __init__(self, t=0):
                self.value = t

            def __str__(self):
                n_type = self.value
                if n_type & N_STAB:
                    stab = Mach.NList.Type.Stab(self.value)
                    return "%s" % stab
                else:
                    type = self.value & N_TYPE
                    type_str = ""
                    if type == N_UNDF:
                        type_str = "N_UNDF"
                    elif type == N_ABS:
                        type_str = "N_ABS "
                    elif type == N_SECT:
                        type_str = "N_SECT"
                    elif type == N_PBUD:
                        type_str = "N_PBUD"
                    elif type == N_INDR:
                        type_str = "N_INDR"
                    else:
                        type_str = "??? (%#2.2x)" % type
                    if n_type & N_PEXT:
                        type_str += " | PEXT"
                    if n_type & N_EXT:
                        type_str += " | EXT "
                    return type_str

        def __init__(self):
            self.index = 0
            self.name_offset = 0
            self.name = 0
            self.type = Mach.NList.Type()
            self.sect_idx = 0
            self.desc = 0
            self.value = 0

        def unpack(self, mach_file, data, symtab_lc):
            self.index = len(mach_file.symbols)
            self.name_offset = data.get_uint32()
            self.type.value, self.sect_idx = data.get_n_uint8(2)
            self.desc = data.get_uint16()
            if mach_file.is_64_bit():
                self.value = data.get_uint64()
            else:
                self.value = data.get_uint32()
            data.push_offset_and_seek(
                mach_file.file_off + symtab_lc.stroff + self.name_offset
            )
            # print "get string for symbol[%u]" % self.index
            self.name = data.get_c_string()
            data.pop_offset_and_seek()

        def __str__(self):
            name_display = ""
            if len(self.name):
                name_display = ' "%s"' % self.name
            return "%#8.8x %#2.2x (%-20s) %#2.2x %#4.4x %16.16x%s" % (
                self.name_offset,
                self.type.value,
                self.type,
                self.sect_idx,
                self.desc,
                self.value,
                name_display,
            )

    class Interactive(cmd.Cmd):
        """Interactive command interpreter to mach-o files."""

        def __init__(self, mach, options):
            cmd.Cmd.__init__(self)
            self.intro = "Interactive mach-o command interpreter"
            self.prompt = "mach-o: %s %% " % mach.path
            self.mach = mach
            self.options = options

        def default(self, line):
            """Catch all for unknown command, which will exit the interpreter."""
            print("uknown command: %s" % line)
            return True

        def do_q(self, line):
            """Quit command"""
            return True

        def do_quit(self, line):
            """Quit command"""
            return True

        def do_header(self, line):
            """Dump mach-o file headers"""
            self.mach.dump_header(True, self.options)
            return False

        def do_load(self, line):
            """Dump all mach-o load commands"""
            self.mach.dump_load_commands(True, self.options)
            return False

        def do_sections(self, line):
            """Dump all mach-o sections"""
            self.mach.dump_sections(True, self.options)
            return False

        def do_symtab(self, line):
            """Dump all mach-o symbols in the symbol table"""
            self.mach.dump_symtab(True, self.options)
            return False


if __name__ == "__main__":
    parser = optparse.OptionParser(
        description="A script that parses skinny and universal mach-o files."
    )
    parser.add_option(
        "--arch",
        "-a",
        type="string",
        metavar="arch",
        dest="archs",
        action="append",
        help="specify one or more architectures by name",
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
        "-H",
        "--header",
        action="store_true",
        dest="dump_header",
        help="dump the mach-o file header",
        default=False,
    )
    parser.add_option(
        "-l",
        "--load-commands",
        action="store_true",
        dest="dump_load_commands",
        help="dump the mach-o load commands",
        default=False,
    )
    parser.add_option(
        "-s",
        "--symtab",
        action="store_true",
        dest="dump_symtab",
        help="dump the mach-o symbol table",
        default=False,
    )
    parser.add_option(
        "-S",
        "--sections",
        action="store_true",
        dest="dump_sections",
        help="dump the mach-o sections",
        default=False,
    )
    parser.add_option(
        "--section",
        type="string",
        metavar="sectname",
        dest="section_names",
        action="append",
        help="Specify one or more section names to dump",
        default=[],
    )
    parser.add_option(
        "-o",
        "--out",
        type="string",
        dest="outfile",
        help="Used in conjunction with the --section=NAME option to save a single section's data to disk.",
        default=False,
    )
    parser.add_option(
        "-i",
        "--interactive",
        action="store_true",
        dest="interactive",
        help="enable interactive mode",
        default=False,
    )
    parser.add_option(
        "-m",
        "--mangled",
        action="store_true",
        dest="find_mangled",
        help="dump all mangled names in a mach file",
        default=False,
    )
    parser.add_option(
        "-c",
        "--compare",
        action="store_true",
        dest="compare",
        help="compare two mach files",
        default=False,
    )
    parser.add_option(
        "-M",
        "--extract-modules",
        action="store_true",
        dest="extract_modules",
        help="Extract modules from file",
        default=False,
    )
    parser.add_option(
        "-C",
        "--count",
        type="int",
        dest="max_count",
        help="Sets the max byte count when dumping section data",
        default=-1,
    )

    (options, mach_files) = parser.parse_args()
    if options.extract_modules:
        if options.section_names:
            print("error: can't use --section option with the --extract-modules option")
            exit(1)
        if not options.outfile:
            print(
                "error: the --output=FILE option must be specified with the --extract-modules option"
            )
            exit(1)
        options.section_names.append("__apple_ast")
    if options.compare:
        if len(mach_files) == 2:
            mach_a = Mach()
            mach_b = Mach()
            mach_a.parse(mach_files[0])
            mach_b.parse(mach_files[1])
            mach_a.compare(mach_b)
        else:
            print("error: --compare takes two mach files as arguments")
    else:
        if not (
            options.dump_header
            or options.dump_load_commands
            or options.dump_symtab
            or options.dump_sections
            or options.find_mangled
            or options.section_names
        ):
            options.dump_header = True
            options.dump_load_commands = True
        if options.verbose:
            print("options", options)
            print("mach_files", mach_files)
        for path in mach_files:
            mach = Mach()
            mach.parse(path)
            if options.interactive:
                interpreter = Mach.Interactive(mach, options)
                interpreter.cmdloop()
            else:
                mach.dump(options)
