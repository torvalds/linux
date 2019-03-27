#!/bin/sh
#
# Copyright (c) 2014 Juniper Networks, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#

#
# Convert a NIC driver to use the procdural API.
# It doesn't take care of all the # cases yet,
# but still does about 95% of work.
#
# Author: Sreekanth Rupavatharam
#

if [ $# -lt 1 ]
then
	echo " $0 <driver source (e.g., if_em.c)>";
	exit 1;
fi

# XXX - This needs to change if the data structure uses different name
__ifp__="ifp";

file=$1

rotateCursor() {
  case $toggle
  in
    1)
      printf " \\ "
      printf "\b\b"
      toggle="2"
    ;;

    2)
      printf " | "
      printf "\b\b\b"
      toggle="3"
    ;;

    3)
      printf " / "
      printf "\b\b\b"
      toggle="4"
    ;;

    *)
      printf " - "
      printf "\b\b\b"
      toggle="1"
    ;;
  esac
}

handle_set() {
# Handle the case where $__ifp__->if_blah = XX;
	line=$1
	set=`echo $line| grep "$__ifp__->.* = "`
	if [ ! -z "$set" ]
	then
		word=`echo $line | awk -F "if_" ' { print $2 }' | awk -F" =" '{ print $1 }'`
		value=`echo $line | awk -F "=" '{ print $2 }' | sed -e 's/;//g'`
		new=`echo if_set$word"\($__ifp__,"$value");"`
		new=`echo $new | sed -e 's/&/\\\&/'`
		old=`echo $line|sed -e 's/^[ 	]*//'`
		line=`echo $line| sed -e's/'$old'/'$new'/g'`
		return 0
	fi
	return 1
}

handle_inc() {
	line=$1	
	inc=`echo $line | grep "$__ifp__->.*++\|++$__ifp__->.*"`
	if [ ! -z "$inc" ]
	then
		word=`echo $line | awk -F"if_" '{ print $2 }'|awk -F"\+" '{ print $1}'`
		value=' 1';
		old=`echo $line|sed -e 's/^[ 	]*//'`
		new=`echo if_inc$word"\($__ifp__,"$value");"`
		new=`echo $new | sed -e 's/&/\\\&/'`
		line=`echo $line| sed -e's/'$old'/'$new'/g'`
		return 0;
	fi	
	return 1;
}

handle_add() {
	line=$1
	add=`echo $line|grep "$__ifp__->.*+= "`
	if [ ! -z "$add" ]
	then
		word=`echo $line | awk -F"if_" '{ print $2 }'|awk '{ print $1}'`
		value=`echo $line | awk -F"=" '{ print $2}' | sed -e 's/;//g'`
		new=`echo if_inc$word"\($__ifp__,$value);"`
		new=`echo $new | sed -e 's/&/\\\&/'`
		old=`echo $line|sed -e 's/^[ 	]*//'`
		line=`echo $line| sed -e's/'$old'/'$new'/g'`
		return 0
	fi
	return 1;

}

handle_or() {
	line=$1
	or=`echo $line|grep "$__ifp__->.*|= "`
	if [ ! -z "$or" ]
	then
		word=`echo $line | awk -F"if_" '{ print $2 }'|awk '{ print $1}'`	
		value=`echo $line | awk -F"=" '{ print $2}' | sed -e 's/;//g'`
		new=`echo if_set${word}bit"($__ifp__,$value, 0);"`
		new=`echo $new | sed -e 's/&/\\\&/'` 
		#line=`echo $line|sed -e 's/&/\\&/'`
		old=`echo $line|sed -e 's/^[ 	]*//'`
		line=`echo $line| sed -e's/'$old'/'$new'/g'`
		return 0;
	fi
	return 1;

}

handle_and() {
	line=$1
	or=`echo $line|grep "$__ifp__->.*&= "`
	if [ ! -z "$or" ]
	then
		word=`echo $line | awk -F"if_" '{ print $2 }'|awk '{ print $1}'`	
		value=`echo $line | awk -F"=" '{ print $2}' | sed -e 's/;//g'`
		value=`echo $value | sed -e's/~//g'`
		new=`echo if_set${word}bit"\($__ifp__, 0,$value);"`
		new=`echo $new | sed -e 's/&/\\\&/'`
		old=`echo $line|sed -e 's/^[ 	]*//'`
		line=`echo $line| sed -e's/'$old'/'$new'/g'`
		return 0;
	fi
	return 1;

}

handle_toggle() {
	line=$1
	if [ ! -z `echo $line | grep "\^="` ]
	then
		line=`echo $line | sed -e 's/'"$__ifp__"'->if_\(.*\) ^=\(.*\);/if_toggle\1('"$__ifp__"',\2);/g'`
		return 0;
	fi
	return 1

}

