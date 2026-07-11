# cb — Codeberg (Forgejo) Repository Management CLI

A command-line tool for managing repositories, issues, pull requests, releases, and CI/CD actions on Codeberg and any Forgejo or Gitea instance. Written in C11 with minimal runtime dependencies (libtls for HTTPS).

## Features

### Repository management

- Create, delete, rename repositories
- Edit repository metadata (description, visibility, features, merge styles, etc.)
- Show repository details
- List repositories (own, user, or organization)
- Transfer repository ownership
- Manage repository topics (add, remove, list, set)

### Releases

- List, create, show, edit, delete releases
- Show latest release or release by tag
- Delete release by tag
- List, upload, edit, delete release assets
- Show release asset details

### Tags

- List, create, show, delete tags

### Branches

- List, create, show, rename, delete branches

### Issues

- List, create, show, edit, delete issues
- Close and reopen issues (shorthands)
- Add comments
- Manage issue labels (add, set, clear, remove)

### Labels

- List, create, edit, delete repository labels
- Show label details

### Milestones

- List, create, edit, delete milestones
- Show milestone details

### Pull requests

- List, create, show, edit, merge pull requests
- Close and reopen PRs (shorthands)
- Merge with style selection (merge, rebase, squash, rebase-merge)
- Auto-merge when checks succeed
- List changed files, commits, diff _(not yet implemented)_
- Manage reviews _(not yet implemented)_

### Commits

- List commits with ref/path filtering
- Show commit details, status, diff _(not yet implemented)_
- Compare refs _(not yet implemented)_
- Manage git notes _(not yet implemented)_

### File contents

- List directory contents
- Create, update, delete files
- Show file details, raw content _(not yet implemented)_
- Download archives _(not yet implemented)_

### Organizations

- Create organizations

### Deploy keys

- List, add, delete deploy keys
- Show deploy key details

### Collaborators

- List, add, remove collaborators
- View collaborator permissions _(not yet implemented)_

### Forks

- List forks, fork a repository

### Webhooks

- List, create, edit, delete webhooks
- Test webhooks
- Show webhook details _(not yet implemented)_

### Wiki

- List, create, edit, delete wiki pages
- View page revisions _(not yet implemented)_
- Show wiki page details _(not yet implemented)_

### CI/CD actions

- List and inspect workflow runs
- View job details and step statuses
- Fetch and display build logs (with step-level filtering)
- List available runners
- Dispatch workflows
- Manage action secrets (list, set, delete)
- Manage action variables (list, show, set, delete)

### General

- JSON output mode for scripting (`--json`)
- Client-side validation with clear error messages
- Token scope error detection with actionable guidance
- Manage your account's SSH public keys (`sshkey list/add/show/rm`)
- Cross-platform: Linux, macOS, and Windows (MSYS2 UCRT64)

## Download

