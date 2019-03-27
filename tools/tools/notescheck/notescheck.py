#!/usr/local/bin/python
#
# This script analyzes sys/conf/files*, sys/conf/options*,
# sys/conf/NOTES, and sys/*/conf/NOTES and checks for inconsistencies
# such as options or devices that are not specified in any NOTES files
# or MI devices specified in MD NOTES files.
#
# $FreeBSD$

from __future__ import print_function

import glob
import os.path
import sys

def usage():
    print("notescheck <path>", file=sys.stderr)
    print(file=sys.stderr)
    print("Where 'path' is a path to a kernel source tree.", file=sys.stderr)

# These files are used to determine if a path is a valid kernel source tree.
requiredfiles = ['conf/files', 'conf/options', 'conf/NOTES']

# This special platform string is used for managing MI options.
global_platform = 'global'

# This is a global string that represents the current file and line
# being parsed.
location = ""

# Format the contents of a set into a sorted, comma-separated string
def format_set(set):
    l = []
    for item in set:
        l.append(item)
    if len(l) == 0:
        return "(empty)"
    l.sort()
    if len(l) == 2:
        return "%s and %s" % (l[0], l[1])
    s = "%s" % (l[0])
    if len(l) == 1:
        return s
    for item in l[1:-1]:
        s = "%s, %s" % (s, item)
    s = "%s, and %s" % (s, l[-1])
    return s

# This class actually covers both options and devices.  For each named
# option we maintain two different lists.  One is the list of
# platforms that the option was defined in via an options or files
# file.  The other is the list of platforms that the option was tested
# in via a NOTES file.  All options are stored as lowercase since
# config(8) treats the names as case-insensitive.
class Option:
    def __init__(self, name):
        self.name = name
        self.type = None
        self.defines = set()
        self.tests = set()

    def set_type(self, type):
        if self.type is None:
            self.type = type
            self.type_location = location
        elif self.type != type:
            print("WARN: Attempt to change type of %s from %s to %s%s" % \
                (self.name, self.type, type, location))
            print("      Previous type set%s" % (self.type_location))

    def add_define(self, platform):
        self.defines.add(platform)

    def add_test(self, platform):
        self.tests.add(platform)

    def title(self):
        if self.type == 'option':
            return 'option %s' % (self.name.upper())
        if self.type == None:
            return self.name
        return '%s %s' % (self.type, self.name)

    def warn(self):
        # If the defined and tested sets are equal, then this option
        # is ok.
        if self.defines == self.tests:
            return

        # If the tested set contains the global platform, then this
        # option is ok.
        if global_platform in self.tests:
            return

        if global_platform in self.defines:
            # If the device is defined globally and is never tested, whine.
            if len(self.tests) == 0:
                print('WARN: %s is defined globally but never tested' % \
                    (self.title()))
                return
            
            # If the device is defined globally and is tested on
            # multiple MD platforms, then it is ok.  This often occurs
            # for drivers that are shared across multiple, but not
            # all, platforms (e.g. acpi, agp).
            if len(self.tests) > 1:
                return

            # If a device is defined globally but is only tested on a
            # single MD platform, then whine about this.
            print('WARN: %s is defined globally but only tested in %s NOTES' % \
                (self.title(), format_set(self.tests)))
            return

        # If an option or device is never tested, whine.
        if len(self.tests) == 0:
            print('WARN: %s is defined in %s but never tested' % \
                (self.title(), format_set(self.defines)))
            return

        # The set of MD platforms where this option is defined, but not tested.
        notest = self.defines - self.tests
        if len(notest) != 0:
            print('WARN: %s is not tested in %s NOTES' % \
                (self.title(), format_set(notest)))
            return

        print('ERROR: bad state for %s: defined in %s, tested in %s' % \
            (self.title(), format_set(self.defines), format_set(self.tests)))

# This class maintains a dictionary of options keyed by name.
class Options:
    def __init__(self):
        self.options = {}

    # Look up the object for a given option by name.  If the option
    # doesn't already exist, then add a new option.
    def find(self, name):
        name = name.lower()
        if name in self.options:
            return self.options[name]
        option = Option(name)
        self.options[name] = option
        return option

    # Warn about inconsistencies
    def warn(self):
        keys = list(self.options.keys())
        keys.sort()
        for key in keys:
            option = self.options[key]
            option.warn()

# Global map of options
options = Options()

# Look for MD NOTES files to build our list of platforms.  We ignore
# platforms that do not have a NOTES file.
def find_platforms(tree):
    platforms = []
    for file in glob.glob(tree + '*/conf/NOTES'):
        if not file.startswith(tree):
            print("Bad MD NOTES file %s" %(file), file=sys.stderr)
            sys.exit(1)
        platforms.append(file[len(tree):].split('/')[0])
    if global_platform in platforms:
        print("Found MD NOTES file for global platform", file=sys.stderr)
        sys.exit(1)
    return platforms

