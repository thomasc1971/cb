/* cb — Codeberg (Forgejo) Repository Management CLI
 * Copyright (C) 2026 Thomas Christensen
 *
 * This file is part of cb.
 *
 * cb is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * cb is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cb.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef CB_API_H
#define CB_API_H

#include "cb_http.h"
#include <stddef.h>
#include <stdint.h>

/* API error codes */
typedef enum
{
    API_OK = 0,
    API_ERR_NETWORK = -1,
    API_ERR_AUTH = -2,
    API_ERR_NOT_FOUND = -3,
    API_ERR_SCOPE = -4,
    API_ERR_QUOTA = -5,
    API_ERR_CONFLICT = -6,
    API_ERR_VALIDATION = -7,
    API_ERR_SERVER = -8,
    API_ERR_UNKNOWN = -9
} ApiError;

/* Repository struct — fields we care about from the Forgejo API response */
typedef struct
{
    char *name;
    char *full_name; /* "owner/repo" */
    char *description;
    char *html_url;
    char *default_branch;
    char *language;
    int private;
    int archived;
    int template;
    int stars;
    int forks;
    int has_issues;
    int has_wiki;
    int has_pull_requests;
    char **topics;
    size_t topic_count;
} Repo;

/* Create repo options */
typedef struct
{
    const char *name;
    const char *description; /* may be NULL */
    int private_set;
    int private_val;
    const char *default_branch; /* may be NULL */
    const char *license;        /* may be NULL */
    const char *gitignores;     /* may be NULL */
    int auto_init;
    int template;
    const char *org;           /* may be NULL — create under org instead of user */
    const char *object_format; /* may be NULL — "sha1" or "sha256" */
} CreateRepoOpts;

/* Edit repo options — all fields are "set + value" pairs.
 * If *_set is 0, the field is omitted from the PATCH body (matching Forgejo's omitempty). */
typedef struct
{
    int name_set;
    const char *name;
    int desc_set;
    const char *description;
    int website_set;
    const char *website;
    int private_set;
    int private_val;
    int default_branch_set;
    const char *default_branch;
    int archived_set;
    int archived_val;
    int template_set;
    int template_val;
    int has_issues_set;
    int has_issues_val;
    int has_wiki_set;
    int has_wiki_val;
    int has_prs_set;
    int has_prs_val;
    int has_projects_set;
    int has_projects_val;
    int has_releases_set;
    int has_releases_val;
    int has_packages_set;
    int has_packages_val;
    int has_actions_set;
    int has_actions_val;
    int allow_merge_set;
    int allow_merge_val;
    int allow_rebase_set;
    int allow_rebase_val;
    int allow_squash_set;
    int allow_squash_val;
    int allow_ff_only_set;
    int allow_ff_only_val;
    int default_merge_style_set;
    const char *default_merge_style;
    int delete_branch_after_merge_set;
    int delete_branch_after_merge_val;
    int allow_maintainer_edit_set;
    int allow_maintainer_edit_val;
} EditRepoOpts;

/* API client */
typedef struct
{
    HttpClient http;
    char path_prefix[256]; /* e.g. "/api/v1" */
    char last_error[512];
} ApiClient;

/* Initialize API client from a base_url and token. Returns 0 on success. */
int api_client_init(ApiClient *a, const char *base_url, const char *token);

/* Free API client resources. */
void api_client_free(ApiClient *a);

/* Repo operations — return API_OK (0) on success, negative ApiError on failure.
 * On success, Repo output structs are populated (caller must call repo_free).
 * last_error is set on failure. */

int api_repo_create(ApiClient *a, const CreateRepoOpts *opts, Repo *out);
int api_repo_delete(ApiClient *a, const char *owner, const char *repo);
int api_repo_edit(ApiClient *a, const char *owner, const char *repo,
                  const EditRepoOpts *opts, Repo *out);
int api_repo_show(ApiClient *a, const char *owner, const char *repo, Repo *out);
int api_repo_list(ApiClient *a, const char *owner, int is_org,
                  Repo **out, size_t *count);
int api_repo_transfer(ApiClient *a, const char *owner, const char *repo,
                      const char *new_owner, const int64_t *team_ids, size_t team_count);

/* Topic operations */
int api_topic_list(ApiClient *a, const char *owner, const char *repo,
                   char ***topics, size_t *count);
int api_topic_set(ApiClient *a, const char *owner, const char *repo,
                  const char **topics, size_t count);
