# cb — Codeberg (Forgejo) Repository Management CLI

A command-line tool for managing repositories and CI/CD actions on Codeberg and any Forgejo or Gitea instance. Written in C11 with minimal runtime dependencies (libtls for HTTPS).

## Features

### Repository management

- Create, delete, rename repositories
- Edit repository metadata (description, visibility, features, merge styles, etc.)
- Show repository details
- List repositories (own, user, or organization)
- Transfer repository ownership
- Manage repository topics (add, remove, list, set)

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
autoreconf -i            # Generate configure + Makefile.in (first time only)
./configure              # Standard build
make                     # Build the binary
./cb                     # Run it
```

<details>
<summary>Building on MSYS2 UCRT64 (Windows)</summary>

```bash
# From an MSYS2 UCRT64 shell:
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libressl \
          mingw-w64-ucrt-x86_64-pkg-config autoconf automake make

export PKG_CONFIG_PATH=/ucrt64/lib/libressl/pkgconfig
autoreconf -i
./configure LDFLAGS=-L/ucrt64/lib/libressl
make
make check               # All 7 test suites pass
```

</details>

### Test

```bash
make check               # Run all 7 test suites
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
3. `~/.config/cb/config` file
4. Default: `https://codeberg.org/api/v1`

Token is read from `CB_TOKEN` env var or the config file:

```toml
# ~/.config/cb/config (mode 0600)
token = "your-token-here"
base_url = "https://codeberg.org/api/v1"
```

## Usage

```
cb [global flags] <command> [subcommand] [args] [flags]
```

### Global flags

| Flag             | Description                               |
| ---------------- | ----------------------------------------- |
| `--json`         | Output raw JSON instead of human-readable |
| `--quiet`, `-q`  | Suppress non-essential output             |
| `--base-url URL` | Override API base URL                     |
| `--yes`          | Skip confirmation prompts                 |
| `--help`, `-h`   | Show help at any command level            |

### Getting help

`--help` / `-h` works at every level of the command tree:

```bash
cb --help                    # top-level help
cb repo --help               # repo subcommands
cb repo create --help         # create flags
cb repo edit --help           # edit flags
cb repo topic --help          # topic subcommands
cb repo topic add --help      # topic add usage
cb actions --help             # actions subcommands
cb actions log --help         # actions log usage
```

### Commands

#### `cb repo create <name>`

```bash
cb repo create myproj --private -d "side project"
cb repo create myproj --org myorg --license MIT --auto-init
cb repo create myproj --object-format sha256
```

Flags: `--private`, `--public`, `--description`/`-d`, `--default-branch`/`-b`, `--license`, `--gitignore`/`-g`, `--auto-init`, `--template`, `--org`, `--object-format`

#### `cb repo delete <owner/repo>`

```bash
cb repo delete thomasc/abandoned-proj --yes
```

Requires `--yes` or interactive confirmation.

#### `cb repo rename <owner/repo> <new-name>`

```bash
cb repo rename thomasc/old-name new-name
```

#### `cb repo edit <owner/repo>`

Only provided flags are sent — unset fields are not modified.

```bash
cb repo edit thomasc/myproj -d "new description" --public
cb repo edit thomasc/myproj --archived --no-issues
cb repo edit thomasc/myproj --default-merge-style squash
```

Flags (each has `--no-*` counterpart for bools): `--description`/`-d`, `--website`/`-w`, `--private`/`--public`, `--default-branch`/`-b`, `--archived`/`--unarchived`, `--template`/`--no-template`, `--has-issues`/`--no-issues`, `--has-wiki`/`--no-wiki`, `--has-prs`/`--no-prs`, `--has-projects`/`--no-projects`, `--has-releases`/`--no-releases`, `--has-packages`/`--no-packages`, `--has-actions`/`--no-actions`, `--allow-merge`/`--no-merge`, `--allow-rebase`/`--no-rebase`, `--allow-squash`/`--no-squash`, `--allow-ff-only`/`--no-ff-only`, `--default-merge-style`, `--delete-branch-after-merge`/`--no-delete-branch-after-merge`, `--allow-maintainer-edit`/`--no-allow-maintainer-edit`

#### `cb repo show <owner/repo>`

```bash
cb repo show thomasc/myproj
cb repo show thomasc/myproj --json
```

#### `cb repo list [--user U | --org O]`

