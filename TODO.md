# TODO — Remaining work for Forgejo `/repos/` endpoints

All 15 phases have API-level structs, functions, and tests. The CLI has 17 top-level
commands with help text and core subcommands implemented. The remaining work is:

## Next session (2026-10-14)

Found while adding the macOS arm64 CI job. A, B and C were all fixed later the
same session; the unchecked bullets are what is still open.

### A. `--limit` was accepted and silently ignored by `cb actions list`

```bash
cb actions list thomasc/cb --limit 1   # printed all 108 runs, exit 0
cb actions list thomasc/cb --bogus     # no error, exit 0
```

Root cause: the `list` entry in `ACTIONS_SUBS` passed `NULL` for its flag table,
and `cmd_actions_list()` never called `parse_flags()` — it only read `argv[0]`,
so every extra argument was dropped. `api_action_run_list()` also built
`/repos/{owner}/{repo}/actions/runs` with no query string at all.

- [x] ~~Give `actions list` a `--limit` flag table and thread an `int limit` through `api_action_run_list()`~~ — added `ACTIONS_LIST_FLAGS` and the `limit` parameter, using the `QueryParam` pattern from `api_issue_list()`.
- [x] ~~Regression tests~~ — `test_action_run_list_with_limit` asserts the `?page=1&limit=2` request.
- [ ] Audit every other subcommand whose `SubCmd` has `NULL` flags for the same silent-drop behaviour.
- [ ] Decide the general rule: for a command that never calls `parse_flags()`, unknown flag / extra positional should still be a `CLI_USAGE` (exit 2) error instead of being ignored.

> Gotcha found while fixing this: Forgejo **ignores `limit` on `/actions/runs`
> unless `page` is also sent** (`?limit=3` → 112 runs, `?page=1&limit=3` → 3
> runs), because a missing page makes the offset negative. `api_action_run_list()`
> therefore always sends `page=1`.

### B. `cb` could not list or download CI run artifacts (had to use curl)

Verifying the arm64 build on run #108 meant hand-rolling API calls:

```text
GET /repos/{owner}/{repo}/actions/artifacts            → list: id, name, size_in_bytes, expired, run_id, archive_download_url
GET /repos/{owner}/{repo}/actions/artifacts/{id}/zip   → ZIP download (needs auth, binary response)
GET /repos/{owner}/{repo}/actions/runs/{id}/artifacts  → run-scoped list; {id} is the DB id, NOT the index
```

- [x] ~~`cb actions artifacts [owner/]repo` — list artifacts (table + `--json`)~~ — `ActionArtifact`, `api_action_artifact_list()`, `--limit N`.
- [x] ~~`cb actions artifact download [owner/]repo <artifact-id|name> [-o file]`~~ — `api_action_artifact_download()`, writes to stdout or `--output`; resolves names via the list endpoint.
- [x] ~~Watch the id trap~~ — `ActionArtifact.run_id` holds the database id, and `parse_action_run()` keeps it in `ActionRun.id`.
- [x] ~~Register the new subcommands in `ACTIONS_SUBS` and document them~~ — help text, help tests and README entries added.
- [ ] Follow-up: a `--run <run-id>` filter on `actions artifacts`, mapping `index_in_repo` → `ActionRun.id` and using `/actions/runs/{db-id}/artifacts`.

### C. `cb release asset upload` was documented but not wired up

Hit while attaching the arm64 binary to the v0.10.0 release:

```console
$ cb release asset upload thomasc/cb 12781740 --file cb-v0.10.0-darwin-arm64.tar.gz
Error: unknown asset subcommand 'upload'
```

`RELEASE_ASSET_SUBS` advertised `upload` and `--help` printed its usage (help is
answered from the command tree), but the `asset` dispatch in `cmd_release()`
only branched on `list`, `show`, `edit` and `delete` — `cmd_release_asset_upload()`
did not exist. Same class as item A: the command tree and the dispatch disagreed.

- [x] ~~Implement `api_release_asset_upload()` and `cmd_release_asset_upload()`, then add the dispatch branch~~ — no multipart needed after all: Forgejo accepts a raw-body `POST /releases/{id}/assets?name=<file>`, so this reuses `http_request_raw()` like `api_package_upload()`.
- [x] ~~Stop `--help` promising a command that does not exist~~ — the command exists now.
- [ ] Same audit as item A: check every `SubCmd` entry against the dispatch chain for commands that are advertised but unreachable.

