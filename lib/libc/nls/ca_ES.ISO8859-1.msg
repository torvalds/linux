$ $FreeBSD$
$
$ Message catalog for ca_ES.ISO8859-1 locale
$
$ strerror() support catalog
$
$set 1
$ EPERM
1 Operació no permesa
$ ENOENT
2 Arxiu o directori inexistent
$ ESRCH
3 Procés inexistent
$ EINTR
4 Crida del sistema interrompuda
$ EIO
5 Error d'entrada/sortida
$ ENXIO
6 Dispositiu no configurat
$ E2BIG
7 Llista de paràmetres massa llarga
$ ENOEXEC
8 Error en el format de l'executable
$ EBADF
9 Descriptor d'arxiu incorrecte
$ ECHILD
10 No hi ha processos fills
$ EDEADLK
11 S'ha evitat el bloqueig del recurs
$ ENOMEM
12 No es pot assignar la memòria demanada
$ EACCES
13 Permís denegat
$ EFAULT
14 Adreça incorrecta
$ ENOTBLK
15 Es necessita un dispositiu de blocs
$ EBUSY
16 Dispositiu ocupat
$ EEXIST
17 L'arxiu ja existeix
$ EXDEV
18 Enllaç entre dispositius
$ ENODEV
19 Operació no suportada pel dispositiu
$ ENOTDIR
20 No és un directori
$ EISDIR
21 És un directori
$ EINVAL
22 Paràmetre incorrecte
$ ENFILE
23 Hi ha massa arxius oberts al sistema
$ EMFILE
24 Hi ha massa arxius oberts
$ ENOTTY
25 L'ioctl no és adecuat per al dispositiu
$ ETXTBSY
26 Arxiu de text ocupat
$ EFBIG
27 Arxiu massa gran
$ ENOSPC
28 No queda espai lliure en el dispositiu
$ ESPIPE
29 Cerca il·legal
$ EROFS
30 Sistema d'arxius de només lectura
$ EMLINK
31 Massa enllaços
$ EPIPE
32 Canal (pipe) trencat
$ EDOM
33 El resultat surt fora de rang
$ ERANGE
34 Resultat massa gran
$ EAGAIN, EWOULDBLOCK
35 El recurs no està disponible temporalment
$ EINPROGRESS
36 L'operació es troba en progrés actualment
$ EALREADY
37 L'operació ja es troba en progrés
$ ENOTSOCK
38 Operació de tipus socket en quelcom que no ho és
$ EDESTADDRREQ
39 Es requereix l'adreça de destí
$ EMSGSIZE
40 Missatge massa llarg
$ EPROTOTYPE
41 Tipus de protocol incorrecte per al socket
$ ENOPROTOOPT
42 Protocol no disponible
$ EPROTONOSUPPORT
43 Protocol no suportat
$ ESOCKTNOSUPPORT
44 Tipus de socket no suportat
$ EOPNOTSUPP
45 Operació no suportada
$ EPFNOSUPPORT
46 Família de protocols no suportada
$ EAFNOSUPPORT
47 Família d'adreces no suportada per la família de protocols
$ EADDRINUSE
48 L'adreça ja es troba en ús
$ EADDRNOTAVAIL
49 No es pot assignar l'adreça demanada
$ ENETDOWN
50 La xarxa no es troba disponible
$ ENETUNREACH
51 No es pot accedir a la xarxa
$ ENETRESET
52 La connexió a la xarxa s'ha perdut durant la reinicialització
$ ECONNABORTED
53 El programari ha causat l'avort de la connexió
$ ECONNRESET
54 L'interlocutor ha reinicialitzat la comunicació
$ ENOBUFS
55 No hi ha prou espai per a la memoria intermèdia (buffer)
$ EISCONN
56 El socket ja es troba connectat
$ ENOTCONN
57 El socket no es troba connectat
$ ESHUTDOWN
58 No es pot enviar desprès de la desconnexió del socket
$ ETOOMANYREFS
59 Hi ha massa referències: no es poden unir
$ ETIMEDOUT
60 El temps de connexió s'ha esgotat
$ ECONNREFUSED
61 Connexió rebutjada
$ ELOOP
62 Hi ha massa nivells d'enllaços simbòlics
$ ENAMETOOLONG
63 Nom d'arxiu massa llarg
$ EHOSTDOWN
64 La màquina no es troba disponible
$ EHOSTUNREACH
65 No hi ha cap camí fins a la màquina
$ ENOTEMPTY
66 El directori no està buit
$ EPROCLIM
67 Hi ha massa processos
$ EUSERS
68 Hi ha massa usuaris
$ EDQUOT
69 Quota de disc sobrepassada
$ ESTALE
70 Descriptor d'arxiu NFS incorrecte
$ EREMOTE
71 Massa nivells en el camí de destí
$ EBADRPC
72 L'estructura RPC es incorrecta
$ ERPCMISMATCH
73 La versió del RPC es incorrecta
$ EPROGUNAVAIL
74 El programa RPC no es troba disponible
$ EPROGMISMATCH
75 Versió incorrecta del programa
$ EPROCUNAVAIL
76 Procediment erroni per al programa
$ ENOLCK
77 No hi ha bloquejos disponibles
$ ENOSYS
78 Funció no implementada
$ EFTYPE
79 Tipus d'arxiu o de format inadequat
$ EAUTH
80 Error d'autenticació
$ ENEEDAUTH
81 Es necessita un autenticador
$ EIDRM
82 Identificador eliminat
$ ENOMSG
83 No hi ha missatges del tipus desitjat
$ EOVERFLOW
84 Valor massa gran per a ésser emmagatzemat en el tipus de dades
$ EILSEQ
85 Seqüència de bytes il·legal
$ ENOTSUP
86 No suportat
$ ECANCELED
87 Operació cancel·lada
$ EBADMSG
88 Missatje incorrecte o corrupte
$ ENODATA
89 No hi ha missatges disponibles
$ ENOSR
90 No hi ha recursos de tipus STREAM
$ ENOSTR
91 No és un STREAM
$ ETIME
92 Temps d'espera esgotat en el ioctl STREAM
$ ENOATTR
93 Atribut inexistent
$ EMULTIHOP
94 S'ha intentat un multisalt
$ ENOLINK
95 L'enllaç s'ha servit
$ EPROTO
96 Error de protocol
$
$ strsignal() support catalog
$
$set 2
$ SIGHUP
1 Fí de línia (hangup)
$ SIGINT
2 Interrupció
$ SIGQUIT
3 Finalització
$ SIGILL
4 Instrucció il·legal
$ SIGTRAP
5 Depuració (Trace/BPT)
$ SIGABRT
6 Crida d'avort
$ SIGEMT
7 Captura d'EMT
$ SIGFPE
8 Excepció de coma flotant
$ SIGKILL
9 Matat
$ SIGBUS
10 Error del bus
$ SIGSEGV
11 Error de segmentació
$ SIGSYS
12 Crida al sistema incorrecta
$ SIGPIPE
13 Canal (pipe) trencat
$ SIGALRM
14 Alarma de rellotge
$ SIGTERM
15 Finalitzat
$ SIGURG
16 Condició urgent d'E/S
$ SIGSTOP
17 Parat (per senyal)
$ SIGTSTP
18 Parat
$ SIGCONT
19 Continuant
$ SIGCHLD
20 El fill ha acabat
$ SIGTTIN
21 Parat (entrada de tty)
$ SIGTTOU
22 Parat (sortida de tty)
$ SIGIO
23 I/O permesa
$ SIGXCPU
24 S'ha sobrepassat el límit de temps de la CPU
$ SIGXFSZ
25 S'ha sobrepassat el límit de la longitud de l'arxiu
$ SIGVTALRM
26 El temporitzador virtual ha expirat
$ SIGPROF
27 El temporitzador del perfilador ha expirat
$ SIGWINCH
28 Canvis en la mida de la finestra
$ SIGINFO
29 Demanda d'informació
$ SIGUSR1
30 Senyal 1 definida per l'usuari
$ SIGUSR2
31 Senyal 2 definida per l'usuari
$ SIGPWR
32 Fallada/reinicialització de l'alimentació
