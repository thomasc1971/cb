# CI/CD Plan for Codeberg

## Goal

On every push to `master`: build `cb` on Linux and Windows, run tests.
On tag push (`v*`): build both platforms and publish binaries as a Forgejo Release.

## CI System: Forgejo Actions

Codeberg supports Forgejo Actions (GitHub Actions-compatible YAML in `.forgejo/workflows/`).
Hosted runners are available with `runs-on: docker` and a `container.image` of your choice.

## Pipeline Design

### File: `.forgejo/workflows/build.yml`

### Triggers

- `push` to `master` → build + test (no release)
- `push` of tags matching `v*` → build + test + release

### Job 1: `build-linux`

- **Container**: `debian:bookworm` (stable, glibc, well-supported)
- **Steps**:
  1. Checkout source (`actions/checkout@v6`)
  2. Install build deps: `gcc libretls-dev pkg-config autoconf automake make`
  3. `autoreconf -fi`
  4. `./configure`
  5. `make`
  6. `make check` (runs all 6 test suites)
  7. Strip binary: `strip cb`
  8. Rename: `cp cb cb-linux-amd64`
  9. Upload artifact (`actions/upload-artifact@v3`)

### Job 2: `build-windows`

- **Container**: `docker.io/msys2/msys2:ucrt64`
- **Steps**:
  1. Checkout source
  2. Install build deps via pacman:
     `mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libressl mingw-w64-ucrt-x86_64-pkg-config autoconf automake make`
  3. Set `PKG_CONFIG_PATH=/ucrt64/lib/libressl/pkgconfig`
  4. `autoreconf -fi`
  5. `./configure LDFLAGS=-L/ucrt64/lib/libressl`
  6. `make`
  7. `make check`
  8. Rename: `cp cb.exe cb-windows-amd64.exe`
  9. Upload artifact

### Job 3: `release` (only on `v*` tags)

- **Depends on**: `build-linux`, `build-windows`
- **Container**: `debian:bookworm`
- **Steps**:
  1. Checkout source
  2. Download both artifacts (`actions/download-artifact@v3`)
  3. Generate SHA256 checksums
  4. Create Forgejo Release and upload binaries using `actions/forgejo-release@v2`
     - `direction: upload`
     - `tag: ${{ forgejo.ref_name }}`
     - `release-dir: ./release`
     - `token: ${{ secrets.GITHUB_TOKEN }}` (Forgejo provides this automatically)
     - `override: true` (in case of re-runs)

### Secret management

- `GITHUB_TOKEN` / `FORGEJO_TOKEN` — Forgejo Actions provides this automatically for releases to the same repo. No manual secret needed for basic release uploads.
- If the auto-provided token lacks scope, create a repo secret `RELEASE_TOKEN` with a token that has `write:repository` scope.

## Windows build caveat

The `msys2/msys2:ucrt64` Docker image provides the MSYS2 UCRT64 environment.
However, running MSYS2 inside Docker requires using the MSYS2 shell (`bash -lc`).
The workflow must invoke commands through the MSYS2 bash shell, not plain `sh`.

An alternative is cross-compiling from Linux using `mingw-w64`:
- Container: `debian:bookworm`
- Install: `gcc-mingw-w64-x86-64` + cross-compile libretls for Windows
- This avoids MSYS2-in-Docker complexity but requires cross-compiling libtls

**Decision**: MSYS2-in-Docker (authentic Windows binary).

## Implementation steps

1. Create `.forgejo/workflows/build.yml`
2. Push to `master` and verify both build jobs pass
3. Tag `v0.1.0` and verify release job creates a release with downloadable binaries
4. Update README with download links / badge

## File changes

- **New**: `.forgejo/workflows/build.yml`
- **No changes** to existing source/build files
