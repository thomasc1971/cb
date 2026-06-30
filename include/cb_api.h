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

#endif /* CB_API_H */
