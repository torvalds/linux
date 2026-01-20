# SPDX-License-Identifier: GPL-2.0

import json
import subprocess
import tempfile

import gdb

from linux import constants, lists, radixtree, utils


if constants.LX_CONFIG_BPF and constants.LX_CONFIG_BPF_JIT:
    bpf_ksym_type = utils.CachedType("struct bpf_ksym")
if constants.LX_CONFIG_BPF_SYSCALL:
    bpf_prog_type = utils.CachedType("struct bpf_prog")


def get_ksym_name(ksym):
    name = ksym["name"].bytes
    end = name.find(b"\x00")
    if end != -1:
        name = name[:end]
    return name.decode()


def list_ksyms():
    if not (constants.LX_CONFIG_BPF and constants.LX_CONFIG_BPF_JIT):
        return []
    bpf_kallsyms = gdb.parse_and_eval("&bpf_kallsyms")
    bpf_ksym_ptr_type = bpf_ksym_type.get_type().pointer()
    return list(lists.list_for_each_entry(bpf_kallsyms,
                                          bpf_ksym_ptr_type,
                                          "lnode"))


class KsymAddBreakpoint(gdb.Breakpoint):
    def __init__(self, monitor):
        super(KsymAddBreakpoint, self).__init__("bpf_ksym_add", internal=True)
        self.silent = True
        self.monitor = monitor

    def stop(self):
        self.monitor.add(gdb.parse_and_eval("ksym"))
        return False


class KsymRemoveBreakpoint(gdb.Breakpoint):
    def __init__(self, monitor):
        super(KsymRemoveBreakpoint, self).__init__("bpf_ksym_del",
                                                   internal=True)
        self.silent = True
        self.monitor = monitor

    def stop(self):
        self.monitor.remove(gdb.parse_and_eval("ksym"))
        return False


class KsymMonitor:
    def __init__(self, add, remove):
        self.add = add
        self.remove = remove

        self.add_bp = KsymAddBreakpoint(self)
        self.remove_bp = KsymRemoveBreakpoint(self)

        self.notify_initial()

    def notify_initial(self):
        for ksym in list_ksyms():
            self.add(ksym)

    def delete(self):
        self.add_bp.delete()
        self.remove_bp.delete()


def list_progs():
    if not constants.LX_CONFIG_BPF_SYSCALL:
        return []
    idr_rt = gdb.parse_and_eval("&prog_idr.idr_rt")
    bpf_prog_ptr_type = bpf_prog_type.get_type().pointer()
    progs = []
    for _, slot in radixtree.for_each_slot(idr_rt):
        prog = slot.dereference().cast(bpf_prog_ptr_type)
        progs.append(prog)
        # Subprogs are not registered in prog_idr, fetch them manually.
        # func[0] is the current prog.
        aux = prog["aux"]
        func = aux["func"]
        real_func_cnt = int(aux["real_func_cnt"])
        for i in range(1, real_func_cnt):
            progs.append(func[i])
    return progs


class ProgAddBreakpoint(gdb.Breakpoint):
    def __init__(self, monitor):
        super(ProgAddBreakpoint, self).__init__("bpf_prog_kallsyms_add",
                                                internal=True)
        self.silent = True
        self.monitor = monitor

    def stop(self):
        self.monitor.add(gdb.parse_and_eval("fp"))
        return False


class ProgRemoveBreakpoint(gdb.Breakpoint):
    def __init__(self, monitor):
        super(ProgRemoveBreakpoint, self).__init__("bpf_prog_free_id",
                                                   internal=True)
        self.silent = True
        self.monitor = monitor

    def stop(self):
        self.monitor.remove(gdb.parse_and_eval("prog"))
        return False


class ProgMonitor:
    def __init__(self, add, remove):
        self.add = add
        self.remove = remove

        self.add_bp = ProgAddBreakpoint(self)
        self.remove_bp = ProgRemoveBreakpoint(self)

        self.notify_initial()

    def notify_initial(self):
        for prog in list_progs():
            self.add(prog)

    def delete(self):
        self.add_bp.delete()
        self.remove_bp.delete()


