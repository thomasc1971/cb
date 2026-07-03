AM_CFLAGS = $(LIBTLS_CFLAGS) -I$(top_srcdir)/include -I$(top_builddir)

# Regenerate cb_version.h on every `make` so the embedded version string
# tracks the current git HEAD. The recipe always runs, but the file is only
# rewritten when the version actually changes — so dependent .o files stay
# cached when nothing changed.
BUILT_SOURCES = cb_version.h
CLEANFILES = cb_version.h

.PHONY: cb_version_force
cb_version.h: cb_version_force
	@v=`"$(top_srcdir)/build-aux/git-version.sh" "$(top_srcdir)" 2>/dev/null`; \
	test -n "$$v" || v="$(CB_VERSION)"; \
	test -n "$$v" || v="unknown"; \
	printf '#ifndef CB_VERSION_H\n#define CB_VERSION_H\n#define CB_VERSION "%s"\n#endif\n' "$$v" > $@.tmp; \
	if cmp -s $@.tmp $@ 2>/dev/null; then \
	  rm -f $@.tmp; \
	else \
	  mv $@.tmp $@; \
	  echo "  GEN      $@ ($$v)"; \
	fi
cb_version_force:

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