int api_topic_add(ApiClient *a, const char *owner, const char *repo, const char *topic);
int api_topic_remove(ApiClient *a, const char *owner, const char *repo, const char *topic);

/* Free a Repo struct */
void repo_free(Repo *r);

/* Free a Repo array (from api_repo_list) */
void repo_array_free(Repo *arr, size_t count);

/* Free a topic array (from api_topic_list) */
void topic_array_free(char **topics, size_t count);

/* ===== Organizations ===== */

/* Organization struct — fields from the Forgejo API response */
typedef struct
{
    char *name;
    char *full_name;
    char *description;
    char *email;
    char *location;
    char *website;
    char *visibility;
    char *avatar_url;
    int64_t id;
    int repo_admin_change_team_access;
} Organization;

/* Create org options */
typedef struct
{
    const char *username; /* required */
    const char *full_name;
    const char *description;
    const char *email;
    const char *location;
    const char *website;
    const char *visibility; /* may be NULL — "public", "limited", "private" */
    int repo_admin_change_team_access;
} CreateOrgOpts;

/* Create an organization. Returns API_OK on success. */
int api_org_create(ApiClient *a, const CreateOrgOpts *opts, Organization *out);

/* Free an Organization struct */
void org_free(Organization *o);

/* ===== Actions (CI/CD) ===== */

/* Action run — workflow execution info */
typedef struct
{
    int64_t id;
    int64_t index_in_repo;
    char *title;
    char *status;      /* "waiting", "running", "success", "failure", "cancelled" */
    char *event;       /* "push", "pull_request", "schedule", etc. */
    char *workflow_id; /* workflow file name */
    char *prettyref;   /* branch/tag ref */
    char *commit_sha;
    char *html_url;
    char *created; /* ISO timestamp */
    char *started;
    char *stopped;
} ActionRun;

/* Action runner — CI runner info */
typedef struct
{
    int64_t id;
    char *name;
    char *uuid;
    char *status; /* "online", "offline" */
    char *version;
} ActionRunner;

/* Action variable — CI/CD variable */
typedef struct
{
    char *name;
    char *data; /* the value */
} ActionVariable;

/* Action secret — name only (API never returns values) */
typedef struct
{
    char *name;
} ActionSecret;

/* List action runs for a repository. Caller frees with action_run_array_free. */
int api_action_run_list(ApiClient *a, const char *owner, const char *repo,
                        ActionRun **out, size_t *count);

/* Get a single action run by its run number (index_in_repo, the #N shown in actions list).
 * Caller frees with action_run_free. */
int api_action_run_show(ApiClient *a, const char *owner, const char *repo,
                        int64_t run_number, ActionRun *out);

/* List runners available to a repository. Caller frees with action_runner_array_free. */
int api_action_runner_list(ApiClient *a, const char *owner, const char *repo,
                           ActionRunner **out, size_t *count);

/* Dispatch a workflow by filename. ref is the git ref (e.g. "master"), may be NULL. */
int api_action_dispatch(ApiClient *a, const char *owner, const char *repo,
                        const char *workflowfile, const char *ref);

/* List repo action secrets. Caller frees with action_secret_array_free. */
int api_action_secret_list(ApiClient *a, const char *owner, const char *repo,
                           ActionSecret **out, size_t *count);

/* Create or update a secret. */
int api_action_secret_set(ApiClient *a, const char *owner, const char *repo,
                          const char *name, const char *value);

/* Delete a secret. */
int api_action_secret_delete(ApiClient *a, const char *owner, const char *repo,
                             const char *name);

/* List repo action variables. Caller frees with action_variable_array_free. */
int api_action_variable_list(ApiClient *a, const char *owner, const char *repo,
                             ActionVariable **out, size_t *count);

/* Get a single repo action variable. Caller frees with action_variable_free. */
int api_action_variable_show(ApiClient *a, const char *owner, const char *repo,
                             const char *name, ActionVariable *out);

/* Create or update a variable. */
int api_action_variable_set(ApiClient *a, const char *owner, const char *repo,
                            const char *name, const char *value);

/* Delete a variable. */
int api_action_variable_delete(ApiClient *a, const char *owner, const char *repo,
                               const char *name);

