## cb v0.4 — Release Asset Editing

Adds `cb release asset edit` for renaming release assets, and
includes the version tag in all release artifact filenames.

### What's new

- **`cb release asset edit`** — Rename release assets in place
  without re-uploading. Usage: `cb release <owner/repo> asset edit
<release-id> <asset-id> --name <new-name>`
- **Versioned artifact filenames** — Release downloads now include
  the version tag (e.g. `cb-v0.4-linux-amd64.tar.gz` instead of
  `cb-linux-amd64.tar.gz`).

### Downloads

| File                          | Platform       | Contents                    |
| ----------------------------- | -------------- | --------------------------- |
| `cb-v0.4-linux-amd64.tar.gz`  | Linux x86_64   | `cb` binary + `COPYING`     |
| `cb-v0.4-darwin-amd64.tar.gz` | macOS x86_64   | `cb` binary + `COPYING`     |
| `cb-v0.4-windows-amd64.zip`   | Windows x86_64 | `cb.exe` + DLLs + `COPYING` |

### Build from source

```bash
./autogen.sh
./configure
make
```

See [README.md](https://codeberg.org/thomasc/cb/src/commit/f2b62d7/README.md) for full build instructions.

### License

GPLv3+
