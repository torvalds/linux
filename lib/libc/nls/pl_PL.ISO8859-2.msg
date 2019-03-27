$ $FreeBSD$
$
$ Message catalog for pl_PL.ISO8859-2 locale
$
$ strerror() support catalog
$
$set 1
$ EPERM
1 Operacja niedozwolona
$ ENOENT
2 Nie ma takiego pliku/katalogu
$ ESRCH
3 Nie ma takiego procesu
$ EINTR
4 Wywo³anie systemowe przerwane
$ EIO
5 B³±d wej¶cia/wyj¶cia
$ ENXIO
6 Nieskonfigurowane urz±dzenie
$ E2BIG
7 Zbyt d³uga lista argumentów
$ ENOEXEC
8 B³êdny format pliku wykonywalnego
$ EBADF
9 Z³y deskryptor pliku
$ ECHILD
10 Brak procesów potomnych
$ EDEADLK
11 Unikniêto zakleszczenia zasobów
$ ENOMEM
12 Brak pamiêci do przydzia³u
$ EACCES
13 Brak dostêpu
$ EFAULT
14 Z³y adres
$ ENOTBLK
15 Wymagane urz±dzenie blokowe
$ EBUSY
16 Urz±dzenie zajête
$ EEXIST
17 Plik istnieje
$ EXDEV
18 Dowi±zanie miedzy urz±dzeniami
$ ENODEV
19 Operacja nieobs³ugiwana przez urz±dzenie
$ ENOTDIR
20 To nie jest katalog
$ EISDIR
21 To jest katalog
$ EINVAL
22 B³êdny argument
$ ENFILE
23 Zbyt wiele otwartych plików w systemie
$ EMFILE
24 Zbyt wiele otwartych plików
$ ENOTTY
25 Niew³a¶ciwy dostêp do urz±dzenia
$ ETXTBSY
26 Plik wykonywalny jest zajêty
$ EFBIG
27 Zbyt du¿y plik
$ ENOSPC
28 Brak miejsca na urz±dzeniu
$ ESPIPE
29 Przesuniêcie niemo¿liwe
$ EROFS
30 System plików tylko do odczytu
$ EMLINK
31 Zbyt wiele dowi±zañ
$ EPIPE
32 Potok przerwany
$ EDOM
33 Argument liczbowy z poza zakresu
$ ERANGE
34 Wynik z poza zakresu
$ EAGAIN, EWOULDBLOCK
35 Zasoby chwilowo niedostêpne
$ EINPROGRESS
36 Operacja jest w³a¶nie wykonywana
$ EALREADY
37 Operacja jest ju¿ wykonywana
$ ENOTSOCK
38 Operacja na obiekcie, który nie jest gniazdem
$ EDESTADDRREQ
39 Wymagany adres przeznaczenia
$ EMSGSIZE
40 Wiadomo¶æ zbyt d³uga
$ EPROTOTYPE
41 Typ protoko³u nie pasuje do gniazda
$ ENOPROTOOPT
42 Protokó³ nie jest dostêpny
$ EPROTONOSUPPORT
43 Nieobs³ugiwany protokó³
$ ESOCKTNOSUPPORT
44 Nieobs³ugiwany typ gniazda
$ EOPNOTSUPP
45 Nieobs³ugiwana operacja
$ EPFNOSUPPORT
46 Nieobs³ugiwana rodzina protoko³ów
$ EAFNOSUPPORT
47 Rodzina adresów nie jest obs³ugiwana przez rodzinê protoko³ów
$ EADDRINUSE
48 Adres jest ju¿ w u¿yciu
$ EADDRNOTAVAIL
49 Adres nie mo¿e byæ przydzielony
$ ENETDOWN
50 Sieæ nie dzia³a
$ ENETUNREACH
51 Sieæ jest niedostêpna
$ ENETRESET
52 Sieæ przerwa³a po³±czenie po resecie
$ ECONNABORTED
53 Oprogramowanie spowodowa³o przerwanie po³±czenia
$ ECONNRESET
54 Po³±czenie zerwane przez drug± stronê
$ ENOBUFS
55 Brak miejsca w buforze
$ EISCONN
56 Gniazdo jest ju¿ po³±czone
$ ENOTCONN
57 Gniazdo nie jest po³±czone
$ ESHUTDOWN
58 Nie mo¿na wysy³aæ po zamkniêciu gniazda
$ ETOOMANYREFS
59 Za du¿o powi±zañ: dowi±zanie niemo¿liwe
$ ETIMEDOUT
60 Limit czasu operacji przekroczony
$ ECONNREFUSED
61 Po³±czenie odrzucone
$ ELOOP
62 Zbyt wiele wzajemnych dowi±zañ symbolicznych
$ ENAMETOOLONG
63 Zbyt d³uga nazwa pliku
$ EHOSTDOWN
64 Host jest wy³±czony
$ EHOSTUNREACH
65 Brak drogi do hosta
$ ENOTEMPTY
66 Katalog nie jest pusty
$ EPROCLIM
67 Zbyt wiele procesów
$ EUSERS
68 Zbyt wielu u¿ytkowników
$ EDQUOT
69 Przekroczono limit miejsca na dysku
$ ESTALE
70 Uchwyt pliku NFS jest nieaktualny
$ EREMOTE
71 Zbyt wiele poziomów zagnie¿d¿enia w ¶cie¿ce
$ EBADRPC
72 RPC b³êdna struktura
$ ERPCMISMATCH
73 RPC niekompatybilna wersja
$ EPROGUNAVAIL
74 RPC program niedostêpny
$ EPROGMISMATCH
75 Nieobs³ugiwana wersja programu
$ EPROCUNAVAIL
76 Nieobs³ugiwana procedura
$ ENOLCK
77 Wyczerpany limit blokad
$ ENOSYS
78 Nieobs³ugiwana funkcja
$ EFTYPE
79 Niew³a¶ciwy typ lub format pliku
$ EAUTH
80 B³±d uwierzytelnienia
$ ENEEDAUTH
81 Wymagane uwierzytelnienie
$ EIDRM
82 Identyfikator zosta³ usuniêty
$ ENOMSG
83 Brak komunikatu po¿±danego typu
$ EOVERFLOW
84 Warto¶æ zbyt du¿a dla zdefiniowanego typu danych
$ ECANCELED
85 Operacja anulowana
$ EILSEQ
86 B³êdna sekwencja bajtów
$ ENOATTR
87 Nie znaleziono atrybutu
$ EDOOFUS
88 Wewnêtrzny b³±d programu
$
$ strsignal() support catalog
$
$set 2
$ SIGHUP
1 Roz³±czenie
$ SIGINT
2 Przerwanie
$ SIGQUIT
3 Wyj¶cie
$ SIGILL
4 Nieznana instrukcja
$ SIGTRAP
5 Pu³apka debuggera/BPT
$ SIGABRT
6 Przerwana pu³apka
$ SIGEMT
7 Pu³apka EMT
$ SIGFPE
8 B³±d w obliczeniach zmiennoprzecinkowych
$ SIGKILL
9 Unicestwiony
$ SIGBUS
10 B³±d szyny
$ SIGSEGV
11 Naruszenie ochrony pamiêci
$ SIGSYS
12 B³êdne wywo³anie systemowe
$ SIGPIPE
13 Przerwany potok
$ SIGALRM
14 Budzik
$ SIGTERM
15 Zakoñczony
$ SIGURG
16 Nag³y wypadek I/O
$ SIGSTOP
17 Zatrzymany (sygna³)
$ SIGTSTP
18 Zatrzymany
$ SIGCONT
19 Kontynuacja
$ SIGCHLD
20 Proces potomny zakoñczy³ pracê
$ SIGTTIN
21 Zatrzymany (wej¶cie z tty)
$ SIGTTOU
22 Zatrzymany (wyj¶cie z tty)
$ SIGIO
23 Wej/Wyj dozwolone
$ SIGXCPU
24 Przekroczony limit czasu procesora
$ SIGXFSZ
25 Przekroczony limit wielko¶ci pliku
$ SIGVTALRM
26 Alarm stopera wirtualnego
$ SIGPROF
27 Alarm stopera profiluj±cego
$ SIGWINCH
28 Zmiana rozmiaru okna
$ SIGINFO
29 ¯±danie informacji
$ SIGUSR1
30 Sygna³ u¿ytkownika 1
$ SIGUSR2
31 Sygna³ u¿ytkownika 2
