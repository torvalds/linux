$ $FreeBSD$
$
$ Message catalog for zh_CN.GB2312 locale
$
$ Derived from FreeBSD: head/lib/libc/nls/zh_CN.UTF-8.msg 244756 2012-12-28 01:09:30Z delphij
$
$ strerror() support catalog
$
$set 1
$ EPERM
1 不允许的操作
$ ENOENT
2 文件或目录不存在
$ ESRCH
3 进程不存在
$ EINTR
4 系统调用中止
$ EIO
5 输入/输出错误
$ ENXIO
6 未配置的设备
$ E2BIG
7 参数表过长
$ ENOEXEC
8 可执行文件格式错误
$ EBADF
9 文件描述符无效
$ ECHILD
10 指定子进程不存在
$ EDEADLK
11 此操作会导致死锁
$ ENOMEM
12 无法分配内存
$ EACCES
13 拒绝访问
$ EFAULT
14 无效地址
$ ENOTBLK
15 该操作需要块设备
$ EBUSY
16 设备使用中
$ EEXIST
17 文件已存在
$ EXDEV
18 链接跨设备
$ ENODEV
19 设备不支持此操作
$ ENOTDIR
20 对象非目录
$ EISDIR
21 对象为目录
$ EINVAL
22 参数无效
$ ENFILE
23 系统打开文件过多
$ EMFILE
24 进程打开文件过多
$ ENOTTY
25 无效设备 ioctl
$ ETXTBSY
26 可执行文件忙
$ EFBIG
27 文件过大
$ ENOSPC
28 设备无可用空间
$ ESPIPE
29 不允许执行 seek 操作
$ EROFS
30 文件系统只读
$ EMLINK
31 文件链接过多
$ EPIPE
32 管道已中止
$ EDOM
33 数值参数越界
$ ERANGE
34 结果过大
$ EAGAIN, EWOULDBLOCK
35 资源暂时不可用
$ EINPROGRESS
36 操作进行中
$ EALREADY
37 操作已开始
$ ENOTSOCK
38 尝试在非 socket 上执行 socket 操作
$ EDESTADDRREQ
39 需要目的地址
$ EMSGSIZE
40 消息过长
$ EPROTOTYPE
41 socket协议类型错误
$ ENOPROTOOPT
42 协议不可用
$ EPROTONOSUPPORT
43 不支持的协议
$ ESOCKTNOSUPPORT
44 不支持的 socket 类型
$ EOPNOTSUPP
45 不支持的操作
$ EPFNOSUPPORT
46 不支持的协议族
$ EAFNOSUPPORT
47 协议族不支持的地址族
$ EADDRINUSE
48 地址已被占用
$ EADDRNOTAVAIL
49 无法指定请求的地址
$ ENETDOWN
50 网络已关闭
$ ENETUNREACH
51 网络不可达
$ ENETRESET
52 复位导致网络连接丢失
$ ECONNABORTED
53 软件导致的连接中止
$ ECONNRESET
54 对方复位了连接
$ ENOBUFS
55 缓冲区空间不足
$ EISCONN
56 socket 已连接
$ ENOTCONN
57 socket 未连接
$ ESHUTDOWN
58 socket shutdown 之后无法发送数据
$ ETOOMANYREFS
59 引用数过多：无法拼接
$ ETIMEDOUT
60 操作超时
$ ECONNREFUSED
61 拒绝连接
$ ELOOP
62 符号链接层数过多
$ ENAMETOOLONG
63 文件名过长
$ EHOSTDOWN
64 主机已关闭
$ EHOSTUNREACH
65 没有到主机的路由
$ ENOTEMPTY
66 目录非空
$ EPROCLIM
67 进程数超限
$ EUSERS
68 用户数超限
$ EDQUOT
69 磁盘空间配额超限
$ ESTALE
70 NFS 文件句柄已失效
$ EREMOTE
71 远程目录层数过多
$ EBADRPC
72 RPC 结构无效
$ ERPCMISMATCH
73 RPC 版本错误
$ EPROGUNAVAIL
74 RPC 程序不可用
$ EPROGMISMATCH
75 程序版本错误
$ EPROCUNAVAIL
76 未提供的远程函数
$ ENOLCK
77 不支持锁
$ ENOSYS
78 功能未实现
$ EFTYPE
79 文件类型或格式无效
$ EAUTH
80 身份无效
$ ENEEDAUTH
81 无信任凭据
$ EIDRM
82 无标识符
$ ENOMSG
83 无消息类型
$ EOVERFLOW
84 数值溢出
$ ECANCELED
85 操作已取消
$ EILSEQ
86 无效字符序列
$ ENOATTR
87 无扩展属性
$ EDOOFUS
88 程序设计错误
$ EBADMSG
89 无效消息
$ EMULTIHOP
90 Multihop attempted
$ ENOLINK
91 Link has been severed
$ EPROTO
92 协议错误
$ ENOTCAPABLE
93 权能不足
$
$ strsignal() support catalog
$
$set 2
$ SIGHUP
1 终端线路挂断
$ SIGINT
2 中断
$ SIGQUIT
3 退出
$ SIGILL
4 无效指令
$ SIGTRAP
5 跟踪/BPT 陷阱
$ SIGABRT
6 终止陷阱
$ SIGEMT
7 EMT 陷阱
$ SIGFPE
8 浮点异常
$ SIGKILL
9 强制终止进程
$ SIGBUS
10 系统内存访问越界
$ SIGSEGV
11 内存访问越界
$ SIGSYS
12 无效系统调用
$ SIGPIPE
13 写管道时读取者不存在
$ SIGALRM
14 实时倒计时到期
$ SIGTERM
15 终止
$ SIGURG
16 需立即处理的 I/O 紧急状况
$ SIGSTOP
17 挂起 (信号)
$ SIGTSTP
18 挂起
$ SIGCONT
19 继续运行
$ SIGCHLD
20 子进程终止
$ SIGTTIN
21 TTY 输入阻塞
$ SIGTTOU
22 TTY 输出阻塞
$ SIGIO
23 I/O 就绪
$ SIGXCPU
24 CPU 使用时间过长
$ SIGXFSZ
25 文件尺寸过大
$ SIGVTALRM
26 虚拟倒计时到期
$ SIGPROF
27 剖析倒计时到期
$ SIGWINCH
28 窗口尺寸变化
$ SIGINFO
29 请求信息
$ SIGUSR1
30 用户自定义信号 1
$ SIGUSR2
31 用户自定义信号 2
$
$ gai_strerror() support catalog
$
$set 3
$ 1 (obsolete)
1 主机名使用了不支持的地址类型
$ EAI_AGAIN
2 暂时无法解析名称
$ EAI_BADFLAGS
3 无效的 ai_flags 值
$ EAI_FAIL
4 解析名称时出现无法恢复的错误
$ EAI_FAMILY
5 不支持的地址信息类型
$ EAI_MEMORY
6 内存分配失败
$ 7 (obsolete)
7 主机名无相关联的地址
$ EAI_NONAME
8 未知的主机名或服务名
$ EAI_SERVICE
9 套接字类型不支持此服务名
$ EAI_SOCKTYPE
10 不支持的套接字类型
$ EAI_SYSTEM
11 系统调用返回了 errno 错误
$ EAI_BADHINTS
12 无效提示参数
$ EAI_PROTOCOL
13 未知协议
$ EAI_OVERFLOW
14 参数缓冲溢出
$ 0
32766 成功
$ NL_MSGMAX
32767 未知错误
