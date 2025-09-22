#!/usr/bin/env python

from __future__ import print_function

import io
import yaml

# Try to use the C parser.
try:
    from yaml import CLoader as Loader
except ImportError:
    print("For faster parsing, you may want to install libYAML for PyYAML")
    from yaml import Loader

import html
from collections import defaultdict
import fnmatch
import functools
from multiprocessing import Lock
import os, os.path
import subprocess

try:
    # The previously builtin function `intern()` was moved
    # to the `sys` module in Python 3.
    from sys import intern
except:
    pass

import re

import optpmap

try:
    dict.iteritems
except AttributeError:
    # Python 3
    def itervalues(d):
        return iter(d.values())

    def iteritems(d):
        return iter(d.items())

else:
    # Python 2
    def itervalues(d):
        return d.itervalues()

    def iteritems(d):
        return d.iteritems()


def html_file_name(filename):
    return filename.replace("/", "_").replace("#", "_") + ".html"


def make_link(File, Line):
    return '"{}#L{}"'.format(html_file_name(File), Line)


class Remark(yaml.YAMLObject):
    # Work-around for http://pyyaml.org/ticket/154.
    yaml_loader = Loader

    default_demangler = "c++filt -n"
    demangler_proc = None

    @classmethod
    def set_demangler(cls, demangler):
        cls.demangler_proc = subprocess.Popen(
            demangler.split(), stdin=subprocess.PIPE, stdout=subprocess.PIPE
        )
        cls.demangler_lock = Lock()

    @classmethod
    def demangle(cls, name):
        with cls.demangler_lock:
            cls.demangler_proc.stdin.write((name + "\n").encode("utf-8"))
            cls.demangler_proc.stdin.flush()
            return cls.demangler_proc.stdout.readline().rstrip().decode("utf-8")

    # Intern all strings since we have lot of duplication across filenames,
    # remark text.
    #
    # Change Args from a list of dicts to a tuple of tuples.  This saves
    # memory in two ways.  One, a small tuple is significantly smaller than a
    # small dict.  Two, using tuple instead of list allows Args to be directly
    # used as part of the key (in Python only immutable types are hashable).
    def _reduce_memory(self):
        self.Pass = intern(self.Pass)
        self.Name = intern(self.Name)
        try:
            # Can't intern unicode strings.
            self.Function = intern(self.Function)
        except:
            pass

        def _reduce_memory_dict(old_dict):
            new_dict = dict()
            for (k, v) in iteritems(old_dict):
                if type(k) is str:
                    k = intern(k)

                if type(v) is str:
                    v = intern(v)
                elif type(v) is dict:
                    # This handles [{'Caller': ..., 'DebugLoc': { 'File': ... }}]
                    v = _reduce_memory_dict(v)
                new_dict[k] = v
            return tuple(new_dict.items())

        self.Args = tuple([_reduce_memory_dict(arg_dict) for arg_dict in self.Args])

    # The inverse operation of the dictonary-related memory optimization in
    # _reduce_memory_dict.  E.g.
    #     (('DebugLoc', (('File', ...) ... ))) -> [{'DebugLoc': {'File': ...} ....}]
    def recover_yaml_structure(self):
        def tuple_to_dict(t):
            d = dict()
            for (k, v) in t:
                if type(v) is tuple:
                    v = tuple_to_dict(v)
                d[k] = v
            return d

        self.Args = [tuple_to_dict(arg_tuple) for arg_tuple in self.Args]

    def canonicalize(self):
        if not hasattr(self, "Hotness"):
            self.Hotness = 0
        if not hasattr(self, "Args"):
            self.Args = []
        self._reduce_memory()

    @property
    def File(self):
        return self.DebugLoc["File"]

    @property
    def Line(self):
        return int(self.DebugLoc["Line"])

    @property
    def Column(self):
        return self.DebugLoc["Column"]

    @property
    def DebugLocString(self):
        return "{}:{}:{}".format(self.File, self.Line, self.Column)

    @property
    def DemangledFunctionName(self):
        return self.demangle(self.Function)

    @property
    def Link(self):
        return make_link(self.File, self.Line)

    def getArgString(self, mapping):
        mapping = dict(list(mapping))
        dl = mapping.get("DebugLoc")
        if dl:
            del mapping["DebugLoc"]

        assert len(mapping) == 1
        (key, value) = list(mapping.items())[0]

        if key == "Caller" or key == "Callee" or key == "DirectCallee":
            value = html.escape(self.demangle(value))

        if dl and key != "Caller":
            dl_dict = dict(list(dl))
            return "<a href={}>{}</a>".format(
                make_link(dl_dict["File"], dl_dict["Line"]), value
            )
        else:
            return value

    # Return a cached dictionary for the arguments.  The key for each entry is
    # the argument key (e.g. 'Callee' for inlining remarks.  The value is a
    # list containing the value (e.g. for 'Callee' the function) and
    # optionally a DebugLoc.
    def getArgDict(self):
        if hasattr(self, "ArgDict"):
            return self.ArgDict
        self.ArgDict = {}
        for arg in self.Args:
            if len(arg) == 2:
                if arg[0][0] == "DebugLoc":
                    dbgidx = 0
                else:
                    assert arg[1][0] == "DebugLoc"
                    dbgidx = 1

                key = arg[1 - dbgidx][0]
                entry = (arg[1 - dbgidx][1], arg[dbgidx][1])
            else:
                arg = arg[0]
                key = arg[0]
                entry = (arg[1],)

            self.ArgDict[key] = entry
        return self.ArgDict

    def getDiffPrefix(self):
        if hasattr(self, "Added"):
            if self.Added:
                return "+"
            else:
                return "-"
        return ""

    @property
    def PassWithDiffPrefix(self):
        return self.getDiffPrefix() + self.Pass

    @property
    def message(self):
        # Args is a list of mappings (dictionaries)
        values = [self.getArgString(mapping) for mapping in self.Args]
        return "".join(values)

    @property
    def RelativeHotness(self):
        if self.max_hotness:
            return "{0:.2f}%".format(self.Hotness * 100.0 / self.max_hotness)
        else:
            return ""

    @property
    def key(self):
        return (
            self.__class__,
            self.PassWithDiffPrefix,
            self.Name,
            self.File,
            self.Line,
            self.Column,
            self.Function,
            self.Args,
        )

    def __hash__(self):
        return hash(self.key)

    def __eq__(self, other):
        return self.key == other.key

    def __repr__(self):
        return str(self.key)


