#!/usr/bin/env python3

# ----------------------------------------------------------------------
# This module is designed to live inside the "lldb" python package
# in the "lldb.macosx" package. To use this in the embedded python
# interpreter using "lldb" just import it:
#
#   (lldb) script import lldb.macosx.heap
# ----------------------------------------------------------------------

import lldb
import optparse
import os
import os.path
import re
import shlex
import string
import sys
import tempfile
import lldb.utils.symbolication

g_libheap_dylib_dir = None
g_libheap_dylib_dict = dict()


def get_iterate_memory_expr(options, process, user_init_code, user_return_code):
    expr = """
typedef unsigned natural_t;
typedef uintptr_t vm_size_t;
typedef uintptr_t vm_address_t;
typedef natural_t task_t;
typedef int kern_return_t;
#define KERN_SUCCESS 0
typedef void (*range_callback_t)(task_t, void *, unsigned, uintptr_t, uintptr_t);
"""
    if options.search_vm_regions:
        expr += """
typedef int vm_prot_t;
typedef unsigned int vm_inherit_t;
typedef unsigned long long	memory_object_offset_t;
typedef unsigned int boolean_t;
typedef int vm_behavior_t;
typedef uint32_t vm32_object_id_t;
typedef natural_t mach_msg_type_number_t;
typedef uint64_t mach_vm_address_t;
typedef uint64_t mach_vm_offset_t;
typedef uint64_t mach_vm_size_t;
typedef uint64_t vm_map_offset_t;
typedef uint64_t vm_map_address_t;
typedef uint64_t vm_map_size_t;
#define	VM_PROT_NONE ((vm_prot_t) 0x00)
#define VM_PROT_READ ((vm_prot_t) 0x01)
#define VM_PROT_WRITE ((vm_prot_t) 0x02)
#define VM_PROT_EXECUTE ((vm_prot_t) 0x04)
typedef struct vm_region_submap_short_info_data_64_t {
    vm_prot_t protection;
    vm_prot_t max_protection;
    vm_inherit_t inheritance;
    memory_object_offset_t offset;		// offset into object/map
    unsigned int user_tag;	// user tag on map entry
    unsigned int ref_count;	 // obj/map mappers, etc
    unsigned short shadow_depth; 	// only for obj
    unsigned char external_pager;  // only for obj
    unsigned char share_mode;	// see enumeration
    boolean_t is_submap;	// submap vs obj
    vm_behavior_t behavior;	// access behavior hint
    vm32_object_id_t object_id;	// obj/map name, not a handle
    unsigned short user_wired_count;
} vm_region_submap_short_info_data_64_t;
#define VM_REGION_SUBMAP_SHORT_INFO_COUNT_64 ((mach_msg_type_number_t)(sizeof(vm_region_submap_short_info_data_64_t)/sizeof(int)))"""
        if user_init_code:
            expr += user_init_code
        expr += """
task_t task = (task_t)mach_task_self();
mach_vm_address_t vm_region_base_addr;
mach_vm_size_t vm_region_size;
natural_t vm_region_depth;
vm_region_submap_short_info_data_64_t vm_region_info;
kern_return_t err;
for (vm_region_base_addr = 0, vm_region_size = 1; vm_region_size != 0; vm_region_base_addr += vm_region_size)
{
    mach_msg_type_number_t vm_region_info_size = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;
    err = (kern_return_t)mach_vm_region_recurse (task,
                                                 &vm_region_base_addr,
                                                 &vm_region_size,
                                                 &vm_region_depth,
                                                 &vm_region_info,
                                                 &vm_region_info_size);
    if (err)
        break;
    // Check all read + write regions. This will cover the thread stacks
    // and any regions of memory like __DATA segments, that might contain
    // data we are looking for
    if (vm_region_info.protection & VM_PROT_WRITE &&
        vm_region_info.protection & VM_PROT_READ)
    {
        baton.callback (task,
                        &baton,
                        64,
                        vm_region_base_addr,
                        vm_region_size);
    }
}"""
    else:
        if options.search_stack:
            expr += get_thread_stack_ranges_struct(process)
        if options.search_segments:
            expr += get_sections_ranges_struct(process)
        if user_init_code:
            expr += user_init_code
        if options.search_heap:
            expr += """
#define MALLOC_PTR_IN_USE_RANGE_TYPE 1
typedef struct vm_range_t {
    vm_address_t	address;
    vm_size_t		size;
} vm_range_t;
typedef kern_return_t (*memory_reader_t)(task_t, vm_address_t, vm_size_t, void **);
typedef void (*vm_range_recorder_t)(task_t, void *, unsigned, vm_range_t *, unsigned);
typedef struct malloc_introspection_t {
    kern_return_t (*enumerator)(task_t task, void *, unsigned type_mask, vm_address_t zone_address, memory_reader_t reader, vm_range_recorder_t recorder); /* enumerates all the malloc pointers in use */
} malloc_introspection_t;
typedef struct malloc_zone_t {
    void *reserved1[12];
    struct malloc_introspection_t	*introspect;
} malloc_zone_t;
kern_return_t malloc_get_all_zones(task_t, memory_reader_t, vm_address_t **, unsigned *);
memory_reader_t task_peek = [](task_t, vm_address_t remote_address, vm_size_t, void **local_memory) -> kern_return_t {
    *local_memory = (void*) remote_address;
    return KERN_SUCCESS;
};
vm_address_t *zones = 0;
unsigned int num_zones = 0;task_t task = 0;
kern_return_t err = (kern_return_t)malloc_get_all_zones (task, task_peek, &zones, &num_zones);
if (KERN_SUCCESS == err)
{
    for (unsigned int i=0; i<num_zones; ++i)
    {
        const malloc_zone_t *zone = (const malloc_zone_t *)zones[i];
        if (zone && zone->introspect)
            zone->introspect->enumerator (task,
                                          &baton,
                                          MALLOC_PTR_IN_USE_RANGE_TYPE,
                                          (vm_address_t)zone,
                                          task_peek,
                                          [] (task_t task, void *baton, unsigned type, vm_range_t *ranges, unsigned size) -> void
                                          {
                                              range_callback_t callback = ((callback_baton_t *)baton)->callback;
                                              for (unsigned i=0; i<size; ++i)
                                              {
                                                  callback (task, baton, type, ranges[i].address, ranges[i].size);
                                              }
                                          });
    }
}"""

        if options.search_stack:
            expr += """
#ifdef NUM_STACKS
// Call the callback for the thread stack ranges
for (uint32_t i=0; i<NUM_STACKS; ++i) {
    range_callback(task, &baton, 8, stacks[i].base, stacks[i].size);
    if (STACK_RED_ZONE_SIZE > 0) {
        range_callback(task, &baton, 16, stacks[i].base - STACK_RED_ZONE_SIZE, STACK_RED_ZONE_SIZE);
    }
}
#endif"""

        if options.search_segments:
            expr += """
#ifdef NUM_SEGMENTS
// Call the callback for all segments
for (uint32_t i=0; i<NUM_SEGMENTS; ++i)
    range_callback(task, &baton, 32, segments[i].base, segments[i].size);
#endif"""

    if user_return_code:
        expr += "\n%s" % (user_return_code,)

    return expr


