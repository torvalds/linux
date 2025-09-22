#!/usr/bin/env python

import lldb
import shlex


@lldb.command("shadow")
def check_shadow_command(debugger, command, exe_ctx, result, dict):
    """Check the currently selected stack frame for shadowed variables"""
    process = exe_ctx.GetProcess()
    state = process.GetState()
    if state != lldb.eStateStopped:
        print(
            "process must be stopped, state is %s"
            % lldb.SBDebugger.StateAsCString(state),
            file=result,
        )
        return
    frame = exe_ctx.GetFrame()
    if not frame:
        print("invalid frame", file=result)
        return
    # Parse command line args
    command_args = shlex.split(command)
    # TODO: add support for using arguments that are passed to this command...

    # Make a dictionary of variable name to "SBBlock and SBValue"
    shadow_dict = {}

    num_shadowed_variables = 0
    # Get the deepest most block from the current frame
    block = frame.GetBlock()
    # Iterate through the block and all of its parents
    while block.IsValid():
        # Get block variables from the current block only
        block_vars = block.GetVariables(frame, True, True, True, 0)
        # Iterate through all variables in the current block
        for block_var in block_vars:
            # Since we can have multiple shadowed variables, we our variable
            # name dictionary to have an array or "block + variable" pairs so
            # We can correctly print out all shadowed variables and whow which
            # blocks they come from
            block_var_name = block_var.GetName()
            if block_var_name in shadow_dict:
                shadow_dict[block_var_name].append(block_var)
            else:
                shadow_dict[block_var_name] = [block_var]
        # Get the parent block and continue
        block = block.GetParent()

    num_shadowed_variables = 0
    if shadow_dict:
        for name in shadow_dict.keys():
            shadow_vars = shadow_dict[name]
            if len(shadow_vars) > 1:
                print('"%s" is shadowed by the following declarations:' % (name))
                num_shadowed_variables += 1
                for shadow_var in shadow_vars:
                    print(str(shadow_var.GetDeclaration()), file=result)
    if num_shadowed_variables == 0:
        print("no variables are shadowed", file=result)
