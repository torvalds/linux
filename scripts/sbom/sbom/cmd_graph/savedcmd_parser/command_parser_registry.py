# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

import re
import shlex
from typing import Callable, Iterator

import sbom.sbom_logging as sbom_logging
from sbom.environment import Environment
from sbom.cmd_graph.savedcmd_parser.command_splitter import IfBlock, split_commands
from sbom.cmd_graph.savedcmd_parser.tokenizer import (
    CmdParsingError,
    Option,
    Positional,
    tokenize_single_command,
    tokenize_single_command_positionals_only,
)
from sbom.path_utils import PathStr

CommandParser = Callable[[str], list[PathStr]]
CommandParserRegistryEntry = tuple[re.Pattern[str], CommandParser]


def _parse_dd_command(command: str) -> list[PathStr]:
    match = re.match(r"dd.*?if=(\S+)", command)
    if match:
        return [match.group(1)]
    return []


def _parse_cat_command(command: str) -> list[PathStr]:
    positionals = tokenize_single_command_positionals_only(command)
    # expect positionals to be ["cat", input1, input2, ...]
    return [p for p in positionals[1:]]


def _parse_compound_command(command: str) -> list[PathStr]:
    compound_command_parsers: list[CommandParserRegistryEntry] = [
        (re.compile(r"dd\b"), _parse_dd_command),
        (re.compile(r"cat.*?\|"), lambda c: _parse_cat_command(c.split("|")[0])),
        (re.compile(r"cat\b[^|>]*$"), _parse_cat_command),
        (re.compile(r"echo\b"), _parse_noop),
        (re.compile(r"\S+="), _parse_noop),
        (re.compile(r"printf\b"), _parse_noop),
        (re.compile(r"sed\b"), _parse_sed_command),
        (
            re.compile(r"(.*/)scripts/bin2c\s*<"),
            lambda c: [input] if (input := c.split("<")[1].split(">")[0].strip()) != "/dev/null" else [],
        ),
        (re.compile(r"^:$"), _parse_noop),
    ]

    match = re.match(r"\s*[\(\{](.*)[\)\}]\s*>", command, re.DOTALL)
    if match is None:
        raise CmdParsingError("No inner commands found for compound command")
    input_files: list[PathStr] = []
    inner_commands = split_commands(match.group(1))
    for inner_command in inner_commands:
        if isinstance(inner_command, IfBlock):
            sbom_logging.error(
                "Skip parsing inner command {inner_command} of compound command because IfBlock is not supported",
                inner_command=inner_command,
            )
            continue

        parser = next((parser for pattern, parser in compound_command_parsers if pattern.match(inner_command)), None)
        if parser is None:
            sbom_logging.error(
                "Skip parsing inner command {inner_command} of compound command because no matching parser was found",
                inner_command=inner_command,
            )
            continue
        try:
            input_files += parser(inner_command)
        except (CmdParsingError, IndexError) as e:
            sbom_logging.error(
                "Skip parsing inner command {inner_command} of compound command because of command parsing error: {error_message}",
                inner_command=inner_command,
                error_message=str(e),
            )
    return input_files


def _parse_objcopy_command(command: str) -> list[PathStr]:
    command_parts = tokenize_single_command(command, flag_options=["-S", "-w"])
    positionals = [part.value for part in command_parts if isinstance(part, Positional)]
    # expect positionals to be ['objcopy', input_file] or ['objcopy', input_file, output_file]
    return [positionals[1]]


def _parse_link_vmlinux_command(command: str) -> list[PathStr]:
    """
    For simplicity we do not parse the `scripts/link-vmlinux.sh` script.
    Instead the `vmlinux.a` dependency is just hardcoded for now.
    """
    return ["vmlinux.a"]


def _parse_cp_command(command: str) -> list[PathStr]:
    positionals = tokenize_single_command_positionals_only(command)
    # expect positionals to be ["cp", input1, ..., destination]
    return positionals[1:-1]