def get_member_types_for_offset(value_type, offset, member_list):
    member = value_type.GetFieldAtIndex(0)
    search_bases = False
    if member:
        if member.GetOffsetInBytes() <= offset:
            for field_idx in range(value_type.GetNumberOfFields()):
                member = value_type.GetFieldAtIndex(field_idx)
                member_byte_offset = member.GetOffsetInBytes()
                member_end_byte_offset = member_byte_offset + member.type.size
                if member_byte_offset <= offset and offset < member_end_byte_offset:
                    member_list.append(member)
                    get_member_types_for_offset(
                        member.type, offset - member_byte_offset, member_list
                    )
                    return
        else:
            search_bases = True
    else:
        search_bases = True
    if search_bases:
        for field_idx in range(value_type.GetNumberOfDirectBaseClasses()):
            member = value_type.GetDirectBaseClassAtIndex(field_idx)
            member_byte_offset = member.GetOffsetInBytes()
            member_end_byte_offset = member_byte_offset + member.type.size
            if member_byte_offset <= offset and offset < member_end_byte_offset:
                member_list.append(member)
                get_member_types_for_offset(
                    member.type, offset - member_byte_offset, member_list
                )
                return
        for field_idx in range(value_type.GetNumberOfVirtualBaseClasses()):
            member = value_type.GetVirtualBaseClassAtIndex(field_idx)
            member_byte_offset = member.GetOffsetInBytes()
            member_end_byte_offset = member_byte_offset + member.type.size
            if member_byte_offset <= offset and offset < member_end_byte_offset:
                member_list.append(member)
                get_member_types_for_offset(
                    member.type, offset - member_byte_offset, member_list
                )
                return


def append_regex_callback(option, opt, value, parser):
    try:
        ivar_regex = re.compile(value)
        parser.values.ivar_regex_exclusions.append(ivar_regex)
    except:
        print(
            'error: an exception was thrown when compiling the ivar regular expression for "%s"'
            % value
        )


def add_common_options(parser):
    parser.add_option(
        "-v",
        "--verbose",
        action="store_true",
        dest="verbose",
        help="display verbose debug info",
        default=False,
    )
    parser.add_option(
        "-t",
        "--type",
        action="store_true",
        dest="print_type",
        help="print the full value of the type for each matching malloc block",
        default=False,
    )
    parser.add_option(
        "-o",
        "--po",
        action="store_true",
        dest="print_object_description",
        help="print the object descriptions for any matches",
        default=False,
    )
    parser.add_option(
        "-z",
        "--size",
        action="store_true",
        dest="show_size",
        help="print the allocation size in bytes",
        default=False,
    )
    parser.add_option(
        "-r",
        "--range",
        action="store_true",
        dest="show_range",
        help="print the allocation address range instead of just the allocation base address",
        default=False,
    )
    parser.add_option(
        "-m",
        "--memory",
        action="store_true",
        dest="memory",
        help="dump the memory for each matching block",
        default=False,
    )
    parser.add_option(
        "-f",
        "--format",
        type="string",
        dest="format",
        help="the format to use when dumping memory if --memory is specified",
        default=None,
    )
    parser.add_option(
        "-I",
        "--omit-ivar-regex",
        type="string",
        action="callback",
        callback=append_regex_callback,
        dest="ivar_regex_exclusions",
        default=[],
        help="specify one or more regular expressions used to backlist any matches that are in ivars",
    )
    parser.add_option(
        "-s",
        "--stack",
        action="store_true",
        dest="stack",
        help="gets the stack that allocated each malloc block if MallocStackLogging is enabled",
        default=False,
    )
    parser.add_option(
        "-S",
        "--stack-history",
        action="store_true",
        dest="stack_history",
        help="gets the stack history for all allocations whose start address matches each malloc block if MallocStackLogging is enabled",
        default=False,
    )
    parser.add_option(
        "-F",
        "--max-frames",
        type="int",
        dest="max_frames",
        help="the maximum number of stack frames to print when using the --stack or --stack-history options (default=128)",
        default=128,
    )
    parser.add_option(
        "-H",
        "--max-history",
        type="int",
        dest="max_history",
        help="the maximum number of stack history backtraces to print for each allocation when using the --stack-history option (default=16)",
        default=16,
    )
    parser.add_option(
        "-M",
        "--max-matches",
        type="int",
        dest="max_matches",
        help="the maximum number of matches to print",
        default=32,
    )
    parser.add_option(
        "-O",
        "--offset",
        type="int",
        dest="offset",
        help="the matching data must be at this offset",
        default=-1,
    )
    parser.add_option(
        "--ignore-stack",
        action="store_false",
        dest="search_stack",
        help="Don't search the stack when enumerating memory",
        default=True,
    )
    parser.add_option(
        "--ignore-heap",
        action="store_false",
        dest="search_heap",
        help="Don't search the heap allocations when enumerating memory",
        default=True,
    )
    parser.add_option(
        "--ignore-segments",
        action="store_false",
        dest="search_segments",
        help="Don't search readable executable segments enumerating memory",
        default=True,
    )
    parser.add_option(
        "-V",
        "--vm-regions",
        action="store_true",
        dest="search_vm_regions",
        help="Check all VM regions instead of searching the heap, stack and segments",
        default=False,
    )


def type_flags_to_string(type_flags):
    if type_flags == 0:
        type_str = "free"
    elif type_flags & 2:
        type_str = "malloc"
    elif type_flags & 4:
        type_str = "free"
    elif type_flags & 1:
        type_str = "generic"
    elif type_flags & 8:
        type_str = "stack"
    elif type_flags & 16:
        type_str = "stack (red zone)"
    elif type_flags & 32:
        type_str = "segment"
    elif type_flags & 64:
        type_str = "vm_region"
    else:
        type_str = hex(type_flags)
    return type_str


def find_variable_containing_address(verbose, frame, match_addr):
    variables = frame.GetVariables(True, True, True, True)
    matching_var = None
    for var in variables:
        var_addr = var.GetLoadAddress()
        if var_addr != lldb.LLDB_INVALID_ADDRESS:
            byte_size = var.GetType().GetByteSize()
            if verbose:
                print(
                    "frame #%u: [%#x - %#x) %s"
                    % (
                        frame.GetFrameID(),
                        var.load_addr,
                        var.load_addr + byte_size,
                        var.name,
                    )
                )
            if var_addr == match_addr:
                if verbose:
                    print("match")
                return var
            else:
                if (
                    byte_size > 0
                    and var_addr <= match_addr
                    and match_addr < (var_addr + byte_size)
                ):
                    if verbose:
                        print("match")
                    return var
    return None


