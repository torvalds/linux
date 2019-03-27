dnl $FreeBSD$
translit(`[HAVE_abc/def.h
]', `
/.', `/  ')
translit(`[HAVE_abc/def.h=]', `=/.', `/~~')
translit(`0123456789', `0123456789', `ABCDEFGHIJ')
translit(`0123456789', `[0-9]', `[A-J]')
translit(`abc-0980-zyx', `abcdefghijklmnopqrstuvwxyz', `ABCDEFGHIJKLMNOPQRSTUVWXYZ') 
translit(`abc-0980-zyx', `[a-z]', `[A-Z]') 