def _parse_noop(command: str) -> list[PathStr]:
    """
    No-op parser for commands with no input files (e.g., 'rm', 'true').
    Returns an empty list.
    """
    return []


def _parse_ar_command(command: str) -> list[PathStr]:
    positionals = tokenize_single_command_positionals_only(command)
    # expect positionals to be ['ar', flags, output, input1, input2, ...]
    flags = positionals[1]
    if "r" not in flags:
        # 'r' option indicates that new files are added to the archive.
        # If this option is missing we won't find any relevant input files.
        return []
    return positionals[3:]


def _parse_ar_piped_xargs_command(command: str) -> list[PathStr]:
    printf_command, _ = command.split("|", 1)
    positionals = tokenize_single_command_positionals_only(printf_command.strip())
    # expect positionals to be ['printf', '{prefix_path}%s ', input1, input2, ...]
    prefix_path = positionals[1].removesuffix("%s ")
    return [f"{prefix_path}{filename}" for filename in positionals[2:]]


def _parse_gcc_or_clang_command(command: str) -> list[PathStr]:
    parts = shlex.split(command)
    # compile mode: expect last positional argument ending in a source file extension to be the input file
    for part in reversed(parts):
        if not part.startswith("-") and any(part.endswith(suffix) for suffix in [".c", ".S", ".dts"]):
            return [part]

    # linking mode: expect all .o files to be the inputs
    return [p for p in parts if p.endswith(".o")]


def _parse_rustc_command(command: str) -> list[PathStr]:
    parts = shlex.split(command)
    # expect last positional argument ending in `.rs` to be the input file
    for part in reversed(parts):
        if not part.startswith("-") and part.endswith(".rs"):
            return [part]
    raise CmdParsingError("Could not find .rs input source file")


def _parse_rustdoc_command(command: str) -> list[PathStr]:
    parts = shlex.split(command)
    # expect last positional argument ending in `.rs` to be the input file
    for part in reversed(parts):
        if not part.startswith("-") and part.endswith(".rs"):
            return [part]
    raise CmdParsingError("Could not find .rs input source file")


def _parse_syscallhdr_command(command: str) -> list[PathStr]:
    command_parts = tokenize_single_command(command.strip(), flag_options=["--emit-nr"])
    positionals = [p.value for p in command_parts if isinstance(p, Positional)]
    # expect positionals to be ["sh", path/to/syscallhdr.sh, input, output]
    return [positionals[2]]


def _parse_syscalltbl_command(command: str) -> list[PathStr]:
    command_parts = tokenize_single_command(command.strip())
    positionals = [p.value for p in command_parts if isinstance(p, Positional)]
    # expect positionals to be ["sh", path/to/syscalltbl.sh, input, output]
    return [positionals[2]]


def _parse_mkcapflags_command(command: str) -> list[PathStr]:
    positionals = tokenize_single_command_positionals_only(command)
    # expect positionals to be ["sh", path/to/mkcapflags.sh, output, input1, input2]
    return [positionals[3], positionals[4]]


def _parse_orc_hash_command(command: str) -> list[PathStr]:
    positionals = tokenize_single_command_positionals_only(command)
    # expect positionals to be ["sh", path/to/orc_hash.sh, '<', input, '>', output]
    return [positionals[3]]


def _parse_xen_hypercalls_command(command: str) -> list[PathStr]:
    positionals = tokenize_single_command_positionals_only(command)
    # expect positionals to be ["sh", path/to/xen-hypercalls.sh, output, input1, input2, ...]
    return positionals[3:]


def _parse_gen_initramfs_command(command: str) -> list[PathStr]:
    command_parts = tokenize_single_command(command)
    positionals = [p.value for p in command_parts if isinstance(p, Positional)]
    # expect positionals to be ["sh", path/to/gen_initramfs.sh, input1, input2, ...]
    return positionals[2:]


