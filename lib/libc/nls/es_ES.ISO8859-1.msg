$ $FreeBSD$
$
$ Message catalog for es_ES.ISO8859-1 locale
$
$ strerror() support catalog
$
$set 1
$ EPERM
1 Operación no permitida
$ ENOENT
2 Fichero o directorio inexistente
$ ESRCH
3 Proceso inexistente
$ EINTR
4 Llamada del sistema interrumpida
$ EIO
5 Error de Entrada/Salida
$ ENXIO
6 Dispositivo no configurado
$ E2BIG
7 La lista de argumentos es demasiado larga
$ ENOEXEC
8 Error en el formato del ejecutable
$ EBADF
9 Descriptor incorrecto de fichero
$ ECHILD
10 No hay procesos hijo
$ EDEADLK
11 Se ha evitado el bloqueo del recurso
$ ENOMEM
12 No se pudo asignar memoria
$ EACCES
13 Permiso denegado
$ EFAULT
14 Dirección incorrecta
$ ENOTBLK
15 Se necesita un dispositivo de bloques
$ EBUSY
16 Dispositivo ocupado
$ EEXIST
17 El fichero ya existe
$ EXDEV
18 Enlace entre dispositivos
$ ENODEV
19 Operación inadecuada para este dispositivo
$ ENOTDIR
20 No es un directorio
$ EISDIR
21 Es un directorio
$ EINVAL
22 Argumento inadecuado
$ ENFILE
23 Hay demasiados ficheros abiertos en el sistema
$ EMFILE
24 Hay demasiados ficheros abiertos
$ ENOTTY
25 ioctl inapropiado para el dispositivo
$ ETXTBSY
26 Fichero de texto ocupado
$ EFBIG
27 Fichero demasiado grande
$ ENOSPC
28 No queda espacio libre en el dispositivo
$ ESPIPE
29 Búsqueda ilegal
$ EROFS
30 Sistema de ficheros de solo lectura
$ EMLINK
31 Demasiados enlaces
$ EPIPE
32 Canal (pipe) roto
$ EDOM
33 Argumento numérico fuera de rango
$ ERANGE
34 El resultado es demasiado grande
$ EAGAIN, EWOULDBLOCK
35 el recurso no está disponible temporalmente
$ EINPROGRESS
36 Operación en proceso
$ EALREADY
37 La operación ya se está ejecutando
$ ENOTSOCK
38 Operación de socket inaceptable para el dispositivo
$ EDESTADDRREQ
39 Se necesita una dirección de destino
$ EMSGSIZE
40 Mensaje demasiado largo
$ EPROTOTYPE
41 Tipo erróneo de protocolo para el socket
$ ENOPROTOOPT
42 Protocolo no disponible
$ EPROTONOSUPPORT
43 Protocolo no contemplado
$ ESOCKTNOSUPPORT
44 Tipo de socket no contemplado
$ EOPNOTSUPP
45 Operación no contemplada
$ EPFNOSUPPORT
46 Familia de protocolos no contemplada
$ EAFNOSUPPORT
47 Familia de direcciones no contemplada por la familia de protocolos
$ EADDRINUSE
48 La dirección ya está siendo usada
$ EADDRNOTAVAIL
49 No se pudo asignar la dirección requerida
$ ENETDOWN
50 La red no funciona
$ ENETUNREACH
51 No se ha podido acceder a la red
$ ENETRESET
52 La conexión de red se ha interrumpido al reinicializar
$ ECONNABORTED
53 Conexión perdida por problemas en el software
$ ECONNRESET
54 El interlocutor ha reinicializado la conexión
$ ENOBUFS
55 No queda espacio en el búfer
$ EISCONN
56 El socket ya estaba conectado
$ ENOTCONN
57 El socket no está conectado
$ ESHUTDOWN
58 No se puede enviar tras la desconexión del socket
$ ETOOMANYREFS
59 Demasiadas referencias: no se pueden unir
$ ETIMEDOUT
60 El tiempo de conexión ha expirado
$ ECONNREFUSED
61 Conexión rehusada
$ ELOOP
62 Demasiados niveles de enlaces simbólicos
$ ENAMETOOLONG
63 Nombre de fichero demasiado largo
$ EHOSTDOWN
64 La máquina está fuera de servicio
$ EHOSTUNREACH
65 No hay ruta hasta la máquina
$ ENOTEMPTY
66 Directorio no vacío
$ EPROCLIM
67 Demasiados procesos
$ EUSERS
68 Demasiados usuarios
$ EDQUOT
69 Cuota de disco sobrepasada
$ ESTALE
70 Descriptor de fichero NFS inválido
$ EREMOTE
71 Ruta con demasiados niveles
$ EBADRPC
72 La estructura de la RPC es errónea
$ ERPCMISMATCH
73 La versón de la RPC es errónea
$ EPROGUNAVAIL
74 La RPC no está accesible
$ EPROGMISMATCH
75 Versión errónea del programa
$ EPROCUNAVAIL
76 Procedimiento erróneo para el programa
$ ENOLCK
77 No hay bloqueos disponibles
$ ENOSYS
78 Función no realizada
$ EFTYPE
79 Tipo de fichero o formato inapropiado
$ EAUTH
80 Error de autentificación
$ ENEEDAUTH
81 Se necesita un autenticador
$ EIDRM
82 Identificador eliminado
$ ENOMSG
83 No hay mensajes del tipo deseado
$ EOVERFLOW
84 Valor demasiado grande para almacenarse en el tipo deseado
$ ECANCELED
85 Operación cancelada
$ EILSEQ
86 Secuencia de bytes ilegal
$ ENOATTR
87 Atributo no encontrado
$ EDOOFUS
88 Error de programación
$ EBADMSG
89 Mensaje inválido
$ EMULTIHOP
90 Intento de hop multiple
$ ENOLINK
91 El enlace se ha roto
$ EPROTO
92 Fallo de protocolo
$ ENOTCAPABLE
93 Habilidades insuficientes
$
$ strsignal() support catalog
$
$set 2
$ SIGHUP
1 Fin de línea (Hangup)
$ SIGINT
2 Interrumpido
$ SIGQUIT
3 Terminado
$ SIGILL
4 Instrucción ilegal
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
10 Error en el bus
$ SIGSEGV
11 Fallo de segmentación
$ SIGSYS
12 Llamada al sistema errónea
$ SIGPIPE
13 Canal (pipe) roto
$ SIGALRM
14 Alarma del reloj
$ SIGTERM
15 Terminado
$ SIGURG
16 Condición urgente de E/S
$ SIGSTOP
17 Detenido (señal)
$ SIGTSTP
18 Detenido
$ SIGCONT
19 Continuando
$ SIGCHLD
20 Proceso hijo terminado
$ SIGTTIN
21 Detenido (entrada tty)
$ SIGTTOU
22 Detenido (salida tty)
$ SIGIO
23 E/S posible
$ SIGXCPU
24 Se ha sobrepasado el tiempo límite de la CPU
$ SIGXFSZ
25 Se ha sobrepasado el límite de tamaño de fichero
$ SIGVTALRM
26 Temporizador virtual expirado
$ SIGPROF
27 Temporizador de perfilación expirado
$ SIGWINCH
28 Cambios en el tamaño de ventana
$ SIGINFO
29 Petición de información
$ SIGUSR1
30 Señal definida por el usuario n1
$ SIGUSR2
31 Señal definida por el usuario n2
$
$ gai_strerror() support catalog
$
$set 3
$ 1 (obsoleto)
1 Tipo de dirección no contemplado
$ EAI_AGAIN
2 Error transitorio en la resolución de nombres
$ EAI_BADFLAGS
3 Valor inválido de ai_flags
$ EAI_FAIL
4 Error no recuperable en la resolución de nombres
$ EAI_FAMILY
5 ai_family no contemplado
$ EAI_MEMORY
6 Error en la asignación de memoria
$ 7 (obsoleto)
7 No hay dirección asociada con el nombre de máquina
$ EAI_NONAME
8 No se dispone nombre de máquina, ni nombre de servicio
$ EAI_SERVICE
9 Nombre de servicio no contemplado en ai_socktype
$ EAI_SOCKTYPE
10 ai_socktype no contemplado
$ EAI_SYSTEM
11 Error de sistema devuelto en errno
$ EAI_BADHINTS
12 Valor inválido de hints
$ EAI_PROTOCOL
13 Protocolo resuelto desconocido
$ EAI_OVERFLOW
14 Búfer de argumentos sobrepasado
$ 0
32766 Éxito
$ NL_MSGMAX
32767 Error desconocido
