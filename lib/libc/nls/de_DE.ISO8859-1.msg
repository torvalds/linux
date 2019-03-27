$ $FreeBSD$
$
$ Message catalog for de_DE.ISO8859-1 locale
$
$ strerror() support catalog
$
$set 1
$ EPERM
1 Operation nicht erlaubt
$ ENOENT
2 Datei oder Verzeichnis nicht gefunden
$ ESRCH
3 Prozess nicht gefunden
$ EINTR
4 Interrupt innerhalb eines Systemaufrufs
$ EIO
5 Ein-/Ausgabefehler
$ ENXIO
6 Gerät ist nicht konfiguriert
$ E2BIG
7 Argumentliste ist zu lang
$ ENOEXEC
8 Die Datei hat kein bekanntes ausführbares Format
$ EBADF
9 Ungültiger Dateideskriptor
$ ECHILD
10 Keine Kindprozesse
$ EDEADLK
11 Ein Deadlock wurde vermieden
$ ENOMEM
12 Kann nicht genug Speicher belegen
$ EACCES
13 Zugriff verweigert
$ EFAULT
14 Ungültige Adresse
$ ENOTBLK
15 Es wird ein Blockgerät benötigt
$ EBUSY
16 Das Gerät ist belegt
$ EEXIST
17 Datei existiert bereits
$ EXDEV
18 Link zwischen verschiedenen Geräten
$ ENODEV
19 Die Operation wird von diesem Gerät nicht unterstützt
$ ENOTDIR
20 Kein Verzeichnis
$ EISDIR
21 Ist ein Verzeichnis
$ EINVAL
22 Ungültiges Argument
$ ENFILE
23 Zu viele offene Dateien im System
$ EMFILE
24 Zu viele offene Dateien
$ ENOTTY
25 Ungültiger IOCTL für dieses Gerät
$ ETXTBSY
26 Datei wird benutzt
$ EFBIG
27 Datei zu groß
$ ENOSPC
28 Kein Platz mehr auf dem Gerät
$ ESPIPE
29 Ungültige Positionierung
$ EROFS
30 Dateisystem ist schreibgeschützt
$ EMLINK
31 Zu viele Links
$ EPIPE
32 Unterbrochene Pipe
$ EDOM
33 Numerisches Argument außerhalb des Wertebereichs
$ ERANGE
34 Ergebnis außerhalb des Wertebereichs
$ EAGAIN, EWOULDBLOCK
35 Ressource vorübergehend nicht verfügbar
$ EINPROGRESS
36 Operation wird gerade ausgeführt
$ EALREADY
37 Operation wird bereits ausgeführt
$ ENOTSOCK
38 Deskriptor ist kein Socket
$ EDESTADDRREQ
39 Zieladresse benötigt
$ EMSGSIZE
40 Nachricht zu lang
$ EPROTOTYPE
41 Ungültiger Protokolltyp für diesen Socket
$ ENOPROTOOPT
42 Protokoll nicht verfügbar
$ EPROTONOSUPPORT
43 Protokoll nicht unterstützt
$ ESOCKTNOSUPPORT
44 Sockettyp nicht unterstützt
$ EOPNOTSUPP
45 Operation nicht unterstützt
$ EPFNOSUPPORT
46 Protokollfamilie nicht unterstützt
$ EAFNOSUPPORT
47 Adressfamilie wird von der Protokollfamilie nicht unterstützt
$ EADDRINUSE
48 Adresse wird bereits benutzt
$ EADDRNOTAVAIL
49 Kann angeforderte Adresse nicht belegen
$ ENETDOWN
50 Netzwerk nicht verfügbar
$ ENETUNREACH
51 Netzwerk nicht erreichbar
$ ENETRESET
52 Netzwerk hat Verbindung mit Reset abgebrochen
$ ECONNABORTED
53 Software verursachte einen Verbindungsabbruch
$ ECONNRESET
54 Verbindung wurde von der Gegenstelle geschlossen
$ ENOBUFS
55 Keine Buffer verfügbar
$ EISCONN
56 Socket ist schon verbunden
$ ENOTCONN
57 Socket ist nicht verbunden
$ ESHUTDOWN
58 Kann nach einem Socket-Shutdown nicht mehr senden
$ ETOOMANYREFS
59 Zu viele Referenzen, kann nicht verbinden
$ ETIMEDOUT
60 Verbindungsabbruch durch Zeitüberschreitung
$ ECONNREFUSED
61 Verbindung wurde abgelehnt
$ ELOOP
62 Zu viele symbolische Links (zirkulär?)
$ ENAMETOOLONG
63 Dateiname zu lang
$ EHOSTDOWN
64 Host nicht verfügbar
$ EHOSTUNREACH
65 Keine Route zum Host
$ ENOTEMPTY
66 Verzeichnis ist nicht leer
$ EPROCLIM
67 Zu viele Prozesse
$ EUSERS
68 Zu viele Benutzer
$ EDQUOT
69 Platzlimit überschritten
$ ESTALE
70 Verwaister NFS-Dateideskriptor
$ EREMOTE
71 Zu viele Fernverweise in diesem Zugriff
$ EBADRPC
72 RPC-Struktur ist ungültig
$ ERPCMISMATCH
73 RPC-Version ist falsch
$ EPROGUNAVAIL
74 RPC-Programm nicht verfügbar
$ EPROGMISMATCH
75 Falsche Programmversion
$ EPROCUNAVAIL
76 Falsche Prozedur für dieses Programm
$ ENOLCK
77 Keine Dateisperren verfügbar
$ ENOSYS
78 Funktion nicht implementiert
$ EFTYPE
79 Ungültiger Dateityp oder Dateiformat
$ EAUTH
80 Authentifizierungsfehler
$ ENEEDAUTH
81 Authentikator benötigt
$ EIDRM
82 Identifizierung entfernt
$ ENOMSG
83 Keine Nachricht vom gewünschten Typ
$ EOVERFLOW
84 Wert zu groß, um in Datentyp zu speichern
$ ECANCELED
85 Operation abgebrochen
$ EILSEQ
86 Ungültige Byte-Sequenz
$ ENOATTR
87 Attribut nicht gefunden
$ EDOOFUS
88 Programmierfehler
$
$ strsignal() support catalog
$
$set 2
$ SIGHUP
1 Verbindungsende
$ SIGINT
2 Unterbrechung
$ SIGQUIT
3 Programmende
$ SIGILL
4 Ungültiger Maschinenbefehl
$ SIGTRAP
5 Trace/BPT trap
$ SIGABRT
6 Abort trap
$ SIGEMT
7 EMT trap
$ SIGFPE
8 Fließkommafehler
$ SIGKILL
9 Unbedingter Programmabbruch
$ SIGBUS
10 Bus-Zugriffsfehler
$ SIGSEGV
11 Illegaler Speicherzugriff
$ SIGSYS
12 Ungültiger Systemaufruf
$ SIGPIPE
13 Unterbrochene Pipe
$ SIGALRM
14 Wecker
$ SIGTERM
15 Beendet
$ SIGURG
16 Dringende Ein/Ausgabeanforderung
$ SIGSTOP
17 Gestoppt (Signal)
$ SIGTSTP
18 Gestoppt
$ SIGCONT
19 Fortgesetzt
$ SIGCHLD
20 Kindprozess beendet
$ SIGTTIN
21 Gestoppt (Eingabe)
$ SIGTTOU
22 Gestoppt (Ausgabe)
$ SIGIO
23 Ein/Ausgabe möglich
$ SIGXCPU
24 CPU-Zeitlimit überschritten
$ SIGXFSZ
25 Dateigrößenlimit überschritten
$ SIGVTALRM
26 Virtueller Wecker abgelaufen
$ SIGPROF
27 Profil-Wecker abgelaufen
$ SIGWINCH
28 Fenstergröße hat sich verändert
$ SIGINFO
29 Informationsanforderung
$ SIGUSR1
30 Benutzerdefiniertes Signal 1
$ SIGUSR2
31 Benutzerdefiniertes Signal 2
