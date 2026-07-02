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

#include "config.h"
#include "cb_api.h"
#include "cb_config.h"
#include "cb_json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== Helpers ===== */

static void set_error(ApiClient *a, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(a->last_error, sizeof(a->last_error), fmt, args);
    va_end(args);
}

/* Map HTTP status code to ApiError */
static ApiError map_status(int status, const char *body)
{
    if (status >= 200 && status < 300)
        return API_OK;
    if (status == 401)
        return API_ERR_AUTH;
    if (status == 403) {
        /* Check if it's a scope error */
        if (body && strstr(body, "scope"))
            return API_ERR_SCOPE;
        return API_ERR_AUTH;
    }
    if (status == 404)
        return API_ERR_NOT_FOUND;
    if (status == 409)
        return API_ERR_CONFLICT;
    if (status == 422) {
        /* Could be quota or validation */
        if (body && (strstr(body, "quota") || strstr(body, "Quota")))
            return API_ERR_QUOTA;
        return API_ERR_VALIDATION;
    }
    if (status >= 500)
        return API_ERR_SERVER;
    return API_ERR_UNKNOWN;
}

static char *json_dup_string(const JsonValue *obj, const char *key)
{
    JsonValue *v = json_object_lookup(obj, key);
    if (!v || !json_is_string(v))
        return NULL;
    return strdup(json_string(v));
}

static int json_get_bool(const JsonValue *obj, const char *key, int default_val)
{
    JsonValue *v = json_object_lookup(obj, key);
    if (!v || !json_is_bool(v))
        return default_val;
    return json_bool(v) ? 1 : 0;
}

static int json_get_int(const JsonValue *obj, const char *key, int default_val)
{
    JsonValue *v = json_object_lookup(obj, key);
    if (!v || !json_is_number(v))
        return default_val;
    return (int)json_number(v);
}

static void parse_repo(const JsonValue *obj, Repo *r)
{
    memset(r, 0, sizeof(*r));
    r->name = json_dup_string(obj, "name");
    r->full_name = json_dup_string(obj, "full_name");
    r->description = json_dup_string(obj, "description");
    r->html_url = json_dup_string(obj, "html_url");
    r->default_branch = json_dup_string(obj, "default_branch");
    r->language = json_dup_string(obj, "language");
    r->private = json_get_bool(obj, "private", 0);
    r->archived = json_get_bool(obj, "archived", 0);
    r->template = json_get_bool(obj, "template", 0);
    r->stars = json_get_int(obj, "stars_count", 0);
    r->forks = json_get_int(obj, "forks_count", 0);
    r->has_issues = json_get_bool(obj, "has_issues", 0);
    r->has_wiki = json_get_bool(obj, "has_wiki", 0);
    r->has_pull_requests = json_get_bool(obj, "has_pull_requests", 0);
}

void repo_free(Repo *r)
{
    if (!r)
        return;
    free(r->name);
    free(r->full_name);
    free(r->description);
    free(r->html_url);
    free(r->default_branch);
    free(r->language);
    memset(r, 0, sizeof(*r));
}

void repo_array_free(Repo *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        repo_free(&arr[i]);
    free(arr);
}

void topic_array_free(char **topics, size_t count)
{
    if (!topics)
        return;
    for (size_t i = 0; i < count; i++)
        free(topics[i]);
    free(topics);
}

/* Build full API path: path_prefix + path. Caller frees. */
static char *build_path(ApiClient *a, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    /* First, format the path portion */
    char path[512];
    vsnprintf(path, sizeof(path), fmt, args);
    va_end(args);

    /* Prepend path_prefix */
    size_t total = strlen(a->path_prefix) + strlen(path) + 1;
    char *full = malloc(total);
    if (!full)
        return NULL;
    snprintf(full, total, "%s%s", a->path_prefix, path);
    return full;
}

/* ===== Client init/free ===== */

int api_client_init(ApiClient *a, const char *base_url, const char *token)
{
    memset(a, 0, sizeof(*a));

    char host[256], prefix[256];
    int port, use_tls;

    if (config_parse_url(base_url, host, sizeof(host), &port, &use_tls,
                         prefix, sizeof(prefix)) != 0) {
        set_error(a, "invalid base URL: %s", base_url);
        return -1;
    }

    if (http_client_init(&a->http, host, port, use_tls, token) != 0) {
        set_error(a, "failed to init HTTP client");
        return -1;
    }

    strncpy(a->path_prefix, prefix, sizeof(a->path_prefix) - 1);
    a->path_prefix[sizeof(a->path_prefix) - 1] = '\0';
    return 0;
}

