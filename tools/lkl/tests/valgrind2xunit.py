#!/usr/bin/env python

##
## Downloader from
## http://humdi.net/wiki/tips/valgrind-to-xunit-xml-converter
##

import xml.etree.ElementTree as ET
import sys
import os

fname = sys.argv[1]
if fname is None:
    fname = 'valgrind.xml'

doc = ET.parse(fname)
errors = doc.findall('.//error')

out = open(os.path.splitext(os.path.basename(fname))[0]+'_xunit.xml',"w")
out.write('<?xml version="1.0" encoding="UTF-8"?>\n')
out.write('<testsuite name="valgrind" tests="'+str(len(errors))+'" errors="0" failures="'+str(len(errors))+'" skip="0">\n')
errorcount=0
for error in errors:
    errorcount=errorcount+1

    kind = error.find('kind')
    what = error.find('what')
    if  what == None:
        what = error.find('xwhat/text')

    stack = error.find('stack')
    frames = stack.findall('frame')

    for frame in frames:
        fi = frame.find('file')
        li = frame.find('line')
        if fi != None and li != None:
            break

    if fi != None and li != None:
        out.write('    <testcase classname="ValgrindMemoryCheck" name="Memory check '+str(errorcount)+' ('+kind.text+', '+fi.text+':'+li.text+')" time="0">\n')
    else:
        out.write('    <testcase classname="ValgrindMemoryCheck" name="Memory check '+str(errorcount)+' ('+kind.text+')" time="0">\n')
    out.write('        <error type="'+kind.text+'">\n')
    out.write('  '+what.text+'\n\n')

    for frame in frames:
        ip = frame.find('ip')
        fn = frame.find('fn')
        fi = frame.find('file')
        li = frame.find('line')

        if fn is None:
            bodytext = '(unresolved symbol)'
        else:
            bodytext = fn.text
        bodytext = bodytext.replace("&","&amp;")
        bodytext = bodytext.replace("<","&lt;")
        bodytext = bodytext.replace(">","&gt;")
        if fi != None and li != None:
            out.write('  '+ip.text+': '+bodytext+' ('+fi.text+':'+li.text+')\n')
        else:
            out.write('  '+ip.text+': '+bodytext+'\n')

    out.write('        </error>\n')
    out.write('    </testcase>\n')
out.write('</testsuite>\n')
out.close()