def find_frame_for_stack_address(process, addr):
    closest_delta = sys.maxsize
    closest_frame = None
    # print 'find_frame_for_stack_address(%#x)' % (addr)
    for thread in process:
        prev_sp = lldb.LLDB_INVALID_ADDRESS
        for frame in thread:
            cfa = frame.GetCFA()
            # print 'frame #%u: cfa = %#x' % (frame.GetFrameID(), cfa)
            if addr < cfa:
                delta = cfa - addr
                # print '%#x < %#x, delta = %i' % (addr, cfa, delta)
                if delta < closest_delta:
                    # print 'closest'
                    closest_delta = delta
                    closest_frame = frame
                # else:
                #     print 'delta >= closest_delta'
    return closest_frame


def type_flags_to_description(
    process, type_flags, ptr_addr, ptr_size, offset, match_addr
):
    show_offset = False
    if type_flags == 0 or type_flags & 4:
        type_str = "free(%#x)" % (ptr_addr,)
    elif type_flags & 2 or type_flags & 1:
        type_str = "malloc(%6u) -> %#x" % (ptr_size, ptr_addr)
        show_offset = True
    elif type_flags & 8:
        type_str = "stack"
        frame = find_frame_for_stack_address(process, match_addr)
        if frame:
            type_str += " in frame #%u of thread #%u: tid %#x" % (
                frame.GetFrameID(),
                frame.GetThread().GetIndexID(),
                frame.GetThread().GetThreadID(),
            )
        variables = frame.GetVariables(True, True, True, True)
        matching_var = None
        for var in variables:
            var_addr = var.GetLoadAddress()
            if var_addr != lldb.LLDB_INVALID_ADDRESS:
                # print 'variable "%s" @ %#x (%#x)' % (var.name, var.load_addr,
                # match_addr)
                if var_addr == match_addr:
                    matching_var = var
                    break
                else:
                    byte_size = var.GetType().GetByteSize()
                    if (
                        byte_size > 0
                        and var_addr <= match_addr
                        and match_addr < (var_addr + byte_size)
                    ):
                        matching_var = var
                        break
        if matching_var:
            type_str += " in variable at %#x:\n    %s" % (
                matching_var.GetLoadAddress(),
                matching_var,
            )
    elif type_flags & 16:
        type_str = "stack (red zone)"
    elif type_flags & 32:
        sb_addr = process.GetTarget().ResolveLoadAddress(ptr_addr + offset)
        type_str = "segment [%#x - %#x), %s + %u, %s" % (
            ptr_addr,
            ptr_addr + ptr_size,
            sb_addr.section.name,
            sb_addr.offset,
            sb_addr,
        )
    elif type_flags & 64:
        sb_addr = process.GetTarget().ResolveLoadAddress(ptr_addr + offset)
        type_str = "vm_region [%#x - %#x), %s + %u, %s" % (
            ptr_addr,
            ptr_addr + ptr_size,
            sb_addr.section.name,
            sb_addr.offset,
            sb_addr,
        )
    else:
        type_str = "%#x" % (ptr_addr,)
        show_offset = True
    if show_offset and offset != 0:
        type_str += " + %-6u" % (offset,)
    return type_str


def dump_stack_history_entry(options, result, stack_history_entry, idx):
    address = int(stack_history_entry.address)
    if address:
        type_flags = int(stack_history_entry.type_flags)
        symbolicator = lldb.utils.symbolication.Symbolicator()
        symbolicator.target = lldb.debugger.GetSelectedTarget()
        type_str = type_flags_to_string(type_flags)
        result.AppendMessage(
            "stack[%u]: addr = 0x%x, type=%s, frames:" % (idx, address, type_str)
        )
        frame_idx = 0
        idx = 0
        pc = int(stack_history_entry.frames[idx])
        while pc != 0:
            if pc >= 0x1000:
                frames = symbolicator.symbolicate(pc)
                if frames:
                    for frame in frames:
                        result.AppendMessage("     [%u] %s" % (frame_idx, frame))
                        frame_idx += 1
                else:
                    result.AppendMessage("     [%u] 0x%x" % (frame_idx, pc))
                    frame_idx += 1
                idx = idx + 1
                pc = int(stack_history_entry.frames[idx])
            else:
                pc = 0
        if idx >= options.max_frames:
            result.AppendMessage(
                'warning: the max number of stack frames (%u) was reached, use the "--max-frames=<COUNT>" option to see more frames'
                % (options.max_frames)
            )

        result.AppendMessage("")


def dump_stack_history_entries(options, result, addr, history):
    # malloc_stack_entry *get_stack_history_for_address (const void * addr)
    expr_prefix = """
typedef int kern_return_t;
typedef struct $malloc_stack_entry {
    uint64_t address;
    uint64_t argument;
    uint32_t type_flags;
    uint32_t num_frames;
    uint64_t frames[512];
    kern_return_t err;
} $malloc_stack_entry;
"""
    single_expr = """
#define MAX_FRAMES %u
typedef unsigned task_t;
$malloc_stack_entry stack;
stack.address = 0x%x;
stack.type_flags = 2;
stack.num_frames = 0;
stack.frames[0] = 0;
uint32_t max_stack_frames = MAX_FRAMES;
stack.err = (kern_return_t)__mach_stack_logging_get_frames (
    (task_t)mach_task_self(),
    stack.address,
    &stack.frames[0],
    max_stack_frames,
    &stack.num_frames);
if (stack.num_frames < MAX_FRAMES)
    stack.frames[stack.num_frames] = 0;
else
    stack.frames[MAX_FRAMES-1] = 0;
stack""" % (
        options.max_frames,
        addr,
    )

    history_expr = """
typedef int kern_return_t;
typedef unsigned task_t;
#define MAX_FRAMES %u
#define MAX_HISTORY %u
typedef struct mach_stack_logging_record_t {
	uint32_t type_flags;
	uint64_t stack_identifier;
	uint64_t argument;
	uint64_t address;
} mach_stack_logging_record_t;
typedef void (*enumerate_callback_t)(mach_stack_logging_record_t, void *);
typedef struct malloc_stack_entry {
    uint64_t address;
    uint64_t argument;
    uint32_t type_flags;
    uint32_t num_frames;
    uint64_t frames[MAX_FRAMES];
    kern_return_t frames_err;
} malloc_stack_entry;
typedef struct $malloc_stack_history {
    task_t task;
    unsigned idx;
    malloc_stack_entry entries[MAX_HISTORY];
} $malloc_stack_history;
$malloc_stack_history lldb_info = { (task_t)mach_task_self(), 0 };
uint32_t max_stack_frames = MAX_FRAMES;
enumerate_callback_t callback = [] (mach_stack_logging_record_t stack_record, void *baton) -> void {
    $malloc_stack_history *lldb_info = ($malloc_stack_history *)baton;
    if (lldb_info->idx < MAX_HISTORY) {
        malloc_stack_entry *stack_entry = &(lldb_info->entries[lldb_info->idx]);
        stack_entry->address = stack_record.address;
        stack_entry->type_flags = stack_record.type_flags;
        stack_entry->argument = stack_record.argument;
        stack_entry->num_frames = 0;
        stack_entry->frames[0] = 0;
        stack_entry->frames_err = (kern_return_t)__mach_stack_logging_frames_for_uniqued_stack (
            lldb_info->task,
            stack_record.stack_identifier,
            stack_entry->frames,
            (uint32_t)MAX_FRAMES,
            &stack_entry->num_frames);
        // Terminate the frames with zero if there is room
        if (stack_entry->num_frames < MAX_FRAMES)
            stack_entry->frames[stack_entry->num_frames] = 0;
    }
    ++lldb_info->idx;
};
(kern_return_t)__mach_stack_logging_enumerate_records (lldb_info.task, (uint64_t)0x%x, callback, &lldb_info);
lldb_info""" % (
        options.max_frames,
        options.max_history,
        addr,
    )

    frame = (
        lldb.debugger.GetSelectedTarget()
        .GetProcess()
        .GetSelectedThread()
        .GetSelectedFrame()
    )
    if history:
        expr = history_expr
    else:
        expr = single_expr
    expr_options = lldb.SBExpressionOptions()
    expr_options.SetIgnoreBreakpoints(True)
    expr_options.SetTimeoutInMicroSeconds(5 * 1000 * 1000)  # 5 second timeout
    expr_options.SetTryAllThreads(True)
    expr_options.SetLanguage(lldb.eLanguageTypeObjC_plus_plus)
    expr_options.SetPrefix(expr_prefix)
    expr_sbvalue = frame.EvaluateExpression(expr, expr_options)
    if options.verbose:
        print("expression:")
        print(expr)
        print("expression result:")
        print(expr_sbvalue)
    if expr_sbvalue.error.Success():
        if history:
            malloc_stack_history = lldb.value(expr_sbvalue)
            num_stacks = int(malloc_stack_history.idx)
            if num_stacks <= options.max_history:
                i_max = num_stacks
            else:
                i_max = options.max_history
            for i in range(i_max):
                stack_history_entry = malloc_stack_history.entries[i]
                dump_stack_history_entry(options, result, stack_history_entry, i)
            if num_stacks > options.max_history:
                result.AppendMessage(
                    'warning: the max number of stacks (%u) was reached, use the "--max-history=%u" option to see all of the stacks'
                    % (options.max_history, num_stacks)
                )
        else:
            stack_history_entry = lldb.value(expr_sbvalue)
            dump_stack_history_entry(options, result, stack_history_entry, 0)

    else:
        result.AppendMessage(
            'error: expression failed "%s" => %s' % (expr, expr_sbvalue.error)
        )


