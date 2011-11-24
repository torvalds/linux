#!/bin/bash

# Copyright (C) 2007 Nanoradio AB
# $Id: replace_nanokey 14981 2010-04-15 13:51:06Z joda $


function help {
    echo "Usage: replace_nanokey <file name>"
    echo "This program will open "file name" and replace lines containing"
    echo "NRX_API_INSERT_COMMAND= with the result from running the bash script"
    echo "written after =."
    echo ""
    echo "Example: The file "t.txt" contains the following lines:"
    echo ""
    echo "   The current time and date is"
    echo "   NRX_API_INSERT_COMMAND=date"
#    echo "   NRX_API_INSERT_COMMAND=X=\"BACKWARDS\"; i=\`echo -n \$X  | wc -c\`; while [ \$i -gt 0 ]; do echo -n \`echo \$X | cut -c \$i\`; i=\`expr \$i - 1\`; done"
    echo "   where this script is run."
    echo ""
    echo "Running \"replace_nanokey t.txt\" will give this output"
    echo ""
    echo "   The current time and date is"
    echo "   Mon Aug 20 15:25:37 CEST 2007"
#    echo "   SDRAWKCAB"
    echo "   where this script is run."
    exit 1
}


# Sanity checks

if [ $# -eq 0 ]; then
    help
fi

if [ "$1" == "--help" ]; then
    help
fi

if [ $# -ne 1 ]; then
    echo Need 1 filename as argument. >&2
    exit 1
fi

if [ ! -f $1 ]; then
    echo File $1 does not exist. >&2
    echo Try option --help for more information.
    exit 1
fi


LOOP=1
PREV_ROW=0
LINES=`cat $1 | wc -l`

while [ 1 ]; do

    TRIGGER=""
    TRIGGER=`grep -n "NRX_API_INSERT_COMMAND" $1  | head -n $LOOP | tail -n 1 `;
    [ "$TRIGGER" = "" ] && break;
    COMMAND=`echo $TRIGGER | cut -d = -f 2-`
    ROW=`echo $TRIGGER | cut -d : -f 1`
    [ "$ROW" = "$PREV_ROW" ] && break;
    HEAD=`expr $ROW - 1`
    TAIL=`expr $ROW - $PREV_ROW - 1`
    cat $1 | head -n $HEAD | tail -n $TAIL 
    sh -c "$COMMAND"
    if [ $? -ne 0 ] ; then 
        echo $1: line $ROW: Failed executing command. >&2
        exit 1
    fi

    LOOP=`expr $LOOP + 1`
    PREV_ROW=$ROW

done;

TAIL=`expr $LINES - $PREV_ROW`
cat $1 | tail -n $TAIL 