## 1. Implement stubbed CLI subcommands

These subcommands appear in help output but print "not yet implemented":

- [ ] **`pr edit`** — flag parsing for title/body/state, call `api_pr_edit`
- [ ] **`pr unmerge`** — call `api_pr_cancel_merge`
- [ ] **`pr files`** — call `api_pr_files`, print changed files
- [ ] **`pr commits`** — call `api_pr_commits`, print commits
- [ ] **`pr diff`** — call `api_pr_diff`, output diff/patch text
- [ ] **`pr review list`** — list reviews on a PR
- [ ] **`pr review create`** — submit a review
- [ ] **`pr review request`** — request a review
- [ ] **`pr review unrequest`** — cancel a review request
- [ ] **`commit show`** — call `api_commit_get`, print commit detail
- [ ] **`commit status`** — call `api_commit_status`, print combined status
- [ ] **`commit diff`** — show diff for a commit
- [ ] **`commit compare`** — call `api_commit_compare`, print comparison
- [ ] **`commit note show`** — show git note for a ref
- [ ] **`commit note set`** — set git note for a ref
- [ ] **`commit note rm`** — remove git note for a ref
- [ ] **`content show`** — call `api_content_get`, print file/dir details
- [ ] **`content create`** — call `api_content_create` (needs base64 encoding of file content)
- [ ] **`content update`** — call `api_content_update` (needs base64 + SHA)
- [ ] **`content delete`** — call `api_content_delete` (needs SHA)
- [ ] **`content raw`** — call `api_content_raw`, output raw bytes
- [ ] **`content archive`** — download repository archive (tar/zip)
- [x] ~~**`key show`** — call `api_key_get`, print deploy key details~~
- [ ] **`collaborator perms`** — call `api_collaborator_perms`, print permission level
- [ ] **`hook show`** — call `api_hook_get`, print webhook config
- [ ] **`hook edit`** — call `api_hook_edit`, update webhook config
- [ ] **`hook test`** — call `api_hook_test`, deliver test payload
- [ ] **`wiki create`** — call `api_wiki_create` (needs base64 encoding)
- [ ] **`wiki show`** — call `api_wiki_get`, print page content
- [ ] **`wiki edit`** — call `api_wiki_edit` (needs base64 encoding)
- [ ] **`wiki revisions`** — list page revision history
- [ ] **`issue pin`** — pin an issue to the top
- [ ] **`issue unpin`** — unpin an issue
- [ ] **`issue deadline`** — set a due date on an issue
- [x] ~~**`label show`** — call `api_label_get`, print label details~~
- [ ] **`label edit`** — call `api_label_edit`, update name/color
- [x] ~~**`milestone show`** — call `api_milestone_get`, print milestone details~~
- [ ] **`milestone edit`** — call `api_milestone_edit`, update title/description/dates

## 2. Add missing API functions

These endpoints from the design doc have no API function yet:

- [ ] **`api_pr_files`** — `GET /repos/{owner}/{repo}/pulls/{index}/files`
- [ ] **`api_pr_commits`** — `GET /repos/{owner}/{repo}/pulls/{index}/commits`
- [ ] **`api_pr_review_list`** — `GET /repos/{owner}/{repo}/pulls/{index}/reviews`
- [ ] **`api_pr_review_create`** — `POST /repos/{owner}/{repo}/pulls/{index}/reviews`
- [ ] **`api_pr_review_request`** — `POST /repos/{owner}/{repo}/pulls/{index}/requested_reviewers`
- [ ] **`api_pr_review_unrequest`** — `DELETE /repos/{owner}/{repo}/pulls/{index}/requested_reviewers`
- [ ] **`api_issue_pin`** — `POST /repos/{owner}/{repo}/issues/{index}/pin`
- [ ] **`api_issue_unpin`** — `DELETE /repos/{owner}/{repo}/issues/{index}/pin`
- [ ] **`api_issue_deadline`** — `POST/PUT /repos/{owner}/{repo}/issues/{index}/deadline`
- [ ] **`api_commit_diff`** — `GET /repos/{owner}/{repo}/git/commits/{sha}.*` (diff/patch)
- [ ] **`api_commit_note_list/set/delete`** — git notes endpoints
- [ ] **`api_content_archive`** — `GET /repos/{owner}/{repo}/archive/*.{tar,zip}`
- [x] ~~**`api_release_asset_upload`** — `POST /repos/{owner}/{repo}/releases/{id}/assets`~~ — implemented; Forgejo accepts a raw body with `?name=<file>`, no multipart required.
- [ ] **`api_hook_edit`** — `PATCH /repos/{owner}/{repo}/hooks/{id}`
- [ ] **`api_wiki_revisions`** — `GET /repos/{owner}/{repo}/wiki/revisions/{page}`

