#!/usr/local/bin/tclsh8.3
# $FreeBSD$

set fo [open _.html w]

proc do_file {file} {
	global names ops op
	set f [open $file]

	set s 0

	while {[gets $f a] >= 0} {
		if {$s == 0} {
			if {[regexp {struct[ 	]*vnodeopv_entry_desc} "$a"]} {
				regsub {.*vnodeopv_entry_desc[ 	]*} $a {} a
				regsub {\[.*} $a {} a
				regsub {_entries} $a {} a
				set name $a
				set names($a) 0
				set s 1
			}
			continue
		}
		if {$s == 1} {
			if {[regexp {NULL} "$a"]} {
				set s 0
				continue
			}
			if {![regexp {vop.*_desc} "$a"]} continue
			regsub -all {[,&]} $a " " a
			regsub -all {\(vop_t \*\)} $a " " a
			set b [lindex $a 0]
			if {"$b" == "/*"} {
				set s 0
				continue
			}
			#puts "$name>> [lindex $b 0] >> [lindex $b 3]"
			set o [lindex $b 0]
			regsub {_desc} $o "" o
			set ops($o) 0
			set op([list $name $o]) [lindex $b 1]
			continue
		}
		puts "$s>> $a"
	}
	close $f
}

set fi [open "|find /usr/src/sys -type f -name *.c -print | xargs grep VNODEOP_SET" ]
while {[gets $fi a] >= 0} {
	puts stderr $a
	if {[regexp {#define} $a]} continue
	if {[regexp {mallocfs} $a]} continue
	do_file [lindex [split $a :] 0]
}
close $fi

puts $fo {<HTML>
<HEAD></HEAD><BODY>
<TABLE BORDER WIDTH="100%" NOSAVE>
}

set opn [lsort [array names ops]]
set a [lsort [array names names]]

set tbn default_vnodeop
foreach i $a {
	if {$i == "default_vnodeop"} continue
	lappend tbn $i
}

foreach i $opn {
	if {$i == "vop_default"} continue
	regsub "vop_" $i "" i
        lappend fl [format "%12s" $i]
}

lappend fl [format "%12s" default]

puts $fo {<TR>}
puts $fo {<TD>}
puts $fo {</TD>}
puts $fo "<TR>"
        puts $fo "<TD></TD>"
	foreach j $fl {
		puts $fo "<TD>"

		for {set i 0} {$i < 12} {incr i} {
			puts $fo "[string index $j $i]<BR>"
		}
		puts $fo "</TD>"
	}
puts $fo "</TR>"

set fnames(vop_defaultop) *

set fnames(vop_null) -
set fnames(vop_ebadf) b
set fnames(vop_einval) i
set fnames(vop_enotty) t
set fnames(vop_panic) !
set fnames(vfs_cache_lookup) C
set fnames(vop_eopnotsupp) *

set fn 0
set nop(aa) 0
unset nop(aa)
foreach i $tbn {
	puts $fo {<TR>}
	puts $fo "<TD>$i</TD>"
	set pfx [lindex [split $i _] 0]
	foreach j $opn {
		if {$j == "vop_default"} continue
		set sfx [lindex [split $j _] 1]
		if {![info exists op([list $i $j])]} {
			puts $fo "<TD BGCOLOR=\"#d0d0d0\"></TD>"
			continue
		}
		set t $op([list $i $j])
	
		set c "#00ddd0"
		if {[info exists fnames($t)]} {
			set t $fnames($t)
		} elseif { $t == "${pfx}_${sfx}" } {
			set t "F"
		} elseif { $t == "vop_${sfx}" } {
			set t "V"
		} elseif { $t == "vop_no${sfx}" } {
			set t "N"
		} elseif { $t == "vop_std${sfx}" } {
			set t "S"
		} elseif { $sfx == "cachedlookup" && $t == "${pfx}_lookup" } {
			set t "L"
		} else {
			if {![info exists nop($t)]} {
				incr fn
				set nop($t) $fn
				set nfn($fn) $t
				set use($fn) 0
			}
			incr use($nop($t))
			set t "<FONT SIZE=-1>$nop($t)</FONT>"
			set c "#ffff00"
		}
		puts $fo "<TD BGCOLOR=\"$c\">$t</TD>"
	}
	set j vop_default
	if {![info exists op([list $i $j])]} {
		puts $fo "<TD></TD>"
		continue
	}
	puts $fo "<TD>$op([list $i $j])</TD>"

	puts $fo "</TR>"
}
puts $fo "</TABLE>"
puts $fo "<HR>"
puts $fo {<PRE>}
foreach i [lsort [array names fnames]] {
	puts $fo [format "%-2s %s" $fnames($i)  $i]
}
puts $fo [format "%-2s %s" "F" "<fs>_<vop>"]
puts $fo [format "%-2s %s" "V" "vop_<vop>"]
puts $fo [format "%-2s %s" "N" "vop_no<vop>"]
puts $fo [format "%-2s %s" "S" "vop_std<vop>"]
puts $fo [format "%-2s %s" "L" "<fs>_lookup"]
puts $fo {
</PRE>
}
puts $fo "<HR>"
puts $fo {<TABLE BORDER NOSAVE>}
set m 8
for {set i 1} {$i <= $fn} {incr i $m} {
	puts $fo "<TR>"
	for {set j 0} {$j < $m} {incr j} {
		set k [expr $i + $j]
		if {$k <= $fn} {
			#puts $fo "<TD>$k</TD><TD><FONT SIZE=-1>$nfn($k)/$use($k)</FONT></TD>"
			puts $fo "<TD>$k</TD><TD><FONT SIZE=-1>$nfn($k)</FONT></TD>"
		}
	}
	puts $fo "</TR>"
}
puts $fo "</TABLE>"

puts $fo "</TABLE>"
puts $fo "</BODY>"
puts $fo "</HTML>"
foreach i $tbn {
	if {$i == "default_vnodeop"} {
		continue
	}
	foreach j $opn {
		set sfx [lindex [split $j _] 1]
		if {![info exists op([list $i $j])]} {
			continue
		}
		set v $op([list $i $j])
		if {$v != "vop_std$sfx"} {
			continue
		}
		if {![info exists op([list default_vnodeop $j])]} {
			continue
		}
		if {$op([list default_vnodeop $j]) != $v} {
			continue
		}
		if {$op([list $i vop_default]) != "vop_defaultop"} {
			continue
		}
		puts "Suspect: uses explicit default, $i $j $v $op([list $i vop_default])"
	}
}