def btf_str_by_offset(btf, offset):
    while offset < btf["start_str_off"]:
        btf = btf["base_btf"]

    offset -= btf["start_str_off"]
    if offset < btf["hdr"]["str_len"]:
        return (btf["strings"] + offset).string()

    return None


def bpf_line_info_line_num(line_col):
    return line_col >> 10


def bpf_line_info_line_col(line_col):
    return line_col & 0x3ff


class LInfoIter:
    def __init__(self, prog):
        # See bpf_prog_get_file_line() for details.
        self.pos = 0
        self.nr_linfo = 0

        if prog is None:
            return

        self.bpf_func = int(prog["bpf_func"])
        aux = prog["aux"]
        self.btf = aux["btf"]
        linfo_idx = aux["linfo_idx"]
        self.nr_linfo = int(aux["nr_linfo"]) - linfo_idx
        if self.nr_linfo == 0:
            return

        linfo_ptr = aux["linfo"]
        tpe = linfo_ptr.type.target().array(self.nr_linfo).pointer()
        self.linfo = (linfo_ptr + linfo_idx).cast(tpe).dereference()
        jited_linfo_ptr = aux["jited_linfo"]
        tpe = jited_linfo_ptr.type.target().array(self.nr_linfo).pointer()
        self.jited_linfo = (jited_linfo_ptr + linfo_idx).cast(tpe).dereference()

        self.filenos = {}

    def get_code_off(self):
        if self.pos >= self.nr_linfo:
            return -1
        return self.jited_linfo[self.pos] - self.bpf_func

    def advance(self):
        self.pos += 1

    def get_fileno(self):
        file_name_off = int(self.linfo[self.pos]["file_name_off"])
        fileno = self.filenos.get(file_name_off)
        if fileno is not None:
            return fileno, None
        file_name = btf_str_by_offset(self.btf, file_name_off)
        fileno = len(self.filenos) + 1
        self.filenos[file_name_off] = fileno
        return fileno, file_name

    def get_line_col(self):
        line_col = int(self.linfo[self.pos]["line_col"])
        return bpf_line_info_line_num(line_col), \
               bpf_line_info_line_col(line_col)


def generate_debug_obj(ksym, prog):
    name = get_ksym_name(ksym)
    # Avoid read_memory(); it throws bogus gdb.MemoryError in some contexts.
    start = ksym["start"]
    code = start.cast(gdb.lookup_type("unsigned char")
                      .array(int(ksym["end"]) - int(start))
                      .pointer()).dereference().bytes
    linfo_iter = LInfoIter(prog)

    result = tempfile.NamedTemporaryFile(suffix=".o", mode="wb")
    try:
        with tempfile.NamedTemporaryFile(suffix=".s", mode="w") as src:
            # ".loc" does not apply to ".byte"s, only to ".insn"s, but since
            # this needs to work for all architectures, the latter are not an
            # option. Ask the assembler to apply ".loc"s to labels as well,
            # and generate dummy labels after each ".loc".
            src.write(".loc_mark_labels 1\n")

            src.write(".globl {}\n".format(name))
            src.write(".type {},@function\n".format(name))
            src.write("{}:\n".format(name))
            for code_off, code_byte in enumerate(code):
                if linfo_iter.get_code_off() == code_off:
                    fileno, file_name = linfo_iter.get_fileno()
                    if file_name is not None:
                        src.write(".file {} {}\n".format(
                            fileno, json.dumps(file_name)))
                    line, col = linfo_iter.get_line_col()
                    src.write(".loc {} {} {}\n".format(fileno, line, col))
                    src.write("0:\n")
                    linfo_iter.advance()
                src.write(".byte {}\n".format(code_byte))
            src.write(".size {},{}\n".format(name, len(code)))
            src.flush()

            try:
                subprocess.check_call(["as", "-c", src.name, "-o", result.name])
            except FileNotFoundError:
                # "as" is not installed.
                result.close()
                return None
        return result
    except:
        result.close()
        raise
