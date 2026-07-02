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

## Implementation order

1. Create `cb_actions.h` with structs and function declarations
2. Create `cb_actions.c` with API functions
3. Add `cmd_actions()` to `cb_cli.c` with subcommand dispatch
4. Update `src/Makefile.am`
5. Create `tests/test_actions.c` and update `tests/Makefile.am`
6. Update help text and top-level dispatch
7. Build, test, format
