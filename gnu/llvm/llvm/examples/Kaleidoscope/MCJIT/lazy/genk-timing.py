#!/usr/bin/env python

from __future__ import print_function

import sys
import random


class TimingScriptGenerator:
    """Used to generate a bash script which will invoke the toy and time it"""

    def __init__(self, scriptname, outputname):
        self.timeFile = outputname
        self.shfile = open(scriptname, "w")
        self.shfile.write('echo "" > %s\n' % self.timeFile)

    def writeTimingCall(self, filename, numFuncs, funcsCalled, totalCalls):
        """Echo some comments and invoke both versions of toy"""
        rootname = filename
        if "." in filename:
            rootname = filename[: filename.rfind(".")]
        self.shfile.write(
            'echo "%s: Calls %d of %d functions, %d total" >> %s\n'
            % (filename, funcsCalled, numFuncs, totalCalls, self.timeFile)
        )
        self.shfile.write('echo "" >> %s\n' % self.timeFile)
        self.shfile.write('echo "With MCJIT" >> %s\n' % self.timeFile)
        self.shfile.write(
            '/usr/bin/time -f "Command %C\\n\\tuser time: %U s\\n\\tsytem time: %S s\\n\\tmax set: %M kb"'
        )
        self.shfile.write(" -o %s -a " % self.timeFile)
        self.shfile.write(
            "./toy-mcjit < %s > %s-mcjit.out 2> %s-mcjit.err\n"
            % (filename, rootname, rootname)
        )
        self.shfile.write('echo "" >> %s\n' % self.timeFile)
        self.shfile.write('echo "With JIT" >> %s\n' % self.timeFile)
        self.shfile.write(
            '/usr/bin/time -f "Command %C\\n\\tuser time: %U s\\n\\tsytem time: %S s\\n\\tmax set: %M kb"'
        )
        self.shfile.write(" -o %s -a " % self.timeFile)
        self.shfile.write(
            "./toy-jit < %s > %s-jit.out 2> %s-jit.err\n"
            % (filename, rootname, rootname)
        )
        self.shfile.write('echo "" >> %s\n' % self.timeFile)
        self.shfile.write('echo "" >> %s\n' % self.timeFile)


class KScriptGenerator:
    """Used to generate random Kaleidoscope code"""

    def __init__(self, filename):
        self.kfile = open(filename, "w")
        self.nextFuncNum = 1
        self.lastFuncNum = None
        self.callWeighting = 0.1
        # A mapping of calls within functions with no duplicates
        self.calledFunctionTable = {}
        # A list of function calls which will actually be executed
        self.calledFunctions = []
        # A comprehensive mapping of calls within functions
        # used for computing the total number of calls
        self.comprehensiveCalledFunctionTable = {}
        self.totalCallsExecuted = 0

    def updateTotalCallCount(self, callee):
        # Count this call
        self.totalCallsExecuted += 1
        # Then count all the functions it calls
        if callee in self.comprehensiveCalledFunctionTable:
            for child in self.comprehensiveCalledFunctionTable[callee]:
                self.updateTotalCallCount(child)

    def updateFunctionCallMap(self, caller, callee):
        """Maintains a map of functions that are called from other functions"""
        if not caller in self.calledFunctionTable:
            self.calledFunctionTable[caller] = []
        if not callee in self.calledFunctionTable[caller]:
            self.calledFunctionTable[caller].append(callee)
        if not caller in self.comprehensiveCalledFunctionTable:
            self.comprehensiveCalledFunctionTable[caller] = []
        self.comprehensiveCalledFunctionTable[caller].append(callee)

    def updateCalledFunctionList(self, callee):
        """Maintains a list of functions that will actually be called"""
        # Update the total call count
        self.updateTotalCallCount(callee)
        # If this function is already in the list, don't do anything else
        if callee in self.calledFunctions:
            return
        # Add this function to the list of those that will be called.
        self.calledFunctions.append(callee)
        # If this function calls other functions, add them too
        if callee in self.calledFunctionTable:
            for subCallee in self.calledFunctionTable[callee]:
                self.updateCalledFunctionList(subCallee)

    def setCallWeighting(self, weight):
        """Sets the probably of generating a function call"""
        self.callWeighting = weight

    def writeln(self, line):
        self.kfile.write(line + "\n")

    def writeComment(self, comment):
        self.writeln("# " + comment)

    def writeEmptyLine(self):
        self.writeln("")

    def writePredefinedFunctions(self):
        self.writeComment(
            "Define ':' for sequencing: as a low-precedence operator that ignores operands"
        )
        self.writeComment("and just returns the RHS.")
        self.writeln("def binary : 1 (x y) y;")
        self.writeEmptyLine()
        self.writeComment("Helper functions defined within toy")
        self.writeln("extern putchard(x);")
        self.writeln("extern printd(d);")
        self.writeln("extern printlf();")
        self.writeEmptyLine()
        self.writeComment("Print the result of a function call")
        self.writeln("def printresult(N Result)")
        self.writeln("  # 'result('")
        self.writeln(
            "  putchard(114) : putchard(101) : putchard(115) : putchard(117) : putchard(108) : putchard(116) : putchard(40) :"
        )
        self.writeln("  printd(N) :")
        self.writeln("  # ') = '")
        self.writeln("  putchard(41) : putchard(32) : putchard(61) : putchard(32) :")
        self.writeln("  printd(Result) :")
        self.writeln("  printlf();")
        self.writeEmptyLine()

    def writeRandomOperation(self, LValue, LHS, RHS):
        shouldCallFunc = self.lastFuncNum > 2 and random.random() < self.callWeighting
        if shouldCallFunc:
            funcToCall = random.randrange(1, self.lastFuncNum - 1)
            self.updateFunctionCallMap(self.lastFuncNum, funcToCall)
            self.writeln("  %s = func%d(%s, %s) :" % (LValue, funcToCall, LHS, RHS))
        else:
            possibleOperations = ["+", "-", "*", "/"]
            operation = random.choice(possibleOperations)
            if operation == "-":
                # Don't let our intermediate value become zero
                # This is complicated by the fact that '<' is our only comparison operator
                self.writeln("  if %s < %s then" % (LHS, RHS))
                self.writeln("    %s = %s %s %s" % (LValue, LHS, operation, RHS))
                self.writeln("  else if %s < %s then" % (RHS, LHS))
                self.writeln("    %s = %s %s %s" % (LValue, LHS, operation, RHS))
                self.writeln("  else")
                self.writeln(
                    "    %s = %s %s %f :"
                    % (LValue, LHS, operation, random.uniform(1, 100))
                )
            else:
                self.writeln("  %s = %s %s %s :" % (LValue, LHS, operation, RHS))

    def getNextFuncNum(self):
        result = self.nextFuncNum
        self.nextFuncNum += 1
        self.lastFuncNum = result
        return result

    def writeFunction(self, elements):
        funcNum = self.getNextFuncNum()
        self.writeComment("Auto-generated function number %d" % funcNum)
        self.writeln("def func%d(X Y)" % funcNum)
        self.writeln("  var temp1 = X,")
        self.writeln("      temp2 = Y,")
        self.writeln("      temp3 in")
        # Initialize the variable names to be rotated
        first = "temp3"
        second = "temp1"
        third = "temp2"
        # Write some random operations
        for i in range(elements):
            self.writeRandomOperation(first, second, third)
            # Rotate the variables
            temp = first
            first = second
            second = third
            third = temp
        self.writeln("  " + third + ";")
        self.writeEmptyLine()

    def writeFunctionCall(self):
        self.writeComment("Call the last function")
        arg1 = random.uniform(1, 100)
        arg2 = random.uniform(1, 100)
        self.writeln(
            "printresult(%d, func%d(%f, %f) )"
            % (self.lastFuncNum, self.lastFuncNum, arg1, arg2)
        )
        self.writeEmptyLine()
        self.updateCalledFunctionList(self.lastFuncNum)

    def writeFinalFunctionCounts(self):
        self.writeComment(
            "Called %d of %d functions" % (len(self.calledFunctions), self.lastFuncNum)
        )


