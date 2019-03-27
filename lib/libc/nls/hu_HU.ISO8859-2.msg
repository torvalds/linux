$ $FreeBSD$
$
$ Message catalog for hu_HU.ISO8859-2 locale
$
$ strerror() support catalog
$
$set 1
$ EPERM
1 Nem engedélyezett mûvelet
$ ENOENT
2 Fájl vagy könyvtár nem található
$ ESRCH
3 Processz nem található
$ EINTR
4 Megszakított rendszerhívás
$ EIO
5 B/K hiba
$ ENXIO
6 Konfigurálatlan eszköz
$ E2BIG
7 Túl hosszú argumentumlista
$ ENOEXEC
8 Hibás végrehajtható formátum
$ EBADF
9 Rossz fájlleíró
$ ECHILD
10 Nem létezõ gyermek processz
$ EDEADLK
11 Resource deadlock avoided
$ ENOMEM
12 Memória nem foglalható le
$ EACCES
13 Hozzáférés megtagadva
$ EFAULT
14 Rossz cím
$ ENOTBLK
15 Blokkos eszköz szükséges
$ EBUSY
16 Eszköz foglalt
$ EEXIST
17 Fájl létezik
$ EXDEV
18 Kereszthivatkozás eszközökön
$ ENODEV
19 A mûvelet nem támogatott az eszköz által
$ ENOTDIR
20 Nem könyvtár
$ EISDIR
21 Könyvtár
$ EINVAL
22 Érvénytelen argumentum
$ ENFILE
23 Túl sok megnyitott fájl a rendszerben
$ EMFILE
24 Túl sok megnyitott fájl
$ ENOTTY
25 Nem megfelelõ ioctl az eszközhöz
$ ETXTBSY
26 Szöveges fájl foglalt
$ EFBIG
27 Fájl túl nagy
$ ENOSPC
28 Nincs hely az eszközön
$ ESPIPE
29 Érvénytelen seek
$ EROFS
30 Csak olvasható fájlrendszer
$ EMLINK
31 Túl sok link
$ EPIPE
32 Hibás csõvezeték
$ EDOM
33 Numerikus argumentum nem esik a tartományba
$ ERANGE
34 Az esermény túl nagy
$ EAGAIN, EWOULDBLOCK
35 Az erõforrás ideiglenesen nem érhetõ el
$ EINPROGRESS
36 A mûvelet folyamatban van
$ EALREADY
37 A mûvelet már folyamatban van
$ ENOTSOCK
38 Socket mûvelet nem socketen
$ EDESTADDRREQ
39 Cél cím szükséges
$ EMSGSIZE
40 Túl hosszú üzenet
$ EPROTOTYPE
41 Rossz protokolltípus a sockethez
$ ENOPROTOOPT
42 Protokoll nem érhetõ el
$ EPROTONOSUPPORT
43 Protokoll nem támogatott
$ ESOCKTNOSUPPORT
44 Sockettípus nem támogatott
$ EOPNOTSUPP
45 Mûvelet nem támogatott
$ EPFNOSUPPORT
46 Protokollcsalád nem támogatott
$ EAFNOSUPPORT
47 A címcsalád nem támogatott a protokollcsalád által
$ EADDRINUSE
48 A cím már használatban van
$ EADDRNOTAVAIL
49 A kért cím nem osztható ki
$ ENETDOWN
50 A hálózat nem mûködik
$ ENETUNREACH
51 A hálózat nem érhetõ el
$ ENETRESET
52 A hálózat megszakította a kapcsolatot
$ ECONNABORTED
53 A szoftver megszakította a kapcsolatot
$ ECONNRESET
54 A kapcsolatot megszakította a peer
$ ENOBUFS
55 Nincs rendelkezésre álló pufferterület
$ EISCONN
56 A socket már kapcsolódva van
$ ENOTCONN
57 A socket nincs kapcsolódva
$ ESHUTDOWN
58 Nem küldhetõ el a socket lebontása után
$ ETOOMANYREFS
59 Túl sok hivatkozás: nem illeszthetõ össze
$ ETIMEDOUT
60 A mûvelet túllépte a rendelkezésre álló idõt
$ ECONNREFUSED
61 Kapcsolat elutasítva
$ ELOOP
62 Túl sok szint a szimbolikus linkekben
$ ENAMETOOLONG
63 Túl hosszú fájlnév
$ EHOSTDOWN
64 A kiszolgáló nem érhetõ el
$ EHOSTUNREACH
65 Nincs útvonal a kiszolgálóhoz
$ ENOTEMPTY
66 Könyvtár nem üres
$ EPROCLIM
67 Túl sok processz
$ EUSERS
68 Túl sok felhasználó
$ EDQUOT
69 A lemezkvóta túllépve
$ ESTALE
70 Elavult NFS fájlkezelõ
$ EREMOTE
71 Túl sok szint a távoli útvonalban
$ EBADRPC
72 Rossz RPC struktúra
$ ERPCMISMATCH
73 Rossz RPC verzió
$ EPROGUNAVAIL
74 RPC program nem érhetõ el
$ EPROGMISMATCH
75 Rossz programverzió
$ EPROCUNAVAIL
76 Rossz eljárás a programhoz
$ ENOLCK
77 Nincs elérhetõ zárolás
$ ENOSYS
78 Nem implementált funkció
$ EFTYPE
79 Nem megfelelõ fájltípus vagy formátum
$ EAUTH
80 Azonosítási hiba
$ ENEEDAUTH
81 Azonosítás szükséges
$ EIDRM
82 Azonosító eltávolítva
$ ENOMSG
83 Nincs üzenet a kívánt típusból
$ EOVERFLOW
84 Az érték túl nagy az adott adattípusban való tároláshoz
$ ECANCELED
85 Mûvelet törölva
$ EILSEQ
86 Érvénytelen bájtsorrend
$ ENOATTR
87 Attribútum nem található
$ EDOOFUS
88 Programozási hiba
$ EBADMSG
89 Helytelen üzenet
$ EMULTIHOP
90 Multihop kísérlet
$ ENOLINK
91 A kapcsolat szigorítva lett
$ EPROTO
92 Protokol hiba
$ ENOTCAPABLE
93 Elégtelen képességek
$
$ strsignal() support catalog
$
$set 2
$ SIGHUP
1 Hangup
$ SIGINT
2 Megszakítás
$ SIGQUIT
3 Kilépés
$ SIGILL
4 Érvénytelen mûvelet
$ SIGTRAP
5 Trace/BPT csapda
$ SIGABRT
6 Abort csapda
$ SIGEMT
7 EMT csapda
$ SIGFPE
8 Lebegõpontos kivétel
$ SIGKILL
9 Megölve
$ SIGBUS
10 Busz hiba
$ SIGSEGV
11 Szegmentációs hiba
$ SIGSYS
12 Rossz rendszerhívás
$ SIGPIPE
13 Hibás csõvezeték
$ SIGALRM
14 Idõzítési riasztás
$ SIGTERM
15 Befejezve
$ SIGURG
16 Sürgõs B/K feltétel
$ SIGSTOP
17 Elaltatva (szignál)
$ SIGTSTP
18 Elaltatva
$ SIGCONT
19 Folytatva
$ SIGCHLD
20 Gyermek befejezõdött
$ SIGTTIN
21 Leállítva (tty bevitel)
$ SIGTTOU
22 Leállítva (tty kivitel)
$ SIGIO
23 B/K lehetséges
$ SIGXCPU
24 Processzoridõ-korlátozás túllépve
$ SIGXFSZ
25 ájlméret-korlátozás túllépve
$ SIGVTALRM
26 Virtuális idõzítõ lejárt
$ SIGPROF
27 Profilidõzítõ lejárt
$ SIGWINCH
28 Ablakméret-változások
$ SIGINFO
29 Információkérés
$ SIGUSR1
30 Felhasználói szignál 1
$ SIGUSR2
31 Felhasználói szignál 2
$
$ gai_strerror() support catalog
$
$set 3
$ 1 (elavult)
1 A hosztnévhez tartozó címcsalád nem támogatott
$ EAI_AGAIN
2 Ideiglenes hiba a névfeloldáskor
$ EAI_BADFLAGS
3 Érvénytelen ai_flags érték
$ EAI_FAIL
4 Nem helyreállítható hiba a névfeloldásban
$ EAI_FAMILY
5 ai_family nem támogatott
$ EAI_MEMORY
6 Memóriafoglalási hiba
$ 7 (elavult)
7 Nem tartozik cím í hosztnévhez
$ EAI_NONAME
8 Se hosztnév, se szolgáltatásnév nem áll rendelkezésre
$ EAI_SERVICE
9 Nem támogatott ai_socktype szolgáltatásnév
$ EAI_SOCKTYPE
10 ai_socktype nem támogatott
$ EAI_SYSTEM
11 Rendszerhiba jött vissza az errno változóban
$ EAI_BADHINTS
12 Érvénytelen hint érték
$ EAI_PROTOCOL
13 A feloldott protokol ismeretlen
$ EAI_OVERFLOW
14 Az argumentumok puffere túlcsordult
$ 0
32766 Siker
$ NL_MSGMAX
32767 Ismeretlen hiba