class Analysis(Remark):
    yaml_tag = "!Analysis"

    @property
    def color(self):
        return "white"


class AnalysisFPCommute(Analysis):
    yaml_tag = "!AnalysisFPCommute"


class AnalysisAliasing(Analysis):
    yaml_tag = "!AnalysisAliasing"


class Passed(Remark):
    yaml_tag = "!Passed"

    @property
    def color(self):
        return "green"


class Missed(Remark):
    yaml_tag = "!Missed"

    @property
    def color(self):
        return "red"


class Failure(Missed):
    yaml_tag = "!Failure"


def get_remarks(input_file, filter_=None):
    max_hotness = 0
    all_remarks = dict()
    file_remarks = defaultdict(functools.partial(defaultdict, list))

    with io.open(input_file, encoding="utf-8") as f:
        docs = yaml.load_all(f, Loader=Loader)

        filter_e = None
        if filter_:
            filter_e = re.compile(filter_)
        for remark in docs:
            remark.canonicalize()
            # Avoid remarks withoug debug location or if they are duplicated
            if not hasattr(remark, "DebugLoc") or remark.key in all_remarks:
                continue

            if filter_e and not filter_e.search(remark.Pass):
                continue

            all_remarks[remark.key] = remark

            file_remarks[remark.File][remark.Line].append(remark)

            # If we're reading a back a diff yaml file, max_hotness is already
            # captured which may actually be less than the max hotness found
            # in the file.
            if hasattr(remark, "max_hotness"):
                max_hotness = remark.max_hotness
            max_hotness = max(max_hotness, remark.Hotness)

    return max_hotness, all_remarks, file_remarks


def gather_results(filenames, num_jobs, should_print_progress, filter_=None):
    if should_print_progress:
        print("Reading YAML files...")
    if not Remark.demangler_proc:
        Remark.set_demangler(Remark.default_demangler)
    remarks = optpmap.pmap(
        get_remarks, filenames, num_jobs, should_print_progress, filter_
    )
    max_hotness = max(entry[0] for entry in remarks)

    def merge_file_remarks(file_remarks_job, all_remarks, merged):
        for filename, d in iteritems(file_remarks_job):
            for line, remarks in iteritems(d):
                for remark in remarks:
                    # Bring max_hotness into the remarks so that
                    # RelativeHotness does not depend on an external global.
                    remark.max_hotness = max_hotness
                    if remark.key not in all_remarks:
                        merged[filename][line].append(remark)

    all_remarks = dict()
    file_remarks = defaultdict(functools.partial(defaultdict, list))
    for _, all_remarks_job, file_remarks_job in remarks:
        merge_file_remarks(file_remarks_job, all_remarks, file_remarks)
        all_remarks.update(all_remarks_job)

    return all_remarks, file_remarks, max_hotness != 0


def find_opt_files(*dirs_or_files):
    all = []
    for dir_or_file in dirs_or_files:
        if os.path.isfile(dir_or_file):
            all.append(dir_or_file)
        else:
            for dir, subdirs, files in os.walk(dir_or_file):
                # Exclude mounted directories and symlinks (os.walk default).
                subdirs[:] = [
                    d for d in subdirs if not os.path.ismount(os.path.join(dir, d))
                ]
                for file in files:
                    if fnmatch.fnmatch(file, "*.opt.yaml*"):
                        all.append(os.path.join(dir, file))
    return all