```bash
cb repo list              # own repos
cb repo list --org myorg  # org repos
cb repo list --user bob   # another user's repos
```

#### `cb repo transfer <owner/repo> <new-owner>`

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

#### `cb actions list <owner/repo>`

List recent workflow runs.

```bash
cb actions list thomasc/cb
cb actions list thomasc/cb --json
```

#### `cb actions show <owner/repo> <run-id>`

Show details of a specific workflow run. `run-id` is the internal API ID (visible in `--json` output as `id`).

```bash
cb actions show thomasc/cb 5218484
```

#### `cb actions jobs <owner/repo> <run-id>`

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

#### `cb actions log <owner/repo> <run-id> [job-index] [step-index]`

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

#### `cb actions runners <owner/repo>`

List CI runners available to the repository.

```bash
cb actions runners thomasc/cb
```

#### `cb actions dispatch <owner/repo> <workflow-file>`

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
├── configure.ac              # Autotools build configuration
├── Makefile.am               # Top-level automake
├── .forgejo/workflows/       # CI/CD pipeline (Forgejo Actions)
├── src/Makefile.am           # Binary build rules
├── tests/Makefile.am         # Test binary rules
├── m4/                       # Autoconf macros (ax_pthread, ax_check_compile_flag)
├── include/                  # Public headers
│   ├── cb_json.h             # JSON parser/serializer
│   ├── cb_http.h             # HTTP client (sockets + libtls, Winsock2/POSIX via cb_compat)
│   ├── cb_config.h           # Config loading (file + env)
│   ├── cb_validate.h         # Client-side validation
│   ├── cb_api.h              # Forgejo API client (repo, topic, actions)
│   ├── cb_compat.h           # Portable compat layer (sockets, memstream, env, fs)
│   └── cb_cli.h              # CLI dispatch
├── src/                      # Implementation
│   ├── cb_json.c             # recursive-descent parser + builder + serializer
│   ├── cb_http.c             # plain HTTP + TLS via libtls
│   ├── cb_config.c           # TOML-ish config + env + URL parser
│   ├── cb_validate.c         # repo name, description, merge style validation
│   ├── cb_api.c              # repo, topic, and actions API operations
│   ├── cb_cli.c              # command parsing, flag dispatch, output
│   ├── cb_compat.c           # Portable wrappers (open_memstream, Winsock2, setenv, etc.)
│   └── main.c                # Entry point
└── tests/                    # Run with: make check (or: ./configure --enable-asan && make check)
    ├── test_helpers.h        # Custom assert macros
    ├── mock_server.h/c       # Minimal HTTP mock server for offline tests
    ├── test_json.c           # JSON parser/serializer tests
    ├── test_http.c           # HTTP client tests
    ├── test_config.c         # Config loading tests
    ├── test_validate.c       # Validation tests
    ├── test_api.c            # API client tests
    ├── test_cli.c            # CLI integration tests
    └── test_actions.c        # Actions (CI/CD) tests
```

### Key design decisions

- **Custom JSON parser**: Hand-coded to keep dependencies low. Covers objects, arrays, strings (with escape/Unicode handling), numbers, booleans, null. The serializer's `omit_null` mode is critical for `repo edit` — it ensures unset fields don't appear in the PATCH body, matching Forgejo's `omitempty` pointer semantics.

- **libretls for TLS**: Codeberg requires HTTPS. libretls provides the `libtls` API as a thin wrapper over the system OpenSSL, keeping TLS setup minimal compared to raw OpenSSL.

- **Mock HTTP server for tests**: A minimal `socket` + `pthread` server in the test harness. Tests are fully offline — no network calls, no TLS. Each test configures canned responses per method+path. The server supports sequential responses to the same path (for multi-request flows like log pagination).

- **TDD**: Every module was built test-first. Run `./configure --enable-asan && make check` to test under ASan+UBSan. All 7 test suites (123 tests) pass with zero leaks.

- **Actions log fetching**: Forgejo does not expose workflow logs through the `/api/v1` REST API. The `actions log` command uses the web UI's JSON endpoint (`POST /{owner}/{repo}/actions/runs/{run_id}/jobs/{job}/attempt/{attempt}`) with token auth, handling cursor-based pagination to fetch complete log output.

## License

GPLv3+ — see [COPYING](COPYING) for the full text. Aligning with the Forgejo ecosystem, which is GPLv3+ from v9.0.