/* Free functions for actions types */
void action_run_free(ActionRun *r);
void action_run_array_free(ActionRun *arr, size_t count);
void action_runner_free(ActionRunner *r);
void action_runner_array_free(ActionRunner *arr, size_t count);
void action_variable_free(ActionVariable *v);
void action_variable_array_free(ActionVariable *arr, size_t count);
void action_secret_free(ActionSecret *s);
void action_secret_array_free(ActionSecret *arr, size_t count);

/* ===== Actions jobs & logs (web UI endpoint, not /api/v1) ===== */

/* Action job — one job within a workflow run */
typedef struct
{
    int64_t id;
    char *name;
    char *status;   /* "success", "failure", "skipped", "running", "waiting" */
    char *duration; /* human-readable, e.g. "59s", "1m2s" */
} ActionJob;

/* Action step — one step within a job */
typedef struct
{
    char *summary;  /* step name, e.g. "Install dependencies" */
    char *status;   /* "success", "failure", "skipped" */
    char *duration; /* human-readable, e.g. "11s" */
} ActionStep;

/* Action log line — one line of log output */
typedef struct
{
    int index;     /* line number within the step */
    char *message; /* log text */
} ActionLogLine;

/* Full job detail: steps + logs */
typedef struct
{
    ActionJob job;
    ActionStep *steps;
    size_t step_count;
} ActionJobDetail;

/* Fetch jobs for a run. run_id is the run number (index_in_repo, the #N shown
 * in actions list). Caller frees with action_job_array_free. */
int api_action_job_list(ApiClient *a, const char *owner, const char *repo,
                        int64_t run_id, ActionJob **out, size_t *count);

/* Fetch job details (steps) for a specific job in a run.
 * job_index is 0-based. Caller frees with action_job_detail_free. */
int api_action_job_detail(ApiClient *a, const char *owner, const char *repo,
                          int64_t run_id, int job_index, ActionJobDetail *out);

/* Fetch log lines for a specific step within a job.
 * Returns lines in *out and count. Caller frees with action_log_lines_free.
 * step_index is 0-based. Fetches all pages (handles cursor pagination). */
int api_action_log_fetch(ApiClient *a, const char *owner, const char *repo,
                         int64_t run_id, int job_index, int step_index,
                         ActionLogLine **out, size_t *count);

void action_job_free(ActionJob *j);
void action_job_array_free(ActionJob *arr, size_t count);
void action_job_detail_free(ActionJobDetail *d);
void action_log_lines_free(ActionLogLine *lines, size_t count);

/* ===== Releases ===== */

typedef struct
{
    int64_t id;
    char *tag_name;
    char *name;
    char *body;
    char *target_commitish;
    char *html_url;
    char *tarball_url;
    char *zipball_url;
    char *upload_url;
    char *url;
    char *created_at;
    char *published_at;
    int draft;
    int prerelease;
    int hide_archive_links;
} Release;

typedef struct
{
    int64_t id;
    char *name;
    char *browser_download_url;
    char *uuid;
    char *created_at;
    char *type;
    int64_t size;
    int64_t download_count;
} Attachment;

typedef struct
{
    const char *tag_name;
    const char *name;
    const char *body;
    const char *target_commitish;
    int draft_set;
    int draft_val;
    int prerelease_set;
    int prerelease_val;
    int hide_archive_links_set;
    int hide_archive_links_val;
} CreateReleaseOpts;

typedef struct
{
    int tag_name_set;
    const char *tag_name;
    int name_set;
    const char *name;
    int body_set;
    const char *body;
    int target_commitish_set;
    const char *target_commitish;
    int draft_set;
    int draft_val;
    int prerelease_set;
    int prerelease_val;
    int hide_archive_links_set;
    int hide_archive_links_val;
} EditReleaseOpts;

int api_release_list(ApiClient *a, const char *owner, const char *repo,
                     int draft, int prerelease, const char *q,
                     int limit, Release **out, size_t *count);
int api_release_create(ApiClient *a, const char *owner, const char *repo,
                       const CreateReleaseOpts *opts, Release *out);
int api_release_get(ApiClient *a, const char *owner, const char *repo,
                    int64_t id, Release *out);
int api_release_get_latest(ApiClient *a, const char *owner, const char *repo,
                           Release *out);
int api_release_get_by_tag(ApiClient *a, const char *owner, const char *repo,
                           const char *tag, Release *out);
int api_release_edit(ApiClient *a, const char *owner, const char *repo,
                     int64_t id, const EditReleaseOpts *opts, Release *out);
