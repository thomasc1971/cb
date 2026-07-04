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

| File                          | Platform       | Contents                    |
| ----------------------------- | -------------- | --------------------------- |
| `cb-v0.3-linux-amd64.tar.gz`  | Linux x86_64   | `cb` binary + `COPYING`     |
| `cb-v0.3-darwin-amd64.tar.gz` | macOS x86_64   | `cb` binary + `COPYING`     |
| `cb-v0.3-windows-amd64.zip`   | Windows x86_64 | `cb.exe` + DLLs + `COPYING` |

### Build from source

```bash
./autogen.sh
./configure
make
```

See [README.md](https://codeberg.org/thomasc/cb/src/commit/fa4a1bd/README.md) for full build instructions.

### License

GPLv3+
