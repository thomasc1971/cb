ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src tests

EXTRA_DIST = \
    autogen.sh \
    build-aux/git-version.sh \
    build-aux/gen-completions.sh \
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
	@if command -v shfmt >/dev/null 2>&1; then \
	    echo "Formatting shell scripts with shfmt..."; \
	    find $(srcdir)/build-aux -name '*.sh' -print0 \
	        | xargs -0 shfmt -w; \
	else \
	    echo "shfmt not found - skipping shell formatting." >&2; \
	fi

completions:
	@CB=$(top_builddir)/src/cb; \
	if [ ! -x "$$CB" ]; then \
	    echo "Building cb first..."; \
	    $(MAKE) -C src cb; \
	    CB=$(top_builddir)/src/cb; \
	fi; \
	$(SHELL) "$(top_srcdir)/build-aux/gen-completions.sh" "$$CB" "$(top_builddir)/completions"

install-data-hook: install-completions

install-completions: completions
	@$(MKDIR_P) "$(DESTDIR)$(bashcompdir)" && \
	    $(INSTALL_DATA) "$(top_builddir)/completions/cb.bash" "$(DESTDIR)$(bashcompdir)/cb"; \
	echo "  BASH   $(bashcompdir)/cb"
	@$(MKDIR_P) "$(DESTDIR)$(zshcompdir)" && \
	    $(INSTALL_DATA) "$(top_builddir)/completions/cb.zsh" "$(DESTDIR)$(zshcompdir)/_cb"; \
	echo "  ZSH    $(zshcompdir)/_cb"
	@$(MKDIR_P) "$(DESTDIR)$(fishcompdir)" && \
	    $(INSTALL_DATA) "$(top_builddir)/completions/cb.fish" "$(DESTDIR)$(fishcompdir)/cb.fish"; \
	echo "  FISH   $(fishcompdir)/cb.fish"

uninstall-hook:
	rm -f "$(DESTDIR)$(bashcompdir)/cb"
	rm -f "$(DESTDIR)$(zshcompdir)/_cb"
	rm -f "$(DESTDIR)$(fishcompdir)/cb.fish"

bear:
	@command -v bear >/dev/null 2>&1 || { echo "bear not found — install bear (e.g. dnf install bear)" >&2; exit 1; }
	bear -- $(MAKE)

.PHONY: format bear completions install-completions