int api_release_delete(ApiClient *a, const char *owner, const char *repo,
                       int64_t id);
int api_release_delete_by_tag(ApiClient *a, const char *owner, const char *repo,
                              const char *tag);

int api_release_asset_list(ApiClient *a, const char *owner, const char *repo,
                           int64_t release_id, Attachment **out, size_t *count);
int api_release_asset_get(ApiClient *a, const char *owner, const char *repo,
                          int64_t release_id, int64_t asset_id, Attachment *out);
int api_release_asset_edit(ApiClient *a, const char *owner, const char *repo,
                           int64_t release_id, int64_t asset_id,
                           const char *name, Attachment *out);
int api_release_asset_delete(ApiClient *a, const char *owner, const char *repo,
                             int64_t release_id, int64_t asset_id);

void release_free(Release *r);
void release_array_free(Release *arr, size_t count);
void attachment_free(Attachment *a);
void attachment_array_free(Attachment *arr, size_t count);

/* ===== Tags ===== */

typedef struct
{
    char *name;
    char *id;
    char *message;
    char *tarball_url;
    char *zipball_url;
} Tag;

typedef struct
{
    const char *tag_name;
    const char *message;
    const char *target;
} CreateTagOpts;

int api_tag_list(ApiClient *a, const char *owner, const char *repo,
                 int limit, Tag **out, size_t *count);
int api_tag_create(ApiClient *a, const char *owner, const char *repo,
                   const CreateTagOpts *opts, Tag *out);
int api_tag_get(ApiClient *a, const char *owner, const char *repo,
                const char *tag, Tag *out);
int api_tag_delete(ApiClient *a, const char *owner, const char *repo,
                   const char *tag);

void tag_free(Tag *t);
void tag_array_free(Tag *arr, size_t count);

/* ===== Branches ===== */

typedef struct
{
    char *name;
    char *commit_sha;
    char *commit_message;
    int protected;
    char *effective_branch_protection_name;
    int user_can_merge;
    int user_can_push;
} Branch;

typedef struct
{
    const char *new_branch_name;
    const char *old_ref_name;
} CreateBranchOpts;

int api_branch_list(ApiClient *a, const char *owner, const char *repo,
                    int limit, Branch **out, size_t *count);
int api_branch_create(ApiClient *a, const char *owner, const char *repo,
                      const CreateBranchOpts *opts, Branch *out);
int api_branch_get(ApiClient *a, const char *owner, const char *repo,
                   const char *branch, Branch *out);
int api_branch_rename(ApiClient *a, const char *owner, const char *repo,
                      const char *branch, const char *new_name, Branch *out);
int api_branch_delete(ApiClient *a, const char *owner, const char *repo,
                      const char *branch);

void branch_free(Branch *b);
void branch_array_free(Branch *arr, size_t count);

/* ===== Issues ===== */

typedef struct
{
    int64_t id;
    int number;
    char *title;
    char *body;
    char *state;
    char *html_url;
    char *created_at;
    char *updated_at;
    char *closed_at;
    char *due_date;
    int is_locked;
    int comments;
    int pin_order;
} Issue;

typedef struct
{
    const char *title;
    const char *body;
    const char *assignee;
    const char *const *assignees;
    int64_t milestone;
    const int64_t *labels;
    size_t label_count;
    const char *due_date;
    const char *ref;
} CreateIssueOpts;

typedef struct
{
    int title_set;
    const char *title;
    int body_set;
    const char *body;
    int state_set;
    const char *state;
    int milestone_set;
    int64_t milestone;
    int due_date_set;
    const char *due_date;
    int unset_due_date;
    const char *const *assignees;
    size_t assignee_count;
    int ref_set;
    const char *ref;
} EditIssueOpts;

int api_issue_list(ApiClient *a, const char *owner, const char *repo,
                   const char *state, const char *labels, const char *type,
                   int limit, Issue **out, size_t *count);
int api_issue_create(ApiClient *a, const char *owner, const char *repo,
                     const CreateIssueOpts *opts, Issue *out);
int api_issue_get(ApiClient *a, const char *owner, const char *repo,
                  int number, Issue *out);
int api_issue_edit(ApiClient *a, const char *owner, const char *repo,
                   int number, const EditIssueOpts *opts, Issue *out);
int api_issue_delete(ApiClient *a, const char *owner, const char *repo,
                     int number);
int api_issue_comment_create(ApiClient *a, const char *owner, const char *repo,
                             int number, const char *body);