# XXX - this needs updating
handle_misc() {
	line=$1
	get=`echo $line | grep "if_capabilities\|if_flags\|if_softc\|if_capenable\|if_mtu\|if_drv_flags"`
	if [ ! -z "$get" ]
	then
		word=`echo $line |awk -F"$__ifp__->if_" '{ print $2 }' | \
			sed -e's/[^a-zA-Z0-9_]/\@/'|awk -F"\@" '{ print $1}'`
		old=`echo "$__ifp__->if_"${word}`
		new=`echo "if_get"${word}"($__ifp__)"`
		new=`echo $new | sed -e 's/&/\\\&/'`
		line=`echo $line| sed -e's/'$old'/'$new'/g'`
		return 0;
	fi
	return 1;

}

replace_str ()
{
	line=$1
	orig=$2
	new=$3
	line=`echo $line | sed -e 's/'"$orig"'\(.*\)/'"$new"'\1/g'`
	return 0;
}

# Handle special cases which do not fall under regular patterns
handle_special ()
{
	line=$1
	replace_str $line "(\*$__ifp__->if_input)" "if_input"
	replace_str $line "if_setinit" "if_setinitfn"
	replace_str $line "if_setioctl" "if_setioctlfn"
	replace_str $line "if_getdrv_flags" "if_getdrvflags"
	replace_str $line "if_setdrv_flagsbit" "if_setdrvflagbits"
	replace_str $line "if_setstart" "if_setstartfn"
	replace_str $line "if_sethwassistbit" "if_sethwassistbits"
	replace_str $line "ifmedia_init" "ifmedia_init_drv"
	replace_str $line "IFQ_DRV_IS_EMPTY(&$__ifp__->if_snd)" "if_sendq_empty($__ifp__)"
	replace_str $line "IFQ_DRV_PREPEND(&$__ifp__->if_snd" "if_sendq_prepend($__ifp__"
	replace_str $line "IFQ_SET_READY(&ifp->if_snd)" "if_setsendqready($__ifp__)"
	line=`echo $line | sed -e 's/IFQ_SET_MAXLEN(&'$__ifp__'->if_snd, \(.*\))/if_setsendqlen('$__ifp__', \1)/g'`
	line=`echo $line | sed -e 's/IFQ_DRV_DEQUEUE(&'$__ifp__'->if_snd, \(.*\))/\1 = if_dequeue('$__ifp__')/g'`
	return 0
}

if [ -e $file.tmp ]
then
	rm $file.tmp
fi
IFS=
echo -n "Conversion for $file started, please wait: "
FAIL_PAT="XXX - DRVAPI"
count=0
cat $1 | while read -r line
do
count=`expr $count + 1`
rotateCursor 
pat=`echo $line | grep "$__ifp__->"`
while  [ "$pat" != "" ]
do
	pat=`echo $line | grep "$__ifp__->"`
	if [ ! -z `echo $pat | grep "$FAIL_PAT"` ]
	then
		break;
	fi

	handle_set $line

	if [ $? != 0 ]
	then 
		handle_inc $line
	fi

	if [ $? != 0 ]
	then
		handle_add $line
	fi

	if [ $? != 0 ]
	then
		handle_or $line
	fi

	if [ $? != 0 ]
	then
		handle_and $line
	fi

	if [ $? != 0 ]
	then
		handle_toggle $line
	fi

	if [ $? != 0 ]
	then
		handle_misc $line
	fi
	
	if [ $? != 0 ]
	then
		handle_special $line
	fi	

	if [ ! -z `echo $line | grep "$__ifp__->"` ]
	then
		line=`echo $line | sed -e 's:$: \/* '${FAIL_PAT}' *\/:g'`
	fi
done
	line=`echo "$line" | sed -e 's:VLAN_CAPABILITIES('$__ifp__'):if_vlancap('$__ifp__'):g'`
	# Replace the ifnet * with if_t
	if [ ! -z `echo $line | grep "struct ifnet"` ]
	then
		line=`echo $line | sed -e 's/struct ifnet[ \t]*\*/if_t /g'`
	fi
	echo "$line" >> $file.tmp
done
echo ""
count=`grep $FAIL_PAT $file.tmp | wc -l`
if [ $count -gt 0 ]
then
	echo "$count lines could not be converted to DRVAPI"
	echo "Look for /* $FAIL_PAT */ in the converted file"
fi
echo "original $file  has been moved to $file.orig"
mv $file $file.orig
mv $file.tmp $file
