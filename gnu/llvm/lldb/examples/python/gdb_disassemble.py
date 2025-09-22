import lldb


def disassemble(debugger, command, result, dict):
    if lldb.frame.function:
        instructions = lldb.frame.function.instructions
        start_addr = lldb.frame.function.addr.load_addr
        name = lldb.frame.function.name
    elif lldb.frame.symbol:
        instructions = lldb.frame.symbol.instructions
        start_addr = lldb.frame.symbol.addr.load_addr
        name = lldb.frame.symbol.name

    for inst in instructions:
        inst_addr = inst.addr.load_addr
        inst_offset = inst_addr - start_addr
        comment = inst.comment
        if comment:
            print(
                "<%s + %-4u> 0x%x %8s  %s ; %s"
                % (name, inst_offset, inst_addr, inst.mnemonic, inst.operands, comment)
            )
        else:
            print(
                "<%s + %-4u> 0x%x %8s  %s"
                % (name, inst_offset, inst_addr, inst.mnemonic, inst.operands)
            )


# Install the command when the module gets imported
def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand(
        "command script add -o -f gdb_disassemble.disassemble gdb-disassemble"
    )
    print('Installed "gdb-disassemble" command for disassembly')
