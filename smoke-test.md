# Smoke Test Report — `cb` v0.7 (2026-07-07)

All non-mutating (read-only) commands were tested against a live Codeberg
instance. The token-authenticated user owns `thomasc/cb` and several other
repos, which were used as test targets.

---

## ~~Critical~~ Fixed — Subcommands advertised in help but not dispatched

**Fixed.** All four `show` subcommands are now dispatched and working:

| Command                 | Status       |
| ----------------------- | ------------ |
| `cb release asset show` | ✓ Dispatched |
| `cb label show`         | ✓ Dispatched |
| `cb milestone show`     | ✓ Dispatched |
| `cb key show`           | ✓ Dispatched |

---

## Critical — Subcommands not yet implemented

These subcommands are listed in help but return "not yet implemented" errors.
They should either be completed or hidden from the help text until they are
ready.

| Command                 | Help text                                         | Error message                            |
| ----------------------- | ------------------------------------------------- | ---------------------------------------- |
| `cb pr files`           | List changed files                                | `pr files not yet implemented`           |
| `cb pr commits`         | List commits                                      | `pr commits not yet implemented`         |
| `cb pr diff`            | Show diff (or patch)                              | `pr diff not yet implemented`            |
| `cb pr review`          | Manage reviews (list, create, request, unrequest) | `pr review not yet implemented`          |
| `cb commit show`        | Show a commit                                     | `commit show not yet implemented`        |
| `cb commit status`      | Show combined status for a ref                    | `commit status not yet implemented`      |
| `cb commit diff`        | Show diff (or patch)                              | `commit diff not yet implemented`        |
| `cb commit compare`     | Compare two refs                                  | `commit compare not yet implemented`     |
| `cb commit note`        | Manage git notes (show, set, rm)                  | `commit note not yet implemented`        |
| `cb content show`       | Show file or directory contents                   | `content show not yet implemented`       |
| `cb content raw`        | Get raw file content                              | `content raw not yet implemented`        |
| `cb content archive`    | Download an archive                               | `content archive not yet implemented`    |
| `cb collaborator perms` | Show collaborator permissions                     | `collaborator perms not yet implemented` |
| `cb hook show`          | Show a webhook                                    | `hook show not yet implemented`          |
| `cb wiki show`          | Show a wiki page                                  | `wiki show not yet implemented`          |
| `cb wiki revisions`     | Show page revisions                               | `wiki revisions not yet implemented`     |

---

## ~~Major~~ Fixed — `actions show` vs `actions jobs`/`log` run-ID inconsistency

**Fixed.** All three commands now accept the run number (the `#N` shown in
`actions list` output). `actions show` was changed to query
`GET /repos/{o}/{r}/actions/runs?run_number={N}` instead of the internal
API ID path. The `int` run ID type in `actions jobs`/`log` was also upgraded
to `int64_t` to prevent truncation of large run numbers.

---

## ~~Major~~ Fixed — `--quiet` flag does not suppress list output

**Fixed and verified.** `--quiet` now suppresses non-essential output on all
commands:

- **List commands**: prints only the identifying field (name, tag, or `#ID`)
  per line, no table headers or metadata
- **Show commands**: suppresses output entirely (like mutation confirmations)
- **`--json --quiet`**: JSON output is unaffected — `--json` takes precedence

Verified against live instance:

| Command                                 | `--quiet` output        |
| --------------------------------------- | ----------------------- |
| `cb --quiet repo list`                  | `owner/repo` names only |
| `cb --quiet repo show thomasc/cb`       | silent (0 bytes)        |
| `cb --quiet --json repo list`           | full JSON array         |
| `cb --quiet tag list thomasc/cb`        | tag names only          |
| `cb --quiet branch list thomasc/cb`     | branch names only       |
| `cb --quiet release list thomasc/cb`    | tag names only          |
| `cb --quiet actions list thomasc/cb`    | `#N` run numbers only   |
| `cb --quiet actions jobs thomasc/cb 71` | job names only          |
| `cb --quiet actions log thomasc/cb 71`  | silent (0 bytes)        |

---

## ~~Minor~~ Fixed — `actions secret list --json` parse failure

**Fixed and verified.** The Forgejo secrets API returns a bare JSON array
(per the swagger spec), but the parser only accepted `{"data":[...]}` (older
Gitea format). Now both formats are handled. Empty list returns `[]` in JSON
mode and "No secrets found." in human mode.

---

## Minor — Inconsistent empty-list messaging

When a resource list is empty, commands behave differently in human mode:

| Style         | Commands                                                                                                                         |
| ------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| **No output** | `issue list`, `label list`, `pr list`, `key list`, `fork list`, `hook list`, `milestone list`, `collaborator list`               |
| **Message**   | `actions secret list` → "No secrets found.", `actions var list` → "No variables found.", `actions runners` → "No runners found." |

Pick one style and apply it consistently.

---

## Working commands (no issues found)

The following non-mutating commands all behaved as expected, including
`--json` output, `--help`/`-h`, and correct error handling for missing args
and nonexistent resources:

- `cb --version` / `cb -v`
- `cb --help` / `cb -h`
- `cb repo show`, `cb repo list` (with `--user`, `--org`, no flags)
- `cb repo topic list`
- `cb actions list`, `actions show` (with run number), `actions jobs` (with run number), `actions log` (with run number), `actions runners`
- `cb actions var list`, `actions var show`
- `cb release list`, `release show`, `release latest`, `release by-tag`, `release asset list`
- `cb tag list`, `tag show`
- `cb branch list`, `branch show`
- `cb content list`
- `cb commit list`

---

## Summary

| Severity | Count | Description                                                      |
| -------- | ----- | ---------------------------------------------------------------- |
| Critical | ~~4~~ | ~~`show` subcommands advertised but not dispatched~~ — **fixed** |
| Critical | 17    | Subcommands listed in help but not implemented                   |
| Major    | ~~1~~ | ~~`actions show` uses a different ID format~~ — **fixed**        |
| Major    | ~~1~~ | ~~`--quiet` flag does not suppress list output~~ — **fixed**     |
| Minor    | ~~1~~ | ~~`actions secret list --json` fails~~ — **fixed**               |
| Minor    | 1     | Inconsistent empty-list messaging across commands                |