def display_match_results(
    process,
    result,
    options,
    arg_str_description,
    expr,
    print_no_matches,
    expr_prefix=None,
):
    frame = (
        lldb.debugger.GetSelectedTarget()
        .GetProcess()
        .GetSelectedThread()
        .GetSelectedFrame()
    )
    if not frame:
        result.AppendMessage("error: invalid frame")
        return 0
    expr_options = lldb.SBExpressionOptions()
    expr_options.SetIgnoreBreakpoints(True)
    expr_options.SetFetchDynamicValue(lldb.eNoDynamicValues)
    expr_options.SetTimeoutInMicroSeconds(30 * 1000 * 1000)  # 30 second timeout
    expr_options.SetTryAllThreads(False)
    expr_options.SetLanguage(lldb.eLanguageTypeObjC_plus_plus)
    if expr_prefix:
        expr_options.SetPrefix(expr_prefix)
    expr_sbvalue = frame.EvaluateExpression(expr, expr_options)
    if options.verbose:
        print("expression:")
        print(expr)
        print("expression result:")
        print(expr_sbvalue)
    if expr_sbvalue.error.Success():
        match_value = lldb.value(expr_sbvalue)
        i = 0
        match_idx = 0
        while True:
            print_entry = True
            match_entry = match_value[i]
            i += 1
            if i > options.max_matches:
                result.AppendMessage(
                    "warning: the max number of matches (%u) was reached, use the --max-matches option to get more results"
                    % (options.max_matches)
                )
                break
            malloc_addr = match_entry.addr.sbvalue.unsigned
            if malloc_addr == 0:
                break
            malloc_size = int(match_entry.size)
            offset = int(match_entry.offset)

            if options.offset >= 0 and options.offset != offset:
                print_entry = False
            else:
                match_addr = malloc_addr + offset
                type_flags = int(match_entry.type)
                # result.AppendMessage (hex(malloc_addr + offset))
                if type_flags == 64:
                    search_stack_old = options.search_stack
                    search_segments_old = options.search_segments
                    search_heap_old = options.search_heap
                    search_vm_regions = options.search_vm_regions
                    options.search_stack = True
                    options.search_segments = True
                    options.search_heap = True
                    options.search_vm_regions = False
                    if malloc_info_impl(
                        lldb.debugger, result, options, [hex(malloc_addr + offset)]
                    ):
                        print_entry = False
                    options.search_stack = search_stack_old
                    options.search_segments = search_segments_old
                    options.search_heap = search_heap_old
                    options.search_vm_regions = search_vm_regions
                if print_entry:
                    description = "%#16.16x: %s" % (
                        match_addr,
                        type_flags_to_description(
                            process,
                            type_flags,
                            malloc_addr,
                            malloc_size,
                            offset,
                            match_addr,
                        ),
                    )
                    if options.show_size:
                        description += " <%5u>" % (malloc_size)
                    if options.show_range:
                        description += " [%#x - %#x)" % (
                            malloc_addr,
                            malloc_addr + malloc_size,
                        )
                    derefed_dynamic_value = None
                    dynamic_value = match_entry.addr.sbvalue.GetDynamicValue(
                        lldb.eDynamicCanRunTarget
                    )
                    if dynamic_value.type.name == "void *":
                        if options.type == "pointer" and malloc_size == 4096:
                            error = lldb.SBError()
                            process = expr_sbvalue.GetProcess()
                            target = expr_sbvalue.GetTarget()
                            data = bytearray(process.ReadMemory(malloc_addr, 16, error))
                            if data == "\xa1\xa1\xa1\xa1AUTORELEASE!":
                                ptr_size = target.addr_size
                                thread = process.ReadUnsignedFromMemory(
                                    malloc_addr + 16 + ptr_size, ptr_size, error
                                )
                                #   4 bytes  0xa1a1a1a1
                                #  12 bytes  'AUTORELEASE!'
                                # ptr bytes  autorelease insertion point
                                # ptr bytes  pthread_t
                                # ptr bytes  next colder page
                                # ptr bytes  next hotter page
                                #   4 bytes  this page's depth in the list
                                #   4 bytes  high-water mark
                                description += " AUTORELEASE! for pthread_t %#x" % (
                                    thread
                                )
                        #     else:
                        #         description += 'malloc(%u)' % (malloc_size)
                        # else:
                        #     description += 'malloc(%u)' % (malloc_size)
                    else:
                        derefed_dynamic_value = dynamic_value.deref
                        if derefed_dynamic_value:
                            derefed_dynamic_type = derefed_dynamic_value.type
                            derefed_dynamic_type_size = derefed_dynamic_type.size
                            derefed_dynamic_type_name = derefed_dynamic_type.name
                            description += " "
                            description += derefed_dynamic_type_name
                            if offset < derefed_dynamic_type_size:
                                member_list = list()
                                get_member_types_for_offset(
                                    derefed_dynamic_type, offset, member_list
                                )
                                if member_list:
                                    member_path = ""
                                    for member in member_list:
                                        member_name = member.name
                                        if member_name:
                                            if member_path:
                                                member_path += "."
                                            member_path += member_name
                                    if member_path:
                                        if options.ivar_regex_exclusions:
                                            for (
                                                ivar_regex
                                            ) in options.ivar_regex_exclusions:
                                                if ivar_regex.match(member_path):
                                                    print_entry = False
                                        description += ".%s" % (member_path)
                            else:
                                description += "%u bytes after %s" % (
                                    offset - derefed_dynamic_type_size,
                                    derefed_dynamic_type_name,
                                )
                        else:
                            # strip the "*" from the end of the name since we
                            # were unable to dereference this
                            description += dynamic_value.type.name[0:-1]
            if print_entry:
                match_idx += 1
                result_output = ""
                if description:
                    result_output += description
                    if options.print_type and derefed_dynamic_value:
                        result_output += " %s" % (derefed_dynamic_value)
                    if options.print_object_description and dynamic_value:
                        desc = dynamic_value.GetObjectDescription()
                        if desc:
                            result_output += "\n%s" % (desc)
                if result_output:
                    result.AppendMessage(result_output)
                if options.memory:
                    cmd_result = lldb.SBCommandReturnObject()
                    if options.format is None:
                        memory_command = "memory read --force 0x%x 0x%x" % (
                            malloc_addr,
                            malloc_addr + malloc_size,
                        )
                    else:
                        memory_command = "memory read --force -f %s 0x%x 0x%x" % (
                            options.format,
                            malloc_addr,
                            malloc_addr + malloc_size,
                        )
                    if options.verbose:
                        result.AppendMessage(memory_command)
                    lldb.debugger.GetCommandInterpreter().HandleCommand(
                        memory_command, cmd_result
                    )
                    result.AppendMessage(cmd_result.GetOutput())
                if options.stack_history:
                    dump_stack_history_entries(options, result, malloc_addr, 1)
                elif options.stack:
                    dump_stack_history_entries(options, result, malloc_addr, 0)
        return i
    else:
        result.AppendMessage(str(expr_sbvalue.error))
    return 0


