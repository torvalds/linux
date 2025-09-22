import lldb
import re


def parse_linespec(linespec, frame, result):
    """Handles a subset of GDB-style linespecs.  Specifically:

    number           - A line in the current file
    +offset          - The line /offset/ lines after this line
    -offset          - The line /offset/ lines before this line
    filename:number  - Line /number/ in file /filename/
    function         - The start of /function/
    *address         - The pointer target of /address/, which must be a literal (but see `` in LLDB)

    We explicitly do not handle filename:function because it is ambiguous in Objective-C.

    This function returns a list of addresses."""

    breakpoint = None
    target = frame.GetThread().GetProcess().GetTarget()

    matched = False

    if not matched:
        mo = re.match("^([0-9]+)$", linespec)
        if mo is not None:
            matched = True
            # print "Matched <linenum>"
            line_number = int(mo.group(1))
            line_entry = frame.GetLineEntry()
            if not line_entry.IsValid():
                result.AppendMessage(
                    "Specified a line in the current file, but the current frame doesn't have line table information."
                )
                return
            breakpoint = target.BreakpointCreateByLocation(
                line_entry.GetFileSpec(), line_number
            )

    if not matched:
        mo = re.match("^\+([0-9]+)$", linespec)
        if mo is not None:
            matched = True
            # print "Matched +<count>"
            line_number = int(mo.group(1))
            line_entry = frame.GetLineEntry()
            if not line_entry.IsValid():
                result.AppendMessage(
                    "Specified a line in the current file, but the current frame doesn't have line table information."
                )
                return
            breakpoint = target.BreakpointCreateByLocation(
                line_entry.GetFileSpec(), (line_entry.GetLine() + line_number)
            )

    if not matched:
        mo = re.match("^\-([0-9]+)$", linespec)
        if mo is not None:
            matched = True
            # print "Matched -<count>"
            line_number = int(mo.group(1))
            line_entry = frame.GetLineEntry()
            if not line_entry.IsValid():
                result.AppendMessage(
                    "Specified a line in the current file, but the current frame doesn't have line table information."
                )
                return
            breakpoint = target.BreakpointCreateByLocation(
                line_entry.GetFileSpec(), (line_entry.GetLine() - line_number)
            )

    if not matched:
        mo = re.match("^(.*):([0-9]+)$", linespec)
        if mo is not None:
            matched = True
            # print "Matched <filename>:<linenum>"
            file_name = mo.group(1)
            line_number = int(mo.group(2))
            breakpoint = target.BreakpointCreateByLocation(file_name, line_number)

    if not matched:
        mo = re.match("\*((0x)?([0-9a-f]+))$", linespec)
        if mo is not None:
            matched = True
            # print "Matched <address-expression>"
            address = int(mo.group(1), base=0)
            breakpoint = target.BreakpointCreateByAddress(address)

    if not matched:
        # print "Trying <function-name>"
        breakpoint = target.BreakpointCreateByName(linespec)

    num_locations = breakpoint.GetNumLocations()

    if num_locations == 0:
        result.AppendMessage(
            "The line specification provided doesn't resolve to any addresses."
        )

    addr_list = []

    for location_index in range(num_locations):
        location = breakpoint.GetLocationAtIndex(location_index)
        addr_list.append(location.GetAddress())

    target.BreakpointDelete(breakpoint.GetID())

    return addr_list


def usage_string():
    return """   Sets the program counter to a specific address.

Syntax: jump <linespec> [<location-id>]

Command Options Usage:
  jump <linenum>
  jump +<count>
  jump -<count>
  jump <filename>:<linenum>
  jump <function-name>
  jump *<address-expression>

<location-id> serves to disambiguate when multiple locations could be meant."""


def jump(debugger, command, result, internal_dict):
    if command == "":
        result.AppendMessage(usage_string())

    args = command.split()

    if not debugger.IsValid():
        result.AppendMessage("Invalid debugger!")
        return

    target = debugger.GetSelectedTarget()
    if not target.IsValid():
        result.AppendMessage("jump requires a valid target.")
        return

    process = target.GetProcess()
    if not process.IsValid():
        result.AppendMessage("jump requires a valid process.")
        return

    thread = process.GetSelectedThread()
    if not thread.IsValid():
        result.AppendMessage("jump requires a valid thread.")
        return

    frame = thread.GetSelectedFrame()
    if not frame.IsValid():
        result.AppendMessage("jump requires a valid frame.")
        return

    addresses = parse_linespec(args[0], frame, result)

    stream = lldb.SBStream()

    if len(addresses) == 0:
        return

    desired_address = addresses[0]

    if len(addresses) > 1:
        if len(args) == 2:
            desired_index = int(args[1])
            if (desired_index >= 0) and (desired_index < len(addresses)):
                desired_address = addresses[desired_index]
            else:
                result.AppendMessage(
                    "Desired index " + args[1] + " is not one of the options."
                )
                return
        else:
            index = 0
            result.AppendMessage("The specified location resolves to multiple targets.")
            for address in addresses:
                stream.Clear()
                address.GetDescription(stream)
                result.AppendMessage(
                    "  Location ID " + str(index) + ": " + stream.GetData()
                )
                index = index + 1
            result.AppendMessage(
                "Please type 'jump " + command + " <location-id>' to choose one."
            )
            return

    frame.SetPC(desired_address.GetLoadAddress(target))


def __lldb_init_module(debugger, internal_dict):
    # Module is being run inside the LLDB interpreter
    jump.__doc__ = usage_string()
    debugger.HandleCommand("command script add -o -f jump.jump jump")
    print(
        'The "jump" command has been installed, type "help jump" or "jump <ENTER>" for detailed help.'
    )
