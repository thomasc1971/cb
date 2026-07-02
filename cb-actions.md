# Plan: Add `cb actions` Command

## Goal

Add a new `actions` top-level command to `cb` for inspecting CI/CD workflow runs, managing runners, and handling repo-level secrets/variables — all via the Forgejo API.

## API Endpoints (repo-scoped)

All under `/repos/{owner}/{repo}/actions/`:

| Method | Endpoint                                   | Operation            | `cb` subcommand       |
| ------ | ------------------------------------------ | -------------------- | --------------------- |
| GET    | `/runs`                                    | List action runs     | `actions list`        |
| GET    | `/runs/{run_id}`                           | Get a single run     | `actions show`        |
| GET    | `/runners`                                 | List repo runners    | `actions runners`     |
| GET    | `/secrets`                                 | List secrets         | `actions secret list` |
| PUT    | `/secrets/{name}`                          | Create/update secret | `actions secret set`  |
| DELETE | `/secrets/{name}`                          | Delete secret        | `actions secret rm`   |
| GET    | `/variables`                               | List variables       | `actions var list`    |
| GET    | `/variables/{name}`                        | Get a variable       | `actions var show`    |
| PUT    | `/variables/{name}`                        | Update variable      | `actions var set`     |
| POST   | `/variables/{name}`                        | Create variable      | `actions var set`     |
| DELETE | `/variables/{name}`                        | Delete variable      | `actions var rm`      |
| POST   | `/workflows/{workflowfilename}/dispatches` | Dispatch a workflow  | `actions dispatch`    |

## Command Tree

```
cb actions <owner/repo> [subcommand] [args] [flags]

  cb actions list <owner/repo>                    List recent workflow runs
  cb actions show <owner/repo> <run-id>           Show details of a specific run
  cb actions runners <owner/repo>                 List runners available to this repo
  cb actions dispatch <owner/repo> <workflow>     Trigger a workflow (--ref, --input key=val)
  cb actions secret list <owner/repo>             List secret names
  cb actions secret set <owner/repo> <name>       Create/update a secret (reads value from --value or stdin)
  cb actions secret rm <owner/repo> <name>        Delete a secret (--yes)
  cb actions var list <owner/repo>                List variables
  cb actions var show <owner/repo> <name>         Show a variable's value
  cb actions var set <owner/repo> <name>          Create/update a variable (--value)
  cb actions var rm <owner/repo> <name>           Delete a variable (--yes)
```

## Data Structures

### `ActionRun` (from `GET /runs` and `GET /runs/{id}`)

```c
typedef struct
{
    int64_t id;
    int64_t index_in_repo;
    char *title;
    char *status;        /* "waiting", "running", "success", "failure", "cancelled" */
    char *event;         /* "push", "pull_request", "schedule", etc. */
    char *workflow_id;   /* workflow file name */
    char *prettyref;     /* branch/tag ref */
    char *commit_sha;
    char *html_url;
    char *created;       /* timestamp */
    char *started;
    char *stopped;
} ActionRun;
```

### `ActionRunner` (from `GET /runners`)

```c
typedef struct
{
    int64_t id;
    char *name;
    char *uuid;
    char *status;        /* "online", "offline" */
    char *version;
} ActionRunner;
```

### `ActionVariable` (from `GET /variables`)

```c
typedef struct
{
    char *name;
    char *data;          /* the value */
} ActionVariable;
```

## Files to Create/Modify

### New files:

- `include/cb_actions.h` — structs + API function declarations
- `src/cb_actions.c` — API functions (list runs, show run, list runners, secret CRUD, variable CRUD, dispatch)

### Modified files:

- `include/cb_api.h` — no changes (actions functions live in their own module)
- `src/cb_cli.c` — add `cmd_actions()` dispatcher + subcommand handlers + help text + top-level dispatch
- `src/Makefile.am` — add `cb_actions.c` to `cb_SOURCES`
- `tests/Makefile.am` — add `test_actions` binary
- `tests/test_actions.c` — tests for actions API functions
- `AGENTS.md` — document the new module

### No changes needed to:

- `cb_api.h`/`cb_api.c` — actions functions use `ApiClient` directly but don't need new repo operations
- `cb_http.h`/`cb_http.c` — HTTP layer is generic
- `cb_json.h`/`cb_json.c` — JSON layer is generic

## Implementation Pattern

Follow the existing `cb_api.c` pattern exactly:

1. **API functions** in `cb_actions.c` take `ApiClient *a`, owner/repo, output structs. Return `ApiError`. Use `do_request()`, `build_path()`, `map_status()`.
2. **Parse functions** (`parse_action_run`, `parse_runner`, etc.) extract fields from JSON using `json_dup_string()`, `json_get_int()`, etc.
3. **Free functions** (`action_run_free`, `action_run_array_free`, etc.) follow the NULL-check + memset-zero pattern.
4. **CLI handlers** in `cb_cli.c` follow the flag-table + `parse_flags()` + `find_flag_idx()` pattern.

## Output Formatting

### `actions list` (human-readable):

```
#3  failure   push       build.yml     master     2026-07-02 13:46
#2  cancelled push       build.yml     master     2026-07-02 13:44
#1  success   push       build.yml     master     2026-07-02 13:37
```

### `actions show`:

```
Run #3: "Remove container key from CI jobs"
Status:    failure
Event:     push
Workflow:  build.yml
Ref:       master
Commit:    86920ea
Created:   2026-07-02T13:45:57+02:00
URL:       https://codeberg.org/thomasc/cb/actions/runs/3
```

### `--json` mode:

Output the raw JSON response from the API, same as `cb repo show --json`.

## Flags

| Flag              | Used by                                 | Description                                      |
| ----------------- | --------------------------------------- | ------------------------------------------------ |
| `--limit N`       | `actions list`                          | Number of runs to show (default 10)              |
| `--ref REF`       | `actions dispatch`                      | Git ref to dispatch on (default: default branch) |
| `--input KEY=VAL` | `actions dispatch`                      | Workflow input (repeatable)                      |
| `--value VAL`     | `actions secret set`, `actions var set` | Value to set                                     |
| `--yes`           | `actions secret rm`, `actions var rm`   | Skip confirmation                                |
| `--help`/`-h`     | all                                     | Help at every level                              |

## Token scope

The existing token (`write:repository`) covers all actions operations. No new scope needed.

## Testing

- `tests/test_actions.c` — uses the mock HTTP server (same as `test_api.c`)
- Test list/show runs, list runners, secret/variable CRUD, dispatch
- Test error cases (404 not found, 403 scope, network errors)

## Implementation order (✅ all completed)

1. ~~Create `cb_actions.h` with structs and function declarations~~ — added to `cb_api.h` instead
2. ~~Create `cb_actions.c` with API functions~~ — added to `cb_api.c` instead
3. ~~Add `cmd_actions()` to `cb_cli.c` with subcommand dispatch~~
4. ~~Update `src/Makefile.am`~~ — no change needed (actions in `cb_api.c`)
5. ~~Create `tests/test_actions.c` and update `tests/Makefile.am`~~
6. ~~Update help text and top-level dispatch~~
7. ~~Build, test, format~~

## Logs (Phase 2 — ✅ implemented)

Forgejo does not expose workflow logs through the `/api/v1` REST API.
However, the web UI fetches logs via a JSON POST endpoint that accepts
token auth and returns structured JSON. Discovered from a HAR capture:

```
POST /{owner}/{repo}/actions/runs/{run_id}/jobs/{job_index}/attempt/{attempt}
Content-Type: application/json
Authorization: Bearer {token}

Body:
{
  "logCursors": [
    {"step": 0, "cursor": null, "expanded": false},
    {"step": 1, "cursor": null, "expanded": true},
    ...
  ]
}
```

### Response structure

```json
{
  "state": {
    "run": {
      "title": "...",
      "status": "failure",
      "jobs": [
        {"id": 9554048, "name": "build-linux", "status": "failure", "duration": "59s"},
        {"id": 9554051, "name": "build-windows", "status": "failure", "duration": "1m2s"},
        {"id": 9554054, "name": "release", "status": "skipped", "duration": "1s"}
      ]
    },
    "currentJob": {
      "title": "build-linux",
      "steps": [
        {"summary": "Set up job", "duration": "10s", "status": "success"},
        {"summary": "checkout@v6", "duration": "1s", "status": "success"},
        {"summary": "Install dependencies", "duration": "11s", "status": "failure"},
        {"summary": "Build", "duration": "0s", "status": "skipped"},
        ...
      ]
    }
  },
  "logs": {
    "stepsLog": [
      {
        "step": 2,
        "cursor": 33,
        "lines": [
          {"index": 1, "message": "Get:1 http://...", "timestamp": 1782992811.12},
          {"index": 2, "message": "Get:2 https://...", "timestamp": 1782992811.13},
          ...
        ]
      }
    ]
  }
}
```

### Key observations

- **Not under `/api/v1`** — the path is `/{owner}/{repo}/actions/...` directly. Path must be constructed without `path_prefix`; `do_request()` is called directly.
- **Uses token auth** — the `Authorization` header works the same as all other API calls.
- **Path uses `run_id` = `index_in_repo`** (the short number like `3`), not the internal `id` (like `5218484`).
- **`job_index`** is 0-based (0 = build-linux, 1 = build-windows, 2 = release).
- **`attempt`** is always `1` for first attempt; increments on reruns.
- **`logCursors`** array has one entry per step. Set `expanded: true` to fetch log lines for that step. `cursor: null` fetches from the beginning; the response returns a new cursor for pagination.
- **`logs.stepsLog[].lines[]`** contains the actual log output with `index` (line number), `message` (text), and `timestamp` (Unix epoch float).

### Proposed subcommands

```
cb actions jobs <owner/repo> <run-id>                  List jobs in a run (name, status, duration)
cb actions log <owner/repo> <run-id> [job-index]       Show log output for a run or specific job
```

### `actions jobs` output (human-readable):

```
Job 0  build-linux     failure    59s
Job 1  build-windows   failure    1m2s
Job 2  release         skipped    1s
```

### `actions log` output:

Prints log lines for all steps (or a specific job). For each step, show a header then the lines:

```
=== Install dependencies (failure, 11s) ===
Get:1 http://security.ubuntu.com/ubuntu noble-security InRelease [126 kB]
...
E: Unable to locate package libretls-dev
```

### Implementation notes

- The log endpoint is outside `/api/v1`, so `build_path()` won't work. Construct the path manually as `/{owner}/{repo}/actions/runs/{run_id}/jobs/{job}/attempt/{attempt}` and call `do_request()` directly.
- The POST body needs to be built from the step count. First, do a POST with empty cursors to discover the number of steps, then POST with `expanded: true` for the step(s) we want logs for.
- Alternatively, POST with all steps `expanded: true` in one request to get all logs at once.
- For `--json` mode, output the raw response JSON.
- Pagination: if a step has more lines than returned (cursor != null), repeat the POST with the returned cursor to fetch the next batch. Loop until cursor is null.