int api_issue_label_add(ApiClient *a, const char *owner, const char *repo,
                        int number, const int64_t *labels, size_t count);
int api_issue_label_set(ApiClient *a, const char *owner, const char *repo,
                        int number, const int64_t *labels, size_t count);
int api_issue_label_clear(ApiClient *a, const char *owner, const char *repo,
                          int number);
int api_issue_label_remove(ApiClient *a, const char *owner, const char *repo,
                           int number, int64_t label_id);

void issue_free(Issue *i);
void issue_array_free(Issue *arr, size_t count);

/* ===== Labels ===== */

typedef struct
{
    int64_t id;
    char *name;
    char *color;
    char *description;
    int exclusive;
    int is_archived;
} Label;

typedef struct
{
    const char *name;
    const char *color;
    const char *description;
    int exclusive_set;
    int exclusive_val;
    int is_archived_set;
    int is_archived_val;
} CreateLabelOpts;

int api_label_list(ApiClient *a, const char *owner, const char *repo,
                   Label **out, size_t *count);
int api_label_create(ApiClient *a, const char *owner, const char *repo,
                     const CreateLabelOpts *opts, Label *out);
int api_label_get(ApiClient *a, const char *owner, const char *repo,
                  int64_t id, Label *out);
int api_label_edit(ApiClient *a, const char *owner, const char *repo,
                   int64_t id, int name_set, const char *name,
                   int color_set, const char *color,
                   int desc_set, const char *description, Label *out);
int api_label_delete(ApiClient *a, const char *owner, const char *repo,
                     int64_t id);

void label_free(Label *l);
void label_array_free(Label *arr, size_t count);

/* ===== Milestones ===== */

typedef struct
{
    int64_t id;
    char *title;
    char *description;
    char *state;
    char *due_on;
    char *created_at;
    char *updated_at;
    int open_issues;
    int closed_issues;
} Milestone;

typedef struct
{
    const char *title;
    const char *description;
    const char *state;
    const char *due_on;
} CreateMilestoneOpts;

int api_milestone_list(ApiClient *a, const char *owner, const char *repo,
                       const char *state, Milestone **out, size_t *count);
int api_milestone_create(ApiClient *a, const char *owner, const char *repo,
                         const CreateMilestoneOpts *opts, Milestone *out);
int api_milestone_get(ApiClient *a, const char *owner, const char *repo,
                      int64_t id, Milestone *out);
int api_milestone_edit(ApiClient *a, const char *owner, const char *repo,
                       int64_t id, int title_set, const char *title,
                       int desc_set, const char *description,
                       int state_set, const char *state,
                       int due_set, const char *due_on, Milestone *out);
int api_milestone_delete(ApiClient *a, const char *owner, const char *repo,
                         int64_t id);

void milestone_free(Milestone *m);
void milestone_array_free(Milestone *arr, size_t count);

/* ===== Pull Requests ===== */

typedef struct
{
    int64_t id;
    int number;
    char *title;
    char *body;
    char *state;
    int draft;
    int merged;
    int mergeable;
    char *merged_at;
    char *closed_at;
    char *created_at;
    char *updated_at;
    char *html_url;
    char *diff_url;
    char *patch_url;
    char *merge_commit_sha;
    char *base_ref;
    char *head_ref;
    int additions;
    int deletions;
    int changed_files;
    int comments;
} PullRequest;

typedef struct
{
    const char *title;
    const char *body;
    const char *head;
    const char *base;
    const char *assignee;
    const char *const *assignees;
    const int64_t *labels;
    size_t label_count;
    int64_t milestone;
    const char *due_date;
} CreatePullRequestOpts;

typedef struct
{
    int title_set;
    const char *title;
    int body_set;
    const char *body;
    int base_set;
    const char *base;
    int state_set;
    const char *state;
    int allow_maintainer_edit_set;
    int allow_maintainer_edit_val;
    int unset_due_date;
} EditPullRequestOpts;

typedef struct
{
    const char *do_style;
    const char *merge_title;
    const char *merge_message;
    int delete_branch_after_merge;
    int force_merge;
    int merge_when_checks_succeed;
    const char *head_commit_id;
} MergePullRequestOpts;

int api_pr_list(ApiClient *a, const char *owner, const char *repo,
                const char *state, int limit, PullRequest **out, size_t *count);