def _parse_vdso2c_command(command: str) -> list[PathStr]:
    positionals = tokenize_single_command_positionals_only(command)
    # expect positionals to be ['vdso2c', raw_input, stripped_input, output]
    return [positionals[1], positionals[2]]


def _parse_vdsomunge_command(command: str) -> list[PathStr]:
    positionals = tokenize_single_command_positionals_only(command)
    # expect positionals to be ['vdsomunge', input, output]
    return [positionals[1]]


def _parse_ld_command(command: str) -> list[PathStr]:
    command_parts = tokenize_single_command(
        command=command.strip(),
        flag_options=[
            "-shared",
            "--no-undefined",
            "--eh-frame-hdr",
            "-Bsymbolic",
            "-r",
            "--no-ld-generated-unwind-info",
            "--no-dynamic-linker",
            "-pie",
            "--no-dynamic-linker--whole-archive",
            "--whole-archive",
            "--no-whole-archive",
            "--start-group",
            "--end-group",
        ],
    )
    positionals = [p.value for p in command_parts if isinstance(p, Positional)]
    # expect positionals to be ["ld", input1, input2, ...]
    return positionals[1:]


def _parse_sed_command(command: str) -> list[PathStr]:
    command_parts = shlex.split(command)
    # expect command parts to be ["sed", *, input]
    input = command_parts[-1]
    if input == "/dev/null":
        return []
    return [input]


def _parse_awk(command: str) -> list[PathStr]:
    command_parts = tokenize_single_command(command)
    options = [p for p in command_parts if isinstance(p, Option)]
    positionals = [p.value for p in command_parts if isinstance(p, Positional)]
    has_script_file = any(p.name == "-f" for p in options)
    # With -f option: expect ["awk", input1, input2, ...]
    # Without -f option: expect ["awk", inline_program, input1, input2, ...]
    return positionals[1:] if has_script_file else positionals[2:]


def _parse_nm_piped_command(command: str) -> list[PathStr]:
    nm_command, _ = command.split("|", 1)
    command_parts = tokenize_single_command(
        command=nm_command.strip(),
        flag_options=["-p", "--defined-only"],
    )
    positionals = [p.value for p in command_parts if isinstance(p, Positional)]
    # expect positionals to be ["nm", input1, input2, ...]
    return [p for p in positionals[1:]]


def _parse_pnm_to_logo_command(command: str) -> list[PathStr]:
    command_parts = shlex.split(command)
    # expect command parts to be ["pnmtologo", <options>, input]
    return [command_parts[-1]]


def _parse_relacheck(command: str) -> list[PathStr]:
    positionals = tokenize_single_command_positionals_only(command)
    # expect positionals to be ["relacheck", input, log_reference]
    return [positionals[1]]


def _parse_gen_hyprel_command(command: str) -> list[PathStr]:
    gen_hyprel_command, _ = command.split(">", 1)
    command_parts = shlex.split(gen_hyprel_command)
    # expect command_parts to be ["gen-hyprel", input]
    return [command_parts[1]]


def _parse_perl_command(command: str) -> list[PathStr]:
    positionals = tokenize_single_command_positionals_only(command.strip())
    # expect positionals to be ["perl", input]
    return [positionals[1]]


def _parse_strip_command(command: str) -> list[PathStr]:
    command_parts = tokenize_single_command(command, flag_options=["--strip-debug"])
    positionals = [p.value for p in command_parts if isinstance(p, Positional)]
    # expect positionals to be ["strip", input1, input2, ...]
    return positionals[1:]


def _parse_mkpiggy_command(command: str) -> list[PathStr]:
    mkpiggy_command, _ = command.split(">", 1)
    positionals = tokenize_single_command_positionals_only(mkpiggy_command)
    # expect positionals to be ["mkpiggy", input]
    return [positionals[1]]


def _parse_relocs_command(command: str) -> list[PathStr]:
    if ">" not in command:
        # Only consider relocs commands that redirect output to a file.
        # If there's no redirection, we assume it produces no output file and therefore has no input we care about.
        return []
    relocs_command, _ = command.split(">", 1)
    command_parts = shlex.split(relocs_command)
    # expect command_parts to be ["relocs", options, input]
    return [command_parts[-1]]


