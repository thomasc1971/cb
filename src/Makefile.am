AM_CFLAGS = $(LIBTLS_CFLAGS) -I$(top_srcdir)/include -I$(top_builddir)

bin_PROGRAMS = cb
cb_SOURCES = \
    main.c \
    cb_json.c \
    cb_http.c \
    cb_config.c \
    cb_validate.c \
    cb_api.c \
    cb_cli.c \
    cb_compat.c
cb_LDADD = $(LIBTLS_LIBS) -lm
