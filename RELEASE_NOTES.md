## cb v0.3 — Organization Management

Adds `cb org create` for creating Forgejo organizations, with full
validation, API, and CLI support.

### What's new

- **`cb org create`** — Create organizations on Codeberg/Forgejo.
  Supports `--description`, `--full-name`, `--email`, `--location`,
  `--website`, `--visibility` (public/limited/private), and
  `--repo-admin-change-team-access` flags.
- **Validation** — Organization names validated (alphanumeric, dash,
  dot, underscore, max 100 chars); visibility checked against
  `public`/`limited`/`private` enum.

### Downloads

| File                            | Platform       | Contents                    |
| ------------------------------- | -------------- | --------------------------- |
| `cb-linux-amd64.tar.gz`         | Linux x86_64   | `cb` binary + `COPYING`     |
| `cb-darwin-amd64.tar.gz`        | macOS x86_64   | `cb` binary + `COPYING`     |
| `cb-windows-amd64-portable.zip` | Windows x86_64 | `cb.exe` + DLLs + `COPYING` |

### Build from source

```bash
./autogen.sh
./configure
make
```

See [README.md](https://codeberg.org/thomasc/cb/src/commit/6f7e270/README.md) for full build instructions.

### License

GPLv3+

## cb v0.2 — macOS Support

Adds pre-built macOS x86_64 binaries, cross-compiled from Linux using zig cc with statically-linked LibreSSL.

### What's new

- **macOS x86_64 build** — CI now cross-compiles `cb` for macOS using zig cc and LibreSSL 4.3.2. The binary has no external dylib dependencies.
- **CI workflow** — `build-macos` job added alongside `build-linux` and `build-windows`. All three artifacts are included in releases.

### Downloads

| File                            | Platform       | Contents                    |
| ------------------------------- | -------------- | --------------------------- |
| `cb-linux-amd64.tar.gz`         | Linux x86_64   | `cb` binary + `COPYING`     |
| `cb-darwin-amd64.tar.gz`        | macOS x86_64   | `cb` binary + `COPYING`     |
| `cb-windows-amd64-portable.zip` | Windows x86_64 | `cb.exe` + DLLs + `COPYING` |

### Build from source

```bash
./autogen.sh
./configure
make
```

See [README.md](https://codeberg.org/thomasc/cb/src/commit/c64e646/README.md) for full build instructions.

### License

GPLv3+
