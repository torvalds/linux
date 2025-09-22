#!/usr/bin/env python
"""
wciia - Whose Code Is It Anyway

Determines code owner of the file/folder relative to the llvm source root.
Code owner is determined from the content of the CODE_OWNERS.TXT 
by parsing the D: field

usage:

utils/wciia.py  path

limitations:
- must be run from llvm source root
- very simplistic algorithm
- only handles * as a wildcard
- not very user friendly 
- does not handle the proposed F: field

"""

from __future__ import print_function
import os

code_owners = {}


def process_files_and_folders(owner):
    filesfolders = owner["filesfolders"]
    # paths must be in ( ... ) so strip them
    lpar = filesfolders.find("(")
    rpar = filesfolders.rfind(")")
    if rpar <= lpar:
        # give up
        return
    paths = filesfolders[lpar + 1 : rpar]
    # split paths
    owner["paths"] = []
    for path in paths.split():
        owner["paths"].append(path)


def process_code_owner(owner):
    if "filesfolders" in owner:
        filesfolders = owner["filesfolders"]
    else:
        # 		print "F: field missing, using D: field"
        owner["filesfolders"] = owner["description"]
    process_files_and_folders(owner)
    code_owners[owner["name"]] = owner


# process CODE_OWNERS.TXT first
code_owners_file = open("CODE_OWNERS.TXT", "r").readlines()
code_owner = {}
for line in code_owners_file:
    for word in line.split():
        if word == "N:":
            name = line[2:].strip()
            if code_owner:
                process_code_owner(code_owner)
                code_owner = {}
            # reset the values
            code_owner["name"] = name
        if word == "E:":
            email = line[2:].strip()
            code_owner["email"] = email
        if word == "D:":
            description = line[2:].strip()
            code_owner["description"] = description
        if word == "F:":
            filesfolders = line[2:].strip()
            code_owner["filesfolders"].append(filesfolders)


def find_owners(fpath):
    onames = []
    lmatch = -1
    #  very simplistic way of findning the best match
    for name in code_owners:
        owner = code_owners[name]
        if "paths" in owner:
            for path in owner["paths"]:
                # 				print "searching (" + path + ")"
                # try exact match
                if fpath == path:
                    return name
                # see if path ends with a *
                rstar = path.rfind("*")
                if rstar > 0:
                    # try the longest match,
                    rpos = -1
                    if len(fpath) < len(path):
                        rpos = path.find(fpath)
                    if rpos == 0:
                        onames.append(name)
    onames.append("Chris Lattner")
    return onames


# now lest try to find the owner of the file or folder
import sys

if len(sys.argv) < 2:
    print("usage " + sys.argv[0] + " file_or_folder")
    exit(-1)

# the path we are checking
path = str(sys.argv[1])

# check if this is real path
if not os.path.exists(path):
    print("path (" + path + ") does not exist")
    exit(-1)

owners_name = find_owners(path)

# be grammatically correct
print("The owner(s) of the (" + path + ") is(are) : " + str(owners_name))

exit(0)

# bottom up walk of the current .
# not yet used
root = "."
for dir, subdirList, fileList in os.walk(root, topdown=False):
    print("dir :", dir)
    for fname in fileList:
        print("-", fname)
    print()
