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
# lockbkl	lock nr (0-7)
# unlockbkl	lock nr (0-7)
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
# lockedbkl	dont care
# blockedbkl	dont care
# unlockedbkl	dont care
# opcodeeq	command opcode or number
# opcodelt	number
# opcodegt	number
# eventeq	number
# eventgt	number
# eventlt	number

#
# 5 threads 4 lock PI - modify priority of blocked threads
#
C: resetevent:		0: 	0
W: opcodeeq:		0: 	0

# Set schedulers
C: schedother:		0: 	0
C: schedfifo:		1: 	81
C: schedfifo:		2: 	82
C: schedfifo:		3: 	83
C: schedfifo:		4: 	84

# T0 lock L0
C: locknowait:		0: 	0
W: locked:		0: 	0

# T1 lock L1
C: locknowait:		1: 	1
W: locked:		1: 	1

# T1 lock L0
C: lockintnowait:	1: 	0
W: blocked:		1: 	0
T: prioeq:		0: 	81

# T2 lock L2
C: locknowait:		2: 	2
W: locked:		2: 	2

# T2 lock L1
C: lockintnowait:	2: 	1
W: blocked:		2: 	1
T: prioeq:		0: 	82
T: prioeq:		1:	82

# T3 lock L3
C: locknowait:		3: 	3
W: locked:		3: 	3

# T3 lock L2
C: lockintnowait:	3: 	2
W: blocked:		3: 	2
T: prioeq:		0: 	83
T: prioeq:		1:	83
T: prioeq:		2:	83

# T4 lock L3
C: lockintnowait:	4:	3
W: blocked:		4: 	3
T: prioeq:		0: 	84
T: prioeq:		1:	84
T: prioeq:		2:	84
T: prioeq:		3:	84

# Reduce prio of T4
C: schedfifo:		4: 	80
T: prioeq:		0: 	83
T: prioeq:		1:	83
T: prioeq:		2:	83
T: prioeq:		3:	83
T: prioeq:		4:	80

# Increase prio of T4
C: schedfifo:		4: 	84
T: prioeq:		0: 	84
T: prioeq:		1:	84
T: prioeq:		2:	84
T: prioeq:		3:	84
T: prioeq:		4:	84

# Reduce prio of T3
C: schedfifo:		3: 	80
T: prioeq:		0: 	84
T: prioeq:		1:	84
T: prioeq:		2:	84
T: prioeq:		3:	84
T: prioeq:		4:	84

# Increase prio of T3
C: schedfifo:		3: 	85
T: prioeq:		0: 	85
T: prioeq:		1:	85
T: prioeq:		2:	85
T: prioeq:		3:	85
T: prioeq:		4:	84

# Reduce prio of T3
C: schedfifo:		3: 	83
T: prioeq:		0: 	84
T: prioeq:		1:	84
T: prioeq:		2:	84
T: prioeq:		3:	84
T: prioeq:		4:	84

# Signal T4
C: signal:		4: 	0
W: unlocked:		4: 	3
T: prioeq:		0: 	83
T: prioeq:		1:	83
T: prioeq:		2:	83
T: prioeq:		3:	83

# Signal T3
C: signal:		3: 	0
W: unlocked:		3: 	2
T: prioeq:		0: 	82
T: prioeq:		1:	82
T: prioeq:		2:	82

# Signal T2
C: signal:		2: 	0
W: unlocked:		2: 	1
T: prioeq:		0: 	81
T: prioeq:		1:	81

# Signal T1
C: signal:		1: 	0
W: unlocked:		1: 	0
T: priolt:		0: 	1

# Unlock and exit
C: unlock:		3:	3
C: unlock:		2:	2
C: unlock:		1:	1
C: unlock:		0:	0

W: unlocked:		3:	3
W: unlocked:		2:	2
W: unlocked:		1:	1
W: unlocked:		0:	0

