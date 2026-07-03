ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src tests

EXTRA_DIST = \
    autogen.sh \
    build-aux/git-version.sh \
    version

# Stamp the version file into the dist tarball so tarball builds can read it
# even without .git. The file is gitignored so it won't be included automatically.
dist-hook:
	$(SHELL) "$(top_srcdir)/build-aux/git-version.sh" "$(top_srcdir)" > "$(distdir)/version"

format:
	@if command -v clang-format >/dev/null 2>&1; then \
	    echo "Formatting C with clang-format..."; \
	    find $(srcdir)/src $(srcdir)/include $(srcdir)/tests \
	        \( -name '*.c' -o -name '*.h' \) -print0 \
	        | xargs -0 clang-format -i; \
	else \
	    echo "clang-format not found - skipping C formatting." >&2; \
	fi
	@if command -v prettier >/dev/null 2>&1; then \
	    echo "Formatting Markdown with prettier..."; \
	    find $(srcdir) -maxdepth 2 -name '*.md' -print0 \
	        | xargs -0 prettier --write; \
	else \
	    echo "prettier not found - skipping Markdown formatting." >&2; \
	fi

bear:
	@command -v bear >/dev/null 2>&1 || { echo "bear not found — install bear (e.g. dnf install bear)" >&2; exit 1; }
	bear -- $(MAKE)

.PHONY: format bear