Pre-built binaries are available from the [Codeberg releases page](https://codeberg.org/thomasc/cb/releases).

| File                               | Platform       | Contents                                  |
| ---------------------------------- | -------------- | ----------------------------------------- |
| `cb-<version>-linux-amd64.tar.gz`  | Linux x86_64   | `cb` binary + `COPYING` + `README.md`     |
| `cb-<version>-darwin-amd64.tar.gz` | macOS x86_64   | `cb` binary + `COPYING` + `README.md`     |
| `cb-<version>-windows-amd64.zip`   | Windows x86_64 | `cb.exe` + DLLs + `COPYING` + `README.md` |

The Linux and macOS tarballs contain the `cb` binary, GPLv3 license text, and README. The macOS build statically links LibreSSL, so there are no external dylib dependencies. The Windows ZIP contains `cb.exe`, the required LibreSSL DLLs, the license text, and README.

## Build

### Prerequisites

- GCC or Clang (C11)
- libretls (Linux/macOS) or LibreSSL (MSYS2) — provides the `libtls` API
- autoconf, automake, make
- pkg-config

| Platform      | Install command                                                                                                              |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| Fedora        | `dnf install gcc libretls-devel pkg-config autoconf automake make`                                                           |
| Debian/Ubuntu | `apt install gcc libretls-dev pkg-config autoconf automake make`                                                             |
| macOS         | `brew install libretls pkg-config autoconf automake make`                                                                    |
| MSYS2 UCRT64  | `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libressl mingw-w64-ucrt-x86_64-pkg-config autoconf automake make` |

> **MSYS2 UCRT64** has extra setup — see the [MSYS2 build section](#building-on-msys2-ucrt64-windows) below.

### Build

```bash
./autogen.sh           # Generate version file + configure + Makefile.in (first time only)
./configure            # Standard build
make                   # Build the binary
./cb                   # Run it
```

<details>
<summary>Building on MSYS2 UCRT64 (Windows)</summary>

libretls is not packaged for MSYS2; LibreSSL provides libtls instead. The UCRT64 toolchain uses Winsock2 instead of POSIX sockets — `cb_compat.h` provides a portable socket layer that conditionally uses Winsock2 on Windows and POSIX sockets elsewhere.

```bash
# From an MSYS2 UCRT64 shell:
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libressl \
          mingw-w64-ucrt-x86_64-pkg-config autoconf automake make

export PKG_CONFIG_PATH=/ucrt64/lib/libressl/pkgconfig
autoreconf -i
./configure CPPFLAGS=-I/ucrt64/include/libressl LDFLAGS=-L/ucrt64/lib/libressl
make
make check               # All 8 test suites pass
```

> **MSYS2 pkg-config path issue:** The UCRT64 pkg-config rewrites `prefix` paths to Windows-style paths that the MSYS2 GCC cannot resolve. Passing `CPPFLAGS` and `LDFLAGS` with MSYS2-style paths works around this. The `cb_compat.h` header conditionally uses Winsock2 on Windows and POSIX sockets elsewhere, so no source changes are needed.

</details>

### Test

```bash
make check               # Run all 8 test suites
```

### Development build with ASan + UBSan

```bash
./configure --enable-asan
make check
```

### clangd / IDE support

The project includes a `.clangd` config and a `make bear` target for clangd-based editors (VS Code, Neovim, Helix, etc.).

```bash
./autogen.sh && ./configure        # Must configure first (generates config.h)
make clean                         # Bear only intercepts commands that actually run
make bear                          # Generates compile_commands.json
```

`make bear` wraps the build with [bear](https://github.com/rizsotto/bear) to produce `compile_commands.json`, giving clangd the exact compiler flags, include paths, and defines from `configure`. The `.clangd` config forces C language mode (`-xc`) to prevent false Objective-C++ errors on struct fields named `private`, `protected`, and `template`.

### Install

```bash
./configure --prefix=$HOME/.local
make install
```

`make install` installs the binary and shell completions for Bash, Zsh, and Fish. Completion directories are auto-detected via `pkg-config` (Bash) or use standard defaults; override them with `--with-bash-completion-dir`, `--with-zsh-completion-dir`, or `--with-fish-completion-dir`.

### Shell completions (manual)

If you prefer to install completions manually, generate them from the built binary:

```bash
make completions
```

This runs `cb --help-spec` and produces `completions/cb.bash`, `completions/cb.zsh`, and `completions/cb.fish`.

**Bash:**

```bash
cp completions/cb.bash ~/.local/share/bash-completion/completions/cb
```

**Zsh:**

```bash
cp completions/cb.zsh ~/.zsh/completions/_cb
```

Add `~/.zsh/completions` to `$fpath` in `~/.zshrc` if not already present.

**Fish:**

```bash
cp completions/cb.fish ~/.config/fish/completions/cb.fish
```

## Configuration

### Token

Generate a token at `https://codeberg.org/user/settings/applications`. The token needs these scopes:

| Scope                | Needed for                                                         |
| -------------------- | ------------------------------------------------------------------ |
| `write:repository`   | All repo CRUD and actions operations                               |
| `write:organization` | Creating/listing repos under organizations, creating organizations |
| `read:user`          | Listing other users' repos                                         |
| `all`                | Everything (easy but broad)                                        |

### Config sources (precedence high → low)

1. `--base-url` CLI flag
2. `CB_BASE_URL` env var
3. `~/.config/cb/config` file (`%APPDATA%\cb\config` on Windows, or `$XDG_CONFIG_HOME/cb/config` if `XDG_CONFIG_HOME` is set)
4. Default: `https://codeberg.org/api/v1`

Token is read from `CB_TOKEN` env var or the config file:

```toml
# ~/.config/cb/config (mode 0600)
# %APPDATA%\cb\config on Windows (or $XDG_CONFIG_HOME/cb/config if set)
token = "your-token-here"
base_url = "https://codeberg.org/api/v1"
```

## Usage

```
cb [global flags] <command> [subcommand] [args] [flags]
```

### Global flags

| Flag              | Description                                                                 |
| ----------------- | --------------------------------------------------------------------------- |
| `--json`          | Output raw JSON instead of human-readable                                   |
| `--quiet`, `-q`   | Suppress non-essential output (lists show names only, show commands silent) |
| `--base-url URL`  | Override API base URL                                                       |
| `--yes`           | Skip confirmation prompts                                                   |
| `--version`, `-v` | Show version                                                                |
| `--help`, `-h`    | Show help at any command level                                              |

### Getting help

`--help` / `-h` works at every level of the command tree:

```bash
cb --help                    # top-level help
cb repo --help               # repo subcommands
cb repo create --help         # create flags
cb repo edit --help           # edit flags
cb repo topic --help          # topic subcommands
cb repo topic add --help     # topic add usage
cb release --help            # release subcommands
cb issue --help              # issue subcommands
cb pr --help                 # PR subcommands
cb actions --help             # actions subcommands
cb actions log --help         # actions log usage
cb org create --help          # org create flags
```

### Commands

In commands that take `[owner/]repo`, the `owner/` part is optional — if omitted, it is automatically filled in from your authenticated user. For example, `cb repo show myproj` is equivalent to `cb repo show thomasc/myproj`.

#### `cb repo create <name>`

```bash
cb repo create myproj --private -d "side project"
cb repo create myproj --org myorg --license MIT --auto-init
cb repo create myproj --object-format sha256
```

Flags: `--private`, `--public`, `--description`/`-d`, `--default-branch`/`-b`, `--license`, `--gitignore`/`-g`, `--auto-init`, `--template`, `--org`, `--object-format`

#### `cb repo delete [owner/]repo`

```bash
cb repo delete thomasc/abandoned-proj --yes
cb repo delete abandoned-proj --yes       # owner defaults to you
```

Requires `--yes` or interactive confirmation.

#### `cb repo rename [owner/]repo <new-name>`

```bash
cb repo rename thomasc/old-name new-name
```

#### `cb repo edit [owner/]repo`

Only provided flags are sent — unset fields are not modified.

```bash
cb repo edit thomasc/myproj -d "new description" --public
cb repo edit thomasc/myproj --archived --no-issues
cb repo edit thomasc/myproj --default-merge-style squash
```

Flags (each has `--no-*` counterpart for bools): `--description`/`-d`, `--website`/`-w`, `--private`/`--public`, `--default-branch`/`-b`, `--archived`/`--unarchived`, `--template`/`--no-template`, `--has-issues`/`--no-issues`, `--has-wiki`/`--no-wiki`, `--has-prs`/`--no-prs`, `--has-projects`/`--no-projects`, `--has-releases`/`--no-releases`, `--has-packages`/`--no-packages`, `--has-actions`/`--no-actions`, `--allow-merge`/`--no-merge`, `--allow-rebase`/`--no-rebase`, `--allow-squash`/`--no-squash`, `--allow-ff-only`/`--no-ff-only`, `--default-merge-style`, `--delete-branch-after-merge`/`--no-delete-branch-after-merge`, `--allow-maintainer-edit`/`--no-allow-maintainer-edit`

#### `cb repo show [owner/]repo`

```bash
cb repo show thomasc/myproj
cb repo show myproj                       # owner defaults to you
cb repo show thomasc/myproj --json
```

#### `cb repo list [--user U | --org O]`

```bash
cb repo list              # own repos
cb repo list --org myorg  # org repos
cb repo list --user bob   # another user's repos
```

#### `cb repo transfer [owner/]repo <new-owner>`

```bash
cb repo transfer thomasc/myproj codeberg-org --yes
```

Requires `--yes` or interactive confirmation.

#### `cb repo topic <add|rm|list|set>`

```bash
cb repo topic add thomasc/myproj go
cb repo topic rm thomasc/myproj go
cb repo topic list thomasc/myproj
cb repo topic set thomasc/myproj go,cli,codeberg
```

#### `cb release [owner/]repo <subcommand>`

Manage releases.

```bash
cb release thomasc/myproj list

cb release thomasc/myproj create --tag v1.0.0 --name "First Release"
cb release thomasc/myproj create --tag v2.0.0 --draft --prerelease

cb release thomasc/myproj show 42
cb release thomasc/myproj latest
cb release thomasc/myproj by-tag v1.0.0

cb release thomasc/myproj edit 42 --name "New Name" --no-draft
cb release thomasc/myproj delete 42 --yes
cb release thomasc/myproj delete-tag v1.0.0 --yes

cb release thomasc/myproj asset list 42
cb release thomasc/myproj asset edit 42 7 --name new-filename.zip
cb release thomasc/myproj asset delete 42 7 --yes
```

Subcommands: `list`, `create`, `show`, `latest`, `edit`, `delete`, `by-tag`, `delete-tag`, `asset`

#### `cb tag [owner/]repo <subcommand>`

Manage tags.

```bash
cb tag thomasc/myproj list
cb tag thomasc/myproj create --tag v1.0.0 --message "Release 1.0"
cb tag thomasc/myproj show v1.0.0
cb tag thomasc/myproj delete v1.0.0 --yes
```

#### `cb branch [owner/]repo <subcommand>`

Manage branches.

```bash
cb branch thomasc/myproj list
cb branch thomasc/myproj create --name feature-x --from main
cb branch thomasc/myproj show main
cb branch thomasc/myproj rename old-name --name new-name
cb branch thomasc/myproj delete old-branch --yes
```

#### `cb issue [owner/]repo <subcommand>`

Manage issues.

```bash
cb issue thomasc/myproj list

cb issue thomasc/myproj list --state closed --limit 20
cb issue thomasc/myproj list --labels bug,feature

cb issue thomasc/myproj create --title "Fix crash" --body "Steps to reproduce..."
cb issue thomasc/myproj show 5
cb issue thomasc/myproj edit 5 --title "New title" --state closed
cb issue thomasc/myproj close 5
cb issue thomasc/myproj reopen 5
cb issue thomasc/myproj delete 5 --yes

cb issue thomasc/myproj comment 5 --body "This is fixed now"

cb issue thomasc/myproj label add 5 3
cb issue thomasc/myproj label clear 5
```

#### `cb label [owner/]repo <subcommand>`

Manage repository labels.

```bash
cb label thomasc/myproj list
cb label thomasc/myproj show 5
cb label thomasc/myproj create --name bug --color ff0000
cb label thomasc/myproj delete 3 --yes
```

#### `cb milestone [owner/]repo <subcommand>`

Manage milestones.

```bash
cb milestone thomasc/myproj list
cb milestone thomasc/myproj show 3
cb milestone thomasc/myproj create --title "v2.0" --due 2025-12-31
cb milestone thomasc/myproj delete 3 --yes
```

#### `cb pr [owner/]repo <subcommand>`

Manage pull requests.

```bash
cb pr thomasc/myproj list
cb pr thomasc/myproj list --state closed

cb pr thomasc/myproj create --title "Add feature" --head feature-x
cb pr thomasc/myproj show 7
cb pr thomasc/myproj edit 7 --title "Updated title"
cb pr thomasc/myproj close 7
cb pr thomasc/myproj reopen 7

cb pr thomasc/myproj merge 7 --style squash --delete-branch
cb pr thomasc/myproj merge 7 --auto
```

> **Note:** `pr files`, `pr commits`, `pr diff`, and `pr review` are not yet implemented.

#### `cb commit [owner/]repo <subcommand>`

View commits and statuses.

```bash
cb commit thomasc/myproj list
cb commit thomasc/myproj list --sha main --path src/
cb commit thomasc/myproj list --limit 20
```

> **Note:** `commit show`, `commit status`, `commit diff`, `commit compare`, and `commit note` are not yet implemented.

#### `cb content [owner/]repo <subcommand>`

View and manage repository file contents.

```bash
cb content thomasc/myproj list
cb content thomasc/myproj list --ref main
```

> **Note:** `content show`, `content raw`, and `content archive` are not yet implemented.

#### `cb key [owner/]repo <subcommand>`

Manage deploy keys.

```bash
cb key thomasc/myproj list
cb key thomasc/myproj show 7
cb key thomasc/myproj add --title "CI key" --key "ssh-ed25519 AAAA..."
cb key thomasc/myproj delete 3 --yes
```

#### `cb collaborator [owner/]repo <subcommand>`

Manage collaborators.

```bash
cb collaborator thomasc/myproj list
cb collaborator thomasc/myproj add bob --permission write
cb collaborator thomasc/myproj rm bob --yes
cb collaborator thomasc/myproj perms bob
```

> **Note:** `collaborator perms` is not yet implemented.

#### `cb fork [owner/]repo <subcommand>`

Manage forks.

```bash
cb fork thomasc/someproj list
cb fork thomasc/someproj create
cb fork thomasc/someproj create --org myorg
```

#### `cb hook [owner/]repo <subcommand>`

Manage webhooks.

```bash
cb hook thomasc/myproj list
cb hook thomasc/myproj create --type gitea --url https://example.com/hook
cb hook thomasc/myproj delete 5 --yes
```

> **Note:** `hook show` is not yet implemented.

#### `cb org create <name>`

```bash
cb org create myorg -d "My organization"
cb org create myorg --visibility private
```

Flags: `--description`/`-d`, `--full-name`, `--email`, `--location`, `--website`, `--visibility` (public/limited/private), `--repo-admin-change-team-access`

#### `cb wiki [owner/]repo <subcommand>`

Manage wiki pages.

```bash
cb wiki thomasc/myproj list
cb wiki thomasc/myproj delete OldPage --yes
```

> **Note:** `wiki show` and `wiki revisions` are not yet implemented.

#### `cb actions list [owner/]repo`

List recent workflow runs.

```bash
cb actions list thomasc/cb
cb actions list thomasc/cb --json
```

#### `cb actions show [owner/]repo <run-id>`

Show details of a specific workflow run.

`run-id` is the run number shown as `#N` in `actions list` output. All
`actions` detail commands (`show`, `jobs`, `log`) accept the same run number.

```bash
cb actions show thomasc/cb 69
```

#### `cb actions jobs [owner/]repo <run-id>`

List jobs in a workflow run with status and duration.

```bash
cb actions jobs thomasc/cb 3
```

> **Note:** `run-id` is the run number from `actions list` output (e.g. `#3`).

Output:

```
Job    Name                 Status     Duration
0      build-linux          success    59s
1      build-windows        success    1m16s
2      build-macos          success    2m03s
3      release              skipped    3s
```

#### `cb actions log [owner/]repo <run-id> [job-index] [step-index]`

Show build logs for a workflow run. `run-id` is the run number from `actions list` output (e.g. `#3`). If `job-index` is omitted, defaults to 0. If `step-index` is omitted, shows all steps.

```bash
cb actions log thomasc/cb 3              # all steps of job 0
cb actions log thomasc/cb 3 0            # all steps of job 0
cb actions log thomasc/cb 3 0 2          # only step 2 of job 0
```

Output includes step headers with status and duration, followed by log lines:

```
Job 0: build-linux (failure, 59s)

=== Install dependencies (failure, 11s) ===
Get:1 http://archive.ubuntu.com/ubuntu noble InRelease [256 kB]
...
E: Unable to locate package libretls-dev
```

#### `cb actions runners [owner/]repo`

List CI runners available to the repository.

```bash
cb actions runners thomasc/cb
```

#### `cb actions dispatch [owner/]repo <workflow-file>`

Trigger a workflow run.

```bash
cb actions dispatch thomasc/cb build.yml
cb actions dispatch thomasc/cb build.yml --ref master
```

Flags: `--ref REF` (git ref to dispatch on, default: master)

#### `cb actions secret [owner/]repo <list|set|rm>`

Manage repository action secrets. Secret values are never returned by the API.

```bash
cb actions secret list thomasc/cb
cb actions secret set thomasc/cb MY_TOKEN --value "secret-value"
cb actions secret rm thomasc/cb MY_TOKEN --yes
```

#### `cb actions var [owner/]repo <list|show|set|rm>`

Manage repository action variables.

```bash
cb actions var list thomasc/cb
cb actions var show thomasc/cb BUILD_OPTS
cb actions var set thomasc/cb BUILD_OPTS --value "-j4"
cb actions var rm thomasc/cb BUILD_OPTS --yes
```

## CI/CD

The project uses [Forgejo Actions](https://codeberg.org/thomasc/cb/actions) with Codeberg's hosted runners. On every push to `master`, the pipeline builds and tests `cb` on Linux, cross-compiles a Windows binary (MinGW), and cross-compiles a macOS binary (zig cc + static LibreSSL). On tag pushes (`v*`), it creates a release with downloadable binaries for all three platforms.

Workflow configuration: [`.forgejo/workflows/build.yml`](.forgejo/workflows/build.yml)

## Error Handling

The CLI decodes Forgejo API errors into actionable messages:

- **401**: Invalid or expired token
- **403 (scope)**: Tells you exactly which scope is missing and where to regenerate the token
- **404**: Repository not found
- **409**: Repository already exists
- **422 (quota)**: Quota exceeded
- **Network errors**: Connection details

Exit codes: `0` success, `1` error, `2` usage error.

## Architecture

```
cb/
├── autogen.sh                # Generates version file + runs autoreconf -fi
├── configure.ac              # Autotools build configuration (git-derived version)
├── Makefile.am               # Top-level automake
├── build-aux/git-version.sh  # Git-based version generation script
├── .clang-format             # clang-format style (Allman braces, 4-space indent)
├── .clangd                   # clangd config (forces C mode, suppresses ObjC++ false positives)
├── .forgejo/workflows/       # CI/CD pipeline (Forgejo Actions)
├── src/Makefile.am           # Binary build rules (regenerates cb_version.h on every make)
├── tests/Makefile.am         # Test binary rules
├── m4/                       # Autoconf macros (ax_pthread, ax_check_compile_flag)
├── include/                  # Public headers
│   ├── cb_json.h             # JSON parser/serializer
│   ├── cb_http.h             # HTTP client (sockets + libtls, Winsock2/POSIX via cb_compat)
│   ├── cb_config.h           # Config loading (file + env)
│   ├── cb_validate.h         # Client-side validation
│   ├── cb_api.h              # Forgejo API client (repo, topic, actions, releases, issues, PRs, etc.)
│   ├── cb_compat.h           # Portable compat layer (sockets, memstream, env, fs, base64, URL encode)
│   └── cb_cli.h              # CLI dispatch
├── src/                      # Implementation
│   ├── cb_json.c             # recursive-descent parser + builder + serializer
│   ├── cb_http.c             # plain HTTP + TLS via libtls
│   ├── cb_config.c           # TOML-ish config + env + URL parser
│   ├── cb_validate.c         # repo name, description, merge style, tag, branch, label color, SHA, org name, visibility validation
│   ├── cb_api.c              # repo, topic, actions, releases, tags, branches, issues, labels, milestones,
│   │                          # PRs, commits, content, keys, collaborators, forks, hooks, orgs, wiki API ops
│   ├── cb_cli.c              # command parsing, flag dispatch, output (17 top-level commands)
│   ├── cb_compat.c           # Portable wrappers (open_memstream, Winsock2, setenv, base64, URL encode)
│   └── main.c                # Entry point
└── tests/                    # Run with: make check (or: ./configure --enable-asan && make check)
    ├── test_helpers.h        # Custom assert macros
    ├── mock_server.h/c       # Minimal HTTP mock server for offline tests
    ├── test_json.c           # JSON parser/serializer tests
    ├── test_http.c           # HTTP client tests
    ├── test_config.c         # Config loading tests
    ├── test_validate.c       # Validation tests
    ├── test_api.c            # API client tests (repo, topic)
    ├── test_cli.c            # CLI integration tests
    ├── test_actions.c        # Actions (CI/CD) tests
    └── test_new_api.c        # API tests for all new endpoints (releases–wiki)
```

### Versioning

Versions are derived from git state — no hardcoded version numbers to update. The build system uses `build-aux/git-version.sh` to compute a version string from `git describe --tags`, falling back to rev-count + short SHA when no tags exist. Uncommitted changes append a `-dirty` suffix.

`./autogen.sh` generates a `version` file (consumed by `configure.ac`'s `AC_INIT`) before running `autoreconf -fi`. On every `make`, `src/Makefile.am` regenerates `src/cb_version.h` from the current git state; a `cmp` guard prevents unnecessary recompiles when the version hasn't changed. Distribution tarballs stamp the version into the included `version` file so builds without `.git` work correctly.

```bash
./cb --version              # Show version (GNU-style output)
./cb -v                     # Short form
```

### Key design decisions

- **Custom JSON parser**: Hand-coded to keep dependencies low. Covers objects, arrays, strings (with escape/Unicode handling), numbers, booleans, null. The serializer's `omit_null` mode is critical for `repo edit` — it ensures unset fields don't appear in the PATCH body, matching Forgejo's `omitempty` pointer semantics.

- **libtls for TLS**: Codeberg requires HTTPS. libretls (Linux/macOS) or LibreSSL (MSYS2) provides the `libtls` API as a thin wrapper over the system OpenSSL, keeping TLS setup minimal compared to raw OpenSSL.

- **Portable socket layer**: `cb_compat.h` abstracts the differences between POSIX sockets and Winsock2 — socket types, close/errno/poll wrappers, timeout APIs, and shutdown constants. Source code uses `cb_socket_t`, `cb_close_socket()`, `cb_poll()`, etc. throughout, so the same `.c` files compile on both platforms without `#ifdef` clutter.

- **Mock HTTP server for tests**: A minimal `socket` + `pthread` server in the test harness. Tests are fully offline — no network calls, no TLS. Each test configures canned responses per method+path. The server supports sequential responses to the same path (for multi-request flows like log pagination).

- **TDD**: Every module was built test-first. Run `./configure --enable-asan && make check` to test under ASan+UBSan. All 8 test suites pass with zero leaks on both Linux and MSYS2 UCRT64.

- **Actions log fetching**: Forgejo does not expose workflow logs through the `/api/v1` REST API. The `actions log` command uses the web UI's JSON endpoint (`POST /{owner}/{repo}/actions/runs/{run_id}/jobs/{job}/attempt/{attempt}`) with token auth, handling cursor-based pagination to fetch complete log output.

## License

GPLv3+ — see [COPYING](COPYING) for the full text. Aligning with the Forgejo ecosystem, which is GPLv3+ from v9.0.
