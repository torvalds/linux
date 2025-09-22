#!/usr/bin/env python

from __future__ import print_function


class TimingScriptGenerator:
    """Used to generate a bash script which will invoke the toy and time it"""

    def __init__(self, scriptname, outputname):
        self.shfile = open(scriptname, "w")
        self.timeFile = outputname
        self.shfile.write('echo "" > %s\n' % self.timeFile)

    def writeTimingCall(self, irname, callname):
        """Echo some comments and invoke both versions of toy"""
        rootname = irname
        if "." in irname:
            rootname = irname[: irname.rfind(".")]
        self.shfile.write(
            'echo "%s: Calls %s" >> %s\n' % (callname, irname, self.timeFile)
        )
        self.shfile.write('echo "" >> %s\n' % self.timeFile)
        self.shfile.write('echo "With MCJIT" >> %s\n' % self.timeFile)
        self.shfile.write(
            '/usr/bin/time -f "Command %C\\n\\tuser time: %U s\\n\\tsytem time: %S s\\n\\tmax set: %M kb"'
        )
        self.shfile.write(" -o %s -a " % self.timeFile)
        self.shfile.write(
            "./toy -suppress-prompts -use-mcjit=true -enable-lazy-compilation=true -use-object-cache -input-IR=%s < %s > %s-mcjit.out 2> %s-mcjit.err\n"
            % (irname, callname, rootname, rootname)
        )
        self.shfile.write('echo "" >> %s\n' % self.timeFile)
        self.shfile.write('echo "With MCJIT again" >> %s\n' % self.timeFile)
        self.shfile.write(
            '/usr/bin/time -f "Command %C\\n\\tuser time: %U s\\n\\tsytem time: %S s\\n\\tmax set: %M kb"'
        )
        self.shfile.write(" -o %s -a " % self.timeFile)
        self.shfile.write(
            "./toy -suppress-prompts -use-mcjit=true -enable-lazy-compilation=true -use-object-cache -input-IR=%s < %s > %s-mcjit.out 2> %s-mcjit.err\n"
            % (irname, callname, rootname, rootname)
        )
        self.shfile.write('echo "" >> %s\n' % self.timeFile)
        self.shfile.write('echo "With JIT" >> %s\n' % self.timeFile)
        self.shfile.write(
            '/usr/bin/time -f "Command %C\\n\\tuser time: %U s\\n\\tsytem time: %S s\\n\\tmax set: %M kb"'
        )
        self.shfile.write(" -o %s -a " % self.timeFile)
        self.shfile.write(
            "./toy -suppress-prompts -use-mcjit=false -input-IR=%s < %s > %s-mcjit.out 2> %s-mcjit.err\n"
            % (irname, callname, rootname, rootname)
        )
        self.shfile.write('echo "" >> %s\n' % self.timeFile)
        self.shfile.write('echo "" >> %s\n' % self.timeFile)


class LibScriptGenerator:
    """Used to generate a bash script which will invoke the toy and time it"""

    def __init__(self, filename):
        self.shfile = open(filename, "w")

    def writeLibGenCall(self, libname, irname):
        self.shfile.write(
            "./toy -suppress-prompts -use-mcjit=false -dump-modules < %s 2> %s\n"
            % (libname, irname)
        )


def splitScript(inputname, libGenScript, timingScript):
    rootname = inputname[:-2]
    libname = rootname + "-lib.k"
    irname = rootname + "-lib.ir"
    callname = rootname + "-call.k"
    infile = open(inputname, "r")
    libfile = open(libname, "w")
    callfile = open(callname, "w")
    print("Splitting %s into %s and %s" % (inputname, callname, libname))
    for line in infile:
        if not line.startswith("#"):
            if line.startswith("print"):
                callfile.write(line)
            else:
                libfile.write(line)
    libGenScript.writeLibGenCall(libname, irname)
    timingScript.writeTimingCall(irname, callname)


# Execution begins here
libGenScript = LibScriptGenerator("make-libs.sh")
timingScript = TimingScriptGenerator("time-lib.sh", "lib-timing.txt")

script_list = [
    "test-5000-3-50-50.k",
    "test-5000-10-100-10.k",
    "test-5000-10-5-10.k",
    "test-5000-10-1-0.k",
    "test-1000-3-10-50.k",
    "test-1000-10-100-10.k",
    "test-1000-10-5-10.k",
    "test-1000-10-1-0.k",
    "test-200-3-2-50.k",
    "test-200-10-40-10.k",
    "test-200-10-2-10.k",
    "test-200-10-1-0.k",
]

for script in script_list:
    splitScript(script, libGenScript, timingScript)
print("All done!")
