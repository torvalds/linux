#
# rt-mutex test
#
# Op: C(ommand)/T(est)/W(ait)
# |  opcode
# |  |     threadid: 0-7
# |  |     |  opcode argument
# |  |     |  |
# C: lock: 0: 0
#
# Commands
#
# opcode	opcode argument
# schedother	nice value
# schedfifo	priority
# lock		lock nr (0-7)
# locknowait	lock nr (0-7)
# lockint	lock nr (0-7)
# lockintnowait	lock nr (0-7)
# lockcont	lock nr (0-7)
# unlock	lock nr (0-7)
# signal	thread to signal (0-7)
# reset		0
# resetevent	0
#
# Tests / Wait
#
# opcode	opcode argument
#
# prioeq	priority
# priolt	priority
# priogt	priority
# nprioeq	normal priority
# npriolt	normal priority
# npriogt	normal priority
# locked	lock nr (0-7)
# blocked	lock nr (0-7)
# blockedwake	lock nr (0-7)
# unlocked	lock nr (0-7)
# opcodeeq	command opcode or number
# opcodelt	number
# opcodegt	number
# eventeq	number
# eventgt	number
# eventlt	number

#
# 4 threads 2 lock PI
#
C: resetevent:		0: 	0
W: opcodeeq:		0: 	0

# Set schedulers
C: schedother:		0: 	0
C: schedother:		1: 	0
C: schedfifo:		2: 	82
C: schedfifo:		3: 	83

# T0 lock L0
C: locknowait:		0: 	0
W: locked:		0: 	0

# T1 lock L1
C: locknowait:		1: 	1
W: locked:		1: 	1

# T3 lock L0
C: lockintnowait:	3: 	0
W: blocked:		3: 	0
T: prioeq:		0: 	83

# T0 lock L1
C: lock:		0: 	1
W: blocked:		0: 	1
T: prioeq:		1: 	83

# T1 unlock L1
C: unlock:		1:	1

# Wait until T0 is in the wakeup code
W: blockedwake:		0:	1

# Verify that T1 is unboosted
W: unlocked:		1: 	1
T: priolt:		1: 	1

# T2 lock L1 (T0 is boosted and pending owner !)
C: locknowait:		2:	1
W: blocked:		2: 	1
T: prioeq:		0: 	83

# Interrupt T3 and wait until T3 returned
C: signal:		3:	0
W: unlocked:		3:	0

# Verify prio of T0 (still pending owner,
# but T2 is enqueued due to the previous boost by T3
T: prioeq:		0:	82

# Let T0 continue
C: lockcont:		0:	1
W: locked:		0:	1

# Unlock L1 and let T2 get L1
C: unlock:		0:	1
W: locked:		2:	1

# Verify that T0 is unboosted
W: unlocked:		0:	1
T: priolt:		0:	1

# Unlock everything and exit
C: unlock:		2:	1
W: unlocked:		2:	1

C: unlock:		0:	0
W: unlocked:		0:	0

