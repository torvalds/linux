#  Copyright (C) 2003,2004 Free Software Foundation, Inc.
#  Contributed by Kelley Cook, June 2004.
#  Original code from Neil Booth, May 2003.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2, or (at your option) any
# later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

# This Awk script takes a list of *.opt files and combines them into 
# a three-field sorted list suitable for input into opt[ch]-gen.awk.
#
# Usage: awk -f opt-gather.awk file1.opt [...] > outputfile

function sort(ARRAY, ELEMENTS)
{
	for (i = 2; i <= ELEMENTS; ++i) {
		for (j = i; ARRAY[j-1] > ARRAY[j]; --j) {
			temp = ARRAY[j]
			ARRAY[j] = ARRAY[j-1]
			ARRAY[j-1] = temp
		}
	}
	return
}

BEGIN {	numrec = 0 }

# Ignore comments and blank lines
/^[ \t]*(;|$)/  { flag = 0; next }
/^[^ \t]/       { if (flag == 0) {
                    record[++numrec] = $0
		    flag = 1 }
		  else {
		    record[numrec] = record[numrec] SUBSEP $0
	          }
}

# Sort it and output it
END {
	sort(record,numrec)
	
	for (i = 1; i <= numrec; i++) {
		print record[i] }
}
