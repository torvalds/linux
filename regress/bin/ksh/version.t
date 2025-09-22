name: version-1
description:
	Check version of shell.
category: pdksh
stdin:
	echo $KSH_VERSION
expected-stdout:
	@(#)PD KSH v5.2.14 99/07/13.2
---
