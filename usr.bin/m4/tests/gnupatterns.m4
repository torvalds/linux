dnl $FreeBSD$
patsubst(`string with a + to replace with a minus', `+', `minus')
patsubst(`string with aaaaa to replace with a b', `a+', `b')
patsubst(`+string with a starting + to replace with a minus', `^+', `minus')