def _parse_mk_elfconfig_command(command: str) -> list[PathStr]:
    positionals = tokenize_single_command_positionals_only(command)
    # expect positionals to be ["mk_elfconfig", "<", input, ">", output]
    return [positionals[2]]


def _parse_flex_command(command: str) -> list[PathStr]:
    parts = shlex.split(command)
    # expect last positional argument ending in `.l` to be the input file
    for part in reversed(parts):
        if not part.startswith("-") and part.endswith(".l"):
            return [part]
    raise CmdParsingError("Could not find .l input source file in command")


def _parse_bison_command(command: str) -> list[PathStr]:
    parts = shlex.split(command)
    # expect last positional argument ending in `.y` to be the input file
    for part in reversed(parts):
        if not part.startswith("-") and part.endswith(".y"):
            return [part]
    raise CmdParsingError("Could not find input .y input source file in command")


def _parse_tools_build_command(command: str) -> list[PathStr]:
    positionals = tokenize_single_command_positionals_only(command)
    # expect positionals to be ["tools/build", "input1", "input2", "input3", "output"]
    return positionals[1:-1]


def _parse_extract_cert_command(command: str) -> list[PathStr]:
    command_parts = shlex.split(command)
    # expect command parts to be [path/to/extract-cert, input, output]
    input = command_parts[1]
    if not input:
        return []
    return [input]


def _parse_dtc_command(command: str) -> list[PathStr]:
    wno_flags = [command_part for command_part in shlex.split(command) if command_part.startswith("-Wno-")]
    command_parts = tokenize_single_command(command, flag_options=wno_flags)
    positionals = [p.value for p in command_parts if isinstance(p, Positional)]
    # expect positionals to be [path/to/dtc, input]
    return [positionals[1]]


def _parse_bindgen_command(command: str) -> list[PathStr]:
    command_parts = shlex.split(command)
    header_file_input_paths = [part for part in command_parts if part.endswith(".h")]
    return header_file_input_paths


def _parse_gen_header(command: str) -> list[PathStr]:
    command_parts = shlex.split(command)
    # expect command parts to be ["python3", path/to/gen_headers.py, ..., "--xml", input]
    i = next((i for i, token in enumerate(command_parts) if token == "--xml"), None)
    if i is None:
        raise CmdParsingError(f"Expected --xml input file in gen_headers command but got {command}")
    return [command_parts[i + 1]]

def _parse_mkuboot_command(command: str) -> list[PathStr]:
    command_parts = tokenize_single_command(command)
    # mkuboot.sh passes all args to mkimage; -d specifies the data/input image file
    for part in command_parts:
        if isinstance(part, Option) and part.name == "-d" and part.value is not None:
            return [part.value]
    raise CmdParsingError("Could not find -d (data file) option in mkuboot.sh command")


def _parse_syscallnr_command(command: str) -> list[PathStr]:
    command_parts = tokenize_single_command(command.strip())
    positionals = [p.value for p in command_parts if isinstance(p, Positional)]
    # expect positionals to be ["sh", path/to/syscallnr.sh, input, output]
    return [positionals[2]]


def _parse_gen_kernel_hwcaps_command(command: str) -> list[PathStr]:
    command_parts = tokenize_single_command(command.strip(), flag_options=["-e"])
    positionals = [p.value for p in command_parts if isinstance(p, Positional)]
    # expect positionals to be ["sh", path/to/gen-kernel-hwcaps.sh, input]
    return [positionals[2]]


