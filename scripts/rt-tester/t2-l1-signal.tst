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
# opcodeeq	command opcode or number
# opcodelt	number
# opcodegt	number
# eventeq	number
# eventgt	number
# eventlt	number

#
# 2 threads 1 lock with priority inversion
#
C: resetevent:		0: 	0
W: opcodeeq:		0: 	0

# Set schedulers
C: schedother:		0: 	0
C: schedother:		1: 	0

# T0 lock L0
C: locknowait:		0: 	0
W: locked:		0: 	0

# T1 lock L0
C: lockintnowait:	1: 	0
W: blocked:		1: 	0

# Interrupt T1
C: signal:		1:	0
W: unlocked:		1: 	0
T: opcodeeq:		1:	-4

# Unlock and exit
C: unlock:		0: 	0
W: unlocked:		0: 	0
