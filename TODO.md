# TODO — Remaining work for Forgejo `/repos/` endpoints

All 15 phases have API-level structs, functions, and tests. The CLI has 16 top-level
commands with help text and core subcommands implemented. The remaining work is:

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
- [ ] **`key show`** — call `api_key_get`, print deploy key details
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
- [ ] **`label show`** — call `api_label_get`, print label details
- [ ] **`label edit`** — call `api_label_edit`, update name/color
- [ ] **`milestone show`** — call `api_milestone_get`, print milestone details
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
- [ ] **`api_release_asset_upload`** — `POST /repos/{owner}/{repo}/releases/{id}/assets` (multipart)
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

- [ ] Implement `http_request_multipart()` convenience function (or build multipart body in `cb_api.c`)
- [ ] Construct multipart/form-data body with boundary, parts, and file content
- [ ] Use for `api_release_asset_upload` to upload release attachments

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

## 8. CI release notes

- [ ] Consider switching to `override: true` + `release-notes-file` in the
      `forgejo-release` action so CI owns the full release (notes + assets).
      This would let the release notes be maintained in a file in the repo
      (e.g. `RELEASE_NOTES.md`) and attached automatically on tag pushes,
      instead of creating releases manually with `cb release create`.
