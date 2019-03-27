$ $FreeBSD$
$
$ Message catalog for fr_FR.ISO8859-1 locale
$
$ strerror() support catalog
$
$set 1
$ EPERM
1 Opération non autorisée
$ ENOENT
2 Fichier ou répertoire inexistant
$ ESRCH
3 Processus inconnu
$ EINTR
4 Appel système interrompu
$ EIO
5 Erreur d'entrée/sortie
$ ENXIO
6 Dispositif non configuré
$ E2BIG
7 Liste d'arguments trop longue
$ ENOEXEC
8 Format d'exécutable invalide
$ EBADF
9 Mauvais descripteur de fichier
$ ECHILD
10 Pas de processus fils
$ EDEADLK
11 Étreinte mortelle détectée
$ ENOMEM
12 Impossible d'allouer de la mémoire
$ EACCES
13 Permission refusée
$ EFAULT
14 Mauvaise adresse
$ ENOTBLK
15 Dispositif en mode bloc requis
$ EBUSY
16 Dispositif occupé
$ EEXIST
17 Le fichier existe
$ EXDEV
18 Lien inter-unités
$ ENODEV
19 Opération non supportée par le dispositif
$ ENOTDIR
20 Pas un répertoire
$ EISDIR
21 Est un répertoire
$ EINVAL
22 Argument invalide
$ ENFILE
23 Trop de fichiers ouverts sur le système
$ EMFILE
24 Trop de fichiers ouverts
$ ENOTTY
25 Fonction inadaptée au dispositif
$ ETXTBSY
26 Fichier en cours d'utilisation
$ EFBIG
27 Fichier trop grand
$ ENOSPC
28 Plus d'espace disponible
$ ESPIPE
29 Recherche illégale
$ EROFS
30 Système de fichier en lecture seulement
$ EMLINK
31 Trop de liens
$ EPIPE
32 Tube déconnecté
$ EDOM
33 Argument numérique hors définition
$ ERANGE
34 Résultat trop grand
$ EAGAIN, EWOULDBLOCK
35 Ressource indisponible temporairement
$ EINPROGRESS
36 Opération en cours
$ EALREADY
37 Opération déjà en cours
$ ENOTSOCK
38 Opération de type socket sur un descripteur de fichier
$ EDESTADDRREQ
39 Adresse de destination obligatoire
$ EMSGSIZE
40 Message trop long
$ EPROTOTYPE
41 Mauvais type de protocole pour la socket
$ ENOPROTOOPT
42 Protocole non disponible
$ EPROTONOSUPPORT
43 Protocole non supporté
$ ESOCKTNOSUPPORT
44 Type de socket non supporté
$ EOPNOTSUPP
45 Opération non supportée
$ EPFNOSUPPORT
46 Famille de protocole non supportée
$ EAFNOSUPPORT
47 Famille d'adresse non supportée par la famille de protocole
$ EADDRINUSE
48 Adresse en cours d'utilisation
$ EADDRNOTAVAIL
49 Impossible d'assigner l'adresse demandée
$ ENETDOWN
50 Plus de réseau
$ ENETUNREACH
51 Réseau inaccessible
$ ENETRESET
52 Connexion coupée par le réseau
$ ECONNABORTED
53 Connexion interrompue
$ ECONNRESET
54 Connexion interrompue par l'hôte distant
$ ENOBUFS
55 Tampon saturé
$ EISCONN
56 La socket est déjà connectée
$ ENOTCONN
57 La socket n'est pas connectée
$ ESHUTDOWN
58 Impossible d'envoyer après la coupure
$ ETOOMANYREFS
59 Trop de références : lien impossible
$ ETIMEDOUT
60 Dépassement du délai d'attente
$ ECONNREFUSED
61 Connexion refusée
$ ELOOP
62 Trop de niveaux de liens symboliques
$ ENAMETOOLONG
63 Nom de fichier trop long
$ EHOSTDOWN
64 Hôte distant arrêté
$ EHOSTUNREACH
65 Pas de route vers l'hôte distant
$ ENOTEMPTY
66 Répertoire non vide
$ EPROCLIM
67 Trop de processus
$ EUSERS
68 Trop d'utilisateurs
$ EDQUOT
69 Quota de disque dépassé
$ ESTALE
70 Identificateur de fichier NFS périmé
$ EREMOTE
71 Trop de niveaux extérieurs dans le path
$ EBADRPC
72 Mauvaise structure RPC
$ ERPCMISMATCH
73 Mauvaise version RPC
$ EPROGUNAVAIL
74 Prog. RPC indisponible
$ EPROGMISMATCH
75 Mauvaise version de programme
$ EPROCUNAVAIL
76 Mauvaise procédure pour le programme
$ ENOLCK
77 Plus de verrous disponibles
$ ENOSYS
78 Fonction non implémentée
$ EFTYPE
79 Type de fichier ou format inapproprié
$ EAUTH
80 Erreur d'authentification
$ ENEEDAUTH
81 Authentification requise
$ EIDRM
82 Identifiant retiré
$ ENOMSG
83 Aucun message du type souhaité
$ EOVERFLOW
84 Valeur trop grande pour le type de donnée
$ ECANCELED
85 Opération annulée
$ EILSEQ
86 Séquence d'octets illégale
$ ENOATTR
87 Attribut non trouvé
$ EDOOFUS
88 Erreur de programmation
$
$ strsignal() support catalog
$
$set 2
$ SIGHUP
1 Fin de connexion
$ SIGINT
2 Interruption
$ SIGQUIT
3 Quitter
$ SIGILL
4 Instruction illégale
$ SIGTRAP
5 Trace/BPT trap
$ SIGABRT
6 Abort trap
$ SIGEMT
7 EMT trap
$ SIGFPE
8 Exception sur virgule flottante
$ SIGKILL
9 Tué
$ SIGBUS
10 Bus error
$ SIGSEGV
11 Segmentation fault
$ SIGSYS
12 Bad system call
$ SIGPIPE
13 Broken pipe
$ SIGALRM
14 Alarm clock
$ SIGTERM
15 Terminé
$ SIGURG
16 Urgent I/O condition
$ SIGSTOP
17 Suspended (signal)
$ SIGTSTP
18 Suspended
$ SIGCONT
19 Continued
$ SIGCHLD
20 Child exited
$ SIGTTIN
21 Stopped (tty input)
$ SIGTTOU
22 Stopped (tty output)
$ SIGIO
23 I/O possible
$ SIGXCPU
24 Cputime limit exceeded
$ SIGXFSZ
25 Filesize limit exceeded
$ SIGVTALRM
26 Virtual timer expired
$ SIGPROF
27 Profiling timer expired
$ SIGWINCH
28 Window size changes
$ SIGINFO
29 Information request
$ SIGUSR1
30 User defined signal 1
$ SIGUSR2
31 User defined signal 2
