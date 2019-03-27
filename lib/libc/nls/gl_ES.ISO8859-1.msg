$ $FreeBSD$
$
$ Message catalog for gl_ES.ISO8859-1 locale
$
$ strerror() support catalog
$
$set 1
$ EPERM
1 Operación non permitida
$ ENOENT
2 Ficheiro ou directorio inexistente
$ ESRCH
3 Proceso inexistente
$ EINTR
4 Chamada do sistema interrompida
$ EIO
5 Erro de Entrada/Saída
$ ENXIO
6 Dispositivo non configurado
$ E2BIG
7 A lista de argumentos é demasiado larga
$ ENOEXEC
8 Erro no formato do executable
$ EBADF
9 Descriptor incorrecto de ficheiro
$ ECHILD
10 Non hai procesos fillos
$ EDEADLK
11 Evitouse o bloqueo do recurso
$ ENOMEM
12 Non se puido asignar memoria
$ EACCES
13 Permiso denegado
$ EFAULT
14 Dirección incorrecta
$ ENOTBLK
15 Precísase un dispositivo de bloques
$ EBUSY
16 Dispositivo ocupado
$ EEXIST
17 O ficheiro xa existe
$ EXDEV
18 Enlace entre dispositivos
$ ENODEV
19 Operación inadecuada para este dispositivo
$ ENOTDIR
20 Non é un directorio
$ EISDIR
21 É un directorio
$ EINVAL
22 Argumento inadecuado
$ ENFILE
23 Hai demasiados ficheiros abertos no sistema
$ EMFILE
24 Hai demasiados ficheiros abertos
$ ENOTTY
25 ioctl inapropiado para o dispositivo
$ ETXTBSY
26 Ficheiro de texto ocupado
$ EFBIG
27 Ficheiro demasiado grande
$ ENOSPC
28 Non queda espacio libre no dispositivo
$ ESPIPE
29 seek inválido
$ EROFS
30 Sistema de ficheiros de só lectura
$ EMLINK
31 Demasiados enlaces
$ EPIPE
32 Canal (pipe) roto
$ EDOM
33 Argumento numérico fóra de rango
$ ERANGE
34 O resultado é demasiado grande
$ EAGAIN, EWOULDBLOCK
35 O recurso non está dispoñible temporalmente
$ EINPROGRESS
36 Operación en proceso
$ EALREADY
37 A operación xa estase executando
$ ENOTSOCK
38 Operación de socket inaceptable para o dispositivo
$ EDESTADDRREQ
39 Precísase unha dirección de destino
$ EMSGSIZE
40 Mensaxe demasiado largo
$ EPROTOTYPE
41 Tipo malo de protocolo para o socket
$ ENOPROTOOPT
42 Protocolo non dispoñible
$ EPROTONOSUPPORT
43 Protocolo non contemplado
$ ESOCKTNOSUPPORT
44 Tipo de socket non contemplado
$ EOPNOTSUPP
45 Operación non contemplada
$ EPFNOSUPPORT
46 Familia de protocolos non contemplada
$ EAFNOSUPPORT
47 Familia de direcciones non contemplada pola familia de protocolos
$ EADDRINUSE
48 A dirección xa está en uso
$ EADDRNOTAVAIL
49 Non se puido asignar a dirección requerida
$ ENETDOWN
50 A rede non funciona
$ ENETUNREACH
51 Non se puido acceder á rede
$ ENETRESET
52 A conexión de rede interrompiuse ao reinicializar
$ ECONNABORTED
53 Conexión perdida por problemas no software
$ ECONNRESET
54 O interlocutor reinicializou a conexión
$ ENOBUFS
55 Non queda espacio no búfer
$ EISCONN
56 O socket xa estaba conectado
$ ENOTCONN
57 O socket non está conectado
$ ESHUTDOWN
58 Non se pode enviar tras a desconexión do socket
$ ETOOMANYREFS
59 Demasiadas referencias: non poden unirse
$ ETIMEDOUT
60 O tempo de conexión expirou
$ ECONNREFUSED
61 Conexión rexeitada
$ ELOOP
62 Demasiados niveles de enlaces simbólicos
$ ENAMETOOLONG
63 Nome de ficheiro demasiado largo
$ EHOSTDOWN
64 A máquina está fóra de servicio
$ EHOSTUNREACH
65 Non hai ruta ata a máquina
$ ENOTEMPTY
66 Directorio non baleiro
$ EPROCLIM
67 Demasiados procesos
$ EUSERS
68 Demasiados usuarios
$ EDQUOT
69 Cuota de disco sobrepasada
$ ESTALE
70 Descriptor de ficheiro NFS inválido
$ EREMOTE
71 Ruta con demasiados niveles
$ EBADRPC
72 A estructura da RPC é mala
$ ERPCMISMATCH
73 A versión da RPC é mala
$ EPROGUNAVAIL
74 A RPC non está accesible
$ EPROGMISMATCH
75 Versión mala do programa
$ EPROCUNAVAIL
76 Procedemento malo para o programa
$ ENOLCK
77 Non hai bloqueos dispoñibles
$ ENOSYS
78 Función non realizada
$ EFTYPE
79 Tipo de ficheiro ou formato inapropiado
$ EAUTH
80 Erro de autenticación
$ ENEEDAUTH
81 Precísase un autenticador
$ EIDRM
82 Identificador eliminado
$ ENOMSG
83 Non hai mensaxes do tipo desexado
$ EOVERFLOW
84 Valor demasiado grande para se almacenar no tipo desexado
$ ECANCELED
85 Operación cancelada
$ EILSEQ
86 Secuencia inválida de byte
$ ENOATTR
87 Atributo non encontrado
$ EDOOFUS
88 Erro de programación
$ EBADMSG
89 Mensaxe inválido
$ EMULTIHOP
90 Intento de Multihop
$ ENOLINK
91 Enlace fortaleceuse
$ EPROTO
92 Erro de protocolo
$ ENOTCAPABLE
93 Habilidades non suficientes
$
$ strsignal() support catalog
$
$set 2
$ SIGHUP
1 Fin de liña (Hangup)
$ SIGINT
2 Interrompido
$ SIGQUIT
3 Terminado
$ SIGILL
4 Instrucción inválida
$ SIGTRAP
5 Trace/BPT trap
$ SIGABRT
6 Abort trap
$ SIGEMT
7 EMT trap
$ SIGFPE
8 Excepción de coma flotante
$ SIGKILL
9 Matado
$ SIGBUS
10 Erro no bus
$ SIGSEGV
11 Fallo de segmentación
$ SIGSYS
12 Chamada ao sistema mala
$ SIGPIPE
13 Canal (pipe) roto
$ SIGALRM
14 Alarma do reloxo
$ SIGTERM
15 Terminado
$ SIGURG
16 Condición urxente de E/S
$ SIGSTOP
17 Detido (sinal)
$ SIGTSTP
18 Detido
$ SIGCONT
19 Continuando
$ SIGCHLD
20 Proceso fillo terminado
$ SIGTTIN
21 Detido (entrada tty)
$ SIGTTOU
22 Detido (sída tty)
$ SIGIO
23 E/S posible
$ SIGXCPU
24 Sobrepasouse o tempo límite da CPU
$ SIGXFSZ
25 Sobrepasouse o límite de tamaño de ficheiro
$ SIGVTALRM
26 Temporizador virtual caducado
$ SIGPROF
27 Temporizador de perfilación caducado
$ SIGWINCH
28 Cambios no tamaño de xanela
$ SIGINFO
29 Petición de información
$ SIGUSR1
30 Sinal definida polo usuario no. 1
$ SIGUSR2
31 Sinal definida polo usuario no. 2
$
$ gai_strerror() support catalog
$
$set 3
$ 1 (obsoleto)
1 Tipo de dirección non soportado
$ EAI_AGAIN
2 Erro temporal na resolución de nomes
$ EAI_BADFLAGS
3 Valor inválido de ai_flags
$ EAI_FAIL
4 Erro non recuperable na resolución de nomes
$ EAI_FAMILY
5 ai_family non soportado
$ EAI_MEMORY
6 Erro na asignación de memoria
$ 7 (obsoleto)
7 Non hai dirección asociada co nome de máquina
$ EAI_NONAME
8 Non se dispón nome de máquina, nin nome de servizo
$ EAI_SERVICE
9 Nome de servizo non soportado en ai_socktype
$ EAI_SOCKTYPE
10 ai_socktype non soportado
$ EAI_SYSTEM
11 Erro de sistema devolto en errno
$ EAI_BADHINTS
12 Valor inválido de hints
$ EAI_PROTOCOL
13 Protocolo resolto descoñecido
$ EAI_OVERFLOW
14 Búfer de argumentos excedido
$ 0
32766 Éxito
$ NL_MSGMAX
32767 Erro descoñecido
