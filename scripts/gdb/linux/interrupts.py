# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2023 Broadcom

import gdb

from linux import constants
from linux import cpus
from linux import utils
from linux import mapletree

irq_desc_type = utils.CachedType("struct irq_desc")

def irq_settings_is_hidden(desc):
    return desc['status_use_accessors'] & constants.LX_IRQ_HIDDEN

def irq_desc_is_chained(desc):
    return desc['action'] and desc['action'] == gdb.parse_and_eval("&chained_action")

def irqd_is_level(desc):
    return desc['irq_data']['common']['state_use_accessors'] & constants.LX_IRQD_LEVEL

def show_irq_desc(prec, chip_width, irq):
    text = ""

    desc = mapletree.mtree_load(gdb.parse_and_eval("&sparse_irqs"), irq)
    if desc is None:
        return text

    desc = desc.cast(irq_desc_type.get_type().pointer())
    if desc == 0:
        return text

    if irq_settings_is_hidden(desc):
        return text

    any_count = 0
    if desc['kstat_irqs']:
        for cpu in cpus.each_online_cpu():
            any_count += cpus.per_cpu(desc['kstat_irqs'], cpu)['cnt']

    if (desc['action'] == 0 or irq_desc_is_chained(desc)) and any_count == 0:
        return text;

    text += "%*d: " % (prec, irq)
    for cpu in cpus.each_online_cpu():
        if desc['kstat_irqs']:
            count = cpus.per_cpu(desc['kstat_irqs'], cpu)['cnt']
        else:
            count = 0
        text += "%10u " % (count)

    name = "None"
    if desc['irq_data']['chip']:
        chip = desc['irq_data']['chip']
        if chip['name']:
            name = chip['name'].string()
        else:
            name = "-"

    text += "  %-*s" % (chip_width, name)

    if desc['irq_data']['domain']:
        text += "  %*lu" % (prec, desc['irq_data']['hwirq'])
    else:
        text += "  %*s" % (prec, "")

    if constants.LX_CONFIG_GENERIC_IRQ_SHOW_LEVEL:
        text += " %-8s" % ("Level" if irqd_is_level(desc) else "Edge")

    if desc['name']:
        text += "-%-8s" % (desc['name'].string())

    """ Some toolchains may not be able to provide information about irqaction """
    try:
        gdb.lookup_type("struct irqaction")
        action = desc['action']
        if action is not None:
            text += "  %s" % (action['name'].string())
            while True:
                action = action['next']
                if action is not None:
                    break
                if action['name']:
                    text += ", %s" % (action['name'].string())
    except:
        pass

    text += "\n"

    return text

def show_irq_err_count(prec):
    cnt = utils.gdb_eval_or_none("irq_err_count")
    text = ""
    if cnt is not None:
        text += "%*s: %10u\n" % (prec, "ERR", cnt['counter'])
    return text

def x86_show_irqstat(prec, pfx, idx, desc):
    irq_stat = gdb.parse_and_eval("&irq_stat.counts[%d]" %idx)
    text = "%*s: " % (prec, pfx)
    for cpu in cpus.each_online_cpu():
        stat = cpus.per_cpu(irq_stat, cpu)
        text += "%10u " % (stat.dereference())
    text += desc
    return text

def x86_show_interupts(prec):
    info_type = gdb.lookup_type('struct irq_stat_info')
    info = gdb.parse_and_eval('irq_stat_info')
    bitmap = gdb.parse_and_eval('irq_stat_count_show')
    bitsperlong = 8 * int(bitmap.type.target().sizeof)

    text = ""
    for idx in range(int(info.type.sizeof / info_type.sizeof)):
        show = bitmap[int(idx / bitsperlong)]
        if not show & 1 << int(idx % bitsperlong):
            continue
        pfx = info[idx]['symbol'].string()
        desc = info[idx]['text'].string()
        text += x86_show_irqstat(prec, pfx, idx, desc)

    return text

def arm_common_show_interrupts(prec):
    text = ""
    nr_ipi = utils.gdb_eval_or_none("nr_ipi")
    ipi_desc = utils.gdb_eval_or_none("ipi_desc")
    ipi_types = utils.gdb_eval_or_none("ipi_types")
    if nr_ipi is None or ipi_desc is None or ipi_types is None:
        return text

    for ipi in range(nr_ipi):
        text += "%*s%u: " % (prec - 1, "IPI", ipi)
        desc = ipi_desc[ipi].cast(irq_desc_type.get_type().pointer())
        if desc == 0:
            continue
        for cpu in cpus.each_online_cpu():
            text += "%10u " % (cpus.per_cpu(desc['kstat_irqs'], cpu)['cnt'])
        text += "%s" % (ipi_types[ipi].string())
        text += "\n"
    return text

def aarch64_show_interrupts(prec):
    # Does not work for ARM64 as "ipi_desc" is not available there
    text = arm_common_show_interrupts(prec)
    text += "%*s: %10lu\n" % (prec, "ERR", gdb.parse_and_eval("irq_err_count"))
    return text

def arch_show_interrupts(prec):
    text = ""
    if utils.is_target_arch("x86"):
        text += x86_show_interupts(prec)
    elif utils.is_target_arch("aarch64"):
        text += aarch64_show_interrupts(prec)
    elif utils.is_target_arch("arm"):
        text += arm_common_show_interrupts(prec)
    elif utils.is_target_arch("mips"):
        text += show_irq_err_count(prec)
    else:
        raise gdb.GdbError("Unsupported architecture: {}".format(target_arch))

    return text

class LxInterruptList(gdb.Command):
    """Print /proc/interrupts"""

    def __init__(self):
        super(LxInterruptList, self).__init__("lx-interruptlist", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        nr_irqs = gdb.parse_and_eval("total_nr_irqs")
        constr = utils.gdb_eval_or_none('irq_proc_constraints')

        if constr:
            prec = int(constr['num_prec'])
            chip_width = int(constr['chip_width'])
        else:
            prec = 4
            j = 10000
            while prec < 10 and j <= nr_irqs:
                prec += 1
                j *= 10
            chip_width = 8

        gdb.write("%*s" % (prec + 8, ""))
        for cpu in cpus.each_online_cpu():
            gdb.write("CPU%-8d" % cpu)
        gdb.write("\n")

        if utils.gdb_eval_or_none("&sparse_irqs") is None:
            raise gdb.GdbError("Unable to find the sparse IRQ tree, is CONFIG_SPARSE_IRQ enabled?")

        for irq in range(nr_irqs):
            gdb.write(show_irq_desc(prec, chip_width, irq))
        gdb.write(arch_show_interrupts(prec))


LxInterruptList()
