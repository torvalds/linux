# dnstap.m4

# dt_DNSTAP(default_dnstap_socket_path, [action-if-true], [action-if-false])
# --------------------------------------------------------------------------
# Check for required dnstap libraries and add dnstap configure args.
AC_DEFUN([dt_DNSTAP],
[
    AC_ARG_ENABLE([dnstap],
        AS_HELP_STRING([--enable-dnstap],
            [Enable dnstap support (requires protobuf-c)]),
        [opt_dnstap=$enableval],
        [opt_dnstap=no])

    AC_ARG_WITH([dnstap-socket-path],
        AS_HELP_STRING([--with-dnstap-socket-path=pathname],
            [set default dnstap socket path]),
        [opt_dnstap_socket_path=$withval],
        [opt_dnstap_socket_path="$1"])

    if test "x$opt_dnstap" != "xno"; then
        AC_PATH_PROG([PROTOC_C], [protoc-c])
        if test -z "$PROTOC_C"; then
          AC_MSG_ERROR([The protoc-c program was not found. Please install protobuf-c!])
        fi
        AC_ARG_WITH([protobuf-c],
            AS_HELP_STRING([--with-protobuf-c=path], [Path where protobuf-c is installed, for dnstap]),
            [
                # workaround for protobuf-c includes at old dir before protobuf-c-1.0.0
                if test -f $withval/include/google/protobuf-c/protobuf-c.h; then
                    CFLAGS="$CFLAGS -I$withval/include/google"
                else
                    CFLAGS="$CFLAGS -I$withval/include"
                fi
                LDFLAGS="$LDFLAGS -L$withval/lib"
            ],
            [
                if test -n "$PKG_CONFIG"; then
                    PKG_CHECK_MODULES([PROTOBUFC], [libprotobuf-c],
                        [
                            CFLAGS="$CFLAGS $PROTOBUFC_CFLAGS"
                            LIBS="$LIBS $PROTOBUFC_LIBS"
                        ],
                        [
                            # pkg-config failed; try falling back to known values
                            # workaround for protobuf-c includes at old dir before protobuf-c-1.0.0
                            if test -f /usr/include/google/protobuf-c/protobuf-c.h; then
                                CFLAGS="$CFLAGS -I/usr/include/google"
                            else
                                if test -f /usr/local/include/google/protobuf-c/protobuf-c.h; then
                                    CFLAGS="$CFLAGS -I/usr/local/include/google"
                                    LDFLAGS="$LDFLAGS -L/usr/local/lib"
                                else
                                    AC_MSG_ERROR([The protobuf-c package was not found with pkg-config. Please install protobuf-c!])
                                fi
                            fi
                        ]
                    )
                else
                    # workaround for protobuf-c includes at old dir before protobuf-c-1.0.0
                    if test -f /usr/include/google/protobuf-c/protobuf-c.h; then
                        CFLAGS="$CFLAGS -I/usr/include/google"
                    else
                        if test -f /usr/local/include/google/protobuf-c/protobuf-c.h; then
                            CFLAGS="$CFLAGS -I/usr/local/include/google"
                            LDFLAGS="$LDFLAGS -L/usr/local/lib"
                        fi
                    fi
                fi
            ]
        )
        AC_SEARCH_LIBS([protobuf_c_message_pack], [protobuf-c], [],
            AC_MSG_ERROR([The protobuf-c library was not found. Please install the development libraries for protobuf-c!]))
        $2
    else
        $3
    fi
])