void api_client_free(ApiClient *a)
{
    if (!a)
        return;
    http_client_free(&a->http);
    memset(a, 0, sizeof(*a));
}

/* ===== Internal request helper ===== */

static ApiError do_request(ApiClient *a, HttpMethod method,
                           const char *path, const char *body,
                           HttpResponse *resp)
{
    int rc = http_request(&a->http, method, path, body, resp);
    if (rc < 0) {
        set_error(a, "%s", resp->error);
        return API_ERR_NETWORK;
    }

    ApiError err = map_status(resp->status, resp->body);
    if (err != API_OK) {
        /* Try to extract message from body */
        const char *json_err = NULL;
        JsonValue *parsed = json_parse(resp->body, &json_err);
        if (parsed && json_is_object(parsed)) {
            JsonValue *msg = json_object_lookup(parsed, "message");
            if (msg && json_is_string(msg))
                set_error(a, "%s", json_string(msg));
            else
                set_error(a, "HTTP %d", resp->status);
        } else {
            set_error(a, "HTTP %d", resp->status);
        }
        json_free(parsed);
    }

    return err;
}

/* ===== Repo operations ===== */

int api_repo_create(ApiClient *a, const CreateRepoOpts *opts, Repo *out)
{
    if (!opts || !opts->name) {
        set_error(a, "repository name is required");
        return API_ERR_VALIDATION;
    }

    /* Build JSON body */
    JsonValue *body = json_object_new();
    json_object_set_string(body, "name", opts->name);
    if (opts->description)
        json_object_set_string(body, "description", opts->description);
    if (opts->private_set)
        json_object_set_bool(body, "private", opts->private_val);
    else
        json_object_set_bool(body, "private", false);
    if (opts->default_branch)
        json_object_set_string(body, "default_branch", opts->default_branch);
    if (opts->license)
        json_object_set_string(body, "license", opts->license);
    if (opts->gitignores)
        json_object_set_string(body, "gitignores", opts->gitignores);
    if (opts->auto_init)
        json_object_set_bool(body, "auto_init", true);
    if (opts->template)
        json_object_set_bool(body, "template", true);
    if (opts->object_format)
        json_object_set_string(body, "object_format_name", opts->object_format);

    char *body_str = json_serialize(body, false);
    json_free(body);

    /* Build path */
    char *path;
    if (opts->org)
        path = build_path(a, "/orgs/%s/repos", opts->org);
    else
        path = build_path(a, "/user/repos");

    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    /* Parse response into Repo */
    const char *json_err = NULL;
    JsonValue *parsed = json_parse(resp.body, &json_err);
    http_response_free(&resp);

    if (!parsed || !json_is_object(parsed)) {
        json_free(parsed);
        set_error(a, "failed to parse API response");
        return API_ERR_UNKNOWN;
    }

    parse_repo(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_repo_delete(ApiClient *a, const char *owner, const char *repo)
{
    char *path = build_path(a, "/repos/%s/%s", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

int api_repo_edit(ApiClient *a, const char *owner, const char *repo,
                  const EditRepoOpts *opts, Repo *out)
{
    JsonValue *body = json_object_new();

    /* Only include fields that are set — critical for omitempty semantics */
    if (opts->name_set)
        json_object_set_string(body, "name", opts->name);
    if (opts->desc_set)
        json_object_set_string(body, "description", opts->description);
    if (opts->website_set)
        json_object_set_string(body, "website", opts->website);
    if (opts->private_set)
        json_object_set_bool(body, "private", opts->private_val);
    if (opts->default_branch_set)
        json_object_set_string(body, "default_branch", opts->default_branch);
    if (opts->archived_set)
        json_object_set_bool(body, "archived", opts->archived_val);
    if (opts->template_set)
        json_object_set_bool(body, "template", opts->template_val);
    if (opts->has_issues_set)
        json_object_set_bool(body, "has_issues", opts->has_issues_val);
    if (opts->has_wiki_set)
        json_object_set_bool(body, "has_wiki", opts->has_wiki_val);
    if (opts->has_prs_set)
        json_object_set_bool(body, "has_pull_requests", opts->has_prs_val);
    if (opts->has_projects_set)
        json_object_set_bool(body, "has_projects", opts->has_projects_val);
    if (opts->has_releases_set)
        json_object_set_bool(body, "has_releases", opts->has_releases_val);
    if (opts->has_packages_set)
        json_object_set_bool(body, "has_packages", opts->has_packages_val);
    if (opts->has_actions_set)
        json_object_set_bool(body, "has_actions", opts->has_actions_val);
    if (opts->allow_merge_set)
        json_object_set_bool(body, "allow_merge_commits", opts->allow_merge_val);
    if (opts->allow_rebase_set)
        json_object_set_bool(body, "allow_rebase", opts->allow_rebase_val);
    if (opts->allow_squash_set)
        json_object_set_bool(body, "allow_squash_merge", opts->allow_squash_val);
    if (opts->allow_ff_only_set)
        json_object_set_bool(body, "allow_fast_forward_only_merge", opts->allow_ff_only_val);
    if (opts->default_merge_style_set)
        json_object_set_string(body, "default_merge_style", opts->default_merge_style);
    if (opts->delete_branch_after_merge_set)
        json_object_set_bool(body, "default_delete_branch_after_merge", opts->delete_branch_after_merge_val);
    if (opts->allow_maintainer_edit_set)
        json_object_set_bool(body, "default_allow_maintainer_edit", opts->allow_maintainer_edit_val);

    char *body_str = json_serialize(body, true); /* omit_null = true — skip any null values */
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_PATCH, path, body_str, &resp);
    free(path);
    free(body_str);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    if (out) {
        const char *json_err = NULL;
        JsonValue *parsed = json_parse(resp.body, &json_err);
        if (parsed && json_is_object(parsed))
            parse_repo(parsed, out);
        json_free(parsed);
    }

    http_response_free(&resp);
    return API_OK;
}

int api_repo_show(ApiClient *a, const char *owner, const char *repo, Repo *out)
{
    char *path = build_path(a, "/repos/%s/%s", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    const char *json_err = NULL;
    JsonValue *parsed = json_parse(resp.body, &json_err);
    http_response_free(&resp);

    if (!parsed || !json_is_object(parsed)) {
        json_free(parsed);
        set_error(a, "failed to parse API response");
        return API_ERR_UNKNOWN;
    }

    parse_repo(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_repo_list(ApiClient *a, const char *owner, int is_org,
                  Repo **out, size_t *count)
{
    char *path;
    if (!owner || owner[0] == '\0')
        path = build_path(a, "/user/repos");
    else if (is_org)
        path = build_path(a, "/orgs/%s/repos", owner);
    else
        path = build_path(a, "/users/%s/repos", owner);

    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    const char *json_err = NULL;
    JsonValue *parsed = json_parse(resp.body, &json_err);
    http_response_free(&resp);

    if (!parsed || !json_is_array(parsed)) {
        json_free(parsed);
        set_error(a, "failed to parse API response");
        return API_ERR_UNKNOWN;
    }

    size_t n = json_array_count(parsed);
    Repo *arr = NULL;
    if (n > 0) {
        arr = calloc(n, sizeof(Repo));
        if (!arr) {
            json_free(parsed);
            set_error(a, "out of memory");
            return API_ERR_UNKNOWN;
        }
    }

    for (size_t i = 0; i < n; i++)
        parse_repo(json_array_get(parsed, i), &arr[i]);

    *out = arr;
    *count = n;
    json_free(parsed);
    return API_OK;
}

int api_repo_transfer(ApiClient *a, const char *owner, const char *repo,
                      const char *new_owner, const int64_t *team_ids, size_t team_count)
{
    if (!new_owner) {
        set_error(a, "new owner is required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "new_owner", new_owner);
    if (team_ids && team_count > 0) {
        JsonValue *teams = json_array_new();
        for (size_t i = 0; i < team_count; i++)
            json_array_push(teams, json_number_new((double)team_ids[i]));
        json_object_set(body, "team_ids", teams);
    }

    char *body_str = json_serialize(body, false);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/transfer", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);
    http_response_free(&resp);
    return err;
}

/* ===== Topic operations ===== */

int api_topic_list(ApiClient *a, const char *owner, const char *repo,
                   char ***topics, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/topics", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    const char *json_err = NULL;
    JsonValue *parsed = json_parse(resp.body, &json_err);
    http_response_free(&resp);

    if (!parsed || !json_is_object(parsed)) {
        json_free(parsed);
        set_error(a, "failed to parse API response");
        return API_ERR_UNKNOWN;
    }

    JsonValue *topics_arr = json_object_lookup(parsed, "topics");
    if (!topics_arr || !json_is_array(topics_arr)) {
        json_free(parsed);
        *topics = NULL;
        *count = 0;
        return API_OK;
    }

    size_t n = json_array_count(topics_arr);
    char **arr = calloc(n, sizeof(char *));
    if (!arr && n > 0) {
        json_free(parsed);
        set_error(a, "out of memory");
        return API_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < n; i++) {
        JsonValue *t = json_array_get(topics_arr, i);
        arr[i] = strdup(json_is_string(t) ? json_string(t) : "");
    }

    *topics = arr;
    *count = n;
    json_free(parsed);
    return API_OK;
}

int api_topic_set(ApiClient *a, const char *owner, const char *repo,
                  const char **topics, size_t count)
{
    JsonValue *body = json_object_new();
    JsonValue *arr = json_array_new();
    for (size_t i = 0; i < count; i++)
        json_array_push(arr, json_string_new(topics[i]));
    json_object_set(body, "topics", arr);

    char *body_str = json_serialize(body, false);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/topics", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_PUT, path, body_str, &resp);
    free(path);
    free(body_str);
    http_response_free(&resp);
    return err;
}

int api_topic_add(ApiClient *a, const char *owner, const char *repo, const char *topic)
{
    char *path = build_path(a, "/repos/%s/%s/topics/%s", owner, repo, topic);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_PUT, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

int api_topic_remove(ApiClient *a, const char *owner, const char *repo, const char *topic)
{
    char *path = build_path(a, "/repos/%s/%s/topics/%s", owner, repo, topic);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

/* ===== Actions (CI/CD) ===== */

static int64_t json_get_int64(const JsonValue *obj, const char *key, int64_t default_val)
{
    JsonValue *v = json_object_lookup(obj, key);
    if (!v || !json_is_number(v))
        return default_val;
    return (int64_t)json_number(v);
}

static void parse_action_run(const JsonValue *obj, ActionRun *r)
{
    memset(r, 0, sizeof(*r));
    r->id = json_get_int64(obj, "id", 0);
    r->index_in_repo = json_get_int64(obj, "index_in_repo", 0);
    r->title = json_dup_string(obj, "title");
    r->status = json_dup_string(obj, "status");
    r->event = json_dup_string(obj, "event");
    r->workflow_id = json_dup_string(obj, "workflow_id");
    r->prettyref = json_dup_string(obj, "prettyref");
    r->commit_sha = json_dup_string(obj, "commit_sha");
    r->html_url = json_dup_string(obj, "html_url");
    r->created = json_dup_string(obj, "created");
    r->started = json_dup_string(obj, "started");
    r->stopped = json_dup_string(obj, "stopped");
}

void action_run_free(ActionRun *r)
{
    if (!r)
        return;
    free(r->title);
    free(r->status);
    free(r->event);
    free(r->workflow_id);
    free(r->prettyref);
    free(r->commit_sha);
    free(r->html_url);
    free(r->created);
    free(r->started);
    free(r->stopped);
    memset(r, 0, sizeof(*r));
}

void action_run_array_free(ActionRun *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        action_run_free(&arr[i]);
    free(arr);
}

static void parse_action_runner(const JsonValue *obj, ActionRunner *r)
{
    memset(r, 0, sizeof(*r));
    r->id = json_get_int64(obj, "id", 0);
    r->name = json_dup_string(obj, "name");
    r->uuid = json_dup_string(obj, "uuid");
    r->status = json_dup_string(obj, "status");
    r->version = json_dup_string(obj, "version");
}

void action_runner_free(ActionRunner *r)
{
    if (!r)
        return;
    free(r->name);
    free(r->uuid);
    free(r->status);
    free(r->version);
    memset(r, 0, sizeof(*r));
}

void action_runner_array_free(ActionRunner *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        action_runner_free(&arr[i]);
    free(arr);
}

static void parse_action_variable(const JsonValue *obj, ActionVariable *v)
{
    memset(v, 0, sizeof(*v));
    v->name = json_dup_string(obj, "name");
    v->data = json_dup_string(obj, "data");
}

void action_variable_free(ActionVariable *v)
{
    if (!v)
        return;
    free(v->name);
    free(v->data);
    memset(v, 0, sizeof(*v));
}

void action_variable_array_free(ActionVariable *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        action_variable_free(&arr[i]);
    free(arr);
}

static void parse_action_secret(const JsonValue *obj, ActionSecret *s)
{
    memset(s, 0, sizeof(*s));
    s->name = json_dup_string(obj, "name");
}

void action_secret_free(ActionSecret *s)
{
    if (!s)
        return;
    free(s->name);
    memset(s, 0, sizeof(*s));
}

void action_secret_array_free(ActionSecret *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        action_secret_free(&arr[i]);
    free(arr);
}

int api_action_run_list(ApiClient *a, const char *owner, const char *repo,
                        ActionRun **out, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/actions/runs", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    const char *json_err = NULL;
    JsonValue *parsed = json_parse(resp.body, &json_err);
    http_response_free(&resp);

    if (!parsed || !json_is_object(parsed)) {
        json_free(parsed);
        set_error(a, "failed to parse API response");
        return API_ERR_UNKNOWN;
    }

    JsonValue *runs_arr = json_object_lookup(parsed, "workflow_runs");
    if (!runs_arr || !json_is_array(runs_arr)) {
        json_free(parsed);
        *out = NULL;
        *count = 0;
        return API_OK;
    }

    size_t n = json_array_count(runs_arr);
    ActionRun *arr = NULL;
    if (n > 0) {
        arr = calloc(n, sizeof(ActionRun));
        if (!arr) {
            json_free(parsed);
            set_error(a, "out of memory");
            return API_ERR_UNKNOWN;
        }
    }

    for (size_t i = 0; i < n; i++)
        parse_action_run(json_array_get(runs_arr, i), &arr[i]);

    *out = arr;
    *count = n;
    json_free(parsed);
    return API_OK;
}

int api_action_run_show(ApiClient *a, const char *owner, const char *repo,
                        int64_t run_id, ActionRun *out)
{
    char *path = build_path(a, "/repos/%s/%s/actions/runs/%lld",
                            owner, repo, (long long)run_id);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    const char *json_err = NULL;
    JsonValue *parsed = json_parse(resp.body, &json_err);
    http_response_free(&resp);

    if (!parsed || !json_is_object(parsed)) {
        json_free(parsed);
        set_error(a, "failed to parse API response");
        return API_ERR_UNKNOWN;
    }

    parse_action_run(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_action_runner_list(ApiClient *a, const char *owner, const char *repo,
                           ActionRunner **out, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/actions/runners", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    const char *json_err = NULL;
    JsonValue *parsed = json_parse(resp.body, &json_err);
    http_response_free(&resp);

    if (!parsed || !json_is_array(parsed)) {
        json_free(parsed);
        set_error(a, "failed to parse API response");
        return API_ERR_UNKNOWN;
    }

    size_t n = json_array_count(parsed);
    ActionRunner *arr = NULL;
    if (n > 0) {
        arr = calloc(n, sizeof(ActionRunner));
        if (!arr) {
            json_free(parsed);
            set_error(a, "out of memory");
            return API_ERR_UNKNOWN;
        }
    }

    for (size_t i = 0; i < n; i++)
        parse_action_runner(json_array_get(parsed, i), &arr[i]);

    *out = arr;
    *count = n;
    json_free(parsed);
    return API_OK;
}

int api_action_dispatch(ApiClient *a, const char *owner, const char *repo,
                        const char *workflowfile, const char *ref)
{
    if (!workflowfile) {
        set_error(a, "workflow filename is required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "ref", ref ? ref : "master");

    char *body_str = json_serialize(body, false);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/actions/workflows/%s/dispatches",
                            owner, repo, workflowfile);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);
    http_response_free(&resp);
    return err;
}

int api_action_secret_list(ApiClient *a, const char *owner, const char *repo,
                           ActionSecret **out, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/actions/secrets", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    const char *json_err = NULL;
    JsonValue *parsed = json_parse(resp.body, &json_err);
    http_response_free(&resp);

    if (!parsed || !json_is_object(parsed)) {
        json_free(parsed);
        set_error(a, "failed to parse API response");
        return API_ERR_UNKNOWN;
    }

    JsonValue *secrets_arr = json_object_lookup(parsed, "data");
    if (!secrets_arr || !json_is_array(secrets_arr)) {
        json_free(parsed);
        *out = NULL;
        *count = 0;
        return API_OK;
    }

    size_t n = json_array_count(secrets_arr);
    ActionSecret *arr = NULL;
    if (n > 0) {
        arr = calloc(n, sizeof(ActionSecret));
        if (!arr) {
            json_free(parsed);
            set_error(a, "out of memory");
            return API_ERR_UNKNOWN;
        }
    }

    for (size_t i = 0; i < n; i++)
        parse_action_secret(json_array_get(secrets_arr, i), &arr[i]);

    *out = arr;
    *count = n;
    json_free(parsed);
    return API_OK;
}

int api_action_secret_set(ApiClient *a, const char *owner, const char *repo,
                          const char *name, const char *value)
{
    if (!name || !value) {
        set_error(a, "secret name and value are required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "data", value);

    char *body_str = json_serialize(body, false);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/actions/secrets/%s", owner, repo, name);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_PUT, path, body_str, &resp);
    free(path);
    free(body_str);
    http_response_free(&resp);
    return err;
}

int api_action_secret_delete(ApiClient *a, const char *owner, const char *repo,
                             const char *name)
{
    char *path = build_path(a, "/repos/%s/%s/actions/secrets/%s", owner, repo, name);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

int api_action_variable_list(ApiClient *a, const char *owner, const char *repo,
                             ActionVariable **out, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/actions/variables", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    const char *json_err = NULL;
    JsonValue *parsed = json_parse(resp.body, &json_err);
    http_response_free(&resp);

    if (!parsed || !json_is_array(parsed)) {
        json_free(parsed);
        set_error(a, "failed to parse API response");
        return API_ERR_UNKNOWN;
    }

    size_t n = json_array_count(parsed);
    ActionVariable *arr = NULL;
    if (n > 0) {
        arr = calloc(n, sizeof(ActionVariable));
        if (!arr) {
            json_free(parsed);
            set_error(a, "out of memory");
            return API_ERR_UNKNOWN;
        }
    }

    for (size_t i = 0; i < n; i++)
        parse_action_variable(json_array_get(parsed, i), &arr[i]);

    *out = arr;
    *count = n;
    json_free(parsed);
    return API_OK;
}

int api_action_variable_show(ApiClient *a, const char *owner, const char *repo,
                             const char *name, ActionVariable *out)
{
    char *path = build_path(a, "/repos/%s/%s/actions/variables/%s", owner, repo, name);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    const char *json_err = NULL;
    JsonValue *parsed = json_parse(resp.body, &json_err);
    http_response_free(&resp);

    if (!parsed || !json_is_object(parsed)) {
        json_free(parsed);
        set_error(a, "failed to parse API response");
        return API_ERR_UNKNOWN;
    }

    parse_action_variable(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_action_variable_set(ApiClient *a, const char *owner, const char *repo,
                            const char *name, const char *value)
{
    if (!name || !value) {
        set_error(a, "variable name and value are required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "value", value);

    char *body_str = json_serialize(body, false);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/actions/variables/%s", owner, repo, name);
    HttpResponse resp;

    /* Try PUT first (update), fall back to POST (create) if 404 */
    ApiError err = do_request(a, HTTP_PUT, path, body_str, &resp);
    if (err == API_ERR_NOT_FOUND) {
        http_response_free(&resp);
        err = do_request(a, HTTP_POST, path, body_str, &resp);
    }

    free(path);
    free(body_str);
    http_response_free(&resp);
    return err;
}

int api_action_variable_delete(ApiClient *a, const char *owner, const char *repo,
                               const char *name)
{
    char *path = build_path(a, "/repos/%s/%s/actions/variables/%s", owner, repo, name);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

/* ===== Actions jobs & logs (web UI endpoint, not /api/v1) ===== */

static void parse_action_job(const JsonValue *obj, ActionJob *j)
{
    memset(j, 0, sizeof(*j));
    j->id = json_get_int64(obj, "id", 0);
    j->name = json_dup_string(obj, "name");
    j->status = json_dup_string(obj, "status");
    j->duration = json_dup_string(obj, "duration");
}

void action_job_free(ActionJob *j)
{
    if (!j)
        return;
    free(j->name);
    free(j->status);
    free(j->duration);
    memset(j, 0, sizeof(*j));
}

void action_job_array_free(ActionJob *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        action_job_free(&arr[i]);
    free(arr);
}

static void parse_action_step(const JsonValue *obj, ActionStep *s)
{
    memset(s, 0, sizeof(*s));
    s->summary = json_dup_string(obj, "summary");
    s->status = json_dup_string(obj, "status");
    s->duration = json_dup_string(obj, "duration");
}

void action_job_detail_free(ActionJobDetail *d)
{
    if (!d)
        return;
    action_job_free(&d->job);
    for (size_t i = 0; i < d->step_count; i++) {
        free(d->steps[i].summary);
        free(d->steps[i].status);
        free(d->steps[i].duration);
    }
    free(d->steps);
    memset(d, 0, sizeof(*d));
}

void action_log_lines_free(ActionLogLine *lines, size_t count)
{
    if (!lines)
        return;
    for (size_t i = 0; i < count; i++)
        free(lines[i].message);
    free(lines);
}

/* POST to the web UI log endpoint.
 * Returns the parsed JSON response (caller must json_free).
 * body is the JSON string to POST. */
static ApiError post_log_endpoint(ApiClient *a, const char *owner, const char *repo,
                                  int run_id, int job_index, int attempt,
                                  const char *body, JsonValue **out)
{
    /* Path is NOT under /api/v1 — construct directly */
    char path[512];
    snprintf(path, sizeof(path), "/%s/%s/actions/runs/%d/jobs/%d/attempt/%d",
             owner, repo, run_id, job_index, attempt);

    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body, &resp);
    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    const char *json_err = NULL;
    JsonValue *parsed = json_parse(resp.body, &json_err);
    http_response_free(&resp);

    if (!parsed || !json_is_object(parsed)) {
        json_free(parsed);
        set_error(a, "failed to parse log response");
        return API_ERR_UNKNOWN;
    }

    *out = parsed;
    return API_OK;
}

/* Build a logCursors JSON body with all steps expanded (or just one). */
static char *build_log_cursors(int step_count, int expand_step)
{
    JsonValue *cursors = json_array_new();
    for (int i = 0; i < step_count; i++) {
        JsonValue *entry = json_object_new();
        json_object_set(entry, "step", json_number_new((double)i));
        json_object_set(entry, "cursor", json_null_new());
        json_object_set_bool(entry, "expanded", (expand_step < 0 || expand_step == i));
        json_array_push(cursors, entry);
    }
    JsonValue *root = json_object_new();
    json_object_set(root, "logCursors", cursors);
    char *s = json_serialize(root, false);
    json_free(root);
    return s;
}

int api_action_job_list(ApiClient *a, const char *owner, const char *repo,
                        int run_id, ActionJob **out, size_t *count)
{
    /* First fetch job 0 with empty cursors to get run state (includes job list) */
    char *body = build_log_cursors(1, -1);
    JsonValue *parsed = NULL;
    ApiError err = post_log_endpoint(a, owner, repo, run_id, 0, 1, body, &parsed);
    free(body);
    if (err != API_OK)
        return err;

    /* Navigate: state -> run -> jobs */
    JsonValue *state = json_object_lookup(parsed, "state");
    if (!state || !json_is_object(state)) {
        json_free(parsed);
        set_error(a, "missing state in log response");
        return API_ERR_UNKNOWN;
    }
    JsonValue *run = json_object_lookup(state, "run");
    if (!run || !json_is_object(run)) {
        json_free(parsed);
        set_error(a, "missing run in log response");
        return API_ERR_UNKNOWN;
    }
    JsonValue *jobs_arr = json_object_lookup(run, "jobs");
    if (!jobs_arr || !json_is_array(jobs_arr)) {
        json_free(parsed);
        *out = NULL;
        *count = 0;
        return API_OK;
    }

    size_t n = json_array_count(jobs_arr);
    ActionJob *arr = NULL;
    if (n > 0) {
        arr = calloc(n, sizeof(ActionJob));
        if (!arr) {
            json_free(parsed);
            set_error(a, "out of memory");
            return API_ERR_UNKNOWN;
        }
    }
    for (size_t i = 0; i < n; i++)
        parse_action_job(json_array_get(jobs_arr, i), &arr[i]);

    *out = arr;
    *count = n;
    json_free(parsed);
    return API_OK;
}

int api_action_job_detail(ApiClient *a, const char *owner, const char *repo,
                          int run_id, int job_index, ActionJobDetail *out)
{
    memset(out, 0, sizeof(*out));

    /* POST with empty cursors to discover steps */
    char *body = build_log_cursors(1, -1);
    JsonValue *parsed = NULL;
    ApiError err = post_log_endpoint(a, owner, repo, run_id, job_index, 1, body, &parsed);
    free(body);
    if (err != API_OK)
        return err;

    JsonValue *state = json_object_lookup(parsed, "state");
    if (!state) {
        json_free(parsed);
        set_error(a, "missing state in log response");
        return API_ERR_UNKNOWN;
    }

    /* Extract job info from run.jobs[job_index] */
    JsonValue *run = json_object_lookup(state, "run");
    if (run) {
        JsonValue *jobs = json_object_lookup(run, "jobs");
        if (jobs && json_is_array(jobs) && (size_t)job_index < json_array_count(jobs))
            parse_action_job(json_array_get(jobs, job_index), &out->job);
    }

    /* Extract steps from currentJob.steps */
    JsonValue *current_job = json_object_lookup(state, "currentJob");
    if (current_job) {
        JsonValue *steps = json_object_lookup(current_job, "steps");
        if (steps && json_is_array(steps)) {
            size_t n = json_array_count(steps);
            if (n > 0) {
                out->steps = calloc(n, sizeof(ActionStep));
                if (!out->steps) {
                    json_free(parsed);
                    set_error(a, "out of memory");
                    return API_ERR_UNKNOWN;
                }
                for (size_t i = 0; i < n; i++)
                    parse_action_step(json_array_get(steps, i), &out->steps[i]);
                out->step_count = n;
            }
        }
    }

    json_free(parsed);
    return API_OK;
}

int api_action_log_fetch(ApiClient *a, const char *owner, const char *repo,
                         int run_id, int job_index, int step_index,
                         ActionLogLine **out, size_t *count)
{
    *out = NULL;
    *count = 0;

    /* First, discover step count by fetching job detail */
    ActionJobDetail detail;
    ApiError err = api_action_job_detail(a, owner, repo, run_id, job_index, &detail);
    if (err != API_OK)
        return err;

    int step_count = (int)detail.step_count;
    action_job_detail_free(&detail);

    if (step_index >= step_count) {
        set_error(a, "step index %d out of range (job has %d steps)", step_index, step_count);
        return API_ERR_VALIDATION;
    }

    /* Now fetch with the specific step expanded */
    ActionLogLine *all_lines = NULL;
    size_t total_lines = 0;
    int cursor = -1; /* -1 means null (first page) */

    for (;;) {
        /* Build logCursors body for this step with current cursor */
        JsonValue *cursors = json_array_new();
        JsonValue *entry = json_object_new();
        json_object_set(entry, "step", json_number_new((double)step_index));
        if (cursor < 0)
            json_object_set(entry, "cursor", json_null_new());
        else
            json_object_set(entry, "cursor", json_number_new((double)cursor));
        json_object_set_bool(entry, "expanded", true);
        json_array_push(cursors, entry);

        JsonValue *root = json_object_new();
        json_object_set(root, "logCursors", cursors);
        char *body = json_serialize(root, false);
        json_free(root);

        JsonValue *parsed = NULL;
        err = post_log_endpoint(a, owner, repo, run_id, job_index, 1, body, &parsed);
        free(body);
        if (err != API_OK)
            return err;

        /* Navigate: logs -> stepsLog[0] -> lines */
        JsonValue *logs = json_object_lookup(parsed, "logs");
        if (!logs || !json_is_object(logs)) {
            json_free(parsed);
            break;
        }
        JsonValue *steps_log = json_object_lookup(logs, "stepsLog");
        if (!steps_log || !json_is_array(steps_log) || json_array_count(steps_log) == 0) {
            json_free(parsed);
            break;
        }

        JsonValue *sl = json_array_get(steps_log, 0);
        JsonValue *lines_arr = json_object_lookup(sl, "lines");
        JsonValue *cursor_val = json_object_lookup(sl, "cursor");

        if (lines_arr && json_is_array(lines_arr)) {
            size_t n = json_array_count(lines_arr);
            if (n > 0) {
                ActionLogLine *new_lines = realloc(all_lines,
                                                   (total_lines + n) * sizeof(ActionLogLine));
                if (!new_lines) {
                    free(all_lines);
                    json_free(parsed);
                    set_error(a, "out of memory");
                    return API_ERR_UNKNOWN;
                }
                all_lines = new_lines;
                for (size_t i = 0; i < n; i++) {
                    JsonValue *line = json_array_get(lines_arr, i);
                    JsonValue *msg = json_object_lookup(line, "message");
                    all_lines[total_lines + i].index = (int)json_get_int64(line, "index", 0);
                    all_lines[total_lines + i].message =
                        (msg && json_is_string(msg)) ? strdup(json_string(msg)) : strdup("");
                }
                total_lines += n;
            }
        }

        /* Check for pagination cursor */
        int new_cursor = -1;
        if (cursor_val && json_is_number(cursor_val))
            new_cursor = (int)json_number(cursor_val);

        json_free(parsed);

        if (new_cursor <= 0 || new_cursor == cursor)
            break;
        cursor = new_cursor;
    }

    *out = all_lines;
    *count = total_lines;
    return API_OK;
}