def get_ptr_refs_options():
    usage = "usage: %prog [options] <EXPR> [EXPR ...]"
    description = """Searches all allocations on the heap for pointer values on
darwin user space programs. Any matches that were found will dump the malloc
blocks that contain the pointers and might be able to print what kind of
objects the pointers are contained in using dynamic type information in the
program."""
    parser = optparse.OptionParser(
        description=description, prog="ptr_refs", usage=usage
    )
    add_common_options(parser)
    return parser


def find_variable(debugger, command, result, dict):
    usage = "usage: %prog [options] <ADDR> [ADDR ...]"
    description = (
        """Searches for a local variable in all frames that contains a hex ADDR."""
    )
    command_args = shlex.split(command)
    parser = optparse.OptionParser(
        description=description, prog="find_variable", usage=usage
    )
    parser.add_option(
        "-v",
        "--verbose",
        action="store_true",
        dest="verbose",
        help="display verbose debug info",
        default=False,
    )
    try:
        (options, args) = parser.parse_args(command_args)
    except:
        return

    process = debugger.GetSelectedTarget().GetProcess()
    if not process:
        result.AppendMessage("error: invalid process")
        return

    for arg in args:
        var_addr = int(arg, 16)
        print("Finding a variable with address %#x..." % (var_addr), file=result)
        done = False
        for thread in process:
            for frame in thread:
                var = find_variable_containing_address(options.verbose, frame, var_addr)
                if var:
                    print(var)
                    done = True
                    break
            if done:
                break


def ptr_refs(debugger, command, result, dict):
    command_args = shlex.split(command)
    parser = get_ptr_refs_options()
    try:
        (options, args) = parser.parse_args(command_args)
    except:
        return

    process = debugger.GetSelectedTarget().GetProcess()
    if not process:
        result.AppendMessage("error: invalid process")
        return
    frame = process.GetSelectedThread().GetSelectedFrame()
    if not frame:
        result.AppendMessage("error: invalid frame")
        return

    options.type = "pointer"
    if options.format is None:
        options.format = "A"  # 'A' is "address" format

    if args:
        # When we initialize the expression, we must define any types that
        # we will need when looking at every allocation. We must also define
        # a type named callback_baton_t and make an instance named "baton"
        # and initialize it how ever we want to. The address of "baton" will
        # be passed into our range callback. callback_baton_t must contain
        # a member named "callback" whose type is "range_callback_t". This
        # will be used by our zone callbacks to call the range callback for
        # each malloc range.
        expr_prefix = """
struct $malloc_match {
    void *addr;
    uintptr_t size;
    uintptr_t offset;
    uintptr_t type;
};
"""
        user_init_code_format = """
#define MAX_MATCHES %u
typedef struct callback_baton_t {
    range_callback_t callback;
    unsigned num_matches;
    $malloc_match matches[MAX_MATCHES];
    void *ptr;
} callback_baton_t;
range_callback_t range_callback = [](task_t task, void *baton, unsigned type, uintptr_t ptr_addr, uintptr_t ptr_size) -> void {
    callback_baton_t *lldb_info = (callback_baton_t *)baton;
    typedef void* T;
    const unsigned size = sizeof(T);
    T *array = (T*)ptr_addr;
    for (unsigned idx = 0; ((idx + 1) * sizeof(T)) <= ptr_size; ++idx) {
        if (array[idx] == lldb_info->ptr) {
            if (lldb_info->num_matches < MAX_MATCHES) {
                lldb_info->matches[lldb_info->num_matches].addr = (void*)ptr_addr;
                lldb_info->matches[lldb_info->num_matches].size = ptr_size;
                lldb_info->matches[lldb_info->num_matches].offset = idx*sizeof(T);
                lldb_info->matches[lldb_info->num_matches].type = type;
                ++lldb_info->num_matches;
            }
        }
    }
};
callback_baton_t baton = { range_callback, 0, {0}, (void *)%s };
"""
        # We must also define a snippet of code to be run that returns
        # the result of the expression we run.
        # Here we return NULL if our pointer was not found in any malloc blocks,
        # and we return the address of the matches array so we can then access
        # the matching results
        user_return_code = """if (baton.num_matches < MAX_MATCHES)
    baton.matches[baton.num_matches].addr = 0; // Terminate the matches array
baton.matches"""
        # Iterate through all of our pointer expressions and display the
        # results
        for ptr_expr in args:
            user_init_code = user_init_code_format % (options.max_matches, ptr_expr)
            expr = get_iterate_memory_expr(
                options, process, user_init_code, user_return_code
            )
            arg_str_description = "malloc block containing pointer %s" % ptr_expr
            display_match_results(
                process, result, options, arg_str_description, expr, True, expr_prefix
            )
    else:
        result.AppendMessage("error: no pointer arguments were given")


