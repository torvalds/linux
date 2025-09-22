#!/usr/bin/env python

import lldb
import shlex
import sys

try:
    from tkinter import *
    import tkinter.ttk as ttk
except ImportError:
    from Tkinter import *
    import ttk


class ValueTreeItemDelegate(object):
    def __init__(self, value):
        self.value = value

    def get_item_dictionary(self):
        name = self.value.name
        if name is None:
            name = ""
        typename = self.value.type
        if typename is None:
            typename = ""
        value = self.value.value
        if value is None:
            value = ""
        summary = self.value.summary
        if summary is None:
            summary = ""
        has_children = self.value.MightHaveChildren()
        return {
            "#0": name,
            "typename": typename,
            "value": value,
            "summary": summary,
            "children": has_children,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        for i in range(self.value.num_children):
            item_delegate = ValueTreeItemDelegate(self.value.GetChildAtIndex(i))
            item_dicts.append(item_delegate.get_item_dictionary())
        return item_dicts


class FrameTreeItemDelegate(object):
    def __init__(self, frame):
        self.frame = frame

    def get_item_dictionary(self):
        id = self.frame.GetFrameID()
        name = "frame #%u" % (id)
        value = "0x%16.16x" % (self.frame.GetPC())
        stream = lldb.SBStream()
        self.frame.GetDescription(stream)
        summary = stream.GetData().split("`")[1]
        return {
            "#0": name,
            "value": value,
            "summary": summary,
            "children": self.frame.GetVariables(True, True, True, True).GetSize() > 0,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        variables = self.frame.GetVariables(True, True, True, True)
        n = variables.GetSize()
        for i in range(n):
            item_delegate = ValueTreeItemDelegate(variables[i])
            item_dicts.append(item_delegate.get_item_dictionary())
        return item_dicts


class ThreadTreeItemDelegate(object):
    def __init__(self, thread):
        self.thread = thread

    def get_item_dictionary(self):
        num_frames = self.thread.GetNumFrames()
        name = "thread #%u" % (self.thread.GetIndexID())
        value = "0x%x" % (self.thread.GetThreadID())
        summary = "%u frames" % (num_frames)
        return {
            "#0": name,
            "value": value,
            "summary": summary,
            "children": num_frames > 0,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        for frame in self.thread:
            item_delegate = FrameTreeItemDelegate(frame)
            item_dicts.append(item_delegate.get_item_dictionary())
        return item_dicts


class ProcessTreeItemDelegate(object):
    def __init__(self, process):
        self.process = process

    def get_item_dictionary(self):
        id = self.process.GetProcessID()
        num_threads = self.process.GetNumThreads()
        value = str(self.process.GetProcessID())
        summary = self.process.target.executable.fullpath
        return {
            "#0": "process",
            "value": value,
            "summary": summary,
            "children": num_threads > 0,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        for thread in self.process:
            item_delegate = ThreadTreeItemDelegate(thread)
            item_dicts.append(item_delegate.get_item_dictionary())
        return item_dicts


class TargetTreeItemDelegate(object):
    def __init__(self, target):
        self.target = target

    def get_item_dictionary(self):
        value = str(self.target.triple)
        summary = self.target.executable.fullpath
        return {
            "#0": "target",
            "value": value,
            "summary": summary,
            "children": True,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        image_item_delegate = TargetImagesTreeItemDelegate(self.target)
        item_dicts.append(image_item_delegate.get_item_dictionary())
        return item_dicts


class TargetImagesTreeItemDelegate(object):
    def __init__(self, target):
        self.target = target

    def get_item_dictionary(self):
        value = str(self.target.triple)
        summary = self.target.executable.fullpath
        num_modules = self.target.GetNumModules()
        return {
            "#0": "images",
            "value": "",
            "summary": "%u images" % num_modules,
            "children": num_modules > 0,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        for i in range(self.target.GetNumModules()):
            module = self.target.GetModuleAtIndex(i)
            image_item_delegate = ModuleTreeItemDelegate(self.target, module, i)
            item_dicts.append(image_item_delegate.get_item_dictionary())
        return item_dicts


class ModuleTreeItemDelegate(object):
    def __init__(self, target, module, index):
        self.target = target
        self.module = module
        self.index = index

    def get_item_dictionary(self):
        name = "module %u" % (self.index)
        value = self.module.file.basename
        summary = self.module.file.dirname
        return {
            "#0": name,
            "value": value,
            "summary": summary,
            "children": True,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        sections_item_delegate = ModuleSectionsTreeItemDelegate(
            self.target, self.module
        )
        item_dicts.append(sections_item_delegate.get_item_dictionary())

        symbols_item_delegate = ModuleSymbolsTreeItemDelegate(self.target, self.module)
        item_dicts.append(symbols_item_delegate.get_item_dictionary())

        comp_units_item_delegate = ModuleCompileUnitsTreeItemDelegate(
            self.target, self.module
        )
        item_dicts.append(comp_units_item_delegate.get_item_dictionary())
        return item_dicts


class ModuleSectionsTreeItemDelegate(object):
    def __init__(self, target, module):
        self.target = target
        self.module = module

    def get_item_dictionary(self):
        name = "sections"
        value = ""
        summary = "%u sections" % (self.module.GetNumSections())
        return {
            "#0": name,
            "value": value,
            "summary": summary,
            "children": True,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        num_sections = self.module.GetNumSections()
        for i in range(num_sections):
            section = self.module.GetSectionAtIndex(i)
            image_item_delegate = SectionTreeItemDelegate(self.target, section)
            item_dicts.append(image_item_delegate.get_item_dictionary())
        return item_dicts


class SectionTreeItemDelegate(object):
    def __init__(self, target, section):
        self.target = target
        self.section = section

    def get_item_dictionary(self):
        name = self.section.name
        section_load_addr = self.section.GetLoadAddress(self.target)
        if section_load_addr != lldb.LLDB_INVALID_ADDRESS:
            value = "0x%16.16x" % (section_load_addr)
        else:
            value = "0x%16.16x *" % (self.section.file_addr)
        summary = ""
        return {
            "#0": name,
            "value": value,
            "summary": summary,
            "children": self.section.GetNumSubSections() > 0,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        num_sections = self.section.GetNumSubSections()
        for i in range(num_sections):
            section = self.section.GetSubSectionAtIndex(i)
            image_item_delegate = SectionTreeItemDelegate(self.target, section)
            item_dicts.append(image_item_delegate.get_item_dictionary())
        return item_dicts


class ModuleCompileUnitsTreeItemDelegate(object):
    def __init__(self, target, module):
        self.target = target
        self.module = module

    def get_item_dictionary(self):
        name = "compile units"
        value = ""
        summary = "%u compile units" % (self.module.GetNumSections())
        return {
            "#0": name,
            "value": value,
            "summary": summary,
            "children": self.module.GetNumCompileUnits() > 0,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        num_cus = self.module.GetNumCompileUnits()
        for i in range(num_cus):
            cu = self.module.GetCompileUnitAtIndex(i)
            image_item_delegate = CompileUnitTreeItemDelegate(self.target, cu)
            item_dicts.append(image_item_delegate.get_item_dictionary())
        return item_dicts


class CompileUnitTreeItemDelegate(object):
    def __init__(self, target, cu):
        self.target = target
        self.cu = cu

    def get_item_dictionary(self):
        name = self.cu.GetFileSpec().basename
        value = ""
        num_lines = self.cu.GetNumLineEntries()
        summary = ""
        return {
            "#0": name,
            "value": value,
            "summary": summary,
            "children": num_lines > 0,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        item_delegate = LineTableTreeItemDelegate(self.target, self.cu)
        item_dicts.append(item_delegate.get_item_dictionary())
        return item_dicts


class LineTableTreeItemDelegate(object):
    def __init__(self, target, cu):
        self.target = target
        self.cu = cu

    def get_item_dictionary(self):
        name = "line table"
        value = ""
        num_lines = self.cu.GetNumLineEntries()
        summary = "%u line entries" % (num_lines)
        return {
            "#0": name,
            "value": value,
            "summary": summary,
            "children": num_lines > 0,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        num_lines = self.cu.GetNumLineEntries()
        for i in range(num_lines):
            line_entry = self.cu.GetLineEntryAtIndex(i)
            item_delegate = LineEntryTreeItemDelegate(self.target, line_entry, i)
            item_dicts.append(item_delegate.get_item_dictionary())
        return item_dicts


class LineEntryTreeItemDelegate(object):
    def __init__(self, target, line_entry, index):
        self.target = target
        self.line_entry = line_entry
        self.index = index

    def get_item_dictionary(self):
        name = str(self.index)
        address = self.line_entry.GetStartAddress()
        load_addr = address.GetLoadAddress(self.target)
        if load_addr != lldb.LLDB_INVALID_ADDRESS:
            value = "0x%16.16x" % (load_addr)
        else:
            value = "0x%16.16x *" % (address.file_addr)
        summary = (
            self.line_entry.GetFileSpec().fullpath + ":" + str(self.line_entry.line)
        )
        return {
            "#0": name,
            "value": value,
            "summary": summary,
            "children": False,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        return item_dicts


class InstructionTreeItemDelegate(object):
    def __init__(self, target, instr):
        self.target = target
        self.instr = instr

    def get_item_dictionary(self):
        address = self.instr.GetAddress()
        load_addr = address.GetLoadAddress(self.target)
        if load_addr != lldb.LLDB_INVALID_ADDRESS:
            name = "0x%16.16x" % (load_addr)
        else:
            name = "0x%16.16x *" % (address.file_addr)
        value = (
            self.instr.GetMnemonic(self.target)
            + " "
            + self.instr.GetOperands(self.target)
        )
        summary = self.instr.GetComment(self.target)
        return {
            "#0": name,
            "value": value,
            "summary": summary,
            "children": False,
            "tree-item-delegate": self,
        }


class ModuleSymbolsTreeItemDelegate(object):
    def __init__(self, target, module):
        self.target = target
        self.module = module

    def get_item_dictionary(self):
        name = "symbols"
        value = ""
        summary = "%u symbols" % (self.module.GetNumSymbols())
        return {
            "#0": name,
            "value": value,
            "summary": summary,
            "children": True,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        num_symbols = self.module.GetNumSymbols()
        for i in range(num_symbols):
            symbol = self.module.GetSymbolAtIndex(i)
            image_item_delegate = SymbolTreeItemDelegate(self.target, symbol, i)
            item_dicts.append(image_item_delegate.get_item_dictionary())
        return item_dicts


class SymbolTreeItemDelegate(object):
    def __init__(self, target, symbol, index):
        self.target = target
        self.symbol = symbol
        self.index = index

    def get_item_dictionary(self):
        address = self.symbol.GetStartAddress()
        name = "[%u]" % self.index
        symbol_load_addr = address.GetLoadAddress(self.target)
        if symbol_load_addr != lldb.LLDB_INVALID_ADDRESS:
            value = "0x%16.16x" % (symbol_load_addr)
        else:
            value = "0x%16.16x *" % (address.file_addr)
        summary = self.symbol.name
        return {
            "#0": name,
            "value": value,
            "summary": summary,
            "children": False,
            "tree-item-delegate": self,
        }

    def get_child_item_dictionaries(self):
        item_dicts = list()
        return item_dicts


class DelegateTree(ttk.Frame):
    def __init__(self, column_dicts, delegate, title, name):
        ttk.Frame.__init__(self, name=name)
        self.pack(expand=Y, fill=BOTH)
        self.master.title(title)
        self.delegate = delegate
        self.columns_dicts = column_dicts
        self.item_id_to_item_dict = dict()
        frame = Frame(self)
        frame.pack(side=TOP, fill=BOTH, expand=Y)
        self._create_treeview(frame)
        self._populate_root()

    def _create_treeview(self, parent):
        frame = ttk.Frame(parent)
        frame.pack(side=TOP, fill=BOTH, expand=Y)

        column_ids = list()
        for i in range(1, len(self.columns_dicts)):
            column_ids.append(self.columns_dicts[i]["id"])
        # create the tree and scrollbars
        self.tree = ttk.Treeview(columns=column_ids)

        scroll_bar_v = ttk.Scrollbar(orient=VERTICAL, command=self.tree.yview)
        scroll_bar_h = ttk.Scrollbar(orient=HORIZONTAL, command=self.tree.xview)
        self.tree["yscroll"] = scroll_bar_v.set
        self.tree["xscroll"] = scroll_bar_h.set

        # setup column headings and columns properties
        for columns_dict in self.columns_dicts:
            self.tree.heading(
                columns_dict["id"],
                text=columns_dict["text"],
                anchor=columns_dict["anchor"],
            )
            self.tree.column(columns_dict["id"], stretch=columns_dict["stretch"])

        # add tree and scrollbars to frame
        self.tree.grid(in_=frame, row=0, column=0, sticky=NSEW)
        scroll_bar_v.grid(in_=frame, row=0, column=1, sticky=NS)
        scroll_bar_h.grid(in_=frame, row=1, column=0, sticky=EW)

        # set frame resizing priorities
        frame.rowconfigure(0, weight=1)
        frame.columnconfigure(0, weight=1)

        # action to perform when a node is expanded
        self.tree.bind("<<TreeviewOpen>>", self._update_tree)

    def insert_items(self, parent_id, item_dicts):
        for item_dict in item_dicts:
            name = None
            values = list()
            first = True
            for columns_dict in self.columns_dicts:
                if first:
                    name = item_dict[columns_dict["id"]]
                    first = False
                else:
                    values.append(item_dict[columns_dict["id"]])
            item_id = self.tree.insert(
                parent_id, END, text=name, values=values  # root item has an empty name
            )
            self.item_id_to_item_dict[item_id] = item_dict
            if item_dict["children"]:
                self.tree.insert(item_id, END, text="dummy")

    def _populate_root(self):
        # use current directory as root node
        self.insert_items("", self.delegate.get_child_item_dictionaries())

    def _update_tree(self, event):
        # user expanded a node - build the related directory
        item_id = self.tree.focus()  # the id of the expanded node
        children = self.tree.get_children(item_id)
        if len(children):
            first_child = children[0]
            # if the node only has a 'dummy' child, remove it and
            # build new directory; skip if the node is already
            # populated
            if self.tree.item(first_child, option="text") == "dummy":
                self.tree.delete(first_child)
                item_dict = self.item_id_to_item_dict[item_id]
                item_dicts = item_dict[
                    "tree-item-delegate"
                ].get_child_item_dictionaries()
                self.insert_items(item_id, item_dicts)


@lldb.command("tk-variables")
def tk_variable_display(debugger, command, result, dict):
    # needed for tree creation in TK library as it uses sys.argv...
    sys.argv = ["tk-variables"]
    target = debugger.GetSelectedTarget()
    if not target:
        print("invalid target", file=result)
        return
    process = target.GetProcess()
    if not process:
        print("invalid process", file=result)
        return
    thread = process.GetSelectedThread()
    if not thread:
        print("invalid thread", file=result)
        return
    frame = thread.GetSelectedFrame()
    if not frame:
        print("invalid frame", file=result)
        return
    # Parse command line args
    command_args = shlex.split(command)
    column_dicts = [
        {"id": "#0", "text": "Name", "anchor": W, "stretch": 0},
        {"id": "typename", "text": "Type", "anchor": W, "stretch": 0},
        {"id": "value", "text": "Value", "anchor": W, "stretch": 0},
        {"id": "summary", "text": "Summary", "anchor": W, "stretch": 1},
    ]
    tree = DelegateTree(
        column_dicts, FrameTreeItemDelegate(frame), "Variables", "lldb-tk-variables"
    )
    tree.mainloop()


@lldb.command("tk-process")
def tk_process_display(debugger, command, result, dict):
    # needed for tree creation in TK library as it uses sys.argv...
    sys.argv = ["tk-process"]
    target = debugger.GetSelectedTarget()
    if not target:
        print("invalid target", file=result)
        return
    process = target.GetProcess()
    if not process:
        print("invalid process", file=result)
        return
    # Parse command line args
    columnd_dicts = [
        {"id": "#0", "text": "Name", "anchor": W, "stretch": 0},
        {"id": "value", "text": "Value", "anchor": W, "stretch": 0},
        {"id": "summary", "text": "Summary", "anchor": W, "stretch": 1},
    ]
    command_args = shlex.split(command)
    tree = DelegateTree(
        columnd_dicts, ProcessTreeItemDelegate(process), "Process", "lldb-tk-process"
    )
    tree.mainloop()


@lldb.command("tk-target")
def tk_target_display(debugger, command, result, dict):
    # needed for tree creation in TK library as it uses sys.argv...
    sys.argv = ["tk-target"]
    target = debugger.GetSelectedTarget()
    if not target:
        print("invalid target", file=result)
        return
    # Parse command line args
    columnd_dicts = [
        {"id": "#0", "text": "Name", "anchor": W, "stretch": 0},
        {"id": "value", "text": "Value", "anchor": W, "stretch": 0},
        {"id": "summary", "text": "Summary", "anchor": W, "stretch": 1},
    ]
    command_args = shlex.split(command)
    tree = DelegateTree(
        columnd_dicts, TargetTreeItemDelegate(target), "Target", "lldb-tk-target"
    )
    tree.mainloop()
