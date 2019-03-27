$ $FreeBSD$
$
$ Message catalog for it_IT.ISO8859-15 locale
$
$ strerror() support catalog
$
$set 1
$ EPERM
1 Operazione non permessa
$ ENOENT
2 File o directory inesistente
$ ESRCH
3 Processo inesistente
$ EINTR
4 Chiamata di sistema interrotta
$ EIO
5 Errore di input/output
$ ENXIO
6 Periferica non configurata
$ E2BIG
7 Lista degli argomenti troppo lunga
$ ENOEXEC
8 Errore nel formato eseguibile
$ EBADF
9 Descrittore di file non valido
$ ECHILD
10 Nessun processo figlio
$ EDEADLK
11 Situazione di deadlock evitata
$ ENOMEM
12 Impossibile allocare memoria
$ EACCES
13 Permesso negato
$ EFAULT
14 Indirizzo non valido
$ ENOTBLK
15 Periferica a blocchi necessaria
$ EBUSY
16 Periferica occupata
$ EEXIST
17 Il file esiste
$ EXDEV
18 Link improprio
$ ENODEV
19 Operazione non supportata dalla periferica
$ ENOTDIR
20 Non è una directory
$ EISDIR
21 E' una directory
$ EINVAL
22 Argomento non valido
$ ENFILE
23 Troppi file aperti nel sistema
$ EMFILE
24 Troppi file aperti
$ ENOTTY
25 Ioctl inappropriata per la periferica
$ ETXTBSY
26 File di testo occupato
$ EFBIG
27 File troppo grande
$ ENOSPC
28 Spazio sulla periferica esaurito
$ ESPIPE
29 Ricerca non valida
$ EROFS
30 Filesystem di sola lettura
$ EMLINK
31 Troppi link
$ EPIPE
32 Pipe rotta
$ EDOM
33 Argomento numerico fuori dal dominio
$ ERANGE
34 Risultato troppo grande
$ EAGAIN, EWOULDBLOCK
35 Risorsa temporaneamente non disponibile
$ EINPROGRESS
36 Operazione in esecuzione
$ EALREADY
37 Operazione già in esecuzione
$ ENOTSOCK
38 Operazione sui socket eseguita su un non-socket
$ EDESTADDRREQ
39 Indirizzo destinazione necessario
$ EMSGSIZE
40 Messaggio troppo lungo
$ EPROTOTYPE
41 Tipo di protocollo non valido per il socket
$ ENOPROTOOPT
42 Protocollo non disponibile
$ EPROTONOSUPPORT
43 Protocollo non supportato
$ ESOCKTNOSUPPORT
44 Tipo di socket non supportato
$ EOPNOTSUPP
45 Operazione non supportata
$ EPFNOSUPPORT
46 Famiglia di protocolli non supportata
$ EAFNOSUPPORT
47 Famiglia di indirizzi non supportata dalla famiglia di protocolli
$ EADDRINUSE
48 Indirizzo già in uso
$ EADDRNOTAVAIL
49 Impossibile assegnare l'indirizzo richiesto
$ ENETDOWN
50 Network fuori uso
$ ENETUNREACH
51 Network non raggiungibile
$ ENETRESET
52 Network dropped connection on reset
$ ECONNABORTED
53 Interruzione della connessione causata dal software
$ ECONNRESET
54 Connessione azzerata dal corrispondente
$ ENOBUFS
55 Spazio del buffer esaurito
$ EISCONN
56 Socket già connesso
$ ENOTCONN
57 Socket non connesso
$ ESHUTDOWN
58 Impossibile inviare dopo la chiusura del socket
$ ETOOMANYREFS
59 Troppe referenze: impossibile raccordare
$ ETIMEDOUT
60 Connessione scaduta
$ ECONNREFUSED
61 Connection rifiutata
$ ELOOP
62 Troppi livelli di link simbolici
$ ENAMETOOLONG
63 Nome di file troppo lungo
$ EHOSTDOWN
64 Host fuori uso
$ EHOSTUNREACH
65 Host irraggiungibile
$ ENOTEMPTY
66 Directory non vuota
$ EPROCLIM
67 Troppi processi
$ EUSERS
68 Troppi utenti
$ EDQUOT
69 Quota disco superata
$ ESTALE
70 Manipolatore di file NFS scaduto
$ EREMOTE
71 Troppi livelli remoti nel path
$ EBADRPC
72 Struttura RPC non valida
$ ERPCMISMATCH
73 Versione RPC non corrispondente
$ EPROGUNAVAIL
74 Programma RPC non disponibile
$ EPROGMISMATCH
75 Versione del programma non corrispodente
$ EPROCUNAVAIL
76 Procedura non disponibile
$ ENOLCK
77 Nessun lock disponibile
$ ENOSYS
78 Funzione non implementata
$ EFTYPE
79 Tipo di file o formato inappropriato
$
$ strsignal() support catalog
$
$set 2
$ SIGHUP
1 Hangup
$ SIGINT
2 Interruzione
$ SIGQUIT
3 Chiusura
$ SIGILL
4 Istruzione illegale
$ SIGTRAP
5 Trappola Trace/breakpoint
$ SIGABRT
6 Trappola abort()
$ SIGEMT
7 Trappola EMT
$ SIGFPE
8 Errore di virgola mobile
$ SIGKILL
9 Ucciso
$ SIGBUS
10 Errore di bus
$ SIGSEGV
11 Errore di segmentazione
$ SIGSYS
12 Chiamata di sistema non valida
$ SIGPIPE
13 Pipe rotta
$ SIGALRM
14 Sveglia
$ SIGTERM
15 Terminato
$ SIGURG
16 I/O urgente
$ SIGSTOP
17 Processo fermato
$ SIGTSTP
18 Stop da terminale
$ SIGCONT
19 Continuato
$ SIGCHLD
20 Processo figlio uscito
$ SIGTTIN
21 Input da terminale per processo in background
$ SIGTTOU
22 Output a terminale per processo in background
$ SIGIO
23 I/O possibile
$ SIGXCPU
24 Limite del tempo di CPU superato
$ SIGXFSZ
25 Limite della dimensione del file superato
$ SIGVTALRM
26 Timer virtuale scaduto
$ SIGPROF
27 Timer di profilo expired
$ SIGWINCH
28 Cambio di dimensione della finestra
$ SIGINFO
29 Richiesta di informazioni
$ SIGUSR1
30 Segnale definito dall'utente 1
$ SIGUSR2
31 Segnale definito dall'utente 2
