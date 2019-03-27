$ $FreeBSD$
$
$ Message catalog for fi_FI.ISO8859-1 locale
$
$ strerror() support catalog
$
$set 1
$ EPERM
1 Toimintoa ei sallita
$ ENOENT
2 Tiedostoa tai hakemistoa ei löydy
$ ESRCH
3 Prosessia ei löydy
$ EINTR
4 Systeemikutsu keskeytyi
$ EIO
5 Syöttö/tulostusvirhe
$ ENXIO
6 Laitetta ei määritelty
$ E2BIG
7 Liikaa argumentteja
$ ENOEXEC
8 Tuntematon ohjelmatyyppi
$ EBADF
9 Virheellinen tiedosto-osoitin
$ ECHILD
10 Ei lapsiprosesseja
$ EDEADLK
11 Resurssin ristiinlukitus vältetty
$ ENOMEM
12 Muistinvaraus epäonnistui
$ EACCES
13 Lupa kielletty
$ EFAULT
14 Virheellinen osoite
$ ENOTBLK
15 Tarvitaan lohko-osoitettava laite
$ EBUSY
16 Laite käytössä
$ EEXIST
17 Tiedosto on jo olemassa
$ EXDEV
18 Laitteiden välinen linkki
$ ENODEV
19 Laite ei tue toimintoa
$ ENOTDIR
20 Kohde ei ole hakemisto
$ EISDIR
21 Kohde on hakemisto
$ EINVAL
22 Virheellinen argumentti
$ ENFILE
23 Järjestelmässä on liian monta avointa tiedostoa
$ EMFILE
24 Liian monta avointa tiedostoa
$ ENOTTY
25 Virheellinen ohjaustoiminto laitteelle
$ ETXTBSY
26 Tiedosto on käytössä
$ EFBIG
27 Tiedosto liian suuri
$ ENOSPC
28 Laitteella ei ole tilaa
$ ESPIPE
29 Virheellinen haku
$ EROFS
30 Vain luettava tiedostojärjestelmä
$ EMLINK
31 Liian monta linkkiä
$ EPIPE
32 Katkennut putki
$ EDOM
33 Numeerinen syöte virheellinen
$ ERANGE
34 Tulos liian suuri
$ EAGAIN, EWOULDBLOCK
35 Resurssi ei ole tilapäisesti saatavilla
$ EINPROGRESS
36 Toiminta on käynnissä
$ EALREADY
37 Toiminta oli jo käynnissä
$ ENOTSOCK
38 Socket-operaatio muulla kuin socketilla
$ EDESTADDRREQ
39 Tarvitaan kohdeosoite
$ EMSGSIZE
40 Sanoma liian pitkä
$ EPROTOTYPE
41 Väärä protokolla socketille
$ ENOPROTOOPT
42 Protokolla ei ole käytettävissä
$ EPROTONOSUPPORT
43 Protokollaa ei tueta
$ ESOCKTNOSUPPORT
44 Socket-tyyppiä ei tueta
$ EOPNOTSUPP
45 Toimintoa ei tueta
$ EPFNOSUPPORT
46 Protokollaperhettä ei tueta
$ EAFNOSUPPORT
47 Protokollaperhe ei tue osoiteperhettä
$ EADDRINUSE
48 Osoite on jo käytössä
$ EADDRNOTAVAIL
49 Ei pysty antamaan pyydettyä osoitetta
$ ENETDOWN
50 Verkko ei ole käytettävissä
$ ENETUNREACH
51 Verkkoon ei ole yhteyttä
$ ENETRESET
52 Verkko sulki yhteyden
$ ECONNABORTED
53 Ohjelmiston aiheuttama yhteyden keskeytyminen
$ ECONNRESET
54 Isäntä nollasi yhteyden
$ ENOBUFS
55 Puskuritila on lopussa
$ EISCONN
56 Yhteys on jo olemassa
$ ENOTCONN
57 Yhteyttä ei ole olemassa
$ ESHUTDOWN
58 Lähettäminen ei ole mahdollista yhteyden katkaisun jälkeen
$ ETOOMANYREFS
59 Liikaa viittauksia: ei voi yhdistää
$ ETIMEDOUT
60 Yhteyden aikavalvontakatkaisu
$ ECONNREFUSED
61 Yhteys hylätty
$ ELOOP
62 Liian monta peräkkäistä symbolista linkkiä
$ ENAMETOOLONG
63 Tiedoston nimi on liian pitkä
$ EHOSTDOWN
64 Isäntä ei vastaa
$ EHOSTUNREACH
65 Ei reittiä isäntään
$ ENOTEMPTY
66 Hakemisto ei ole tyhjä
$ EPROCLIM
67 Liian monta prosessia
$ EUSERS
68 Liian monta käyttäjää
$ EDQUOT
69 Levytilarajoitus ylittyi
$ ESTALE
70 Vanhentunut NFS-yhteys
$ EREMOTE
71 Liian monta verkkolevyä polussa
$ EBADRPC
72 Virheellinen RPC-pyyntö
$ ERPCMISMATCH
73 Väärä RPC-versio
$ EPROGUNAVAIL
74 RPC ei käytettävissä
$ EPROGMISMATCH
75 Väärä ohjelmaversio
$ EPROCUNAVAIL
76 Väärä RPC-pyyntö ohjelmalle
$ ENOLCK
77 Lukitus ei käytettävissä
$ ENOSYS
78 Toimintoa ei ole
$ EFTYPE
79 Väärä tiedostotyyppi tai -formaatti
$
$ strsignal() support catalog
$
$set 2
$ SIGHUP
1 Katkaisu
$ SIGINT
2 Keskeytys
$ SIGQUIT
3 Lopetus
$ SIGILL
4 Laiton käsky
$ SIGTRAP
5 Jäljitys/BPT ansa
$ SIGABRT
6 Poistumisansa
$ SIGEMT
7 EMT-ansa
$ SIGFPE
8 Liukulukuvirhe
$ SIGKILL
9 Tapettu
$ SIGBUS
10 Väylävirhe
$ SIGSEGV
11 Suojausvirhe
$ SIGSYS
12 Virheellinen systeemikutsu
$ SIGPIPE
13 Katkennut putki
$ SIGALRM
14 Hälytyskello
$ SIGTERM
15 Lopetettu
$ SIGURG
16 Kiireellinen syöttö/tulostus
$ SIGSTOP
17 Pysäytetty (signaali)
$ SIGTSTP
18 Pysäytetty
$ SIGCONT
19 Jatkettu
$ SIGCHLD
20 Lapsiprosessi päättynyt
$ SIGTTIN
21 Pysäytetty (tty-syöte)
$ SIGTTOU
22 Pysäytetty (tty-tuloste)
$ SIGIO
23 Syöttö ja tulostus mahdollisia
$ SIGXCPU
24 Keskusyksikköaikarajoitus ylitetty
$ SIGXFSZ
25 Tiedoston kokorajoitus ylitetty
$ SIGVTALRM
26 Virtuaali-ajastin laukesi
$ SIGPROF
27 Profilointiajastin laukesi
$ SIGWINCH
28 Ikkunan koko muuttuu
$ SIGINFO
29 Informaatiopyyntö
$ SIGUSR1
30 Käyttäjän määriteltävä signaali 1
$ SIGUSR2
31 Käyttäjän määriteltävä signaali 2
$ SIGPWR
32 Virransaannin tilassa muutos