# Parse a file that has escaped newlines.  Any escaped newlines are
# coalesced and each logical line is passed to the callback function.
# This also skips blank lines and comments.
def parse_file(file, callback, *args):
    global location

    f = open(file)
    current = None
    i = 0
    for line in f:
        # Update parsing location
        i = i + 1
        location = ' at %s:%d' % (file, i)

        # Trim the newline
        line = line[:-1]

        # If the previous line had an escaped newline, append this
        # line to that.
        if current is not None:
            line = current + line
            current = None

        # If the line ends in a '\', set current to the line (minus
        # the escape) and continue.
        if len(line) > 0 and line[-1] == '\\':
            current = line[:-1]
            continue

        # Skip blank lines or lines with only whitespace
        if len(line) == 0 or len(line.split()) == 0:
            continue

        # Skip comment lines.  Any line whose first non-space
        # character is a '#' is considered a comment.
        if line.split()[0][0] == '#':
            continue

        # Invoke the callback on this line
        callback(line, *args)
    if current is not None:
        callback(current, *args)

    location = ""

# Split a line into words on whitespace with the exception that quoted
# strings are always treated as a single word.
def tokenize(line):
    if len(line) == 0:
        return []

    # First, split the line on quote characters.
    groups = line.split('"')

    # Ensure we have an even number of quotes.  The 'groups' array
    # will contain 'number of quotes' + 1 entries, so it should have
    # an odd number of entries.
    if len(groups) % 2 == 0:
        print("Failed to tokenize: %s%s" (line, location), file=sys.stderr)
        return []

    # String split all the "odd" groups since they are not quoted strings.
    quoted = False
    words = []
    for group in groups:
        if quoted:
            words.append(group)
            quoted = False
        else:
            for word in group.split():
                words.append(word)
            quoted = True
    return words

# Parse a sys/conf/files* file adding defines for any options
# encountered.  Note files does not differentiate between options and
# devices.
def parse_files_line(line, platform):
    words = tokenize(line)

    # Skip include lines.
    if words[0] == 'include':
        return

    # Skip standard lines as they have no devices or options.
    if words[1] == 'standard':
        return

    # Remaining lines better be optional or mandatory lines.
    if words[1] != 'optional' and words[1] != 'mandatory':
        print("Invalid files line: %s%s" % (line, location), file=sys.stderr)

    # Drop the first two words and begin parsing keywords and devices.
    skip = False
    for word in words[2:]:
        if skip:
            skip = False
            continue

        # Skip keywords
        if word == 'no-obj' or word == 'no-implicit-rule' or \
                word == 'before-depend' or word == 'local' or \
                word == 'no-depend' or word == 'profiling-routine' or \
                word == 'nowerror':
            continue

        # Skip keywords and their following argument
        if word == 'dependency' or word == 'clean' or \
                word == 'compile-with' or word == 'warning':
            skip = True
            continue

        # Ignore pipes
        if word == '|':
            continue

        option = options.find(word)
        option.add_define(platform)

# Parse a sys/conf/options* file adding defines for any options
# encountered.  Unlike a files file, options files only add options.
def parse_options_line(line, platform):
    # The first word is the option name.
    name = line.split()[0]

    # Ignore DEV_xxx options.  These are magic options that are
    # aliases for 'device xxx'.
    if name.startswith('DEV_'):
        return

    option = options.find(name)
    option.add_define(platform)
    option.set_type('option')

# Parse a sys/conf/NOTES file adding tests for any options or devices
# encountered.
def parse_notes_line(line, platform):
    words = line.split()

    # Skip lines with just whitespace
    if len(words) == 0:
        return

    if words[0] == 'device' or words[0] == 'devices':
        option = options.find(words[1])
        option.add_test(platform)
        option.set_type('device')
        return

    if words[0] == 'option' or words[0] == 'options':
        option = options.find(words[1].split('=')[0])
        option.add_test(platform)
        option.set_type('option')
        return

def main(argv=None):
    if argv is None:
        argv = sys.argv
    if len(sys.argv) != 2:
        usage()
        return 2

    # Ensure the path has a trailing '/'.
    tree = sys.argv[1]
    if tree[-1] != '/':
        tree = tree + '/'
    for file in requiredfiles:
        if not os.path.exists(tree + file):
            print("Kernel source tree missing %s" % (file), file=sys.stderr)
            return 1
    
    platforms = find_platforms(tree)

    # First, parse global files.
    parse_file(tree + 'conf/files', parse_files_line, global_platform)
    parse_file(tree + 'conf/options', parse_options_line, global_platform)
    parse_file(tree + 'conf/NOTES', parse_notes_line, global_platform)

    # Next, parse MD files.
    for platform in platforms:
        files_file = tree + 'conf/files.' + platform
        if os.path.exists(files_file):
            parse_file(files_file, parse_files_line, platform)
        options_file = tree + 'conf/options.' + platform
        if os.path.exists(options_file):
            parse_file(options_file, parse_options_line, platform)
        parse_file(tree + platform + '/conf/NOTES', parse_notes_line, platform)

    options.warn()
    return 0

if __name__ == "__main__":
    sys.exit(main())
