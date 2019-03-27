$ $FreeBSD$
$
$ Message catalog for ja_JP.UTF-8 locale
$
$ strerror() support catalog
$
$set 1
$ EPERM
1 許可されていない操作です
$ ENOENT
2 そのようなファイルまたはディレクトリはありません
$ ESRCH
3 そのようなプロセスはありません
$ EINTR
4 システムコールが中断されました
$ EIO
5 入出力エラーです
$ ENXIO
6 デバイスが準備されていません
$ E2BIG
7 引数のリストが長すぎます
$ ENOEXEC
8 無効な実行形式です
$ EBADF
9 無効なファイル記述子です
$ ECHILD
10 子プロセスがありません
$ EDEADLK
11 リソースデッドロックを回避しました
$ ENOMEM
12 メモリの割り当てができません
$ EACCES
13 パーミッションが拒絶されました
$ EFAULT
14 無効なアドレスです
$ ENOTBLK
15 ブロックデバイスが要求されています
$ EBUSY
16 デバイスがビジー状態です
$ EEXIST
17 ファイルが存在します
$ EXDEV
18 デバイスをまたぐリンクです
$ ENODEV
19 デバイスが対応してない操作です
$ ENOTDIR
20 ディレクトリではありません
$ EISDIR
21 ディレクトリです
$ EINVAL
22 無効な引数です
$ ENFILE
23 システム内でオープンされているファイルが多すぎます
$ EMFILE
24 オープンしているファイルが多すぎます
$ ENOTTY
25 デバイスが対応していない ioctl です
$ ETXTBSY
26 テキストファイルがビジー状態です
$ EFBIG
27 ファイルが大きすぎます
$ ENOSPC
28 デバイスの空き領域不足です
$ ESPIPE
29 無効なシークです
$ EROFS
30 読み込み専用ファイルシステムです
$ EMLINK
31 リンク数が多すぎます
$ EPIPE
32 パイプが破壊されてました
$ EDOM
33 数値引数が範囲外です
$ ERANGE
34 結果が大き過ぎます
$ EAGAIN, EWOULDBLOCK
35 リソースが一時的に利用できません
$ EINPROGRESS
36 操作が現在進行中です
$ EALREADY
37 操作は既に進行中です
$ ENOTSOCK
38 ソケットでないものについてソケット操作を行いました
$ EDESTADDRREQ
39 宛先アドレスが要求されています
$ EMSGSIZE
40 メッセージが長すぎます
$ EPROTOTYPE
41 ソケットが対応していないプロトコルタイプです
$ ENOPROTOOPT
42 利用できないプロトコルです
$ EPROTONOSUPPORT
43 対応していないプロトコルです
$ ESOCKTNOSUPPORT
44 対応していないソケットタイプです
$ EOPNOTSUPP
45 対応していない操作です
$ EPFNOSUPPORT
46 対応していないプロトコルファミリです
$ EAFNOSUPPORT
47 プロトコルファミリが対応していないアドレスファミリが指定されました
$ EADDRINUSE
48 アドレスが既に使用中です
$ EADDRNOTAVAIL
49 要求されたアドレスを割り当てできません
$ ENETDOWN
50 ネットワークがダウンしています
$ ENETUNREACH
51 ネットワークに到達できません
$ ENETRESET
52 リセットによりネットワークの接続が失われました
$ ECONNABORTED
53 ソフトウェアによって接続が切断されました
$ ECONNRESET
54 接続が通信相手によってリセットされました
$ ENOBUFS
55 バッファの容量不足です
$ EISCONN
56 ソケットは既に接続されています
$ ENOTCONN
57 ソケットは接続されていません
$ ESHUTDOWN
58 ソケットのシャットダウンの後で送信ができません
$ ETOOMANYREFS
59 処理限界を超える多重参照です
$ ETIMEDOUT
60 操作がタイムアウトしました
$ ECONNREFUSED
61 接続が拒絶されました
$ ELOOP
62 処理限界を超えるシンボリックリンクレベルです
$ ENAMETOOLONG
63 ファイル名が長すぎます
$ EHOSTDOWN
64 ホストがダウンしています
$ EHOSTUNREACH
65 ホストへの経路がありません
$ ENOTEMPTY
66 ディレクトリが空ではありません
$ EPROCLIM
67 プロセスが多すぎます
$ EUSERS
68 ユーザが多すぎます
$ EDQUOT
69 ディスククォータが超過しました
$ ESTALE
70 失効した NFS ファイルハンドルです
$ EREMOTE
71 パス中のリモートのレベルが多すぎます
$ EBADRPC
72 無効な RPC 構造体です
$ ERPCMISMATCH
73 RPC バージョンが間違っています
$ EPROGUNAVAIL
74 RPC プログラムが利用できません
$ EPROGMISMATCH
75 プログラムのバージョンが合っていません
$ EPROCUNAVAIL
76 プログラムでは利用できない procedure です
$ ENOLCK
77 ロックが利用できません
$ ENOSYS
78 関数が実装されていません
$ EFTYPE
79 ファイルの型または形式が不適切です
$ EAUTH
80 認証エラーです
$ ENEEDAUTH
81 認証物が必要です
$ EIDRM
82 識別子は削除されました
$ ENOMSG
83 要求された型のメッセージがありません
$ EOVERFLOW
84 データ型に格納するには大きすぎる値です
$ ECANCELED
85 処理がキャンセルされました
$ EILSEQ
86 不正なバイト列です
$ ENOATTR
87 そのような属性はありません
$ EDOOFUS
88 プログラミングエラーです
$ EBADMSG
89 無効なメッセージです
$ EMULTIHOP
90 マルチホップが試みられました
$ ENOLINK
91 リンクが切断されています
$ EPROTO
92 プロトコルエラーです
$ ENOTCAPABLE
93 ケーパビリティが不足です
$
$ strsignal() support catalog
$
$set 2
$ SIGHUP
1 ハングアップ
$ SIGINT
2 割り込み
$ SIGQUIT
3 中断
$ SIGILL
4 不正命令
$ SIGTRAP
5 トレース/BPT トラップ
$ SIGABRT
6 アボートトラップ
$ SIGEMT
7 EMT トラップ
$ SIGFPE
8 浮動小数点例外
$ SIGKILL
9 Kill された
$ SIGBUS
10 バスエラー
$ SIGSEGV
11 セグメンテーション違反
$ SIGSYS
12 存在しないシステムコール
$ SIGPIPE
13 パイプ破壊
$ SIGALRM
14 アラームクロック
$ SIGTERM
15 終了
$ SIGURG
16 緊急入出力状況
$ SIGSTOP
17 一時停止 (シグナル)
$ SIGTSTP
18 一時停止
$ SIGCONT
19 継続
$ SIGCHLD
20 子プロセスの終了
$ SIGTTIN
21 一時停止 (tty 入力)
$ SIGTTOU
22 一時停止 (tty 出力)
$ SIGIO
23 入出力可能
$ SIGXCPU
24 CPU 時間の制限超過
$ SIGXFSZ
25 ファイルサイズの制限超過
$ SIGVTALRM
26 仮想タイマの期限超過
$ SIGPROF
27 プロファイルタイマの期限超過
$ SIGWINCH
28 ウィンドウサイズの変化
$ SIGINFO
29 情報要求
$ SIGUSR1
30 ユーザ定義シグナル 1
$ SIGUSR2
31 ユーザ定義シグナル 2
$
$ gai_strerror() support catalog
$
$set 3
$ 1 (obsolete)
1 ホスト名のアドレスファミリーはサポートされません
$ EAI_AGAIN
2 名前解決での一時的な失敗
$ EAI_BADFLAGS
3 ai_flags の値が無効
$ EAI_FAIL
4 名前解決での回復不能な失敗
$ EAI_FAMILY
5 ai_family はサポートされません
$ EAI_MEMORY
6 メモリ割り当て失敗
$ 7 (obsolete)
7 ホスト名に対応するアドレスはありません
$ EAI_NONAME
8 ホスト名かサービス名が指定されない、または不明
$ EAI_SERVICE
9 サービス名は ai_socktype に対してサポートされません
$ EAI_SOCKTYPE
10 ai_socktype はサポートされません
$ EAI_SYSTEM
11 システムエラー、errno 参照
$ EAI_BADHINTS
12 hints の値が無効
$ EAI_PROTOCOL
13 解決されたプロトコルは不明です
$ EAI_OVERFLOW
14 引数バッファオーバフロー
$ 0
32766 成功
$ NL_MSGMAX
32767 不明なエラー
