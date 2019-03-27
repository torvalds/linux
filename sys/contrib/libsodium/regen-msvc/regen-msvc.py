#! /usr/bin/env python3

import fileinput
import glob
import os
import uuid

dirs = set()

tlv1 = ""
for file in glob.iglob("src/libsodium/**/*.c", recursive=True):
    file = file.replace("/", "\\")
    tlv1 = tlv1 + "    <ClCompile Include=\"{}\" />\r\n".format(file)

tlv2 = ""
for file in glob.iglob("src/libsodium/**/*.h", recursive=True):
    file = file.replace("/", "\\")
    tlv2 = tlv2 + "    <ClInclude Include=\"{}\" />\r\n".format(file)

tlf1 = ""
for file in glob.iglob("src/libsodium/**/*.c", recursive=True):
    file = file.replace("/", "\\")
    tlf1 = tlf1 + "    <ClCompile Include=\"{}\">\r\n".format(file)
    tlf1 = tlf1 + "      <Filter>Source Files</Filter>\r\n"
    tlf1 = tlf1 + "    </ClCompile>\r\n"

tlf2 = ""
for file in glob.iglob("src/libsodium/**/*.h", recursive=True):
    file = file.replace("/", "\\")
    tlf2 = tlf2 + "    <ClInclude Include=\"{}\">\r\n".format(file)
    tlf2 = tlf2 + "      <Filter>Header Files</Filter>\r\n"
    tlf2 = tlf2 + "    </ClInclude>\r\n"

v1 = ""
for file in glob.iglob("src/libsodium/**/*.c", recursive=True):
    file = file.replace("/", "\\")
    v1 = v1 + \
        "    <ClCompile Include=\"..\\..\\..\\..\\{}\" />\r\n".format(file)

v2 = ""
for file in glob.iglob("src/libsodium/**/*.h", recursive=True):
    file = file.replace("/", "\\")
    v2 = v2 + \
        "    <ClInclude Include=\"..\\..\\..\\..\\{}\" />\r\n".format(file)

f1 = ""
for file in glob.iglob("src/libsodium/**/*.c", recursive=True):
    basedir = os.path.dirname(file).replace("src/libsodium/", "")
    t = basedir
    while t != '':
        dirs.add(t)
        t = os.path.dirname(t)
    basedir = basedir.replace("/", "\\")
    file = file.replace("/", "\\")
    f1 = f1 + "    <ClCompile Include=\"..\\..\\..\\..\\{}\">\r\n".format(file)
    f1 = f1 + "      <Filter>{}</Filter>\r\n".format(basedir)
    f1 = f1 + "    </ClCompile>\r\n"

f2 = ""
for file in glob.iglob("src/libsodium/**/*.h", recursive=True):
    basedir = os.path.dirname(file).replace("src/libsodium/", "")
    t = basedir
    while t != '':
        dirs.add(t)
        t = os.path.dirname(t)
    basedir = basedir.replace("/", "\\")
    file = file.replace("/", "\\")
    f2 = f2 + "    <ClInclude Include=\"..\\..\\..\\..\\{}\">\r\n".format(file)
    f2 = f2 + "      <Filter>{}</Filter>\r\n".format(basedir)
    f2 = f2 + "    </ClInclude>\r\n"

fd = ""
dirs = list(dirs)
dirs.sort()
for dir in dirs:
    dir = dir.replace("/", "\\")
    uid = uuid.uuid3(uuid.UUID(bytes=b'LibSodiumMSVCUID'), dir)
    fd = fd + "    <Filter Include=\"{}\">\r\n".format(dir)
    fd = fd + \
        "      <UniqueIdentifier>{{{}}}</UniqueIdentifier>\r\n".format(uid)
    fd = fd + "    </Filter>\r\n"


def apply_template(tplfile, outfile, sbox):
    tpl = ""
    with open(tplfile, 'rb') as fd:
        tpl = fd.read()
    for s in sbox.keys():
        tpl = tpl.replace(str.encode(
            "{{" + s + "}}", "utf8"), str.encode(str.strip(sbox[s]), "utf8"))

    with open(outfile, 'wb') as fd:
        fd.write(tpl)

sbox = {"tlv1": tlv1, "tlv2": tlv2, "tlf1": tlf1, "tlf2": tlf2, "v1": v1,
        "v2": v2, "f1": f1, "f2": f2, "fd": fd}

sd = os.path.dirname(os.path.realpath(__file__))

apply_template(sd + "/tl_libsodium.vcxproj.filters.tpl",
               "libsodium.vcxproj.filters", sbox)

sbox.update({"platform": "v140"})
apply_template(sd + "/tl_libsodium.vcxproj.tpl",
               "libsodium.vcxproj", sbox)

apply_template(sd + "/libsodium.vcxproj.filters.tpl",
               "builds/msvc/vs2017/libsodium/libsodium.vcxproj.filters", sbox)
apply_template(sd + "/libsodium.vcxproj.filters.tpl",
               "builds/msvc/vs2015/libsodium/libsodium.vcxproj.filters", sbox)
apply_template(sd + "/libsodium.vcxproj.filters.tpl",
               "builds/msvc/vs2013/libsodium/libsodium.vcxproj.filters", sbox)
apply_template(sd + "/libsodium.vcxproj.filters.tpl",
               "builds/msvc/vs2012/libsodium/libsodium.vcxproj.filters", sbox)
apply_template(sd + "/libsodium.vcxproj.filters.tpl",
               "builds/msvc/vs2010/libsodium/libsodium.vcxproj.filters", sbox)

sbox.update({"platform": "v141"})
apply_template(sd + "/libsodium.vcxproj.tpl",
               "builds/msvc/vs2017/libsodium/libsodium.vcxproj", sbox)

sbox.update({"platform": "v140"})
apply_template(sd + "/libsodium.vcxproj.tpl",
               "builds/msvc/vs2015/libsodium/libsodium.vcxproj", sbox)

sbox.update({"platform": "v120"})
apply_template(sd + "/libsodium.vcxproj.tpl",
               "builds/msvc/vs2013/libsodium/libsodium.vcxproj", sbox)

sbox.update({"platform": "v110"})
apply_template(sd + "/libsodium.vcxproj.tpl",
               "builds/msvc/vs2012/libsodium/libsodium.vcxproj", sbox)

sbox.update({"platform": "v100"})
apply_template(sd + "/libsodium.vcxproj.tpl",
               "builds/msvc/vs2010/libsodium/libsodium.vcxproj", sbox)
