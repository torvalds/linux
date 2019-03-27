$ $FreeBSD$
$
$ Message catalog for pt_BR.ISO8859-1 locale
$
$ strerror() support catalog
$
$set 1
$ EPERM
1 Operação não permitida
$ ENOENT
2 Nenhum arquivo ou diretório encontrado
$ ESRCH
3 Nenhum processo encontrado
$ EINTR
4 Chamada de sistema interrompida
$ EIO
5 Erro de entrada/saída
$ ENXIO
6 Dispositivo não configurado
$ E2BIG
7 Lista de argumentos muito grande
$ ENOEXEC
8 Erro no formato de execução
$ EBADF
9 Erro no descritor de arquivo
$ ECHILD
10 Nenhum processo filho
$ EDEADLK
11 Bloqueio de recurso evitado
$ ENOMEM
12 Impossível alocar memória
$ EACCES
13 Permissão negada
$ EFAULT
14 Endereço errado
$ ENOTBLK
15 Dispositivo de bloco requerido
$ EBUSY
16 Dispositivo ocupado
$ EEXIST
17 Arquivo existe
$ EXDEV
18 Ligação entre dispositivos
$ ENODEV
19 Operação não suportada pelo dispositivo
$ ENOTDIR
20 Não é um diretório
$ EISDIR
21 É um diretório
$ EINVAL
22 Argumento inválido
$ ENFILE
23 Muitos arquivos abertos no sistema
$ EMFILE
24 Muitos arquivos abertos no sistema
$ ENOTTY
25 ioctl inapropriado para o dispositivo
$ ETXTBSY
26 Arquivo texto sendo utilizado
$ EFBIG
27 Arquivo muito grande
$ ENOSPC
28 Sem espaço no dispositivo
$ ESPIPE
29 seek ilegal
$ EROFS
30 Sistema de arquivos apenas para leitura
$ EMLINK
31 Muitos links
$ EPIPE
32 pipe quebrado
$ EDOM
33 Argumento numérico fora do domínio
$ ERANGE
34 Resultado muito grande
$ EAGAIN, EWOULDBLOCK
35 Recurso temporariamente indisponível
$ EINPROGRESS
36 Operação em progresso agora
$ EALREADY
37 Operação em progresso pronta
$ ENOTSOCK
38 Operação de socket em um não-socket
$ EDESTADDRREQ
39 Endereço de destino requerido
$ EMSGSIZE
40 Mensagem muito grande
$ EPROTOTYPE
41 Tipo de protocolo errado para socket
$ ENOPROTOOPT
42 Protocolo não disponível
$ EPROTONOSUPPORT
43 Protocolo não suportado
$ ESOCKTNOSUPPORT
44 Tipo de socket não suportado
$ EOPNOTSUPP
45 Operação não permitida
$ EPFNOSUPPORT
46 Família de protocolo não suportada
$ EAFNOSUPPORT
47 Família de endereços não suportada pela família de protocolos
$ EADDRINUSE
48 Endereço já está em uso
$ EADDRNOTAVAIL
49 Impossível obter endereço solicitado
$ ENETDOWN
50 Rede está parada
$ ENETUNREACH
51 Rede está inalcançável
$ ENETRESET
52 Conexão de rede perdida durante reset
$ ECONNABORTED
53 Conexão abortada por software
$ ECONNRESET
54 Conexão reiniciada pelo outro ponto
$ ENOBUFS
55 Sem espaço de buffer disponível
$ EISCONN
56 Socket já está conectado
$ ENOTCONN
57 Socket não está conectado
$ ESHUTDOWN
58 Impossível enviar depois que o socket foi finalizado
$ ETOOMANYREFS
59 Muitas referências: impossível ligar
$ ETIMEDOUT
60 Tempo de operação expirou
$ ECONNREFUSED
61 Conexão recusada
$ ELOOP
62 Muitos níveis de links simbólicos
$ ENAMETOOLONG
63 Nome de arquivo muito grande
$ EHOSTDOWN
64 Host está desligado
$ EHOSTUNREACH
65 Sem rota para o host
$ ENOTEMPTY
66 Diretório não está vazio
$ EPROCLIM
67 Muitos processos
$ EUSERS
68 Muitos usuários
$ EDQUOT
69 Quota de disco excedida
$ ESTALE
70 Manipulador de arquivo NFS velho
$ EREMOTE
71 Muitos níveis no caminho remoto
$ EBADRPC
72 Estrutura RPC incorreta
$ ERPCMISMATCH
73 Versão RPC incorreta
$ EPROGUNAVAIL
74 Programa RPC indisponível
$ EPROGMISMATCH
75 Versão do programa incorreta
$ EPROCUNAVAIL
76 Procedimento incorreto para o programa
$ ENOLCK
77 Nenhum lock disponível
$ ENOSYS
78 Função não implementada
$ EFTYPE
79 Tipo de arquivo ou formato inapropriados
$ EAUTH
80 Erro de autenticação
$ ENEEDAUTH
81 Necessidade de autenticador
$ EIDRM
82 Identificador removido
$ ENOMSG
83 Nenhuma mensagem do tipo desejado
$ EOVERFLOW
84 Valor muito grande para ser armazenado neste tipo de dado
$ ECANCELED
85 Operação cancelada
$ EILSEQ
86 Sequência de bytes ilegal
$ ENOATTR
87 Atributo não encontrado
$ EDOOFUS
88 Erro de programação
$
$ strsignal() support catalog
$
$set 2
$ SIGHUP
1 Fim da linha (Hangup)
$ SIGINT
2 Interrupção
$ SIGQUIT
3 Saída
$ SIGILL
4 Instrução ilegal
$ SIGTRAP
5 Trap de Trace/BPT
$ SIGABRT
6 Abortar trap
$ SIGEMT
7 EMT trap
$ SIGFPE
8 Exceção de ponto flutuante
$ SIGKILL
9 Morreu
$ SIGBUS
10 Erro de barramento
$ SIGSEGV
11 Falha de segmentação
$ SIGSYS
12 Chamada de sistema incorreta
$ SIGPIPE
13 Pipe incorreto
$ SIGALRM
14 Alarme do relógio
$ SIGTERM
15 Terminado
$ SIGURG
16 Condição urgente de E/S
$ SIGSTOP
17 Suspendido (sinal)
$ SIGTSTP
18 Suspendido
$ SIGCONT
19 Continuado
$ SIGCHLD
20 Filho saiu
$ SIGTTIN
21 Parado (entrada de tty)
$ SIGTTOU
22 Parado (saída de tty)
$ SIGIO
23 E/S possível
$ SIGXCPU
24 Limite de tempo de CPU excedido
$ SIGXFSZ
25 Tamanho de arquivo excedido
$ SIGVTALRM
26 Temporizador virtual expirou
$ SIGPROF
27 Temporizador de profiling expirou
$ SIGWINCH
28 Mudança no tamanho de janela
$ SIGINFO
29 Requisição de informação
$ SIGUSR1
30 Sinal 1 definido pelo usuário
$ SIGUSR2
31 Sinal 2 definido pelo usuário
