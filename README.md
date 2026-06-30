# cb — Codeberg (Forgejo) Repository Management CLI

A command-line tool for managing repositories on Codeberg and any Forgejo or Gitea instance. Written in C11 with zero external runtime dependencies (only libretls for HTTPS).

## Features

- Create, delete, rename repositories
- Edit repository metadata (description, visibility, features, merge styles, etc.)
- Show repository details
- List repositories (own, user, or organization)
- Transfer repository ownership
- Manage repository topics (add, remove, list, set)
- JSON output mode for scripting (`--json`)
- Client-side validation with clear error messages
- Token scope error detection with actionable guidance

## Build

### Prerequisites

- GCC or Clang (C11)
- libretls (`dnf install libretls` on Fedora — provides `libtls` API over system OpenSSL)
- make

### Build

```bash
make          # Build with ASan+UBSan (default for development)
make test     # Run all tests
./cb          # Run the binary
```

### Test

```bash
make test     # all under ASan+UBSan
```

## Configuration

### Token

Generate a token at `https://codeberg.org/user/settings/applications`. The token needs these scopes:

| Scope                | Needed for                                 |
| -------------------- | ------------------------------------------ |
| `write:repository`   | All repo CRUD operations                   |
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
├── include/          # Public headers
│   ├── cb_json.h     # JSON parser/serializer
│   ├── cb_http.h     # HTTP client (POSIX sockets + libtls)
│   ├── cb_config.h   # Config loading (file + env)
│   ├── cb_validate.h # Client-side validation
│   ├── cb_api.h      # Forgejo API client
│   └── cb_cli.h      # CLI dispatch
├── src/              # Implementation
│   ├── cb_json.c     # recursive-descent parser + builder + serializer
│   ├── cb_http.c     # plain HTTP + TLS via libtls
│   ├── cb_config.c   # TOML-ish config + env + URL parser
│   ├── cb_validate.c # repo name, description, merge style validation
│   ├── cb_api.c      # all repo + topic API operations
│   ├── cb_cli.c      # command parsing, flag dispatch, output
│   └── main.c        # Entry point
└── tests/            # all under ASan+UBSan
    ├── test_helpers.h  # Custom assert macros (coffer pattern)
    ├── mock_server.h/c # Minimal HTTP mock server for offline tests
    ├── test_json.c     # JSON parser/serializer tests
    ├── test_http.c     # HTTP client tests
    ├── test_config.c   # Config loading tests
    ├── test_validate.c # Validation tests
    ├── test_api.c      # API client tests
    └── test_cli.c      # CLI integration tests
```

### Key design decisions

- **Custom JSON parser**: Hand-coded to keep dependencies low. Covers objects, arrays, strings (with escape/Unicode handling), numbers, booleans, null. The serializer's `omit_null` mode is critical for `repo edit` — it ensures unset fields don't appear in the PATCH body, matching Forgejo's `omitempty` pointer semantics.

- **libretls for TLS**: Codeberg requires HTTPS. libretls provides the `libtls` API as a thin wrapper over the system OpenSSL, keeping TLS setup minimal compared to raw OpenSSL.

- **Mock HTTP server for tests**: A minimal `socket` + `pthread` server in the test harness. Tests are fully offline — no network calls, no TLS. Each test configures canned responses per method+path.

- **TDD**: Every module was built test-first. Tests run under ASan+UBSan to catch memory leaks and undefined behavior. All tests pass with zero leaks.

## License

GPLv3+ (aligning with the Forgejo ecosystem, which is GPLv3+ from v9.0)