## 3. Add repo misc subcommands (Phase 15)

API functions exist but have no CLI dispatch:

- [ ] **`cb repo watch <owner/repo>`** — call `api_repo_watch`
- [ ] **`cb repo unwatch <owner/repo>`** — call `api_repo_unwatch`
- [ ] **`cb repo is-watching <owner/repo>`** — call `api_repo_is_watching`
- [ ] **`cb repo stars <owner/repo>`** — call `api_repo_stargazers`, list stargazers
- [ ] **`cb repo languages <owner/repo>`** — call `api_repo_languages`, show language breakdown
- [ ] **`cb repo mirror-sync <owner/repo>`** — call `api_repo_mirror_sync`
- [ ] **`cb repo generate`** — new repo from template (needs API function)
- [ ] **`cb repo migrate`** — migrate repo from another instance (needs API function)

## 4. Multipart upload support

Not needed for release assets after all — Forgejo accepts a raw body for
`/releases/{id}/assets` (see the Next session section). Kept for endpoints that
genuinely require `multipart/form-data`.

- [ ] Implement `http_request_multipart()` convenience function (or build multipart body in `cb_api.c`)
- [ ] Construct multipart/form-data body with boundary, parts, and file content

## 5. Base64 integration in CLI

- [ ] `content create/update` — read file from disk, base64-encode, send to API
- [ ] `wiki create/edit` — read page content from file or stdin, base64-encode
- [ ] `content raw` — base64-decode API response and write to stdout/file

## 6. CLI-level tests

- [ ] Add test functions in `tests/test_cli.c` (or new `test_new_cli.c`) covering:
  - Help text for each new command (subcommand list, flag descriptions)
  - Flag parsing for create/edit commands
  - JSON output mode for list/show commands
  - Error cases (missing args, unknown flags, validation failures)
  - `require_owner_repo()` parsing and error messages

## 7. Code cleanup

- [x] ~~Fill in `owner` default with current user in `require_owner_repo()` — added `api_user_get_current()`, auto-fill owner when omitted~~
- [x] ~~Add `cb sshkey` command for user SSH public key management (list/add/show/rm) — uses `/user/keys` API endpoints~~
- [x] ~~Add `cb package` command for Forgejo Package Registry management (list/show/delete/files/link/unlink) — uses `/packages/{owner}/...` API endpoints~~

## 8. CI release notes

- [x] ~~Consider switching to `override: true` + `release-notes-file` in the
      `forgejo-release` action so CI owns the full release (notes + assets).
      This would let the release notes be maintained in a file in the repo
      (e.g. `RELEASE_NOTES.md`) and attached automatically on tag pushes,
      instead of creating releases manually with `cb release create`.~~
      Reverted — notes are drafted per release and approved by a human, so they
      do not belong in a file in the repo. CI still owns _creating_ the release;
      `cb release edit` fills in the body afterwards. The release flow is:
      `cb tag create` → CI's `release` job creates the release (as a draft),
      uploads the assets and publishes it → `cb release by-tag` for the id →
      `cb release edit <id> --body ...` with the approved notes.

- [x] ~~Document why the release must not be created before CI runs.~~
      `forgejo-release` (upload) runs `ensure_tag` and then POSTs
      `/repos/{owner}/{repo}/releases` unconditionally, and its `api()` helper
      calls `curl --fail`, so an existing release for the tag is fatal: the
      `release` job dies with HTTP 409 before it uploads anything. `override: true`
      is not a workaround: `maybe_override()` DELETEs the release first, taking
      its assets with it. Because the action creates the release as
      `draft: true` and only PATCHes it to published at the very end, a job that
      fails after creation leaves a release behind that makes every retry 409 as
      well. Recovery is to delete the release but keep the tag —
      `cb release delete-tag <owner/repo> <tag> --yes` maps to
      `DELETE /releases/tags/{tag}`, which does not touch the tag — and re-run
      the job. Same flow is documented in the `codeberg-release` skill.