def generateKScript(
    filename, numFuncs, elementsPerFunc, funcsBetweenExec, callWeighting, timingScript
):
    """Generate a random Kaleidoscope script based on the given parameters"""
    print("Generating " + filename)
    print(
        "  %d functions, %d elements per function, %d functions between execution"
        % (numFuncs, elementsPerFunc, funcsBetweenExec)
    )
    print("  Call weighting = %f" % callWeighting)
    script = KScriptGenerator(filename)
    script.setCallWeighting(callWeighting)
    script.writeComment(
        "==========================================================================="
    )
    script.writeComment("Auto-generated script")
    script.writeComment(
        "  %d functions, %d elements per function, %d functions between execution"
        % (numFuncs, elementsPerFunc, funcsBetweenExec)
    )
    script.writeComment("  call weighting = %f" % callWeighting)
    script.writeComment(
        "==========================================================================="
    )
    script.writeEmptyLine()
    script.writePredefinedFunctions()
    funcsSinceLastExec = 0
    for i in range(numFuncs):
        script.writeFunction(elementsPerFunc)
        funcsSinceLastExec += 1
        if funcsSinceLastExec == funcsBetweenExec:
            script.writeFunctionCall()
            funcsSinceLastExec = 0
    # Always end with a function call
    if funcsSinceLastExec > 0:
        script.writeFunctionCall()
    script.writeEmptyLine()
    script.writeFinalFunctionCounts()
    funcsCalled = len(script.calledFunctions)
    print(
        "  Called %d of %d functions, %d total"
        % (funcsCalled, numFuncs, script.totalCallsExecuted)
    )
    timingScript.writeTimingCall(
        filename, numFuncs, funcsCalled, script.totalCallsExecuted
    )


# Execution begins here
random.seed()

timingScript = TimingScriptGenerator("time-toy.sh", "timing-data.txt")

dataSets = [
    (5000, 3, 50, 0.50),
    (5000, 10, 100, 0.10),
    (5000, 10, 5, 0.10),
    (5000, 10, 1, 0.0),
    (1000, 3, 10, 0.50),
    (1000, 10, 100, 0.10),
    (1000, 10, 5, 0.10),
    (1000, 10, 1, 0.0),
    (200, 3, 2, 0.50),
    (200, 10, 40, 0.10),
    (200, 10, 2, 0.10),
    (200, 10, 1, 0.0),
]

# Generate the code
for (numFuncs, elementsPerFunc, funcsBetweenExec, callWeighting) in dataSets:
    filename = "test-%d-%d-%d-%d.k" % (
        numFuncs,
        elementsPerFunc,
        funcsBetweenExec,
        int(callWeighting * 100),
    )
    generateKScript(
        filename,
        numFuncs,
        elementsPerFunc,
        funcsBetweenExec,
        callWeighting,
        timingScript,
    )
print("All done!")
