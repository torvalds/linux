#!/bin/sh
#   convert_bootptab	Jeroen.Scheerder@let.ruu.nl 02/25/94
#	This script can be used to convert bootptab files in old format
#	to new (termcap-like) bootptab files
#
# The old format - real entries are commented out by '###'
#
# Old-style bootp files consist of two sections.
# The first section has two entries:
# First, a line that specifies the home directory
# (where boot file paths are relative to)

###/tftpboot

# The next non-empty non-comment line specifies the default bootfile

###no-file

# End of first section - indicated by '%%' at the start of the line

###%%

# The remainder of this file contains one line per client
# interface with the information shown by the table headings
# below. The host name is also tried as a suffix for the
# bootfile when searching the home directory (that is,
# bootfile.host)
#
# Note that htype is always 1, indicating the hardware type Ethernet.
# Conversion therefore always yields ':ha=ether:'.
#
# host	htype	haddr	iaddr	bootfile
#

###somehost	1	00:0b:ad:01:de:ad	128.128.128.128	dummy

# That's all for the description of the old format.
# For the new-and-improved format, see bootptab(5).

set -u$DX

case $#
in	2 )	OLDTAB=$1 ; NEWTAB=$2 ;;
	* )	echo "Usage: `basename $0` <Input> <Output>"
		exit 1
esac

if [ ! -r $OLDTAB ]
then
	echo "`basename $0`: $OLDTAB does not exist or is unreadable."
	exit 1
fi

if touch $NEWTAB 2> /dev/null
then
	:
else
	echo "`basename $0`: cannot write to $NEWTAB."
	exit 1
fi


cat << END_OF_HEADER >> $NEWTAB
# /etc/bootptab: database for bootp server (/etc/bootpd)
# This file was generated automagically

# Blank lines and lines beginning with '#' are ignored.
#
# Legend:	(see bootptab.5)
#	first field -- hostname (not indented)
#	bf -- bootfile
#	bs -- bootfile size in 512-octet blocks
#	cs -- cookie servers
#	df -- dump file name
#	dn -- domain name
#	ds -- domain name servers
#	ef -- extension file
#	gw -- gateways
#	ha -- hardware address
#	hd -- home directory for bootfiles
#	hn -- host name set for client
#	ht -- hardware type
#	im -- impress servers
#	ip -- host IP address
#	lg -- log servers
#	lp -- LPR servers
#	ns -- IEN-116 name servers
#	ra -- reply address
#	rl -- resource location protocol servers
#	rp -- root path
#	sa -- boot server address
#	sm -- subnet mask
#	sw -- swap server
#	tc -- template host (points to similar host entry)
#	td -- TFTP directory
#	to -- time offset (seconds)
#	ts -- time servers
#	vm -- vendor magic number
#	Tn -- generic option tag n
#
# Be careful about including backslashes where they're needed.  Weird (bad)
# things can happen when a backslash is omitted where one is intended.
# Also, note that generic option data must be either a string or a
# sequence of bytes where each byte is a two-digit hex value.

# First, we define a global entry which specifies the stuff every host uses.
# (Host name lookups are relative to the domain: your.domain.name)

END_OF_HEADER

# Fix up HW addresses in aa:bb:cc:dd:ee:ff and aa-bb-cc-dd-ee-ff style first
# Then awk our stuff together
sed -e  's/[:-]//g' < $OLDTAB | \
nawk 'BEGIN	{ PART = 0 ; FIELD=0 ; BOOTPATH="unset" ; BOOTFILE="unset" }
	/^%%/	{
				PART = 1
				printf ".default:\\\n\t:ht=ether:\\\n\t:hn:\\\n\t:dn=your.domain.name:\\\n\t:ds=your,dns,servers:\\\n\t:sm=255.255.0.0:\\\n\t:hd=%s:\\\n\t:rp=%s:\\\n\t:td=%s:\\\n\t:bf=%s:\\\n\t:to=auto:\n\n", BOOTPATH, BOOTPATH, BOOTPATH, BOOTFILE
				next
			}
	/^$/	{ next }
	/^#/	{ next }
		{
			if ( PART == 0 && FIELD < 2 )
		  	{
				if ( FIELD == 0 ) BOOTPATH=$1
				if ( FIELD == 1 ) BOOTFILE=$1
				FIELD++
			}
		}
		{
			if ( PART == 1 )
			{
				HOST=$1
				HA=$3
				IP=$4
				BF=$5
				printf "%s:\\\n\t:tc=.default:\\\n\t:ha=0x%s:\\\n\t:ip=%s:\\\n\t:bf=%s:\n", HOST, HA, IP, BF
			}
		}' >> $NEWTAB

exit 0
