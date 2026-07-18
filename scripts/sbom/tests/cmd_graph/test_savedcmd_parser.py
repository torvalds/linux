# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

import os
import unittest
from unittest.mock import patch

from sbom.cmd_graph.savedcmd_parser import parse_inputs_from_commands
from sbom.cmd_graph.savedcmd_parser.command_parser_registry import CommandParserRegistry
import sbom.sbom_logging as sbom_logging


class TestSavedCmdParser(unittest.TestCase):
    def _assert_parsing(self, cmd: str, expected: str, registry: CommandParserRegistry | None = None) -> None:
        sbom_logging.init()
        parsed = parse_inputs_from_commands(cmd, fail_on_unknown_build_command=False, registry=registry)
        target = [] if expected == "" else expected.split(" ")
        self.assertEqual(parsed, target)
        errors = sbom_logging._error_logger._message_counts # type: ignore
        self.assertEqual(errors, {})

    # Compound command tests
    def test_dd_cat(self):
        cmd = "(dd if=arch/x86/boot/setup.bin bs=4k conv=sync status=none; cat arch/x86/boot/vmlinux.bin) >arch/x86/boot/bzImage"
        expected = "arch/x86/boot/setup.bin arch/x86/boot/vmlinux.bin"
        self._assert_parsing(cmd, expected)

    def test_manual_file_creation(self):
        cmd = """{ symbase=__dtbo_overlay_bad_unresolved; echo '$(pound)include <asm-generic/vmlinux.lds.h>'; echo '.section .rodata,"a"'; echo '.balign STRUCT_ALIGNMENT'; echo ".global $${symbase}_begin"; echo "$${symbase}_begin:"; echo '.incbin "drivers/of/unittest-data/overlay_bad_unresolved.dtbo" '; echo ".global $${symbase}_end"; echo "$${symbase}_end:"; echo '.balign STRUCT_ALIGNMENT'; } > drivers/of/unittest-data/overlay_bad_unresolved.dtbo.S"""
        expected = ""
        self._assert_parsing(cmd, expected)

    def test_cat_xz_wrap(self):
        cmd = "{ cat arch/x86/boot/compressed/vmlinux.bin | sh ../scripts/xz_wrap.sh; printf \\130\\064\\024\\000; } > arch/x86/boot/compressed/vmlinux.bin.xz"
        expected = "arch/x86/boot/compressed/vmlinux.bin"
        self._assert_parsing(cmd, expected)

    def test_printf_sed(self):
        cmd = r"""{  printf 'static char tomoyo_builtin_profile[] __initdata =\n'; sed -e 's/\\/\\\\/g' -e 's/\"/\\"/g' -e 's/\(.*\)/\t"\1\\n"/' -- /dev/null; printf '\t"";\n';  printf 'static char tomoyo_builtin_exception_policy[] __initdata =\n'; sed -e 's/\\/\\\\/g' -e 's/\"/\\"/g' -e 's/\(.*\)/\t"\1\\n"/' -- ../security/tomoyo/policy/exception_policy.conf.default; printf '\t"";\n';  printf 'static char tomoyo_builtin_domain_policy[] __initdata =\n'; sed -e 's/\\/\\\\/g' -e 's/\"/\\"/g' -e 's/\(.*\)/\t"\1\\n"/' -- /dev/null; printf '\t"";\n';  printf 'static char tomoyo_builtin_manager[] __initdata =\n'; sed -e 's/\\/\\\\/g' -e 's/\"/\\"/g' -e 's/\(.*\)/\t"\1\\n"/' -- /dev/null; printf '\t"";\n';  printf 'static char tomoyo_builtin_stat[] __initdata =\n'; sed -e 's/\\/\\\\/g' -e 's/\"/\\"/g' -e 's/\(.*\)/\t"\1\\n"/' -- /dev/null; printf '\t"";\n'; } > security/tomoyo/builtin-policy.h"""
        expected = "../security/tomoyo/policy/exception_policy.conf.default"
        self._assert_parsing(cmd, expected)

    def test_bin2c_echo(self):
        cmd = """(echo "static char tomoyo_builtin_profile[] __initdata ="; ./scripts/bin2c </dev/null; echo ";"; echo "static char tomoyo_builtin_exception_policy[] __initdata ="; ./scripts/bin2c <../security/tomoyo/policy/exception_policy.conf.default; echo ";"; echo "static char tomoyo_builtin_domain_policy[] __initdata ="; ./scripts/bin2c </dev/null; echo ";"; echo "static char tomoyo_builtin_manager[] __initdata ="; ./scripts/bin2c </dev/null; echo ";"; echo "static char tomoyo_builtin_stat[] __initdata ="; ./scripts/bin2c </dev/null; echo ";") >security/tomoyo/builtin-policy.h"""
        expected = "../security/tomoyo/policy/exception_policy.conf.default"
        self._assert_parsing(cmd, expected)

    def test_cat_colon(self):
        cmd = "{   cat init/modules.order;   cat usr/modules.order;   cat arch/x86/modules.order;   cat arch/x86/boot/startup/modules.order;   cat kernel/modules.order;   cat certs/modules.order;   cat mm/modules.order;   cat fs/modules.order;   cat ipc/modules.order;   cat security/modules.order;   cat crypto/modules.order;   cat block/modules.order;   cat io_uring/modules.order;   cat lib/modules.order;   cat arch/x86/lib/modules.order;   cat drivers/modules.order;   cat sound/modules.order;   cat samples/modules.order;   cat net/modules.order;   cat virt/modules.order;   cat arch/x86/pci/modules.order;   cat arch/x86/power/modules.order;   cat arch/x86/video/modules.order; :; } > modules.order"
        expected = "init/modules.order usr/modules.order arch/x86/modules.order arch/x86/boot/startup/modules.order kernel/modules.order certs/modules.order mm/modules.order fs/modules.order ipc/modules.order security/modules.order crypto/modules.order block/modules.order io_uring/modules.order lib/modules.order arch/x86/lib/modules.order drivers/modules.order sound/modules.order samples/modules.order net/modules.order virt/modules.order arch/x86/pci/modules.order arch/x86/power/modules.order arch/x86/video/modules.order"
        self._assert_parsing(cmd, expected)

    def test_cat_zstd(self):
        cmd = "{ cat arch/x86/boot/compressed/vmlinux.bin arch/x86/boot/compressed/vmlinux.relocs | zstd -22 --ultra; printf \\340\\362\\066\\003; } > arch/x86/boot/compressed/vmlinux.bin.zst"
        expected = "arch/x86/boot/compressed/vmlinux.bin arch/x86/boot/compressed/vmlinux.relocs"
        self._assert_parsing(cmd, expected)

    # cat command tests
    def test_cat_redirect(self):
        cmd = "cat ../fs/unicode/utf8data.c_shipped > fs/unicode/utf8data.c"
        expected = "../fs/unicode/utf8data.c_shipped"
        self._assert_parsing(cmd, expected)

    def test_cat_piped(self):
        cmd = "cat arch/x86/boot/compressed/vmlinux.bin arch/x86/boot/compressed/vmlinux.relocs | gzip -n -f -9 > arch/x86/boot/compressed/vmlinux.bin.gz"
        expected = "arch/x86/boot/compressed/vmlinux.bin arch/x86/boot/compressed/vmlinux.relocs"
        self._assert_parsing(cmd, expected)

    # sed command tests
    def test_sed(self):
        cmd = "sed -n 's/.*define *BLIST_\\([A-Z0-9_]*\\) *.*/BLIST_FLAG_NAME(\\1),/p' ../include/scsi/scsi_devinfo.h > drivers/scsi/scsi_devinfo_tbl.c"
        expected = "../include/scsi/scsi_devinfo.h"
        self._assert_parsing(cmd, expected)

    # awk command tests
    def test_awk(self):
        cmd = "awk -f ../arch/arm64/tools/gen-cpucaps.awk ../arch/arm64/tools/cpucaps > arch/arm64/include/generated/asm/cpucap-defs.h"
        expected = "../arch/arm64/tools/cpucaps"
        self._assert_parsing(cmd, expected)

    def test_awk_with_input_redirection(self):
        cmd = "awk -v N=1 -f ../lib/raid6/unroll.awk < ../lib/raid6/int.uc > lib/raid6/int1.c"
        expected = "../lib/raid6/int.uc"
        self._assert_parsing(cmd, expected)

    # openssl command tests
    def test_openssl(self):
        cmd = "openssl req -new -nodes -utf8 -sha256 -days 36500 -batch -x509 -config certs/x509.genkey -outform PEM -out certs/signing_key.pem -keyout certs/signing_key.pem  2>&1"
        expected = ""
        self._assert_parsing(cmd, expected)

    # gcc/clang command tests
    def test_gcc(self):
        cmd = (
            "gcc -Wp,-MMD,arch/x86/pci/.i386.o.d -nostdinc -I../arch/x86/include -I./arch/x86/include/generated -I../include -I./include -I../arch/x86/include/uapi -I./arch/x86/include/generated/uapi -I../include/uapi -I./include/generated/uapi -include ../include/linux/compiler-version.h -include ../include/linux/kconfig.h -include ../include/linux/compiler_types.h -D__KERNEL__ -fmacro-prefix-map=../= -Werror -std=gnu11 -fshort-wchar -funsigned-char -fno-common -fno-PIE -fno-strict-aliasing -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx -fcf-protection=branch -fno-jump-tables -m64 -falign-jumps=1 -falign-loops=1 -mno-80387 -mno-fp-ret-in-387 -mpreferred-stack-boundary=3 -mskip-rax-setup -march=x86-64 -mtune=generic -mno-red-zone -mcmodel=kernel -mstack-protector-guard-reg=gs -mstack-protector-guard-symbol=__ref_stack_chk_guard -Wno-sign-compare -fno-asynchronous-unwind-tables -mindirect-branch=thunk-extern -mindirect-branch-register -mindirect-branch-cs-prefix -mfunction-return=thunk-extern -fno-jump-tables -fpatchable-function-entry=16,16 -fno-delete-null-pointer-checks -O2 -fno-allow-store-data-races -fstack-protector-strong -fomit-frame-pointer -fno-stack-clash-protection -falign-functions=16 -fno-strict-overflow -fno-stack-check -fconserve-stack -fno-builtin-wcslen -Wall -Wextra -Wundef -Werror=implicit-function-declaration -Werror=implicit-int -Werror=return-type -Werror=strict-prototypes -Wno-format-security -Wno-trigraphs -Wno-frame-address -Wno-address-of-packed-member -Wmissing-declarations -Wmissing-prototypes -Wframe-larger-than=2048 -Wno-main -Wvla-larger-than=1 -Wno-pointer-sign -Wcast-function-type -Wno-array-bounds -Wno-stringop-overflow -Wno-alloc-size-larger-than -Wimplicit-fallthrough=5 -Werror=date-time -Werror=incompatible-pointer-types -Werror=designated-init -Wenum-conversion -Wunused -Wno-unused-but-set-variable -Wno-unused-const-variable -Wno-packed-not-aligned -Wno-format-overflow -Wno-format-truncation -Wno-stringop-truncation -Wno-override-init -Wno-missing-field-initializers -Wno-type-limits -Wno-shift-negative-value -Wno-maybe-uninitialized -Wno-sign-compare -Wno-unused-parameter -I../arch/x86/pci -Iarch/x86/pci    -DKBUILD_MODFILE="
            "arch/x86/pci/i386"
            " -DKBUILD_BASENAME="
            "i386"
            " -DKBUILD_MODNAME="
            "i386"
            " -D__KBUILD_MODNAME=kmod_i386 -c -o arch/x86/pci/i386.o ../arch/x86/pci/i386.c  "
        )
        expected = "../arch/x86/pci/i386.c"
        self._assert_parsing(cmd, expected)

    def test_gcc_linking(self):
        cmd = "gcc   -o arch/x86/tools/relocs arch/x86/tools/relocs_32.o arch/x86/tools/relocs_64.o arch/x86/tools/relocs_common.o"
        expected = "arch/x86/tools/relocs_32.o arch/x86/tools/relocs_64.o arch/x86/tools/relocs_common.o"
        self._assert_parsing(cmd, expected)

    def test_gcc_without_compile_flag(self):
        cmd = "gcc -Wp,-MMD,arch/x86/boot/compressed/.mkpiggy.d -Wall -Wmissing-prototypes -Wstrict-prototypes -O2 -fomit-frame-pointer -std=gnu11   -I ../scripts/include -I../tools/include  -I arch/x86/boot/compressed   -o arch/x86/boot/compressed/mkpiggy ../arch/x86/boot/compressed/mkpiggy.c"
        expected = "../arch/x86/boot/compressed/mkpiggy.c"
        self._assert_parsing(cmd, expected)

    def test_gcc_with_env_override(self):
        with patch.dict(os.environ, {"CC": "ccache gcc"}):
            registry = CommandParserRegistry.create()
            cmd = "gcc   -o arch/x86/tools/relocs arch/x86/tools/relocs_32.o arch/x86/tools/relocs_64.o arch/x86/tools/relocs_common.o"
            expected = "arch/x86/tools/relocs_32.o arch/x86/tools/relocs_64.o arch/x86/tools/relocs_common.o"
            self._assert_parsing(cmd, expected, registry)
            self._assert_parsing(f"ccache {cmd}", expected, registry)

    def test_gcc_dts_preprocessing(self):
        cmd = "gcc -E -Wp,-MMD,drivers/of/.empty_root.dtb.d.pre.tmp -nostdinc -I ../scripts/dtc/include-prefixes -undef -D__DTS__ -x assembler-with-cpp -o drivers/of/.empty_root.dtb.dts.tmp ../drivers/of/empty_root.dts"
        expected = "../drivers/of/empty_root.dts"
        self._assert_parsing(cmd, expected)

    def test_clang(self):
        cmd = """clang -Wp,-MMD,arch/x86/entry/.entry_64_compat.o.d -nostdinc -I../arch/x86/include -I./arch/x86/include/generated -I../include -I./include -I../arch/x86/include/uapi -I./arch/x86/include/generated/uapi -I../include/uapi -I./include/generated/uapi -include ../include/linux/compiler-version.h -include ../include/linux/kconfig.h -D__KERNEL__ --target=x86_64-linux-gnu -fintegrated-as -Werror=unknown-warning-option -Werror=ignored-optimization-argument -Werror=option-ignored -Werror=unused-command-line-argument -fmacro-prefix-map=../= -Werror -D__ASSEMBLY__ -fno-PIE -m64 -I../arch/x86/entry -Iarch/x86/entry    -DKBUILD_MODFILE='"arch/x86/entry/entry_64_compat"' -DKBUILD_MODNAME='"entry_64_compat"' -D__KBUILD_MODNAME=kmod_entry_64_compat -c -o arch/x86/entry/entry_64_compat.o ../arch/x86/entry/entry_64_compat.S"""
        expected = "../arch/x86/entry/entry_64_compat.S"
        self._assert_parsing(cmd, expected)

    # ld command tests
    def test_ld(self):
        cmd = r'ld -o arch/x86/entry/vdso/vdso64.so.dbg -shared --hash-style=both --build-id=sha1 --no-undefined  --eh-frame-hdr -Bsymbolic -z noexecstack -m elf_x86_64 -soname linux-vdso.so.1 -z max-page-size=4096 -T arch/x86/entry/vdso/vdso.lds arch/x86/entry/vdso/vdso-note.o arch/x86/entry/vdso/vclock_gettime.o arch/x86/entry/vdso/vgetcpu.o arch/x86/entry/vdso/vgetrandom.o arch/x86/entry/vdso/vgetrandom-chacha.o; if readelf -rW arch/x86/entry/vdso/vdso64.so.dbg | grep -v _NONE | grep -q " R_\w*_"; then (echo >&2 "arch/x86/entry/vdso/vdso64.so.dbg: dynamic relocations are not supported"; rm -f arch/x86/entry/vdso/vdso64.so.dbg; /bin/false); fi'
        expected = "arch/x86/entry/vdso/vdso-note.o arch/x86/entry/vdso/vclock_gettime.o arch/x86/entry/vdso/vgetcpu.o arch/x86/entry/vdso/vgetrandom.o arch/x86/entry/vdso/vgetrandom-chacha.o"
        self._assert_parsing(cmd, expected)

    def test_ld_with_env_override(self):
        with patch.dict(os.environ, {"LD": "some-tool ld"}):
            registry = CommandParserRegistry.create()
            cmd = r'ld -o arch/x86/entry/vdso/vdso64.so.dbg -shared --hash-style=both --build-id=sha1 --no-undefined  --eh-frame-hdr -Bsymbolic -z noexecstack -m elf_x86_64 -soname linux-vdso.so.1 -z max-page-size=4096 -T arch/x86/entry/vdso/vdso.lds arch/x86/entry/vdso/vdso-note.o arch/x86/entry/vdso/vclock_gettime.o arch/x86/entry/vdso/vgetcpu.o arch/x86/entry/vdso/vgetrandom.o arch/x86/entry/vdso/vgetrandom-chacha.o; if readelf -rW arch/x86/entry/vdso/vdso64.so.dbg | grep -v _NONE | grep -q " R_\w*_"; then (echo >&2 "arch/x86/entry/vdso/vdso64.so.dbg: dynamic relocations are not supported"; rm -f arch/x86/entry/vdso/vdso64.so.dbg; /bin/false); fi'
            expected = "arch/x86/entry/vdso/vdso-note.o arch/x86/entry/vdso/vclock_gettime.o arch/x86/entry/vdso/vgetcpu.o arch/x86/entry/vdso/vgetrandom.o arch/x86/entry/vdso/vgetrandom-chacha.o"
            self._assert_parsing(cmd, expected, registry)
            self._assert_parsing(f"some-tool {cmd}", expected, registry)

    def test_ld_whole_archive(self):
        cmd = "ld -m elf_x86_64 -z noexecstack -r -o vmlinux.o   --whole-archive vmlinux.a --no-whole-archive --start-group  --end-group"
        expected = "vmlinux.a"
        self._assert_parsing(cmd, expected)

    def test_ld_with_at_symbol(self):
        cmd = "ld.lld -m elf_x86_64 -z noexecstack   -r -o fs/efivarfs/efivarfs.o @fs/efivarfs/efivarfs.mod  ; ./tools/objtool/objtool --hacks=jump_label --hacks=noinstr --hacks=skylake --ibt --orc --retpoline --rethunk --static-call --uaccess --prefix=16  --link  --module fs/efivarfs/efivarfs.o"
        expected = "@fs/efivarfs/efivarfs.mod"
        self._assert_parsing(cmd, expected)

    def test_ld_if_objdump(self):
        cmd = """ld -o arch/x86/entry/vdso/vdso64.so.dbg -shared --hash-style=both --build-id=sha1  --eh-frame-hdr -Bsymbolic -z noexecstack -m elf_x86_64 -soname linux-vdso.so.1 --no-undefined -z max-page-size=4096 -T arch/x86/entry/vdso/vdso.lds arch/x86/entry/vdso/vdso-note.o arch/x86/entry/vdso/vclock_gettime.o arch/x86/entry/vdso/vgetcpu.o arch/x86/entry/vdso/vsgx.o && sh ./arch/x86/entry/vdso/checkundef.sh 'nm' 'arch/x86/entry/vdso/vdso64.so.dbg'; if objdump -R arch/x86/entry/vdso/vdso64.so.dbg | grep -E -h "R_X86_64_JUMP_SLOT|R_X86_64_GLOB_DAT|R_X86_64_RELATIVE| R_386_GLOB_DAT|R_386_JMP_SLOT|R_386_RELATIVE"; then (echo >&2 "arch/x86/entry/vdso/vdso64.so.dbg: dynamic relocations are not supported"; rm -f arch/x86/entry/vdso/vdso64.so.dbg; /bin/false); fi"""
        expected = "arch/x86/entry/vdso/vdso-note.o arch/x86/entry/vdso/vclock_gettime.o arch/x86/entry/vdso/vgetcpu.o arch/x86/entry/vdso/vsgx.o"
        self._assert_parsing(cmd, expected)

    # printf | xargs ar command tests
    def test_ar_printf(self):
        cmd = 'rm -f built-in.a;  printf "./%s " init/built-in.a usr/built-in.a arch/x86/built-in.a arch/x86/boot/startup/built-in.a kernel/built-in.a certs/built-in.a mm/built-in.a fs/built-in.a ipc/built-in.a security/built-in.a crypto/built-in.a block/built-in.a io_uring/built-in.a lib/built-in.a arch/x86/lib/built-in.a drivers/built-in.a sound/built-in.a net/built-in.a virt/built-in.a arch/x86/pci/built-in.a arch/x86/power/built-in.a arch/x86/video/built-in.a | xargs ar cDPrST built-in.a'
        expected = "./init/built-in.a ./usr/built-in.a ./arch/x86/built-in.a ./arch/x86/boot/startup/built-in.a ./kernel/built-in.a ./certs/built-in.a ./mm/built-in.a ./fs/built-in.a ./ipc/built-in.a ./security/built-in.a ./crypto/built-in.a ./block/built-in.a ./io_uring/built-in.a ./lib/built-in.a ./arch/x86/lib/built-in.a ./drivers/built-in.a ./sound/built-in.a ./net/built-in.a ./virt/built-in.a ./arch/x86/pci/built-in.a ./arch/x86/power/built-in.a ./arch/x86/video/built-in.a"
        self._assert_parsing(cmd, expected)

    def test_ar_printf_nested(self):
        cmd = 'rm -f arch/x86/pci/built-in.a;  printf "arch/x86/pci/%s " i386.o init.o mmconfig_64.o direct.o mmconfig-shared.o fixup.o acpi.o legacy.o irq.o common.o early.o bus_numa.o amd_bus.o | xargs ar cDPrST arch/x86/pci/built-in.a'
        expected = "arch/x86/pci/i386.o arch/x86/pci/init.o arch/x86/pci/mmconfig_64.o arch/x86/pci/direct.o arch/x86/pci/mmconfig-shared.o arch/x86/pci/fixup.o arch/x86/pci/acpi.o arch/x86/pci/legacy.o arch/x86/pci/irq.o arch/x86/pci/common.o arch/x86/pci/early.o arch/x86/pci/bus_numa.o arch/x86/pci/amd_bus.o"
        self._assert_parsing(cmd, expected)

    # ar command tests
    def test_ar_reordering(self):
        cmd = "rm -f vmlinux.a; ar cDPrST vmlinux.a built-in.a  lib/lib.a arch/x86/lib/lib.a; ar mPiT $$(ar t vmlinux.a | sed -n 1p) vmlinux.a $$(ar t vmlinux.a | grep -F -f ../scripts/head-object-list.txt)"
        expected = "built-in.a lib/lib.a arch/x86/lib/lib.a"
        self._assert_parsing(cmd, expected)

    def test_ar_default(self):
        cmd = "rm -f lib/lib.a; ar cDPrsT lib/lib.a lib/argv_split.o lib/bug.o lib/buildid.o lib/clz_tab.o lib/cmdline.o lib/cpumask.o lib/ctype.o lib/dec_and_lock.o lib/decompress.o lib/decompress_bunzip2.o lib/decompress_inflate.o lib/decompress_unlz4.o lib/decompress_unlzma.o lib/decompress_unlzo.o lib/decompress_unxz.o lib/decompress_unzstd.o lib/dump_stack.o lib/earlycpio.o lib/extable.o lib/flex_proportions.o lib/idr.o lib/iomem_copy.o lib/irq_regs.o lib/is_single_threaded.o lib/klist.o lib/kobject.o lib/kobject_uevent.o lib/logic_pio.o lib/maple_tree.o lib/memcat_p.o lib/nmi_backtrace.o lib/objpool.o lib/plist.o lib/radix-tree.o lib/ratelimit.o lib/rbtree.o lib/seq_buf.o lib/siphash.o lib/string.o lib/sys_info.o lib/timerqueue.o lib/union_find.o lib/vsprintf.o lib/win_minmax.o lib/xarray.o"
        expected = "lib/argv_split.o lib/bug.o lib/buildid.o lib/clz_tab.o lib/cmdline.o lib/cpumask.o lib/ctype.o lib/dec_and_lock.o lib/decompress.o lib/decompress_bunzip2.o lib/decompress_inflate.o lib/decompress_unlz4.o lib/decompress_unlzma.o lib/decompress_unlzo.o lib/decompress_unxz.o lib/decompress_unzstd.o lib/dump_stack.o lib/earlycpio.o lib/extable.o lib/flex_proportions.o lib/idr.o lib/iomem_copy.o lib/irq_regs.o lib/is_single_threaded.o lib/klist.o lib/kobject.o lib/kobject_uevent.o lib/logic_pio.o lib/maple_tree.o lib/memcat_p.o lib/nmi_backtrace.o lib/objpool.o lib/plist.o lib/radix-tree.o lib/ratelimit.o lib/rbtree.o lib/seq_buf.o lib/siphash.o lib/string.o lib/sys_info.o lib/timerqueue.o lib/union_find.o lib/vsprintf.o lib/win_minmax.o lib/xarray.o"
        self._assert_parsing(cmd, expected)

    def test_ar_llvm(self):
        cmd = "llvm-ar mPiT $$(llvm-ar t vmlinux.a | sed -n 1p) vmlinux.a $$(llvm-ar t vmlinux.a | grep -F -f ../scripts/head-object-list.txt)"
        expected = ""
        self._assert_parsing(cmd, expected)

    # nm command tests
    def test_nm(self):
        cmd = """llvm-nm -p --defined-only rust/core.o | awk '$$2~/(T|R|D|B)/ && $$3!~/__(pfx|cfi|odr_asan)/ { printf "EXPORT_SYMBOL_RUST_GPL(%s);\n",$$3 }' > rust/exports_core_generated.h"""
        expected = "rust/core.o"
        self._assert_parsing(cmd, expected)

    def test_nm_vmlinux(self):
        cmd = r"nm vmlinux | sed -n -e 's/^\([0-9a-fA-F]*\) [ABbCDGRSTtVW] \(_text\|__start_rodata\|__bss_start\|_end\)$/#define VO_\2 _AC(0x\1,UL)/p' > arch/x86/boot/voffset.h"
        expected = "vmlinux"
        self._assert_parsing(cmd, expected)

    # objcopy command tests
    def test_objcopy(self):
        cmd = "objcopy --remove-section='.rel*' --remove-section=!'.rel*.dyn' vmlinux.unstripped vmlinux"
        expected = "vmlinux.unstripped"
        self._assert_parsing(cmd, expected)

    def test_objcopy_llvm(self):
        cmd = "llvm-objcopy --remove-section='.rel*' --remove-section=!'.rel*.dyn' vmlinux.unstripped vmlinux"
        expected = "vmlinux.unstripped"
        self._assert_parsing(cmd, expected)

    # strip command tests
    def test_strip(self):
        cmd = "strip --strip-debug -o drivers/firmware/efi/libstub/mem.stub.o drivers/firmware/efi/libstub/mem.o"
        expected = "drivers/firmware/efi/libstub/mem.o"
        self._assert_parsing(cmd, expected)

    # cp command tests
    def test_cp_truncate(self):
        cmd = "cp arch/arm64/boot/Image arch/arm64/boot/vmlinux.bin; truncate -s $$(hexdump -s16 -n4 -e '\"%u\"' arch/arm64/boot/Image) arch/arm64/boot/vmlinux.bin"
        expected = "arch/arm64/boot/Image"
        self._assert_parsing(cmd, expected)

    # rustc command tests
    def test_rustc(self):
        cmd = """OBJTREE=/workspace/linux/kernel_build rustc -Zbinary_dep_depinfo=y -Astable_features -Dnon_ascii_idents -Dunsafe_op_in_unsafe_fn -Wmissing_docs -Wrust_2018_idioms -Wclippy::all -Wclippy::as_ptr_cast_mut -Wclippy::as_underscore -Wclippy::cast_lossless -Wclippy::ignored_unit_patterns -Wclippy::mut_mut -Wclippy::needless_bitwise_bool -Aclippy::needless_lifetimes -Wclippy::no_mangle_with_rust_abi -Wclippy::ptr_as_ptr -Wclippy::ptr_cast_constness -Wclippy::ref_as_ptr -Wclippy::undocumented_unsafe_blocks -Wclippy::unnecessary_safety_comment -Wclippy::unnecessary_safety_doc -Wrustdoc::missing_crate_level_docs -Wrustdoc::unescaped_backticks -Cpanic=abort -Cembed-bitcode=n -Clto=n -Cforce-unwind-tables=n -Ccodegen-units=1 -Csymbol-mangling-version=v0 -Crelocation-model=static -Zfunction-sections=n -Wclippy::float_arithmetic --target=./scripts/target.json -Ctarget-feature=-sse,-sse2,-sse3,-ssse3,-sse4.1,-sse4.2,-avx,-avx2 -Zcf-protection=branch -Zno-jump-tables -Ctarget-cpu=x86-64 -Ztune-cpu=generic -Cno-redzone=y -Ccode-model=kernel -Zfunction-return=thunk-extern -Zpatchable-function-entry=16,16 -Copt-level=2 -Cdebug-assertions=n -Coverflow-checks=y -Dwarnings @./include/generated/rustc_cfg --edition=2021 --cfg no_fp_fmt_parse --emit=dep-info=rust/.core.o.d --emit=obj=rust/core.o --emit=metadata=rust/libcore.rmeta --crate-type rlib -L./rust --crate-name core /usr/lib/rust-1.84/lib/rustlib/src/rust/library/core/src/lib.rs --sysroot=/dev/null ;llvm-objcopy --redefine-sym __addsf3=__rust__addsf3 --redefine-sym __eqsf2=__rust__eqsf2 --redefine-sym __extendsfdf2=__rust__extendsfdf2 --redefine-sym __gesf2=__rust__gesf2 --redefine-sym __lesf2=__rust__lesf2 --redefine-sym __ltsf2=__rust__ltsf2 --redefine-sym __mulsf3=__rust__mulsf3 --redefine-sym __nesf2=__rust__nesf2 --redefine-sym __truncdfsf2=__rust__truncdfsf2 --redefine-sym __unordsf2=__rust__unordsf2 --redefine-sym __adddf3=__rust__adddf3 --redefine-sym __eqdf2=__rust__eqdf2 --redefine-sym __ledf2=__rust__ledf2 --redefine-sym __ltdf2=__rust__ltdf2 --redefine-sym __muldf3=__rust__muldf3 --redefine-sym __unorddf2=__rust__unorddf2 --redefine-sym __muloti4=__rust__muloti4 --redefine-sym __multi3=__rust__multi3 --redefine-sym __udivmodti4=__rust__udivmodti4 --redefine-sym __udivti3=__rust__udivti3 --redefine-sym __umodti3=__rust__umodti3 rust/core.o"""
        expected = "/usr/lib/rust-1.84/lib/rustlib/src/rust/library/core/src/lib.rs rust/core.o"
        self._assert_parsing(cmd, expected)

    # rustdoc command tests
    def test_rustdoc(self):
        cmd = """OBJTREE=/workspace/linux/kernel_build rustdoc --test --edition=2021 -Zbinary_dep_depinfo=y -Astable_features -Dnon_ascii_idents -Dunsafe_op_in_unsafe_fn -Wmissing_docs -Wrust_2018_idioms -Wunreachable_pub -Wclippy::all -Wclippy::as_ptr_cast_mut -Wclippy::as_underscore -Wclippy::cast_lossless -Wclippy::ignored_unit_patterns -Wclippy::mut_mut -Wclippy::needless_bitwise_bool -Aclippy::needless_lifetimes -Wclippy::no_mangle_with_rust_abi -Wclippy::ptr_as_ptr -Wclippy::ptr_cast_constness -Wclippy::ref_as_ptr -Wclippy::undocumented_unsafe_blocks -Wclippy::unnecessary_safety_comment -Wclippy::unnecessary_safety_doc -Wrustdoc::missing_crate_level_docs -Wrustdoc::unescaped_backticks -Cpanic=abort -Cembed-bitcode=n -Clto=n -Cforce-unwind-tables=n -Ccodegen-units=1 -Csymbol-mangling-version=v0 -Crelocation-model=static -Zfunction-sections=n -Wclippy::float_arithmetic --target=aarch64-unknown-none -Ctarget-feature="-neon" -Cforce-unwind-tables=n -Zbranch-protection=pac-ret -Copt-level=2 -Cdebug-assertions=y -Coverflow-checks=y -Dwarnings -Cforce-frame-pointers=y -Zsanitizer=kernel-address -Zsanitizer-recover=kernel-address -Cllvm-args=-asan-mapping-offset=0xdfff800000000000 -Cpasses=sancov-module -Cllvm-args=-sanitizer-coverage-level=3 -Cllvm-args=-sanitizer-coverage-trace-pc -Cllvm-args=-sanitizer-coverage-trace-compares @./include/generated/rustc_cfg -L./rust --extern ffi --extern pin_init --extern kernel --extern build_error --extern macros --extern bindings --extern uapi --no-run --crate-name kernel -Zunstable-options --sysroot=/dev/null  --test-builder ./scripts/rustdoc_test_builder ../rust/kernel/lib.rs >/dev/null"""
        expected = "../rust/kernel/lib.rs"
        self._assert_parsing(cmd, expected)

    def test_rustdoc_test_gen(self):
        cmd = "./scripts/rustdoc_test_gen"
        expected = ""
        self._assert_parsing(cmd, expected)

    # flex command tests
    def test_flex(self):
        cmd = "flex -oscripts/kconfig/lexer.lex.c -L ../scripts/kconfig/lexer.l"
        expected = "../scripts/kconfig/lexer.l"
        self._assert_parsing(cmd, expected)

    # bison command tests
    def test_bison(self):
        cmd = "bison -o scripts/kconfig/parser.tab.c --defines=scripts/kconfig/parser.tab.h -t -l ../scripts/kconfig/parser.y"
        expected = "../scripts/kconfig/parser.y"
        self._assert_parsing(cmd, expected)

    # bindgen command tests
    def test_bindgen(self):
        cmd = (
            "bindgen ../rust/bindings/bindings_helper.h "
            "--blocklist-type __kernel_s?size_t --blocklist-type __kernel_ptrdiff_t "
            "--opaque-type xregs_state --opaque-type desc_struct --no-doc-comments "
            "--rust-target 1.68 --use-core --with-derive-default -o rust/bindings/bindings_generated.rs "
            "-- -Wp,-MMD,rust/bindings/.bindings_generated.rs.d -nostdinc -I../arch/x86/include "
            "-include ../include/linux/compiler-version.h -D__KERNEL__ -fintegrated-as -fno-builtin -DMODULE; "
            "sed -Ei 's/pub const RUST_CONST_HELPER_([a-zA-Z0-9_]*)/pub const \\1/g' rust/bindings/bindings_generated.rs"
        )
        expected = "../rust/bindings/bindings_helper.h ../include/linux/compiler-version.h"
        self._assert_parsing(cmd, expected)

    # perl command tests
    def test_perl(self):
        cmd = "perl ../lib/crypto/x86/poly1305-x86_64-cryptogams.pl > lib/crypto/x86/poly1305-x86_64-cryptogams.S"
        expected = "../lib/crypto/x86/poly1305-x86_64-cryptogams.pl"
        self._assert_parsing(cmd, expected)

    # link-vmlinux.sh command tests
    def test_link_vmlinux(self):
        cmd = '../scripts/link-vmlinux.sh "ld" "-m elf_x86_64 -z noexecstack" "-z max-page-size=0x200000 --build-id=sha1 --orphan-handling=error --emit-relocs --discard-none" "vmlinux.unstripped";  true'
        expected = "vmlinux.a"
        self._assert_parsing(cmd, expected)

    def test_link_vmlinux_postlink(self):
        cmd = '../scripts/link-vmlinux.sh "ld" "-m elf_x86_64 -z noexecstack --no-warn-rwx-segments" "--emit-relocs --discard-none -z max-page-size=0x200000 --build-id=sha1 -X --orphan-handling=error";  make -f ../arch/x86/Makefile.postlink vmlinux'
        expected = "vmlinux.a"
        self._assert_parsing(cmd, expected)

    # syscallhdr.sh command tests
    def test_syscallhdr(self):
        cmd = "sh ../scripts/syscallhdr.sh --abis common,64 --emit-nr   ../arch/x86/entry/syscalls/syscall_64.tbl arch/x86/include/generated/uapi/asm/unistd_64.h"
        expected = "../arch/x86/entry/syscalls/syscall_64.tbl"
        self._assert_parsing(cmd, expected)

    # syscalltbl.sh command tests
    def test_syscalltbl(self):
        cmd = "sh ../scripts/syscalltbl.sh --abis common,64 ../arch/x86/entry/syscalls/syscall_64.tbl arch/x86/include/generated/asm/syscalls_64.h"
        expected = "../arch/x86/entry/syscalls/syscall_64.tbl"
        self._assert_parsing(cmd, expected)

    # mkcapflags.sh command tests
    def test_mkcapflags(self):
        cmd = "sh ../arch/x86/kernel/cpu/mkcapflags.sh arch/x86/kernel/cpu/capflags.c ../arch/x86/kernel/cpu/../../include/asm/cpufeatures.h ../arch/x86/kernel/cpu/../../include/asm/vmxfeatures.h ../arch/x86/kernel/cpu/mkcapflags.sh FORCE"
        expected = "../arch/x86/kernel/cpu/../../include/asm/cpufeatures.h ../arch/x86/kernel/cpu/../../include/asm/vmxfeatures.h"
        self._assert_parsing(cmd, expected)

    # orc_hash.sh command tests
    def test_orc_hash(self):
        cmd = "mkdir -p arch/x86/include/generated/asm/; sh ../scripts/orc_hash.sh < ../arch/x86/include/asm/orc_types.h > arch/x86/include/generated/asm/orc_hash.h"
        expected = "../arch/x86/include/asm/orc_types.h"
        self._assert_parsing(cmd, expected)

    # xen-hypercalls.sh command tests
    def test_xen_hypercalls(self):
        cmd = "sh '../scripts/xen-hypercalls.sh' arch/x86/include/generated/asm/xen-hypercalls.h ../include/xen/interface/xen-mca.h ../include/xen/interface/xen.h ../include/xen/interface/xenpmu.h"
        expected = "../include/xen/interface/xen-mca.h ../include/xen/interface/xen.h ../include/xen/interface/xenpmu.h"
        self._assert_parsing(cmd, expected)

    # gen_initramfs.sh command tests
    def test_gen_initramfs(self):
        cmd = "sh ../usr/gen_initramfs.sh -o usr/initramfs_data.cpio -l usr/.initramfs_data.cpio.d    ../usr/default_cpio_list"
        expected = "../usr/default_cpio_list"
        self._assert_parsing(cmd, expected)

    # mkuboot.sh command tests
    def test_mkuboot(self):
        cmd = "bash ../scripts/mkuboot.sh -A arm -O linux -C none -T kernel -a 0x8000 -e 0x8000 -n 'Linux-6.15.0' -d arch/arm/boot/zImage arch/arm/boot/uImage"
        expected = "arch/arm/boot/zImage"
        self._assert_parsing(cmd, expected)

    # syscallnr.sh command tests
    def test_syscallnr(self):
        cmd = "sh ../arch/arm/tools/syscallnr.sh ../arch/arm/tools/syscall.tbl arch/arm/include/generated/asm/unistd-nr.h"
        expected = "../arch/arm/tools/syscall.tbl"
        self._assert_parsing(cmd, expected)

    # gen-kernel-hwcaps.sh command tests
    def test_gen_kernel_hwcaps(self):
        cmd = "/bin/sh -e ../arch/arm64/tools/gen-kernel-hwcaps.sh ../arch/arm64/include/uapi/asm/hwcap.h > arch/arm64/include/generated/asm/kernel-hwcap.h"
        expected = "../arch/arm64/include/uapi/asm/hwcap.h"
        self._assert_parsing(cmd, expected)

    # vdso2c command tests
    def test_vdso2c(self):
        cmd = "arch/x86/entry/vdso/vdso2c arch/x86/entry/vdso/vdso64.so.dbg arch/x86/entry/vdso/vdso64.so arch/x86/entry/vdso/vdso-image-64.c"
        expected = "arch/x86/entry/vdso/vdso64.so.dbg arch/x86/entry/vdso/vdso64.so"
        self._assert_parsing(cmd, expected)

    # vdsomunge command tests
    def test_vdsomunge(self):
        cmd = "arch/arm64/kernel/vdso32/../../../arm/vdso/vdsomunge arch/arm64/kernel/vdso32/vdso.so.raw arch/arm64/kernel/vdso32/vdso32.so.dbg"
        expected = "arch/arm64/kernel/vdso32/vdso.so.raw"
        self._assert_parsing(cmd, expected)

    # mkpiggy command tests
    def test_mkpiggy(self):
        cmd = "arch/x86/boot/compressed/mkpiggy arch/x86/boot/compressed/vmlinux.bin.gz > arch/x86/boot/compressed/piggy.S"
        expected = "arch/x86/boot/compressed/vmlinux.bin.gz"
        self._assert_parsing(cmd, expected)

    # relocs command tests
    def test_relocs(self):
        cmd = "arch/x86/tools/relocs vmlinux.unstripped > arch/x86/boot/compressed/vmlinux.relocs;arch/x86/tools/relocs --abs-relocs vmlinux.unstripped"
        expected = "vmlinux.unstripped"
        self._assert_parsing(cmd, expected)

    def test_relocs_with_realmode(self):
        cmd = (
            "arch/x86/tools/relocs --realmode arch/x86/realmode/rm/realmode.elf > arch/x86/realmode/rm/realmode.relocs"
        )
        expected = "arch/x86/realmode/rm/realmode.elf"
        self._assert_parsing(cmd, expected)

    # mk_elfconfig command tests
    def test_mk_elfconfig(self):
        cmd = "scripts/mod/mk_elfconfig < scripts/mod/empty.o > scripts/mod/elfconfig.h"
        expected = "scripts/mod/empty.o"
        self._assert_parsing(cmd, expected)

    # tools/build command tests
    def test_build(self):
        cmd = "arch/x86/boot/tools/build arch/x86/boot/setup.bin arch/x86/boot/vmlinux.bin arch/x86/boot/zoffset.h arch/x86/boot/bzImage"
        expected = "arch/x86/boot/setup.bin arch/x86/boot/vmlinux.bin arch/x86/boot/zoffset.h"
        self._assert_parsing(cmd, expected)

    # extract-cert command tests
    def test_extract_cert(self):
        cmd = 'certs/extract-cert ""  certs/signing_key.x509'
        expected = ""
        self._assert_parsing(cmd, expected)

    # dtc command tests
    def test_dtc_cat(self):
        cmd = "./scripts/dtc/dtc -o drivers/of/empty_root.dtb -b 0 -i../drivers/of/ -i../scripts/dtc/include-prefixes -Wno-unique_unit_address -Wno-unit_address_vs_reg -Wno-avoid_unnecessary_addr_size -Wno-alias_paths -Wno-graph_child_address -Wno-simple_bus_reg   -d drivers/of/.empty_root.dtb.d.dtc.tmp drivers/of/.empty_root.dtb.dts.tmp ; cat drivers/of/.empty_root.dtb.d.pre.tmp drivers/of/.empty_root.dtb.d.dtc.tmp > drivers/of/.empty_root.dtb.d"
        expected = "drivers/of/.empty_root.dtb.dts.tmp drivers/of/.empty_root.dtb.d.pre.tmp drivers/of/.empty_root.dtb.d.dtc.tmp"
        self._assert_parsing(cmd, expected)

    # pnmtologo command tests
    def test_pnmtologo(self):
        cmd = "drivers/video/logo/pnmtologo -t clut224 -n logo_linux_clut224 -o drivers/video/logo/logo_linux_clut224.c ../drivers/video/logo/logo_linux_clut224.ppm"
        expected = "../drivers/video/logo/logo_linux_clut224.ppm"
        self._assert_parsing(cmd, expected)

    # relacheck command tests
    def test_relacheck(self):
        cmd = "arch/arm64/kernel/pi/relacheck arch/arm64/kernel/pi/idreg-override.pi.o arch/arm64/kernel/pi/idreg-override.o"
        expected = "arch/arm64/kernel/pi/idreg-override.pi.o"
        self._assert_parsing(cmd, expected)

    # gen-hyprel command tests
    def test_gen_hyprel(self):
        cmd = "arch/arm64/kvm/hyp/nvhe/gen-hyprel arch/arm64/kvm/hyp/nvhe/kvm_nvhe.tmp.o > arch/arm64/kvm/hyp/nvhe/hyp-reloc.S"
        expected = "arch/arm64/kvm/hyp/nvhe/kvm_nvhe.tmp.o"
        self._assert_parsing(cmd, expected)

    # mkregtable command tests
    def test_mkregtable(self):
        cmd = "drivers/gpu/drm/radeon/mkregtable ../drivers/gpu/drm/radeon/reg_srcs/r100 > drivers/gpu/drm/radeon/r100_reg_safe.h"
        expected = "../drivers/gpu/drm/radeon/reg_srcs/r100"
        self._assert_parsing(cmd, expected)

    # genheaders command tests
    def test_genheaders(self):
        cmd = "security/selinux/genheaders security/selinux/flask.h security/selinux/av_permissions.h"
        expected = ""
        self._assert_parsing(cmd, expected)

    # mkcpustr command tests
    def test_mkcpustr(self):
        cmd = "arch/x86/boot/mkcpustr > arch/x86/boot/cpustr.h"
        expected = ""
        self._assert_parsing(cmd, expected)

    # polgen command tests
    def test_polgen(self):
        cmd = "scripts/ipe/polgen/polgen security/ipe/boot_policy.c"
        expected = ""
        self._assert_parsing(cmd, expected)

    # gen_header.py command tests
    def test_gen_header(self):
        cmd = "mkdir -p drivers/gpu/drm/msm/generated && python3 ../drivers/gpu/drm/msm/registers/gen_header.py --no-validate --rnn ../drivers/gpu/drm/msm/registers --xml ../drivers/gpu/drm/msm/registers/adreno/a2xx.xml c-defines > drivers/gpu/drm/msm/generated/a2xx.xml.h"
        expected = "../drivers/gpu/drm/msm/registers/adreno/a2xx.xml"
        self._assert_parsing(cmd, expected)


if __name__ == "__main__":
    unittest.main()
