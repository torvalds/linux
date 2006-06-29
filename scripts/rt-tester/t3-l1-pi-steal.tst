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
# 3 threads 1 lock PI steal pending ownership
#
C: resetevent:		0: 	0
W: opcodeeq:		0: 	0

# Set schedulers
C: schedother:		0: 	0
C: schedfifo:		1: 	80
C: schedfifo:		2: 	81

# T0 lock L0
C: lock:		0: 	0
W: locked:		0: 	0

# T1 lock L0
C: lock:		1: 	0
W: blocked:		1: 	0
T: prioeq:		0: 	80

# T0 unlock L0
C: unlock:		0: 	0

# Wait until T1 is in the wakeup loop
W: blockedwake:		1: 	0
T: priolt:		0: 	1

# T2 lock L0
C: lock:		2: 	0
# T1 leave wakeup loop
C: lockcont:		1: 	0

# T2 must have the lock and T1 must be blocked
W: locked:		2: 	0
W: blocked:		1: 	0

# T2 unlock L0
C: unlock:		2: 	0

# Wait until T1 is in the wakeup loop and let it run
W: blockedwake:		1: 	0
C: lockcont:		1: 	0
W: locked:		1: 	0
C: unlock:		1: 	0
W: unlocked:		1: 	0