def get_cstr_refs_options():
    usage = "usage: %prog [options] <CSTR> [CSTR ...]"
    description = """Searches all allocations on the heap for C string values on
darwin user space programs. Any matches that were found will dump the malloc
blocks that contain the C strings and might be able to print what kind of
objects the pointers are contained in using dynamic type information in the
program."""
    parser = optparse.OptionParser(
        description=description, prog="cstr_refs", usage=usage
    )
    add_common_options(parser)
    return parser


def cstr_refs(debugger, command, result, dict):
    command_args = shlex.split(command)
    parser = get_cstr_refs_options()
    try:
        (options, args) = parser.parse_args(command_args)
    except:
        return

    process = debugger.GetSelectedTarget().GetProcess()
    if not process:
        result.AppendMessage("error: invalid process")
        return
    frame = process.GetSelectedThread().GetSelectedFrame()
    if not frame:
        result.AppendMessage("error: invalid frame")
        return

    options.type = "cstr"
    if options.format is None:
        options.format = "Y"  # 'Y' is "bytes with ASCII" format

    if args:
        # When we initialize the expression, we must define any types that
        # we will need when looking at every allocation. We must also define
        # a type named callback_baton_t and make an instance named "baton"
        # and initialize it how ever we want to. The address of "baton" will
        # be passed into our range callback. callback_baton_t must contain
        # a member named "callback" whose type is "range_callback_t". This
        # will be used by our zone callbacks to call the range callback for
        # each malloc range.
        expr_prefix = """
struct $malloc_match {
    void *addr;
    uintptr_t size;
    uintptr_t offset;
    uintptr_t type;
};
"""
        user_init_code_format = """
#define MAX_MATCHES %u
typedef struct callback_baton_t {
    range_callback_t callback;
    unsigned num_matches;
    $malloc_match matches[MAX_MATCHES];
    const char *cstr;
    unsigned cstr_len;
} callback_baton_t;
range_callback_t range_callback = [](task_t task, void *baton, unsigned type, uintptr_t ptr_addr, uintptr_t ptr_size) -> void {
    callback_baton_t *lldb_info = (callback_baton_t *)baton;
    if (lldb_info->cstr_len < ptr_size) {
        const char *begin = (const char *)ptr_addr;
        const char *end = begin + ptr_size - lldb_info->cstr_len;
        for (const char *s = begin; s < end; ++s) {
            if ((int)memcmp(s, lldb_info->cstr, lldb_info->cstr_len) == 0) {
                if (lldb_info->num_matches < MAX_MATCHES) {
                    lldb_info->matches[lldb_info->num_matches].addr = (void*)ptr_addr;
                    lldb_info->matches[lldb_info->num_matches].size = ptr_size;
                    lldb_info->matches[lldb_info->num_matches].offset = s - begin;
                    lldb_info->matches[lldb_info->num_matches].type = type;
                    ++lldb_info->num_matches;
                }
            }
        }
    }
};
const char *cstr = "%s";
callback_baton_t baton = { range_callback, 0, {0}, cstr, (unsigned)strlen(cstr) };"""
        # We must also define a snippet of code to be run that returns
        # the result of the expression we run.
        # Here we return NULL if our pointer was not found in any malloc blocks,
        # and we return the address of the matches array so we can then access
        # the matching results
        user_return_code = """if (baton.num_matches < MAX_MATCHES)
    baton.matches[baton.num_matches].addr = 0; // Terminate the matches array
baton.matches"""
        # Iterate through all of our pointer expressions and display the
        # results
        for cstr in args:
            user_init_code = user_init_code_format % (options.max_matches, cstr)
            expr = get_iterate_memory_expr(
                options, process, user_init_code, user_return_code
            )
            arg_str_description = 'malloc block containing "%s"' % cstr
            display_match_results(
                process, result, options, arg_str_description, expr, True, expr_prefix
            )
    else:
        result.AppendMessage("error: command takes one or more C string arguments")


def get_malloc_info_options():
    usage = "usage: %prog [options] <EXPR> [EXPR ...]"
    description = """Searches the heap a malloc block that contains the addresses
specified as one or more address expressions. Any matches that were found will
dump the malloc blocks that match or contain the specified address. The matching
blocks might be able to show what kind of objects they are using dynamic type
information in the program."""
    parser = optparse.OptionParser(
        description=description, prog="malloc_info", usage=usage
    )
    add_common_options(parser)
    return parser


def malloc_info(debugger, command, result, dict):
    command_args = shlex.split(command)
    parser = get_malloc_info_options()
    try:
        (options, args) = parser.parse_args(command_args)
    except:
        return
    malloc_info_impl(debugger, result, options, args)


def malloc_info_impl(debugger, result, options, args):
    # We are specifically looking for something on the heap only
    options.type = "malloc_info"

    process = debugger.GetSelectedTarget().GetProcess()
    if not process:
        result.AppendMessage("error: invalid process")
        return
    frame = process.GetSelectedThread().GetSelectedFrame()
    if not frame:
        result.AppendMessage("error: invalid frame")
        return
    expr_prefix = """
struct $malloc_match {
    void *addr;
    uintptr_t size;
    uintptr_t offset;
    uintptr_t type;
};
"""

    user_init_code_format = """
typedef struct callback_baton_t {
    range_callback_t callback;
    unsigned num_matches;
    $malloc_match matches[2]; // Two items so they can be NULL terminated
    void *ptr;
} callback_baton_t;
range_callback_t range_callback = [](task_t task, void *baton, unsigned type, uintptr_t ptr_addr, uintptr_t ptr_size) -> void {
    callback_baton_t *lldb_info = (callback_baton_t *)baton;
    if (lldb_info->num_matches == 0) {
        uint8_t *p = (uint8_t *)lldb_info->ptr;
        uint8_t *lo = (uint8_t *)ptr_addr;
        uint8_t *hi = lo + ptr_size;
        if (lo <= p && p < hi) {
            lldb_info->matches[lldb_info->num_matches].addr = (void*)ptr_addr;
            lldb_info->matches[lldb_info->num_matches].size = ptr_size;
            lldb_info->matches[lldb_info->num_matches].offset = p - lo;
            lldb_info->matches[lldb_info->num_matches].type = type;
            lldb_info->num_matches = 1;
        }
    }
};
callback_baton_t baton = { range_callback, 0, {0}, (void *)%s };
baton.matches[0].addr = 0;
baton.matches[1].addr = 0;"""
    if args:
        total_matches = 0
        for ptr_expr in args:
            user_init_code = user_init_code_format % (ptr_expr)
            expr = get_iterate_memory_expr(
                options, process, user_init_code, "baton.matches"
            )
            arg_str_description = "malloc block that contains %s" % ptr_expr
            total_matches += display_match_results(
                process, result, options, arg_str_description, expr, True, expr_prefix
            )
        return total_matches
    else:
        result.AppendMessage("error: command takes one or more pointer expressions")
        return 0


