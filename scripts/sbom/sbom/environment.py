# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

import os

KERNEL_BUILD_VARIABLES_ALLOWLIST = [
    "AFLAGS_KERNEL",
    "AFLAGS_MODULE",
    "AR",
    "ARCH",
    "ARCH_CORE",
    "ARCH_DRIVERS",
    "ARCH_LIB",
    "AWK",
    "BASH",
    "BINDGEN",
    "BITS",
    "CC",
    "CC_FLAGS_FPU",
    "CC_FLAGS_NO_FPU",
    "CFLAGS_GCOV",
    "CFLAGS_KERNEL",
    "CFLAGS_MODULE",
    "CHECK",
    "CHECKFLAGS",
    "CLIPPY_CONF_DIR",
    "CONFIG_SHELL",
    "CPP",
    "CROSS_COMPILE",
    "CURDIR",
    "GNUMAKEFLAGS",
    "HOSTCC",
    "HOSTCXX",
    "HOSTPKG_CONFIG",
    "HOSTRUSTC",
    "INSTALLKERNEL",
    "INSTALL_DTBS_PATH",
    "INSTALL_HDR_PATH",
    "INSTALL_PATH",
    "KBUILD_AFLAGS",
    "KBUILD_AFLAGS_KERNEL",
    "KBUILD_AFLAGS_MODULE",
    "KBUILD_BUILTIN",
    "KBUILD_CFLAGS",
    "KBUILD_CFLAGS_KERNEL",
    "KBUILD_CFLAGS_MODULE",
    "KBUILD_CHECKSRC",
    "KBUILD_CLIPPY",
    "KBUILD_CPPFLAGS",
    "KBUILD_EXTMOD",
    "KBUILD_EXTRA_WARN",
    "KBUILD_HOSTCFLAGS",
    "KBUILD_HOSTCXXFLAGS",
    "KBUILD_HOSTLDFLAGS",
    "KBUILD_HOSTLDLIBS",
    "KBUILD_HOSTRUSTFLAGS",
    "KBUILD_IMAGE",
    "KBUILD_LDFLAGS",
    "KBUILD_LDFLAGS_MODULE",
    "KBUILD_LDS",
    "KBUILD_MODULES",
    "KBUILD_PROCMACROLDFLAGS",
    "KBUILD_RUSTFLAGS",
    "KBUILD_RUSTFLAGS_KERNEL",
    "KBUILD_RUSTFLAGS_MODULE",
    "KBUILD_USERCFLAGS",
    "KBUILD_USERLDFLAGS",
    "KBUILD_VERBOSE",
    "KBUILD_VMLINUX_LIBS",
    "KBZIP2",
    "KCONFIG_CONFIG",
    "KERNELDOC",
    "KERNELRELEASE",
    "KERNELVERSION",
    "KGZIP",
    "KLZOP",
    "LC_COLLATE",
    "LC_NUMERIC",
    "LD",
    "LDFLAGS_MODULE",
    "LEX",
    "LINUXINCLUDE",
    "LZ4",
    "LZMA",
    "MAKE",
    "MAKEFILES",
    "MAKEFILE_LIST",
    "MAKEFLAGS",
    "MAKELEVEL",
    "MAKEOVERRIDES",
    "MAKE_COMMAND",
    "MAKE_HOST",
    "MAKE_TERMERR",
    "MAKE_TERMOUT",
    "MAKE_VERSION",
    "MFLAGS",
    "MODLIB",
    "NM",
    "NOSTDINC_FLAGS",
    "O",
    "OBJCOPY",
    "OBJCOPYFLAGS",
    "OBJDUMP",
    "PAHOLE",
    "PATCHLEVEL",
    "PERL",
    "PYTHON3",
    "Q",
    "RCS_FIND_IGNORE",
    "READELF",
    "REALMODE_CFLAGS",
    "RESOLVE_BTFIDS",
    "RETHUNK_CFLAGS",
    "RETHUNK_RUSTFLAGS",
    "RETPOLINE_CFLAGS",
    "RETPOLINE_RUSTFLAGS",
    "RETPOLINE_VDSO_CFLAGS",
    "RUSTC",
    "RUSTC_BOOTSTRAP",
    "RUSTC_OR_CLIPPY",
    "RUSTC_OR_CLIPPY_QUIET",
    "RUSTDOC",
    "RUSTFLAGS_KERNEL",
    "RUSTFLAGS_MODULE",
    "RUSTFMT",
    "SRCARCH",
    "STRIP",
    "SUBLEVEL",
    "SUFFIXES",
    "TAR",
    "UTS_MACHINE",
    "VERSION",
    "VPATH",
    "XZ",
    "YACC",
    "ZSTD",
    "building_out_of_srctree",
    "cross_compiling",
    "objtree",
    "quiet",
    "rust_common_flags",
    "srcroot",
    "srctree",
    "sub_make_done",
    "subdir",
]


class Environment:
    """
    Read-only accessor for kernel build environment variables.
    """

    @classmethod
    def KERNEL_BUILD_VARIABLES(cls) -> dict[str, str]:
        return {
            name: value.strip()
            for name in KERNEL_BUILD_VARIABLES_ALLOWLIST
            if (value := os.getenv(name)) is not None and value.strip()
        }

    @classmethod
    def ARCH(cls) -> str | None:
        return os.getenv("ARCH")

    @classmethod
    def SRCARCH(cls) -> str | None:
        return os.getenv("SRCARCH")

    @classmethod
    def CC(cls) -> str | None:
        return os.getenv("CC")

    @classmethod
    def LD(cls) -> str | None:
        return os.getenv("LD")

    @classmethod
    def AR(cls) -> str | None:
        return os.getenv("AR")

    @classmethod
    def NM(cls) -> str | None:
        return os.getenv("NM")

    @classmethod
    def OBJCOPY(cls) -> str | None:
        return os.getenv("OBJCOPY")

    @classmethod
    def STRIP(cls) -> str | None:
        return os.getenv("STRIP")
