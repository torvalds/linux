#!/usr/bin/awk
/^[^#]/ {
	gsub(/^\./,"", $1)
	uname = gname = mode = flags = tags = type = ""
	for (i=2; i<=NF; i++) {
		if ($i ~ /^uname=/) {
			uname=$i
			gsub(/uname=/, "", uname)
		} else if ($i ~ /^gname=/) {
			gname=$i
			gsub(/gname=/, "", gname)
		} else if ($i ~ /^mode=/) {
			mode=$i
			gsub(/mode=/,"", mode)
		} else if ($i ~ /^flags=/) {
			flags=$i
			gsub(/flags=/, "", flags)
		} else if ($i ~ /^tags=/) {
			tags=$i
			gsub(/tags=/, "", tags)
		} else if ($i ~ /^type=dir/) {
			type="dir"
		}
	}
	if (kernel != "") {
		tags="package=kernel"
		if (_kernconf != "") {
			tags=tags""_kernconf
		}
	}
	if (length(tags) == 0)
		next
	if (tags ~ /package=/) {
		ext = pkgname = pkgend = ""
		split(tags, a, ",");
		for (i in a) {
			if (a[i] ~ /^package=/) {
				pkgname=a[i]
				gsub(/package=/, "", pkgname)
			} else if (a[i] == "config") {
				type="config"
			} else if (a[i] == "development" || a[i] == "profile" || a[i] == "debug" || a[i] == "docs") {
				pkgend=a[i]
			} else {
				if (ext != "")
					ext=ext"-"a[i]
				else
					ext=a[i]
			}
		}
		if (ext != "") {
			pkgname=pkgname"-"ext
		}
		if (pkgend != "") {
			if (pkgend == "docs") {
				pkgname=pkgend
			} else {
				pkgname=pkgname"-"pkgend
			}
		}
	} else {
		print "No packages specified in line: $0"
		next
	}
	if (kernel != "") {
		output="kernel"
		if (_kernconf != "") {
			output=output"."_kernconf
		}
		if ($1 ~ /^\/usr\/lib\/debug\/boot/) {
			output=output"-debug.plist"
		} else {
			output=output".plist"
		}
	} else {
		output=pkgname".plist"
	}

	print "@"type"("uname","gname","mode","flags") " $1 > output
}