def get_thread_stack_ranges_struct(process):
    """Create code that defines a structure that represents threads stack bounds
    for all  threads. It returns a static sized array initialized with all of
    the tid, base, size structs for all the threads."""
    stack_dicts = list()
    if process:
        i = 0
        for thread in process:
            min_sp = thread.frame[0].sp
            max_sp = min_sp
            for frame in thread.frames:
                sp = frame.sp
                if sp < min_sp:
                    min_sp = sp
                if sp > max_sp:
                    max_sp = sp
            if min_sp < max_sp:
                stack_dicts.append(
                    {
                        "tid": thread.GetThreadID(),
                        "base": min_sp,
                        "size": max_sp - min_sp,
                        "index": i,
                    }
                )
                i += 1
    stack_dicts_len = len(stack_dicts)
    if stack_dicts_len > 0:
        result = """
#define NUM_STACKS %u
#define STACK_RED_ZONE_SIZE %u
typedef struct thread_stack_t { uint64_t tid, base, size; } thread_stack_t;
thread_stack_t stacks[NUM_STACKS];""" % (
            stack_dicts_len,
            process.target.GetStackRedZoneSize(),
        )
        for stack_dict in stack_dicts:
            result += (
                """
stacks[%(index)u].tid  = 0x%(tid)x;
stacks[%(index)u].base = 0x%(base)x;
stacks[%(index)u].size = 0x%(size)x;"""
                % stack_dict
            )
        return result
    else:
        return ""


def get_sections_ranges_struct(process):
    """Create code that defines a structure that represents all segments that
    can contain data for all images in "target". It returns a static sized
    array initialized with all of base, size structs for all the threads."""
    target = process.target
    segment_dicts = list()
    for module_idx, module in enumerate(target.modules):
        for sect_idx in range(module.GetNumSections()):
            section = module.GetSectionAtIndex(sect_idx)
            if not section:
                break
            name = section.name
            if name != "__TEXT" and name != "__LINKEDIT" and name != "__PAGEZERO":
                base = section.GetLoadAddress(target)
                size = section.GetByteSize()
                if base != lldb.LLDB_INVALID_ADDRESS and size > 0:
                    segment_dicts.append({"base": base, "size": size})
    segment_dicts_len = len(segment_dicts)
    if segment_dicts_len > 0:
        result = """
#define NUM_SEGMENTS %u
typedef struct segment_range_t { uint64_t base; uint32_t size; } segment_range_t;
segment_range_t segments[NUM_SEGMENTS];""" % (
            segment_dicts_len,
        )
        for idx, segment_dict in enumerate(segment_dicts):
            segment_dict["index"] = idx
            result += (
                """
segments[%(index)u].base = 0x%(base)x;
segments[%(index)u].size = 0x%(size)x;"""
                % segment_dict
            )
        return result
    else:
        return ""


def section_ptr_refs(debugger, command, result, dict):
    command_args = shlex.split(command)
    usage = "usage: %prog [options] <EXPR> [EXPR ...]"
    description = """Searches section contents for pointer values in darwin user space programs."""
    parser = optparse.OptionParser(
        description=description, prog="section_ptr_refs", usage=usage
    )
    add_common_options(parser)
    parser.add_option(
        "--section",
        action="append",
        type="string",
        dest="section_names",
        help="section name to search",
        default=list(),
    )
    try:
        (options, args) = parser.parse_args(command_args)
    except:
        return

    options.type = "pointer"

    sections = list()
    section_modules = list()
    if not options.section_names:
        result.AppendMessage(
            "error: at least one section must be specified with the --section option"
        )
        return

    target = debugger.GetSelectedTarget()
    for module in target.modules:
        for section_name in options.section_names:
            section = module.section[section_name]
            if section:
                sections.append(section)
                section_modules.append(module)
    if sections:
        dylid_load_err = load_dylib()
        if dylid_load_err:
            result.AppendMessage(dylid_load_err)
            return
        frame = target.GetProcess().GetSelectedThread().GetSelectedFrame()
        for expr_str in args:
            for idx, section in enumerate(sections):
                expr = "find_pointer_in_memory(0x%xllu, %ullu, (void *)%s)" % (
                    section.addr.load_addr,
                    section.size,
                    expr_str,
                )
                arg_str_description = 'section %s.%s containing "%s"' % (
                    section_modules[idx].file.fullpath,
                    section.name,
                    expr_str,
                )
                num_matches = display_match_results(
                    target.GetProcess(),
                    result,
                    options,
                    arg_str_description,
                    expr,
                    False,
                )
                if num_matches:
                    if num_matches < options.max_matches:
                        options.max_matches = options.max_matches - num_matches
                    else:
                        options.max_matches = 0
                if options.max_matches == 0:
                    return
    else:
        result.AppendMessage(
            "error: no sections were found that match any of %s"
            % (", ".join(options.section_names))
        )


def get_objc_refs_options():
    usage = "usage: %prog [options] <CLASS> [CLASS ...]"
    description = """Searches all allocations on the heap for instances of
objective C classes, or any classes that inherit from the specified classes
in darwin user space programs. Any matches that were found will dump the malloc
blocks that contain the C strings and might be able to print what kind of
objects the pointers are contained in using dynamic type information in the
program."""
    parser = optparse.OptionParser(
        description=description, prog="objc_refs", usage=usage
    )
    add_common_options(parser)
    return parser


