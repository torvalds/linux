name: break-1
description:
	See if break breaks out of loops
stdin:
	for i in a b c; do echo $i; break; echo bad-$i; done
	echo end-1
	for i in a b c; do echo $i; break 1; echo bad-$i; done
	echo end-2
	for i in a b c; do
	    for j in x y z; do
		echo $i:$j
		break
		echo bad-$i
	    done
	    echo end-$i
	done
	echo end-3
expected-stdout:
	a
	end-1
	a
	end-2
	a:x
	end-a
	b:x
	end-b
	c:x
	end-c
	end-3
---

name: break-2
description:
	See if break breaks out of nested loops
stdin:
	for i in a b c; do
	    for j in x y z; do
		echo $i:$j
		break 2
		echo bad-$i
	    done
	    echo end-$i
	done
	echo end
expected-stdout:
	a:x
	end
---


name: break-3
description:
	What if break used outside of any loops
	(ksh88,ksh93 don't print error messages here)
stdin:
	break
expected-stderr-pattern:
	/.*break.*/
---


name: break-4
description:
	What if break N used when only N-1 loops
	(ksh88,ksh93 don't print error messages here)
stdin:
	for i in a b c; do echo $i; break 2; echo bad-$i; done
	echo end
expected-stdout:
	a
	end
expected-stderr-pattern:
	/.*break.*/
---


name: break-5
description:
	Error if break argument isn't a number
stdin:
	for i in a b c; do echo $i; break abc; echo more-$i; done
	echo end
expected-stdout:
	a
expected-exit: e != 0
expected-stderr-pattern:
	/.*break.*/
---


name: continue-1
description:
	See if continue continues loops
stdin:
	for i in a b c; do echo $i; continue; echo bad-$i ; done
	echo end-1
	for i in a b c; do echo $i; continue 1; echo bad-$i; done
	echo end-2
	for i in a b c; do
	    for j in x y z; do
		echo $i:$j
		continue
		echo bad-$i-$j
	    done
	    echo end-$i
	done
	echo end-3
expected-stdout:
	a
	b
	c
	end-1
	a
	b
	c
	end-2
	a:x
	a:y
	a:z
	end-a
	b:x
	b:y
	b:z
	end-b
	c:x
	c:y
	c:z
	end-c
	end-3
---


name: continue-2
description:
	See if continue breaks out of nested loops
stdin:
	for i in a b c; do
	    for j in x y z; do
		echo $i:$j
		continue 2
		echo bad-$i-$j
	    done
	    echo end-$i
	done
	echo end
expected-stdout:
	a:x
	b:x
	c:x
	end
---


name: continue-3
description:
	What if continue used outside of any loops
	(ksh88,ksh93 don't print error messages here)
stdin:
	continue
expected-stderr-pattern:
	/.*continue.*/
---


name: continue-4
description:
	What if continue N used when only N-1 loops
	(ksh88,ksh93 don't print error messages here)
stdin:
	for i in a b c; do echo $i; continue 2; echo bad-$i; done
	echo end
expected-stdout:
	a
	b
	c
	end
expected-stderr-pattern:
	/.*continue.*/
---


name: continue-5
description:
	Error if continue argument isn't a number
stdin:
	for i in a b c; do echo $i; continue abc; echo more-$i; done
	echo end
expected-stdout:
	a
expected-exit: e != 0
expected-stderr-pattern:
	/.*continue.*/
---


