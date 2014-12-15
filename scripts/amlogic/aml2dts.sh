#!/bin/bash
# This shell script will convert the xxx.dtd file to xxx.dtb.
# You should pass two parameters to this shell script, input dtd file path and output dtb file path,
# if the second parameter is not set, the default value is the dtd file path.
# Written by Cai Yun 2013-09-11

#debug
print=echo
#print=test

process_file(){
    $print "process file $1 start"
    echo "processing... please wait..."

    sed -i '/\/\/\$\$ ADD/a\\/\{' "$1"  # delete //$$ ADD, add "/{"
    sed -i '/void/d' "$1"       #delete void line
    sed -i '/\/\/\//d' "$1"     #delete "///" line
    sed -i '/\/\/\$\$/d' "$1"   #delete "//$$" line

    $print "process file $1 end"
    $print ""
}

#DTD_FILE="arch/arm/boot/meson.dtd"
if [ -z "$1" ]; then
    echo "input error: no dtd file, please input a path:"
    read DTD_FILE
    echo "your input dtd file is    $DTD_FILE"
else
    DTD_FILE="$1"
    echo "DTD_FILE:                 $DTD_FILE"
fi

if [ -n "$2" ]; then
    DTB_FILE="$2"
    DTS_FILE="${DTB_FILE/.dtb/.dts}"
    echo "output dtb file:          $DTB_FILE"
    echo "middle dts file:          $DTS_FILE"
else
#    DTB_FILE="${DTD_FILE/.dtd/.dtb}"
    DTS_FILE="${DTD_FILE/.dtd/.dts}"
#    echo "Do not specify the output dtb file"
#    echo "use the default dtb file: $DTB_FILE"
    echo "the middle dts file:      $DTS_FILE"
fi

#if [ -f $DTB_FILE ] ; then
#    rm $DTB_FILE
#fi
if [ -f $DTS_FILE ] ; then
    rm $DTS_FILE
fi

touch "$DTS_FILE"
chmod 777 "$DTS_FILE"
cat "$DTD_FILE" >> "$DTS_FILE"

process_file $DTS_FILE

#echo "DTB_FILE is $DTB_FILE"

#DTB_FILE_NAME=`basename $DTB_FILE`

#make  "$DTB_FILE_NAME" -n
#if [ -f $DTB_FILE ]; then
#    echo "dtc compile over, delte middle file $DTS_FILE"
#    rm "$DTS_FILE"
#    echo "$DTB_FILE is OK!"
#else
#    echo "wrong dts file, dtc can not compiler!!!"
#fi