def objc_refs(debugger, command, result, dict):
    command_args = shlex.split(command)
    parser = get_objc_refs_options()
    try:
        (options, args) = parser.parse_args(command_args)
    except:
        return

    process = debugger.GetSelectedTarget().GetProcess()
    if not process:
        result.AppendMessage("error: invalid process")
        return
    frame = process.GetSelectedThread().GetSelectedFrame()
    if not frame:
        result.AppendMessage("error: invalid frame")
        return

    options.type = "isa"
    if options.format is None:
        options.format = "A"  # 'A' is "address" format

    expr_options = lldb.SBExpressionOptions()
    expr_options.SetIgnoreBreakpoints(True)
    expr_options.SetTimeoutInMicroSeconds(3 * 1000 * 1000)  # 3 second infinite timeout
    expr_options.SetTryAllThreads(True)
    expr_options.SetLanguage(lldb.eLanguageTypeObjC_plus_plus)
    num_objc_classes_value = frame.EvaluateExpression(
        "(int)objc_getClassList((void *)0, (int)0)", expr_options
    )
    if not num_objc_classes_value.error.Success():
        result.AppendMessage("error: %s" % num_objc_classes_value.error.GetCString())
        return

    num_objc_classes = num_objc_classes_value.GetValueAsUnsigned()
    if num_objc_classes == 0:
        result.AppendMessage("error: no objective C classes in program")
        return

    if args:
        # When we initialize the expression, we must define any types that
        # we will need when looking at every allocation. We must also define
        # a type named callback_baton_t and make an instance named "baton"
        # and initialize it how ever we want to. The address of "baton" will
        # be passed into our range callback. callback_baton_t must contain
        # a member named "callback" whose type is "range_callback_t". This
        # will be used by our zone callbacks to call the range callback for
        # each malloc range.
        expr_prefix = """
struct $malloc_match {
    void *addr;
    uintptr_t size;
    uintptr_t offset;
    uintptr_t type;
};
"""

        user_init_code_format = """
#define MAX_MATCHES %u
typedef int (*compare_callback_t)(const void *a, const void *b);
typedef struct callback_baton_t {
    range_callback_t callback;
    compare_callback_t compare_callback;
    unsigned num_matches;
    $malloc_match matches[MAX_MATCHES];
    void *isa;
    Class classes[%u];
} callback_baton_t;
compare_callback_t compare_callback = [](const void *a, const void *b) -> int {
     Class a_ptr = *(Class *)a;
     Class b_ptr = *(Class *)b;
     if (a_ptr < b_ptr) return -1;
     if (a_ptr > b_ptr) return +1;
     return 0;
};
typedef Class (*class_getSuperclass_type)(void *isa);
range_callback_t range_callback = [](task_t task, void *baton, unsigned type, uintptr_t ptr_addr, uintptr_t ptr_size) -> void {
    class_getSuperclass_type class_getSuperclass_impl = (class_getSuperclass_type)class_getSuperclass;
    callback_baton_t *lldb_info = (callback_baton_t *)baton;
    if (sizeof(Class) <= ptr_size) {
        Class *curr_class_ptr = (Class *)ptr_addr;
        Class *matching_class_ptr = (Class *)bsearch (curr_class_ptr,
                                                      (const void *)lldb_info->classes,
                                                      sizeof(lldb_info->classes)/sizeof(Class),
                                                      sizeof(Class),
                                                      lldb_info->compare_callback);
        if (matching_class_ptr) {
            bool match = false;
            if (lldb_info->isa) {
                Class isa = *curr_class_ptr;
                if (lldb_info->isa == isa)
                    match = true;
                else { // if (lldb_info->objc.match_superclasses) {
                    Class super = class_getSuperclass_impl(isa);
                    while (super) {
                        if (super == lldb_info->isa) {
                            match = true;
                            break;
                        }
                        super = class_getSuperclass_impl(super);
                    }
                }
            }
            else
                match = true;
            if (match) {
                if (lldb_info->num_matches < MAX_MATCHES) {
                    lldb_info->matches[lldb_info->num_matches].addr = (void*)ptr_addr;
                    lldb_info->matches[lldb_info->num_matches].size = ptr_size;
                    lldb_info->matches[lldb_info->num_matches].offset = 0;
                    lldb_info->matches[lldb_info->num_matches].type = type;
                    ++lldb_info->num_matches;
                }
            }
        }
    }
};
callback_baton_t baton = { range_callback, compare_callback, 0, {0}, (void *)0x%x, {0} };
int nc = (int)objc_getClassList(baton.classes, sizeof(baton.classes)/sizeof(Class));
(void)qsort (baton.classes, sizeof(baton.classes)/sizeof(Class), sizeof(Class), compare_callback);"""
        # We must also define a snippet of code to be run that returns
        # the result of the expression we run.
        # Here we return NULL if our pointer was not found in any malloc blocks,
        # and we return the address of the matches array so we can then access
        # the matching results
        user_return_code = """if (baton.num_matches < MAX_MATCHES)
    baton.matches[baton.num_matches].addr = 0; // Terminate the matches array
        baton.matches"""
        # Iterate through all of our ObjC class name arguments
        for class_name in args:
            addr_expr_str = "(void *)[%s class]" % class_name
            expr_options = lldb.SBExpressionOptions()
            expr_options.SetIgnoreBreakpoints(True)
            expr_options.SetTimeoutInMicroSeconds(1 * 1000 * 1000)  # 1 second timeout
            expr_options.SetTryAllThreads(True)
            expr_options.SetLanguage(lldb.eLanguageTypeObjC_plus_plus)
            expr_sbvalue = frame.EvaluateExpression(addr_expr_str, expr_options)
            if expr_sbvalue.error.Success():
                isa = expr_sbvalue.unsigned
                if isa:
                    options.type = "isa"
                    result.AppendMessage(
                        'Searching for all instances of classes or subclasses of "%s" (isa=0x%x)'
                        % (class_name, isa)
                    )
                    user_init_code = user_init_code_format % (
                        options.max_matches,
                        num_objc_classes,
                        isa,
                    )
                    expr = get_iterate_memory_expr(
                        options, process, user_init_code, user_return_code
                    )
                    arg_str_description = "objective C classes with isa 0x%x" % isa
                    display_match_results(
                        process,
                        result,
                        options,
                        arg_str_description,
                        expr,
                        True,
                        expr_prefix,
                    )
                else:
                    result.AppendMessage(
                        'error: Can\'t find isa for an ObjC class named "%s"'
                        % (class_name)
                    )
            else:
                result.AppendMessage(
                    'error: expression error for "%s": %s'
                    % (addr_expr_str, expr_sbvalue.error)
                )
    else:
        result.AppendMessage("error: command takes one or more C string arguments")


if __name__ == "__main__":
    lldb.debugger = lldb.SBDebugger.Create()


def __lldb_init_module(debugger, internal_dict):
    # Make the options so we can generate the help text for the new LLDB
    # command line command prior to registering it with LLDB below. This way
    # if clients in LLDB type "help malloc_info", they will see the exact same
    # output as typing "malloc_info --help".
    ptr_refs.__doc__ = get_ptr_refs_options().format_help()
    cstr_refs.__doc__ = get_cstr_refs_options().format_help()
    malloc_info.__doc__ = get_malloc_info_options().format_help()
    objc_refs.__doc__ = get_objc_refs_options().format_help()
    debugger.HandleCommand("command script add -o -f %s.ptr_refs ptr_refs" % __name__)
    debugger.HandleCommand("command script add -o -f %s.cstr_refs cstr_refs" % __name__)
    debugger.HandleCommand(
        "command script add -o -f %s.malloc_info malloc_info" % __name__
    )
    debugger.HandleCommand(
        "command script add -o -f %s.find_variable find_variable" % __name__
    )
    # debugger.HandleCommand('command script add -o -f %s.section_ptr_refs section_ptr_refs' % package_name)
    debugger.HandleCommand("command script add -o -f %s.objc_refs objc_refs" % __name__)
    print(
        '"malloc_info", "ptr_refs", "cstr_refs", "find_variable", and "objc_refs" commands have been installed, use the "--help" options on these commands for detailed help.'
    )
