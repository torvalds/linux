#
# RT-Mutex test
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
# signal	0
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
# 2 threads 1 lock
#
C: resetevent:		0: 	0
W: opcodeeq:		0: 	0

# Set schedulers
C: schedfifo:		0: 	80
C: schedfifo:		1: 	80

# T0 lock L0
C: locknowait:		0: 	0
C: locknowait:		1:	0
W: locked:		0: 	0
W: blocked:		1: 	0
T: prioeq:		0: 	80

# T0 unlock L0
C: unlock:		0: 	0
W: locked:		1: 	0

# Verify T0
W: unlocked:		0: 	0
T: prioeq:		0: 	80

# Unlock
C: unlock:		1: 	0
W: unlocked:		1: 	0

# T1,T0 lock L0
C: locknowait:		1: 	0
C: locknowait:		0:	0
W: locked:		1: 	0
W: blocked:		0: 	0
T: prioeq:		1: 	80

# T1 unlock L0
C: unlock:		1: 	0
W: locked:		0: 	0

# Verify T1
W: unlocked:		1: 	0
T: prioeq:		1: 	80

# Unlock and exit
C: unlock:		0: 	0
W: unlocked:		0: 	0