int api_pr_create(ApiClient *a, const char *owner, const char *repo,
                  const CreatePullRequestOpts *opts, PullRequest *out);
int api_pr_get(ApiClient *a, const char *owner, const char *repo,
               int number, PullRequest *out);
int api_pr_edit(ApiClient *a, const char *owner, const char *repo,
                int number, const EditPullRequestOpts *opts, PullRequest *out);
int api_pr_merge(ApiClient *a, const char *owner, const char *repo,
                 int number, const MergePullRequestOpts *opts);
int api_pr_cancel_merge(ApiClient *a, const char *owner, const char *repo,
                        int number);
int api_pr_is_merged(ApiClient *a, const char *owner, const char *repo,
                     int number, int *out);
int api_pr_diff(ApiClient *a, const char *owner, const char *repo,
                int number, int patch, char **out, size_t *out_len);

void pullrequest_free(PullRequest *p);
void pullrequest_array_free(PullRequest *arr, size_t count);

/* ===== Commits ===== */

typedef struct
{
    char *sha;
    char *created;
    char *html_url;
    char *message;
    char *author_name;
    char *author_email;
    char *committer_name;
    char *committer_email;
    int additions;
    int deletions;
    int total;
} Commit;

typedef struct
{
    char *status;
    char *context;
    char *description;
    char *target_url;
    char *created_at;
    char *updated_at;
} CommitStatus;

int api_commit_list(ApiClient *a, const char *owner, const char *repo,
                    const char *sha, const char *path,
                    int limit, Commit **out, size_t *count);
int api_commit_get(ApiClient *a, const char *owner, const char *repo,
                   const char *sha, Commit *out);
int api_commit_status(ApiClient *a, const char *owner, const char *repo,
                      const char *ref, CommitStatus **out, size_t *count);
int api_commit_compare(ApiClient *a, const char *owner, const char *repo,
                       const char *basehead, Commit **out, size_t *count);

void commit_free(Commit *c);
void commit_array_free(Commit *arr, size_t count);
void commitstatus_free(CommitStatus *s);
void commitstatus_array_free(CommitStatus *arr, size_t count);

/* ===== File Contents ===== */

typedef struct
{
    char *type;
    char *name;
    char *path;
    char *sha;
    int64_t size;
    char *encoding;
    char *content;
    char *download_url;
    char *html_url;
    char *git_url;
    char *last_commit_sha;
} ContentEntry;

int api_content_list(ApiClient *a, const char *owner, const char *repo,
                     const char *ref, ContentEntry **out, size_t *count);
int api_content_get(ApiClient *a, const char *owner, const char *repo,
                    const char *filepath, const char *ref, ContentEntry *out);
int api_content_create(ApiClient *a, const char *owner, const char *repo,
                       const char *filepath, const char *message,
                       const char *content_b64, const char *branch,
                       const char *new_branch);
int api_content_update(ApiClient *a, const char *owner, const char *repo,
                       const char *filepath, const char *message,
                       const char *content_b64, const char *sha,
                       const char *branch, const char *new_branch);
int api_content_delete(ApiClient *a, const char *owner, const char *repo,
                       const char *filepath, const char *message,
                       const char *sha, const char *branch,
                       const char *new_branch);
int api_content_raw(ApiClient *a, const char *owner, const char *repo,
                    const char *filepath, const char *ref,
                    char **out, size_t *out_len);

void content_entry_free(ContentEntry *e);
void content_entry_array_free(ContentEntry *arr, size_t count);

/* ===== Deploy Keys ===== */

typedef struct
{
    int64_t id;
    char *title;
    char *key;
    char *fingerprint;
    int read_only;
    char *created_at;
} DeployKey;

typedef struct
{
    const char *title;
    const char *key;
    int read_only;
} CreateKeyOpts;

int api_key_list(ApiClient *a, const char *owner, const char *repo,
                 DeployKey **out, size_t *count);
int api_key_add(ApiClient *a, const char *owner, const char *repo,
                const CreateKeyOpts *opts, DeployKey *out);
int api_key_get(ApiClient *a, const char *owner, const char *repo,
                int64_t id, DeployKey *out);
int api_key_delete(ApiClient *a, const char *owner, const char *repo,
                   int64_t id);

void deploykey_free(DeployKey *k);
void deploykey_array_free(DeployKey *arr, size_t count);

/* ===== Collaborators ===== */

typedef struct
{
    int64_t id;
    char *login;
    char *full_name;
    char *email;
    char *html_url;
} User;

