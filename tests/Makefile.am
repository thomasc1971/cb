AM_CFLAGS = $(LIBTLS_CFLAGS) \
    -I$(top_srcdir)/include -I$(top_builddir) -I$(srcdir) $(PTHREAD_CFLAGS)
AM_LDFLAGS = $(LIBTLS_LIBS)

EXTRA_DIST = test_helpers.h mock_server.h

check_LIBRARIES = libmockserver.a
libmockserver_a_SOURCES = mock_server.c
libmockserver_a_CFLAGS = $(AM_CFLAGS)

check_PROGRAMS = test_json test_http test_config test_validate test_api test_cli test_actions test_new_api

test_json_SOURCES = test_json.c $(top_srcdir)/src/cb_json.c $(top_srcdir)/src/cb_compat.c
test_json_LDADD = $(LIBTLS_LIBS) -lm

test_http_SOURCES = test_http.c $(top_srcdir)/src/cb_http.c $(top_srcdir)/src/cb_json.c \
    $(top_srcdir)/src/cb_compat.c
test_http_LDADD = libmockserver.a $(PTHREAD_LIBS) $(LIBTLS_LIBS) -lm

test_config_SOURCES = test_config.c $(top_srcdir)/src/cb_config.c $(top_srcdir)/src/cb_compat.c
test_config_LDADD = $(LIBTLS_LIBS)

test_validate_SOURCES = test_validate.c $(top_srcdir)/src/cb_validate.c
test_validate_LDADD = $(LIBTLS_LIBS)

test_api_SOURCES = test_api.c $(top_srcdir)/src/cb_api.c $(top_srcdir)/src/cb_http.c \
    $(top_srcdir)/src/cb_json.c $(top_srcdir)/src/cb_validate.c $(top_srcdir)/src/cb_config.c \
    $(top_srcdir)/src/cb_compat.c
test_api_LDADD = libmockserver.a $(PTHREAD_LIBS) $(LIBTLS_LIBS) -lm

test_cli_SOURCES = test_cli.c $(top_srcdir)/src/cb_cli.c $(top_srcdir)/src/cb_api.c \
    $(top_srcdir)/src/cb_http.c $(top_srcdir)/src/cb_json.c $(top_srcdir)/src/cb_validate.c \
    $(top_srcdir)/src/cb_config.c $(top_srcdir)/src/cb_compat.c
test_cli_LDADD = libmockserver.a $(PTHREAD_LIBS) $(LIBTLS_LIBS) -lm

test_actions_SOURCES = test_actions.c $(top_srcdir)/src/cb_api.c $(top_srcdir)/src/cb_http.c \
    $(top_srcdir)/src/cb_json.c $(top_srcdir)/src/cb_validate.c $(top_srcdir)/src/cb_config.c \
    $(top_srcdir)/src/cb_compat.c
test_actions_LDADD = libmockserver.a $(PTHREAD_LIBS) $(LIBTLS_LIBS) -lm

test_new_api_SOURCES = test_new_api.c $(top_srcdir)/src/cb_api.c $(top_srcdir)/src/cb_http.c \
    $(top_srcdir)/src/cb_json.c $(top_srcdir)/src/cb_validate.c $(top_srcdir)/src/cb_config.c \
    $(top_srcdir)/src/cb_compat.c
test_new_api_LDADD = libmockserver.a $(PTHREAD_LIBS) $(LIBTLS_LIBS) -lm

TESTS = $(check_PROGRAMS)
