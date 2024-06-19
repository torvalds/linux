# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2023 Broadcom

import gdb

from linux import constants
from linux import cpus
from linux import utils
from linux import radixtree

irq_desc_type = utils.CachedType("struct irq_desc")

def irq_settings_is_hidden(desc):
    return desc['status_use_accessors'] & constants.LX_IRQ_HIDDEN

def irq_desc_is_chained(desc):
    return desc['action'] and desc['action'] == gdb.parse_and_eval("&chained_action")

def irqd_is_level(desc):
    return desc['irq_data']['common']['state_use_accessors'] & constants.LX_IRQD_LEVEL

def show_irq_desc(prec, irq):
    text = ""

    desc = radixtree.lookup(gdb.parse_and_eval("&irq_desc_tree"), irq)
    if desc is None:
        return text

    desc = desc.cast(irq_desc_type.get_type())
    if desc is None:
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
        text += "%10u" % (count)

    name = "None"
    if desc['irq_data']['chip']:
        chip = desc['irq_data']['chip']
        if chip['name']:
            name = chip['name'].string()
        else:
            name = "-"

    text += "  %8s" % (name)

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

def x86_show_irqstat(prec, pfx, field, desc):
    irq_stat = gdb.parse_and_eval("&irq_stat")
    text = "%*s: " % (prec, pfx)
    for cpu in cpus.each_online_cpu():
        stat = cpus.per_cpu(irq_stat, cpu)
        text += "%10u " % (stat[field])
    text += "  %s\n" % (desc)
    return text

def x86_show_mce(prec, var, pfx, desc):
    pvar = gdb.parse_and_eval(var)
    text = "%*s: " % (prec, pfx)
    for cpu in cpus.each_online_cpu():
        text += "%10u " % (cpus.per_cpu(pvar, cpu))
    text += "  %s\n" % (desc)
    return text

def x86_show_interupts(prec):
    text = x86_show_irqstat(prec, "NMI", '__nmi_count', 'Non-maskable interrupts')

    if constants.LX_CONFIG_X86_LOCAL_APIC:
        text += x86_show_irqstat(prec, "LOC", 'apic_timer_irqs', "Local timer interrupts")
        text += x86_show_irqstat(prec, "SPU", 'irq_spurious_count', "Spurious interrupts")
        text += x86_show_irqstat(prec, "PMI", 'apic_perf_irqs', "Performance monitoring interrupts")
        text += x86_show_irqstat(prec, "IWI", 'apic_irq_work_irqs', "IRQ work interrupts")
        text += x86_show_irqstat(prec, "RTR", 'icr_read_retry_count', "APIC ICR read retries")
        if utils.gdb_eval_or_none("x86_platform_ipi_callback") is not None:
            text += x86_show_irqstat(prec, "PLT", 'x86_platform_ipis', "Platform interrupts")

    if constants.LX_CONFIG_SMP:
        text += x86_show_irqstat(prec, "RES", 'irq_resched_count', "Rescheduling interrupts")
        text += x86_show_irqstat(prec, "CAL", 'irq_call_count', "Function call interrupts")
        text += x86_show_irqstat(prec, "TLB", 'irq_tlb_count', "TLB shootdowns")

    if constants.LX_CONFIG_X86_THERMAL_VECTOR:
        text += x86_show_irqstat(prec, "TRM", 'irq_thermal_count', "Thermal events interrupts")

    if constants.LX_CONFIG_X86_MCE_THRESHOLD:
        text += x86_show_irqstat(prec, "THR", 'irq_threshold_count', "Threshold APIC interrupts")

    if constants.LX_CONFIG_X86_MCE_AMD:
        text += x86_show_irqstat(prec, "DFR", 'irq_deferred_error_count', "Deferred Error APIC interrupts")

    if constants.LX_CONFIG_X86_MCE:
        text += x86_show_mce(prec, "&mce_exception_count", "MCE", "Machine check exceptions")
        text == x86_show_mce(prec, "&mce_poll_count", "MCP", "Machine check polls")

    text += show_irq_err_count(prec)

    if constants.LX_CONFIG_X86_IO_APIC:
        cnt = utils.gdb_eval_or_none("irq_mis_count")
        if cnt is not None:
            text += "%*s: %10u\n" % (prec, "MIS", cnt['counter'])

    if constants.LX_CONFIG_KVM:
        text += x86_show_irqstat(prec, "PIN", 'kvm_posted_intr_ipis', 'Posted-interrupt notification event')
        text += x86_show_irqstat(prec, "NPI", 'kvm_posted_intr_nested_ipis', 'Nested posted-interrupt event')
        text += x86_show_irqstat(prec, "PIW", 'kvm_posted_intr_wakeup_ipis', 'Posted-interrupt wakeup event')

    return text

def arm_common_show_interrupts(prec):
    text = ""
    nr_ipi = utils.gdb_eval_or_none("nr_ipi")
    ipi_desc = utils.gdb_eval_or_none("ipi_desc")
    ipi_types = utils.gdb_eval_or_none("ipi_types")
    if nr_ipi is None or ipi_desc is None or ipi_types is None:
        return text

    if prec >= 4:
        sep = " "
    else:
        sep = ""

    for ipi in range(nr_ipi):
        text += "%*s%u:%s" % (prec - 1, "IPI", ipi, sep)
        desc = ipi_desc[ipi].cast(irq_desc_type.get_type().pointer())
        if desc == 0:
            continue
        for cpu in cpus.each_online_cpu():
            text += "%10u" % (cpus.per_cpu(desc['kstat_irqs'], cpu)['cnt'])
        text += "      %s" % (ipi_types[ipi].string())
        text += "\n"
    return text

def aarch64_show_interrupts(prec):
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
        nr_irqs = gdb.parse_and_eval("nr_irqs")
        prec = 3
        j = 1000
        while prec < 10 and j <= nr_irqs:
            prec += 1
            j *= 10

        gdb.write("%*s" % (prec + 8, ""))
        for cpu in cpus.each_online_cpu():
            gdb.write("CPU%-8d" % cpu)
        gdb.write("\n")

        if utils.gdb_eval_or_none("&irq_desc_tree") is None:
            return

        for irq in range(nr_irqs):
            gdb.write(show_irq_desc(prec, irq))
        gdb.write(arch_show_interrupts(prec))


LxInterruptList()