int api_collaborator_list(ApiClient *a, const char *owner, const char *repo,
                          User **out, size_t *count);
int api_collaborator_add(ApiClient *a, const char *owner, const char *repo,
                         const char *username, const char *permission);
int api_collaborator_remove(ApiClient *a, const char *owner, const char *repo,
                            const char *username);
int api_collaborator_perms(ApiClient *a, const char *owner, const char *repo,
                           const char *username, char *perm_out, size_t perm_sz);

void user_free(User *u);
void user_array_free(User *arr, size_t count);

/* ===== Current user ===== */

int api_user_get_current(ApiClient *a, User *out);

/* ===== Forks ===== */

int api_fork_list(ApiClient *a, const char *owner, const char *repo,
                  Repo **out, size_t *count);
int api_fork_create(ApiClient *a, const char *owner, const char *repo,
                    const char *name, const char *org, Repo *out);

/* ===== Webhooks ===== */

typedef struct
{
    int64_t id;
    char *type;
    int active;
    char *url;
    char *content_type;
    char *branch_filter;
    char *authorization_header;
} Hook;

typedef struct
{
    const char *type;
    const char *url;
    const char *content_type;
    const char *secret;
    const char *const *events;
    int active;
    const char *branch_filter;
    const char *authorization_header;
} CreateHookOpts;

int api_hook_list(ApiClient *a, const char *owner, const char *repo,
                  Hook **out, size_t *count);
int api_hook_create(ApiClient *a, const char *owner, const char *repo,
                    const CreateHookOpts *opts, Hook *out);
int api_hook_get(ApiClient *a, const char *owner, const char *repo,
                 int64_t id, Hook *out);
int api_hook_delete(ApiClient *a, const char *owner, const char *repo,
                    int64_t id);
int api_hook_test(ApiClient *a, const char *owner, const char *repo,
                  int64_t id);

void hook_free(Hook *h);
void hook_array_free(Hook *arr, size_t count);

/* ===== Wiki ===== */

typedef struct
{
    char *title;
    char *content_base64;
    char *html_url;
    char *sub_url;
    int commit_count;
    char *last_commit_sha;
} WikiPage;

typedef struct
{
    const char *title;
    const char *content_base64;
    const char *message;
} CreateWikiPageOpts;

int api_wiki_list(ApiClient *a, const char *owner, const char *repo,
                  WikiPage **out, size_t *count);
int api_wiki_create(ApiClient *a, const char *owner, const char *repo,
                    const CreateWikiPageOpts *opts, WikiPage *out);
int api_wiki_get(ApiClient *a, const char *owner, const char *repo,
                 const char *page_name, WikiPage *out);
int api_wiki_edit(ApiClient *a, const char *owner, const char *repo,
                  const char *page_name, const char *content_b64,
                  const char *message, WikiPage *out);
int api_wiki_delete(ApiClient *a, const char *owner, const char *repo,
                    const char *page_name);

void wikipage_free(WikiPage *w);
void wikipage_array_free(WikiPage *arr, size_t count);

/* ===== Repo misc ===== */

int api_repo_watch(ApiClient *a, const char *owner, const char *repo);
int api_repo_unwatch(ApiClient *a, const char *owner, const char *repo);
int api_repo_is_watching(ApiClient *a, const char *owner, const char *repo,
                         int *out);
int api_repo_stargazers(ApiClient *a, const char *owner, const char *repo,
                        User **out, size_t *count);
int api_repo_languages(ApiClient *a, const char *owner, const char *repo,
                       char ***langs, int64_t **bytes, size_t *count);
int api_repo_mirror_sync(ApiClient *a, const char *owner, const char *repo);

/* ===== User SSH public keys ===== */

typedef struct
{
    int64_t id;
    char *title;
    char *key;
    char *fingerprint;
    char *key_type;
    int read_only;
    char *url;
    char *created_at;
} PublicKey;

int api_user_key_list(ApiClient *a, PublicKey **out, size_t *count);
int api_user_key_add(ApiClient *a, const char *title, const char *key,
                     int read_only, PublicKey *out);
int api_user_key_get(ApiClient *a, int64_t id, PublicKey *out);
int api_user_key_delete(ApiClient *a, int64_t id);

void public_key_free(PublicKey *k);
void public_key_array_free(PublicKey *arr, size_t count);

#endif /* CB_API_H */
