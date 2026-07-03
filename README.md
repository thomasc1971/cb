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
- Manage release assets (list, upload, show, edit, delete)

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

- List, create, show, edit, delete repository labels

### Milestones

- List, create, show, edit, delete milestones

### Pull requests

- List, create, show, edit, merge pull requests
- Close and reopen PRs (shorthands)
- Merge with style selection (merge, rebase, squash, rebase-merge)
- Auto-merge when checks succeed

### Commits

- List commits with ref/path filtering
- Show commit details
- View combined commit status

### File contents

- List directory contents
- Show file or directory details
- Create, update, delete files
- Get raw file content
- Download archives

### Deploy keys

- List, add, show, delete deploy keys

### Collaborators

- List, add, remove collaborators
- View collaborator permissions

### Forks

- List forks, fork a repository

### Webhooks

- List, create, show, edit, delete webhooks
- Test webhooks

### Wiki

- List, create, show, edit, delete wiki pages
- View page revisions

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
- Cross-platform: Linux, Windows (portable ZIP with bundled DLLs)

## Download

Pre-built binaries are available from the [Codeberg releases page](https://codeberg.org/thomasc/cb/releases).

| File                            | Platform       |
| ------------------------------- | -------------- |
| `cb-linux-amd64`                | Linux x86_64   |
| `cb-windows-amd64-portable.zip` | Windows x86_64 |

The Windows ZIP contains `cb.exe` and the required LibreSSL DLLs. Extract and run — no installation needed.

## Build

### Prerequisites

- GCC or Clang (C11)
- libretls (provides `libtls` API over system OpenSSL)
- autoconf, automake, make
- pkg-config

| Platform      | Install command                                                                                                              |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| Fedora        | `dnf install gcc libretls-devel pkg-config autoconf automake make`                                                           |
| Debian/Ubuntu | `apt install gcc libretls-dev pkg-config autoconf automake make`                                                             |
| MSYS2 UCRT64  | `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libressl mingw-w64-ucrt-x86_64-pkg-config autoconf automake make` |

> **Note on MSYS2 UCRT64:** libretls is not packaged for MSYS2; LibreSSL provides libtls instead. Set `PKG_CONFIG_PATH=/ucrt64/lib/libressl/pkgconfig` before `./configure`. The UCRT64 toolchain uses Winsock2 instead of POSIX sockets — `cb_compat.h` provides a portable socket layer that conditionally uses Winsock2 on Windows and POSIX sockets elsewhere. On UCRT64, pass `LDFLAGS=-L/ucrt64/lib/libressl` to `./configure` if the linker cannot find `-ltls`.

### Build

```bash
./autogen.sh           # Generate version file + configure + Makefile.in (first time only)
./configure            # Standard build
make                   # Build the binary
./cb                   # Run it
```

<details>
<summary>Building on MSYS2 UCRT64 (Windows)</summary>

```bash
# From an MSYS2 UCRT64 shell:
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libressl \
          mingw-w64-ucrt-x86_64-pkg-config autoconf automake make

export PKG_CONFIG_PATH=/ucrt64/lib/libressl/pkgconfig
./autogen.sh
./configure LDFLAGS=-L/ucrt64/lib/libressl
make
make check               # All 8 test suites pass
```

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

### Install

```bash
./configure --prefix=$HOME/.local
make install
```

## Configuration

### Token

Generate a token at `https://codeberg.org/user/settings/applications`. The token needs these scopes:

| Scope                | Needed for                                 |
| -------------------- | ------------------------------------------ |
| `write:repository`   | All repo CRUD and actions operations       |
| `write:organization` | Creating/listing repos under organizations |
| `read:user`          | Listing other users' repos                 |
| `all`                | Everything (easy but broad)                |

### Config sources (precedence high → low)

1. `--base-url` CLI flag
2. `CB_BASE_URL` env var
3. `~/.config/cb/config` file (`%APPDATA%\cb\config` on Windows)
4. Default: `https://codeberg.org/api/v1`

Token is read from `CB_TOKEN` env var or the config file:

```toml
# ~/.config/cb/config (mode 0600)
# %APPDATA%\cb\config on Windows
token = "your-token-here"
base_url = "https://codeberg.org/api/v1"
```

## Usage

```
cb [global flags] <command> [subcommand] [args] [flags]
```

### Global flags

| Flag              | Description                               |
| ----------------- | ----------------------------------------- |
| `--json`          | Output raw JSON instead of human-readable |
| `--quiet`, `-q`   | Suppress non-essential output             |
| `--base-url URL`  | Override API base URL                     |
| `--yes`           | Skip confirmation prompts                 |
| `--version`, `-v` | Show version                              |
| `--help`, `-h`    | Show help at any command level            |

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
cb label thomasc/myproj create --name bug --color ff0000
cb label thomasc/myproj delete 3 --yes
```

#### `cb milestone [owner/]repo <subcommand>`

Manage milestones.

```bash
cb milestone thomasc/myproj list
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

#### `cb commit [owner/]repo <subcommand>`

View commits and statuses.

```bash
cb commit thomasc/myproj list
cb commit thomasc/myproj list --sha main --path src/
cb commit thomasc/myproj list --limit 20
```

#### `cb content [owner/]repo <subcommand>`

View and manage repository file contents.

```bash
cb content thomasc/myproj list
cb content thomasc/myproj list --ref main
```

#### `cb key [owner/]repo <subcommand>`

Manage deploy keys.

```bash
cb key thomasc/myproj list
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

#### `cb wiki [owner/]repo <subcommand>`

Manage wiki pages.

```bash
cb wiki thomasc/myproj list
cb wiki thomasc/myproj delete OldPage --yes
```

#### `cb actions list [owner/]repo`

List recent workflow runs.

```bash
cb actions list thomasc/cb
cb actions list thomasc/cb --json
```

#### `cb actions show [owner/]repo <run-id>`

Show details of a specific workflow run. `run-id` is the internal API ID (visible in `--json` output as `id`).

```bash
cb actions show thomasc/cb 5218484
```

#### `cb actions jobs [owner/]repo <run-id>`

List jobs in a workflow run with status and duration.

```bash
cb actions jobs thomasc/cb 3
```

Output:

```
Job    Name                 Status     Duration
0      build-linux          success    59s
1      build-windows        success    1m16s
2      release              skipped    3s
```

#### `cb actions log [owner/]repo <run-id> [job-index] [step-index]`

Show build logs for a workflow run. `run-id` is the short `index_in_repo` number (e.g. `3`). If `job-index` is omitted, defaults to 0. If `step-index` is omitted, shows all steps.

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

#### `cb actions secret <list|set|rm>`

Manage repository action secrets. Secret values are never returned by the API.

```bash
cb actions secret list thomasc/cb
cb actions secret set thomasc/cb MY_TOKEN --value "secret-value"
cb actions secret rm thomasc/cb MY_TOKEN --yes
```

#### `cb actions var <list|show|set|rm>`

Manage repository action variables.

```bash
cb actions var list thomasc/cb
cb actions var show thomasc/cb BUILD_OPTS
cb actions var set thomasc/cb BUILD_OPTS --value "-j4"
cb actions var rm thomasc/cb BUILD_OPTS --yes
```

## CI/CD

The project uses [Forgejo Actions](https://codeberg.org/thomasc/cb/actions) with Codeberg's hosted runners. On every push to `master`, the pipeline builds and tests `cb` on Linux and cross-compiles a Windows binary. On tag pushes (`v*`), it creates a release with downloadable binaries.

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
├── autogen.sh               # Generates version file + runs autoreconf -fi
├── configure.ac              # Autotools build configuration (git-derived version)
├── Makefile.am               # Top-level automake
├── build-aux/git-version.sh  # Git-based version generation script
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
│   ├── cb_validate.c         # repo name, description, merge style, tag, branch, label color, SHA validation
│   ├── cb_api.c              # repo, topic, actions, releases, tags, branches, issues, labels, milestones,
│   │                          # PRs, commits, content, keys, collaborators, forks, hooks, wiki API ops
│   ├── cb_cli.c              # command parsing, flag dispatch, output (16 top-level commands)
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

- **libretls for TLS**: Codeberg requires HTTPS. libretls provides the `libtls` API as a thin wrapper over the system OpenSSL, keeping TLS setup minimal compared to raw OpenSSL.

- **Mock HTTP server for tests**: A minimal `socket` + `pthread` server in the test harness. Tests are fully offline — no network calls, no TLS. Each test configures canned responses per method+path. The server supports sequential responses to the same path (for multi-request flows like log pagination).

- **TDD**: Every module was built test-first. Run `./configure --enable-asan && make check` to test under ASan+UBSan. All 8 test suites pass with zero leaks.

- **Actions log fetching**: Forgejo does not expose workflow logs through the `/api/v1` REST API. The `actions log` command uses the web UI's JSON endpoint (`POST /{owner}/{repo}/actions/runs/{run_id}/jobs/{job}/attempt/{attempt}`) with token auth, handling cursor-based pagination to fetch complete log output.

## License

GPLv3+ — see [COPYING](COPYING) for the full text. Aligning with the Forgejo ecosystem, which is GPLv3+ from v9.0.
