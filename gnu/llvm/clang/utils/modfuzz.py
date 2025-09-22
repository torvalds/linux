#!/usr/bin/env python

# To use:
#  1) Update the 'decls' list below with your fuzzing configuration.
#  2) Run with the clang binary as the command-line argument.

from __future__ import absolute_import, division, print_function
import random
import subprocess
import sys
import os

clang = sys.argv[1]
none_opts = 0.3


class Decl(object):
    def __init__(self, text, depends=[], provides=[], conflicts=[]):
        self.text = text
        self.depends = depends
        self.provides = provides
        self.conflicts = conflicts

    def valid(self, model):
        for i in self.depends:
            if i not in model.decls:
                return False
        for i in self.conflicts:
            if i in model.decls:
                return False
        return True

    def apply(self, model, name):
        for i in self.provides:
            model.decls[i] = True
        model.source += self.text % {"name": name}


decls = [
    Decl("struct X { int n; };\n", provides=["X"], conflicts=["X"]),
    Decl('static_assert(X{.n=1}.n == 1, "");\n', depends=["X"]),
    Decl("X %(name)s;\n", depends=["X"]),
]


class FS(object):
    def __init__(self):
        self.fs = {}
        self.prevfs = {}

    def write(self, path, contents):
        self.fs[path] = contents

    def done(self):
        for f, s in self.fs.items():
            if self.prevfs.get(f) != s:
                f = file(f, "w")
                f.write(s)
                f.close()

        for f in self.prevfs:
            if f not in self.fs:
                os.remove(f)

        self.prevfs, self.fs = self.fs, {}


fs = FS()


class CodeModel(object):
    def __init__(self):
        self.source = ""
        self.modules = {}
        self.decls = {}
        self.i = 0

    def make_name(self):
        self.i += 1
        return "n" + str(self.i)

    def fails(self):
        fs.write(
            "module.modulemap",
            "".join(
                'module %s { header "%s.h" export * }\n' % (m, m)
                for m in self.modules.keys()
            ),
        )

        for m, (s, _) in self.modules.items():
            fs.write("%s.h" % m, s)

        fs.write("main.cc", self.source)
        fs.done()

        return (
            subprocess.call(
                [clang, "-std=c++11", "-c", "-fmodules", "main.cc", "-o", "/dev/null"]
            )
            != 0
        )


def generate():
    model = CodeModel()
    m = []

    try:
        for d in mutations(model):
            d(model)
            m.append(d)
        if not model.fails():
            return
    except KeyboardInterrupt:
        print()
        return True

    sys.stdout.write("\nReducing:\n")
    sys.stdout.flush()

    try:
        while True:
            assert m, "got a failure with no steps; broken clang binary?"
            i = random.choice(list(range(len(m))))
            x = m[0:i] + m[i + 1 :]
            m2 = CodeModel()
            for d in x:
                d(m2)
            if m2.fails():
                m = x
                model = m2
            else:
                sys.stdout.write(".")
                sys.stdout.flush()
    except KeyboardInterrupt:
        # FIXME: Clean out output directory first.
        model.fails()
        return model


def choose(options):
    while True:
        i = int(random.uniform(0, len(options) + none_opts))
        if i >= len(options):
            break
        yield options[i]


def mutations(model):
    options = [create_module, add_top_level_decl]
    for opt in choose(options):
        yield opt(model, options)


def create_module(model, options):
    n = model.make_name()

    def go(model):
        model.modules[n] = (model.source, model.decls)
        (model.source, model.decls) = ("", {})

    options += [lambda model, options: add_import(model, options, n)]
    return go


def add_top_level_decl(model, options):
    n = model.make_name()
    d = random.choice([decl for decl in decls if decl.valid(model)])

    def go(model):
        if not d.valid(model):
            return
        d.apply(model, n)

    return go


def add_import(model, options, module_name):
    def go(model):
        if module_name in model.modules:
            model.source += '#include "%s.h"\n' % module_name
            model.decls.update(model.modules[module_name][1])

    return go


sys.stdout.write("Finding bug: ")
while True:
    if generate():
        break
    sys.stdout.write(".")
    sys.stdout.flush()