class CommandParserRegistry:
    """
    Registry mapping command patterns to their input-file parsers.
    """

    def __init__(self, entries: list[CommandParserRegistryEntry]) -> None:
        self._entries = entries

    def __iter__(self) -> Iterator[CommandParserRegistryEntry]:
        return iter(self._entries)

    @staticmethod
    def create() -> "CommandParserRegistry":
        def env_or_default_pattern(env_value: str | None, default_pattern: str) -> str:
            if env_value is None or not env_value.strip():
                return default_pattern
            return rf"(?:{re.escape(env_value.strip())}|{default_pattern})"

        cc_pattern = env_or_default_pattern(Environment.CC(), r"([^\s]+-)?(gcc|clang)")
        ld_pattern = env_or_default_pattern(Environment.LD(), r"([^\s]+-)?ld")
        ar_pattern = env_or_default_pattern(Environment.AR(), r"([^\s]+-)?ar")
        nm_pattern = env_or_default_pattern(Environment.NM(), r"([^\s]+-)?nm")
        objcopy_pattern = env_or_default_pattern(Environment.OBJCOPY(), r"([^\s]+-)?objcopy")
        strip_pattern = env_or_default_pattern(Environment.STRIP(), r"([^\s]+-)?strip")

        entries: list[CommandParserRegistryEntry] = [
            # Compound commands
            (re.compile(r"\(.*?\)\s*>", re.DOTALL), _parse_compound_command),
            (re.compile(r"\{.*?\}\s*>", re.DOTALL), _parse_compound_command),
            # Standard Unix utilities and system tools
            (re.compile(r"^rm\b"), _parse_noop),
            (re.compile(r"^mkdir\b"), _parse_noop),
            (re.compile(r"^touch\b"), _parse_noop),
            (re.compile(r"^cp\b"), _parse_cp_command),
            (re.compile(r"^truncate\b"), _parse_noop),
            (re.compile(r"^cat\b.*?[\|>]"), lambda c: _parse_cat_command(c.split("|")[0].split(">")[0])),
            (re.compile(r"^echo[^|]*$"), _parse_noop),
            (re.compile(r"^sed.*?>"), lambda c: _parse_sed_command(c.split(">")[0])),
            (re.compile(r"^sed\b"), _parse_noop),
            (re.compile(r"^awk.*?<.*?>"), lambda c: [c.split("<")[1].split(">")[0]]),
            (re.compile(r"^awk.*?>"), lambda c: _parse_awk(c.split(">")[0])),
            (re.compile(r"^(/bin/)?true\b"), _parse_noop),
            (re.compile(r"^(/bin/)?false\b"), _parse_noop),
            (re.compile(r"^openssl\s+req.*?-new.*?-keyout"), _parse_noop),
            # Compilers and code generators
            # (C/LLVM toolchain, Rust, Flex/Bison, Bindgen, Perl, etc.)
            (
                re.compile(rf"^{cc_pattern}\b"),
                lambda command: _parse_gcc_or_clang_command(re.sub(rf"^{cc_pattern}\b", "gcc", command, count=1)),
            ),
            (
                re.compile(rf"^{ld_pattern}\b"),
                lambda command: _parse_ld_command(re.sub(rf"^{ld_pattern}\b", "ld", command, count=1)),
            ),
            (
                re.compile(rf"^printf\b.*\| xargs {ar_pattern}\b"),
                lambda command: _parse_ar_piped_xargs_command(
                    re.sub(rf"xargs {ar_pattern}\b", "xargs ar", command, count=1)
                ),
            ),
            (
                re.compile(rf"^{ar_pattern}\b"),
                lambda command: _parse_ar_command(re.sub(rf"^{ar_pattern}\b", "ar", command, count=1)),
            ),
            (
                re.compile(rf"^{nm_pattern}\b.*?\|"),
                lambda command: _parse_nm_piped_command(re.sub(rf"^{nm_pattern}\b", "nm", command, count=1)),
            ),
            (
                re.compile(rf"^{objcopy_pattern}\b"),
                lambda command: _parse_objcopy_command(re.sub(rf"^{objcopy_pattern}\b", "objcopy", command, count=1)),
            ),
            (
                re.compile(rf"^{strip_pattern}\b"),
                lambda command: _parse_strip_command(re.sub(rf"^{strip_pattern}\b", "strip", command, count=1)),
            ),
            (re.compile(r".*?rustc\b"), _parse_rustc_command),
            (re.compile(r".*?rustdoc\b"), _parse_rustdoc_command),
            (re.compile(r"^flex\b"), _parse_flex_command),
            (re.compile(r"^bison\b"), _parse_bison_command),
            (re.compile(r"^bindgen\b"), _parse_bindgen_command),
            (re.compile(r"^perl\b"), _parse_perl_command),
            # Kernel-specific build scripts and tools
            (re.compile(r"^(.*/)?link-vmlinux\.sh\b"), _parse_link_vmlinux_command),
            (re.compile(r"sh (.*/)?syscallhdr\.sh\b"), _parse_syscallhdr_command),
            (re.compile(r"sh (.*/)?syscalltbl\.sh\b"), _parse_syscalltbl_command),
            (re.compile(r"sh (.*/)?mkcapflags\.sh\b"), _parse_mkcapflags_command),
            (re.compile(r"sh (.*/)?orc_hash\.sh\b"), _parse_orc_hash_command),
            (re.compile(r"sh (.*/)?xen-hypercalls\.sh\b"), _parse_xen_hypercalls_command),
            (re.compile(r"sh (.*/)?gen_initramfs\.sh\b"), _parse_gen_initramfs_command),
            (re.compile(r"sh (.*/)?checkundef\.sh\b"), _parse_noop),
            (re.compile(r"(bash|sh) (.*/)?mkuboot\.sh\b"), _parse_mkuboot_command),
            (re.compile(r"sh (.*/)?syscallnr\.sh\b"), _parse_syscallnr_command),
            (re.compile(r"(/bin/)?sh (.*/)?gen-kernel-hwcaps\.sh\b"), lambda c: _parse_gen_kernel_hwcaps_command(c.split(">")[0])),
            (re.compile(r"(.*/)?vdso2c\b"), _parse_vdso2c_command),
            (re.compile(r"(.*/)?vdsomunge\b"), _parse_vdsomunge_command),
            (re.compile(r"^(.*/)?mkpiggy.*?>"), _parse_mkpiggy_command),
            (re.compile(r"^(.*/)?relocs\b"), _parse_relocs_command),
            (re.compile(r"^(.*/)?mk_elfconfig.*?<.*?>"), _parse_mk_elfconfig_command),
            (re.compile(r"^(.*/)?tools/build\b"), _parse_tools_build_command),
            (re.compile(r"^(.*/)?certs/extract-cert"), _parse_extract_cert_command),
            (re.compile(r"^(.*/)?scripts/dtc/dtc\b"), _parse_dtc_command),
            (re.compile(r"^(.*/)?pnmtologo\b"), _parse_pnm_to_logo_command),
            (re.compile(r"^(.*/)?kernel/pi/relacheck"), _parse_relacheck),
            (re.compile(r"^(.*/)?gen-hyprel\b"), _parse_gen_hyprel_command),
            (re.compile(r"^drivers/gpu/drm/radeon/mkregtable"), lambda c: [c.split(" ")[1]]),
            (re.compile(r"(.*/)?genheaders\b"), _parse_noop),
            (re.compile(r"^(.*/)?mkcpustr\s+>"), _parse_noop),
            (re.compile(r"^(.*/)polgen\b"), _parse_noop),
            (re.compile(r"make -f .*/arch/x86/Makefile\.postlink"), _parse_noop),
            (re.compile(r"^(.*/)?raid6/mktables\s+>"), _parse_noop),
            (re.compile(r"^(.*/)?objtool\b"), _parse_noop),
            (re.compile(r"^(.*/)?module/gen_test_kallsyms.sh"), _parse_noop),
            (re.compile(r"^(.*/)?gen_header.py"), _parse_gen_header),
            (re.compile(r"^(.*/)?scripts/rustdoc_test_gen"), _parse_noop),
        ]
        return CommandParserRegistry(entries)
