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

static int64_t json_get_int64(const JsonValue *obj, const char *key, int64_t default_val)
{
    JsonValue *v = json_object_lookup(obj, key);
    if (!v || !json_is_number(v))
        return default_val;
    return (int64_t)json_number(v);
}

/* ===== Organizations ===== */

static void parse_org(const JsonValue *obj, Organization *o)
{
    memset(o, 0, sizeof(*o));
    o->name = json_dup_string(obj, "name");
    o->full_name = json_dup_string(obj, "full_name");
    o->description = json_dup_string(obj, "description");
    o->email = json_dup_string(obj, "email");
    o->location = json_dup_string(obj, "location");
    o->website = json_dup_string(obj, "website");
    o->visibility = json_dup_string(obj, "visibility");
    o->avatar_url = json_dup_string(obj, "avatar_url");
    o->id = json_get_int64(obj, "id", 0);
    o->repo_admin_change_team_access = json_get_bool(obj, "repo_admin_change_team_access", 0);
}

void org_free(Organization *o)
{
    if (!o)
        return;
    free(o->name);
    free(o->full_name);
    free(o->description);
    free(o->email);
    free(o->location);
    free(o->website);
    free(o->visibility);
    free(o->avatar_url);
    memset(o, 0, sizeof(*o));
}

int api_org_create(ApiClient *a, const CreateOrgOpts *opts, Organization *out)
{
    if (!opts || !opts->username) {
        set_error(a, "organization username is required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "username", opts->username);
    if (opts->full_name)
        json_object_set_string(body, "full_name", opts->full_name);
    if (opts->description)
        json_object_set_string(body, "description", opts->description);
    if (opts->email)
        json_object_set_string(body, "email", opts->email);
    if (opts->location)
        json_object_set_string(body, "location", opts->location);
    if (opts->website)
        json_object_set_string(body, "website", opts->website);
    if (opts->visibility)
        json_object_set_string(body, "visibility", opts->visibility);
    if (opts->repo_admin_change_team_access)
        json_object_set_bool(body, "repo_admin_change_team_access", true);

    char *body_str = json_serialize(body, false);
    json_free(body);

    char *path = build_path(a, "/orgs");
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);

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

    parse_org(parsed, out);
    json_free(parsed);
    return API_OK;
}

/* ===== Actions (CI/CD) ===== */

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
                        int64_t run_number, ActionRun *out)
{
    char path[512];
    snprintf(path, sizeof(path), "/repos/%s/%s/actions/runs?run_number=%lld",
             owner, repo, (long long)run_number);
    size_t total = strlen(a->path_prefix) + strlen(path) + 1;
    char *full = malloc(total);
    if (!full) {
        set_error(a, "out of memory");
        return API_ERR_UNKNOWN;
    }
    snprintf(full, total, "%s%s", a->path_prefix, path);

    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, full, NULL, &resp);
    free(full);

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
    if (!runs_arr || !json_is_array(runs_arr) || json_array_count(runs_arr) == 0) {
        json_free(parsed);
        set_error(a, "run #%lld not found", (long long)run_number);
        return API_ERR_NOT_FOUND;
    }

    parse_action_run(json_array_get(runs_arr, 0), out);
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
                                  int64_t run_id, int job_index, int attempt,
                                  const char *body, JsonValue **out)
{
    /* Path is NOT under /api/v1 — construct directly */
    char path[512];
    snprintf(path, sizeof(path), "/%s/%s/actions/runs/%lld/jobs/%d/attempt/%d",
             owner, repo, (long long)run_id, job_index, attempt);

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
                        int64_t run_id, ActionJob **out, size_t *count)
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
                          int64_t run_id, int job_index, ActionJobDetail *out)
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
                         int64_t run_id, int job_index, int step_index,
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
/* New API endpoints — appended to cb_api.c */
/* This file is concatenated during build. See Makefile.am. */

/* ===== Query string helper ===== */

typedef struct
{
    const char *key;
    const char *value;
} QueryParam;

static char *build_path_q(ApiClient *a, const char *fmt, const QueryParam *params,
                          size_t param_count, ...)
{
    va_list args;
    va_start(args, param_count);

    char path[1024];
    vsnprintf(path, sizeof(path), fmt, args);
    va_end(args);

    if (param_count > 0) {
        size_t off = strlen(path);
        path[off++] = '?';
        for (size_t i = 0; i < param_count; i++) {
            if (i > 0 && off < sizeof(path) - 1)
                path[off++] = '&';
            size_t room = sizeof(path) - off;
            if (room <= 1)
                break;
            snprintf(path + off, room, "%s=%s", params[i].key, params[i].value);
            off = strlen(path);
        }
    }

    size_t total = strlen(a->path_prefix) + strlen(path) + 1;
    char *full = malloc(total);
    if (!full)
        return NULL;
    snprintf(full, total, "%s%s", a->path_prefix, path);
    return full;
}

/* ===== Parse helpers for new types ===== */

static void parse_release(const JsonValue *obj, Release *r)
{
    memset(r, 0, sizeof(*r));
    r->id = json_get_int64(obj, "id", 0);
    r->tag_name = json_dup_string(obj, "tag_name");
    r->name = json_dup_string(obj, "name");
    r->body = json_dup_string(obj, "body");
    r->target_commitish = json_dup_string(obj, "target_commitish");
    r->html_url = json_dup_string(obj, "html_url");
    r->tarball_url = json_dup_string(obj, "tarball_url");
    r->zipball_url = json_dup_string(obj, "zipball_url");
    r->upload_url = json_dup_string(obj, "upload_url");
    r->url = json_dup_string(obj, "url");
    r->created_at = json_dup_string(obj, "created_at");
    r->published_at = json_dup_string(obj, "published_at");
    r->draft = json_get_bool(obj, "draft", 0);
    r->prerelease = json_get_bool(obj, "prerelease", 0);
    r->hide_archive_links = json_get_bool(obj, "hide_archive_links", 0);
}

static void parse_attachment(const JsonValue *obj, Attachment *a)
{
    memset(a, 0, sizeof(*a));
    a->id = json_get_int64(obj, "id", 0);
    a->name = json_dup_string(obj, "name");
    a->browser_download_url = json_dup_string(obj, "browser_download_url");
    a->uuid = json_dup_string(obj, "uuid");
    a->created_at = json_dup_string(obj, "created_at");
    a->type = json_dup_string(obj, "type");
    a->size = json_get_int64(obj, "size", 0);
    a->download_count = json_get_int64(obj, "download_count", 0);
}

static void parse_tag(const JsonValue *obj, Tag *t)
{
    memset(t, 0, sizeof(*t));
    t->name = json_dup_string(obj, "name");
    t->id = json_dup_string(obj, "id");
    t->message = json_dup_string(obj, "message");
    t->tarball_url = json_dup_string(obj, "tarball_url");
    t->zipball_url = json_dup_string(obj, "zipball_url");
}

static void parse_branch(const JsonValue *obj, Branch *b)
{
    memset(b, 0, sizeof(*b));
    b->name = json_dup_string(obj, "name");
    b->protected = json_get_bool(obj, "protected", 0);
    b->effective_branch_protection_name = json_dup_string(obj, "effective_branch_protection_name");
    b->user_can_merge = json_get_bool(obj, "user_can_merge", 0);
    b->user_can_push = json_get_bool(obj, "user_can_push", 0);
    JsonValue *commit = json_object_lookup(obj, "commit");
    if (commit && json_is_object(commit)) {
        b->commit_sha = json_dup_string(commit, "id");
        JsonValue *cobj = json_object_lookup(commit, "commit");
        if (cobj && json_is_object(cobj))
            b->commit_message = json_dup_string(cobj, "message");
    }
}

static void parse_issue(const JsonValue *obj, Issue *i)
{
    memset(i, 0, sizeof(*i));
    i->id = json_get_int64(obj, "id", 0);
    i->number = (int)json_get_int64(obj, "number", 0);
    i->title = json_dup_string(obj, "title");
    i->body = json_dup_string(obj, "body");
    i->state = json_dup_string(obj, "state");
    i->html_url = json_dup_string(obj, "html_url");
    i->created_at = json_dup_string(obj, "created_at");
    i->updated_at = json_dup_string(obj, "updated_at");
    i->closed_at = json_dup_string(obj, "closed_at");
    i->due_date = json_dup_string(obj, "due_date");
    i->is_locked = json_get_bool(obj, "is_locked", 0);
    i->comments = json_get_int(obj, "comments", 0);
    i->pin_order = json_get_int(obj, "pin_order", 0);
}

static void parse_label(const JsonValue *obj, Label *l)
{
    memset(l, 0, sizeof(*l));
    l->id = json_get_int64(obj, "id", 0);
    l->name = json_dup_string(obj, "name");
    l->color = json_dup_string(obj, "color");
    l->description = json_dup_string(obj, "description");
    l->exclusive = json_get_bool(obj, "exclusive", 0);
    l->is_archived = json_get_bool(obj, "is_archived", 0);
}

static void parse_milestone(const JsonValue *obj, Milestone *m)
{
    memset(m, 0, sizeof(*m));
    m->id = json_get_int64(obj, "id", 0);
    m->title = json_dup_string(obj, "title");
    m->description = json_dup_string(obj, "description");
    m->state = json_dup_string(obj, "state");
    m->due_on = json_dup_string(obj, "due_on");
    m->created_at = json_dup_string(obj, "created_at");
    m->updated_at = json_dup_string(obj, "updated_at");
    m->open_issues = json_get_int(obj, "open_issues", 0);
    m->closed_issues = json_get_int(obj, "closed_issues", 0);
}

static void parse_pullrequest(const JsonValue *obj, PullRequest *p)
{
    memset(p, 0, sizeof(*p));
    p->id = json_get_int64(obj, "id", 0);
    p->number = (int)json_get_int64(obj, "number", 0);
    p->title = json_dup_string(obj, "title");
    p->body = json_dup_string(obj, "body");
    p->state = json_dup_string(obj, "state");
    p->draft = json_get_bool(obj, "draft", 0);
    p->merged = json_get_bool(obj, "merged", 0);
    p->mergeable = json_get_bool(obj, "mergeable", 0);
    p->merged_at = json_dup_string(obj, "merged_at");
    p->closed_at = json_dup_string(obj, "closed_at");
    p->created_at = json_dup_string(obj, "created_at");
    p->updated_at = json_dup_string(obj, "updated_at");
    p->html_url = json_dup_string(obj, "html_url");
    p->diff_url = json_dup_string(obj, "diff_url");
    p->patch_url = json_dup_string(obj, "patch_url");
    p->merge_commit_sha = json_dup_string(obj, "merge_commit_sha");
    p->additions = json_get_int(obj, "additions", 0);
    p->deletions = json_get_int(obj, "deletions", 0);
    p->changed_files = json_get_int(obj, "changed_files", 0);
    p->comments = json_get_int(obj, "comments", 0);
    JsonValue *base = json_object_lookup(obj, "base");
    if (base && json_is_object(base))
        p->base_ref = json_dup_string(base, "ref");
    JsonValue *head = json_object_lookup(obj, "head");
    if (head && json_is_object(head))
        p->head_ref = json_dup_string(head, "ref");
}

static void parse_commit(const JsonValue *obj, Commit *c)
{
    memset(c, 0, sizeof(*c));
    c->sha = json_dup_string(obj, "sha");
    c->created = json_dup_string(obj, "created");
    c->html_url = json_dup_string(obj, "html_url");
    JsonValue *commit_obj = json_object_lookup(obj, "commit");
    if (commit_obj && json_is_object(commit_obj)) {
        c->message = json_dup_string(commit_obj, "message");
        JsonValue *author = json_object_lookup(commit_obj, "author");
        if (author && json_is_object(author)) {
            c->author_name = json_dup_string(author, "name");
            c->author_email = json_dup_string(author, "email");
        }
        JsonValue *committer = json_object_lookup(commit_obj, "committer");
        if (committer && json_is_object(committer)) {
            c->committer_name = json_dup_string(committer, "name");
            c->committer_email = json_dup_string(committer, "email");
        }
    }
    JsonValue *stats = json_object_lookup(obj, "stats");
    if (stats && json_is_object(stats)) {
        c->additions = json_get_int(stats, "additions", 0);
        c->deletions = json_get_int(stats, "deletions", 0);
        c->total = json_get_int(stats, "total", 0);
    }
}

static void parse_commitstatus(const JsonValue *obj, CommitStatus *s)
{
    memset(s, 0, sizeof(*s));
    s->status = json_dup_string(obj, "status");
    s->context = json_dup_string(obj, "context");
    s->description = json_dup_string(obj, "description");
    s->target_url = json_dup_string(obj, "target_url");
    s->created_at = json_dup_string(obj, "created_at");
    s->updated_at = json_dup_string(obj, "updated_at");
}

static void parse_content_entry(const JsonValue *obj, ContentEntry *e)
{
    memset(e, 0, sizeof(*e));
    e->type = json_dup_string(obj, "type");
    e->name = json_dup_string(obj, "name");
    e->path = json_dup_string(obj, "path");
    e->sha = json_dup_string(obj, "sha");
    e->size = json_get_int64(obj, "size", 0);
    e->encoding = json_dup_string(obj, "encoding");
    e->content = json_dup_string(obj, "content");
    e->download_url = json_dup_string(obj, "download_url");
    e->html_url = json_dup_string(obj, "html_url");
    e->git_url = json_dup_string(obj, "git_url");
    e->last_commit_sha = json_dup_string(obj, "last_commit_sha");
}

static void parse_deploykey(const JsonValue *obj, DeployKey *k)
{
    memset(k, 0, sizeof(*k));
    k->id = json_get_int64(obj, "id", 0);
    k->title = json_dup_string(obj, "title");
    k->key = json_dup_string(obj, "key");
    k->fingerprint = json_dup_string(obj, "fingerprint");
    k->read_only = json_get_bool(obj, "read_only", 0);
    k->created_at = json_dup_string(obj, "created_at");
}

static void parse_user(const JsonValue *obj, User *u)
{
    memset(u, 0, sizeof(*u));
    u->id = json_get_int64(obj, "id", 0);
    u->login = json_dup_string(obj, "login");
    u->full_name = json_dup_string(obj, "full_name");
    u->email = json_dup_string(obj, "email");
    u->html_url = json_dup_string(obj, "html_url");
}

static void parse_hook(const JsonValue *obj, Hook *h)
{
    memset(h, 0, sizeof(*h));
    h->id = json_get_int64(obj, "id", 0);
    h->type = json_dup_string(obj, "type");
    h->active = json_get_bool(obj, "active", 0);
    h->branch_filter = json_dup_string(obj, "branch_filter");
    h->authorization_header = json_dup_string(obj, "authorization_header");
    JsonValue *config = json_object_lookup(obj, "config");
    if (config && json_is_object(config)) {
        h->url = json_dup_string(config, "url");
        h->content_type = json_dup_string(config, "content_type");
    }
}

static void parse_wikipage(const JsonValue *obj, WikiPage *w)
{
    memset(w, 0, sizeof(*w));
    w->title = json_dup_string(obj, "title");
    w->content_base64 = json_dup_string(obj, "content_base64");
    w->html_url = json_dup_string(obj, "html_url");
    w->sub_url = json_dup_string(obj, "sub_url");
    w->commit_count = json_get_int(obj, "commit_count", 0);
    JsonValue *lc = json_object_lookup(obj, "last_commit");
    if (lc && json_is_object(lc))
        w->last_commit_sha = json_dup_string(lc, "sha");
}

/* ===== Generic array parse helper ===== */

static ApiError parse_array(ApiClient *a, const char *body, size_t elem_size,
                            void (*parse_fn)(const JsonValue *, void *),
                            void **out, size_t *count)
{
    const char *json_err = NULL;
    JsonValue *parsed = json_parse(body, &json_err);

    if (!parsed || !json_is_array(parsed)) {
        json_free(parsed);
        set_error(a, "failed to parse API response");
        return API_ERR_UNKNOWN;
    }

    size_t n = json_array_count(parsed);
    char *arr = NULL;
    if (n > 0) {
        arr = calloc(n, elem_size);
        if (!arr) {
            json_free(parsed);
            set_error(a, "out of memory");
            return API_ERR_UNKNOWN;
        }
    }

    for (size_t i = 0; i < n; i++)
        parse_fn(json_array_get(parsed, i), arr + i * elem_size);

    *out = arr;
    *count = n;
    json_free(parsed);
    return API_OK;
}

/* ===== Releases ===== */

void release_free(Release *r)
{
    if (!r)
        return;
    free(r->tag_name);
    free(r->name);
    free(r->body);
    free(r->target_commitish);
    free(r->html_url);
    free(r->tarball_url);
    free(r->zipball_url);
    free(r->upload_url);
    free(r->url);
    free(r->created_at);
    free(r->published_at);
    memset(r, 0, sizeof(*r));
}

void release_array_free(Release *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        release_free(&arr[i]);
    free(arr);
}

void attachment_free(Attachment *a)
{
    if (!a)
        return;
    free(a->name);
    free(a->browser_download_url);
    free(a->uuid);
    free(a->created_at);
    free(a->type);
    memset(a, 0, sizeof(*a));
}

void attachment_array_free(Attachment *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        attachment_free(&arr[i]);
    free(arr);
}

int api_release_list(ApiClient *a, const char *owner, const char *repo,
                     int draft, int prerelease, const char *q,
                     int limit, Release **out, size_t *count)
{
    QueryParam params[4];
    size_t pc = 0;
    if (draft)
        params[pc++] = (QueryParam){ "draft", "true" };
    if (prerelease)
        params[pc++] = (QueryParam){ "pre-release", "true" };
    if (q && q[0])
        params[pc++] = (QueryParam){ "q", q };
    if (limit > 0) {
        static char lim[16];
        snprintf(lim, sizeof(lim), "%d", limit);
        params[pc++] = (QueryParam){ "limit", lim };
    }

    char *path = build_path_q(a, "/repos/%s/%s/releases", params, pc, owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(Release), (void (*)(const JsonValue *, void *))parse_release,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_release_create(ApiClient *a, const char *owner, const char *repo,
                       const CreateReleaseOpts *opts, Release *out)
{
    if (!opts || !opts->tag_name) {
        set_error(a, "tag_name is required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "tag_name", opts->tag_name);
    if (opts->name)
        json_object_set_string(body, "name", opts->name);
    if (opts->body)
        json_object_set_string(body, "body", opts->body);
    if (opts->target_commitish)
        json_object_set_string(body, "target_commitish", opts->target_commitish);
    if (opts->draft_set)
        json_object_set_bool(body, "draft", opts->draft_val);
    if (opts->prerelease_set)
        json_object_set_bool(body, "prerelease", opts->prerelease_val);
    if (opts->hide_archive_links_set)
        json_object_set_bool(body, "hide_archive_links", opts->hide_archive_links_val);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/releases", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);

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

    parse_release(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_release_get(ApiClient *a, const char *owner, const char *repo,
                    int64_t id, Release *out)
{
    char *path = build_path(a, "/repos/%s/%s/releases/%lld", owner, repo, (long long)id);
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

    parse_release(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_release_get_latest(ApiClient *a, const char *owner, const char *repo,
                           Release *out)
{
    char *path = build_path(a, "/repos/%s/%s/releases/latest", owner, repo);
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

    parse_release(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_release_get_by_tag(ApiClient *a, const char *owner, const char *repo,
                           const char *tag, Release *out)
{
    char *path = build_path(a, "/repos/%s/%s/releases/tags/%s", owner, repo, tag);
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

    parse_release(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_release_edit(ApiClient *a, const char *owner, const char *repo,
                     int64_t id, const EditReleaseOpts *opts, Release *out)
{
    JsonValue *body = json_object_new();
    if (opts->tag_name_set)
        json_object_set_string(body, "tag_name", opts->tag_name);
    if (opts->name_set)
        json_object_set_string(body, "name", opts->name);
    if (opts->body_set)
        json_object_set_string(body, "body", opts->body);
    if (opts->target_commitish_set)
        json_object_set_string(body, "target_commitish", opts->target_commitish);
    if (opts->draft_set)
        json_object_set_bool(body, "draft", opts->draft_val);
    if (opts->prerelease_set)
        json_object_set_bool(body, "prerelease", opts->prerelease_val);
    if (opts->hide_archive_links_set)
        json_object_set_bool(body, "hide_archive_links", opts->hide_archive_links_val);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/releases/%lld", owner, repo, (long long)id);
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
            parse_release(parsed, out);
        json_free(parsed);
    }

    http_response_free(&resp);
    return API_OK;
}

int api_release_delete(ApiClient *a, const char *owner, const char *repo,
                       int64_t id)
{
    char *path = build_path(a, "/repos/%s/%s/releases/%lld", owner, repo, (long long)id);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

int api_release_delete_by_tag(ApiClient *a, const char *owner, const char *repo,
                              const char *tag)
{
    char *path = build_path(a, "/repos/%s/%s/releases/tags/%s", owner, repo, tag);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

int api_release_asset_list(ApiClient *a, const char *owner, const char *repo,
                           int64_t release_id, Attachment **out, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/releases/%lld/assets", owner, repo, (long long)release_id);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(Attachment), (void (*)(const JsonValue *, void *))parse_attachment,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_release_asset_get(ApiClient *a, const char *owner, const char *repo,
                          int64_t release_id, int64_t asset_id, Attachment *out)
{
    char *path = build_path(a, "/repos/%s/%s/releases/%lld/assets/%lld",
                            owner, repo, (long long)release_id, (long long)asset_id);
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

    parse_attachment(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_release_asset_edit(ApiClient *a, const char *owner, const char *repo,
                           int64_t release_id, int64_t asset_id,
                           const char *name, Attachment *out)
{
    JsonValue *body = json_object_new();
    if (name)
        json_object_set_string(body, "name", name);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/releases/%lld/assets/%lld",
                            owner, repo, (long long)release_id, (long long)asset_id);
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
            parse_attachment(parsed, out);
        json_free(parsed);
    }

    http_response_free(&resp);
    return API_OK;
}

int api_release_asset_delete(ApiClient *a, const char *owner, const char *repo,
                             int64_t release_id, int64_t asset_id)
{
    char *path = build_path(a, "/repos/%s/%s/releases/%lld/assets/%lld",
                            owner, repo, (long long)release_id, (long long)asset_id);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

/* ===== Tags ===== */

void tag_free(Tag *t)
{
    if (!t)
        return;
    free(t->name);
    free(t->id);
    free(t->message);
    free(t->tarball_url);
    free(t->zipball_url);
    memset(t, 0, sizeof(*t));
}

void tag_array_free(Tag *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        tag_free(&arr[i]);
    free(arr);
}

int api_tag_list(ApiClient *a, const char *owner, const char *repo,
                 int limit, Tag **out, size_t *count)
{
    char *path;
    if (limit > 0) {
        static char lim[16];
        snprintf(lim, sizeof(lim), "%d", limit);
        QueryParam params[] = { { "limit", lim } };
        path = build_path_q(a, "/repos/%s/%s/tags", params, 1, owner, repo);
    } else {
        path = build_path(a, "/repos/%s/%s/tags", owner, repo);
    }

    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(Tag), (void (*)(const JsonValue *, void *))parse_tag,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_tag_create(ApiClient *a, const char *owner, const char *repo,
                   const CreateTagOpts *opts, Tag *out)
{
    if (!opts || !opts->tag_name) {
        set_error(a, "tag_name is required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "tag_name", opts->tag_name);
    if (opts->message)
        json_object_set_string(body, "message", opts->message);
    if (opts->target)
        json_object_set_string(body, "target", opts->target);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/tags", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);

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

    parse_tag(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_tag_get(ApiClient *a, const char *owner, const char *repo,
                const char *tag, Tag *out)
{
    char *path = build_path(a, "/repos/%s/%s/tags/%s", owner, repo, tag);
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

    parse_tag(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_tag_delete(ApiClient *a, const char *owner, const char *repo,
                   const char *tag)
{
    char *path = build_path(a, "/repos/%s/%s/tags/%s", owner, repo, tag);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

/* ===== Branches ===== */

void branch_free(Branch *b)
{
    if (!b)
        return;
    free(b->name);
    free(b->commit_sha);
    free(b->commit_message);
    free(b->effective_branch_protection_name);
    memset(b, 0, sizeof(*b));
}

void branch_array_free(Branch *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        branch_free(&arr[i]);
    free(arr);
}

int api_branch_list(ApiClient *a, const char *owner, const char *repo,
                    int limit, Branch **out, size_t *count)
{
    char *path;
    if (limit > 0) {
        static char lim[16];
        snprintf(lim, sizeof(lim), "%d", limit);
        QueryParam params[] = { { "limit", lim } };
        path = build_path_q(a, "/repos/%s/%s/branches", params, 1, owner, repo);
    } else {
        path = build_path(a, "/repos/%s/%s/branches", owner, repo);
    }

    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(Branch), (void (*)(const JsonValue *, void *))parse_branch,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_branch_create(ApiClient *a, const char *owner, const char *repo,
                      const CreateBranchOpts *opts, Branch *out)
{
    if (!opts || !opts->new_branch_name) {
        set_error(a, "new_branch_name is required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "new_branch_name", opts->new_branch_name);
    if (opts->old_ref_name)
        json_object_set_string(body, "old_ref_name", opts->old_ref_name);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/branches", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);

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

    parse_branch(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_branch_get(ApiClient *a, const char *owner, const char *repo,
                   const char *branch, Branch *out)
{
    char *path = build_path(a, "/repos/%s/%s/branches/%s", owner, repo, branch);
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

    parse_branch(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_branch_rename(ApiClient *a, const char *owner, const char *repo,
                      const char *branch, const char *new_name, Branch *out)
{
    if (!new_name) {
        set_error(a, "new name is required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "name", new_name);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/branches/%s", owner, repo, branch);
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
            parse_branch(parsed, out);
        json_free(parsed);
    }

    http_response_free(&resp);
    return API_OK;
}

int api_branch_delete(ApiClient *a, const char *owner, const char *repo,
                      const char *branch)
{
    char *path = build_path(a, "/repos/%s/%s/branches/%s", owner, repo, branch);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

/* ===== Issues ===== */

void issue_free(Issue *i)
{
    if (!i)
        return;
    free(i->title);
    free(i->body);
    free(i->state);
    free(i->html_url);
    free(i->created_at);
    free(i->updated_at);
    free(i->closed_at);
    free(i->due_date);
    memset(i, 0, sizeof(*i));
}

void issue_array_free(Issue *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t j = 0; j < count; j++)
        issue_free(&arr[j]);
    free(arr);
}

int api_issue_list(ApiClient *a, const char *owner, const char *repo,
                   const char *state, const char *labels, const char *type,
                   int limit, Issue **out, size_t *count)
{
    QueryParam params[5];
    size_t pc = 0;
    if (state && state[0])
        params[pc++] = (QueryParam){ "state", state };
    if (labels && labels[0])
        params[pc++] = (QueryParam){ "labels", labels };
    if (type && type[0])
        params[pc++] = (QueryParam){ "type", type };
    if (limit > 0) {
        static char lim[16];
        snprintf(lim, sizeof(lim), "%d", limit);
        params[pc++] = (QueryParam){ "limit", lim };
    }

    char *path = build_path_q(a, "/repos/%s/%s/issues", params, pc, owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(Issue), (void (*)(const JsonValue *, void *))parse_issue,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_issue_create(ApiClient *a, const char *owner, const char *repo,
                     const CreateIssueOpts *opts, Issue *out)
{
    if (!opts || !opts->title) {
        set_error(a, "title is required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "title", opts->title);
    if (opts->body)
        json_object_set_string(body, "body", opts->body);
    if (opts->assignee)
        json_object_set_string(body, "assignee", opts->assignee);
    if (opts->assignees) {
        JsonValue *arr = json_array_new();
        for (size_t i = 0; opts->assignees[i]; i++)
            json_array_push(arr, json_string_new(opts->assignees[i]));
        json_object_set(body, "assignees", arr);
    }
    if (opts->milestone > 0)
        json_object_set_number(body, "milestone", (double)opts->milestone);
    if (opts->labels && opts->label_count > 0) {
        JsonValue *arr = json_array_new();
        for (size_t i = 0; i < opts->label_count; i++)
            json_array_push(arr, json_number_new((double)opts->labels[i]));
        json_object_set(body, "labels", arr);
    }
    if (opts->due_date)
        json_object_set_string(body, "due_date", opts->due_date);
    if (opts->ref)
        json_object_set_string(body, "ref", opts->ref);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/issues", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);

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

    parse_issue(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_issue_get(ApiClient *a, const char *owner, const char *repo,
                  int number, Issue *out)
{
    char *path = build_path(a, "/repos/%s/%s/issues/%d", owner, repo, number);
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

    parse_issue(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_issue_edit(ApiClient *a, const char *owner, const char *repo,
                   int number, const EditIssueOpts *opts, Issue *out)
{
    JsonValue *body = json_object_new();
    if (opts->title_set)
        json_object_set_string(body, "title", opts->title);
    if (opts->body_set)
        json_object_set_string(body, "body", opts->body);
    if (opts->state_set)
        json_object_set_string(body, "state", opts->state);
    if (opts->milestone_set)
        json_object_set_number(body, "milestone", (double)opts->milestone);
    if (opts->due_date_set)
        json_object_set_string(body, "due_date", opts->due_date);
    if (opts->unset_due_date)
        json_object_set_null(body, "due_date");
    if (opts->ref_set)
        json_object_set_string(body, "ref", opts->ref);
    if (opts->assignees && opts->assignee_count > 0) {
        JsonValue *arr = json_array_new();
        for (size_t i = 0; i < opts->assignee_count; i++)
            json_array_push(arr, json_string_new(opts->assignees[i]));
        json_object_set(body, "assignees", arr);
    }

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/issues/%d", owner, repo, number);
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
            parse_issue(parsed, out);
        json_free(parsed);
    }

    http_response_free(&resp);
    return API_OK;
}

int api_issue_delete(ApiClient *a, const char *owner, const char *repo,
                     int number)
{
    char *path = build_path(a, "/repos/%s/%s/issues/%d", owner, repo, number);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

int api_issue_comment_create(ApiClient *a, const char *owner, const char *repo,
                             int number, const char *body_text)
{
    if (!body_text) {
        set_error(a, "comment body is required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "body", body_text);
    char *body_str = json_serialize(body, false);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/issues/%d/comments", owner, repo, number);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);
    http_response_free(&resp);
    return err;
}

int api_issue_label_add(ApiClient *a, const char *owner, const char *repo,
                        int number, const int64_t *labels, size_t count)
{
    JsonValue *body = json_array_new();
    for (size_t i = 0; i < count; i++)
        json_array_push(body, json_number_new((double)labels[i]));

    char *body_str = json_serialize(body, false);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/issues/%d/labels", owner, repo, number);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);
    http_response_free(&resp);
    return err;
}

int api_issue_label_set(ApiClient *a, const char *owner, const char *repo,
                        int number, const int64_t *labels, size_t count)
{
    JsonValue *body = json_array_new();
    for (size_t i = 0; i < count; i++)
        json_array_push(body, json_number_new((double)labels[i]));

    char *body_str = json_serialize(body, false);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/issues/%d/labels", owner, repo, number);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_PUT, path, body_str, &resp);
    free(path);
    free(body_str);
    http_response_free(&resp);
    return err;
}

int api_issue_label_clear(ApiClient *a, const char *owner, const char *repo,
                          int number)
{
    char *path = build_path(a, "/repos/%s/%s/issues/%d/labels", owner, repo, number);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

int api_issue_label_remove(ApiClient *a, const char *owner, const char *repo,
                           int number, int64_t label_id)
{
    char *path = build_path(a, "/repos/%s/%s/issues/%d/labels/%lld",
                            owner, repo, number, (long long)label_id);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

/* ===== Labels ===== */

void label_free(Label *l)
{
    if (!l)
        return;
    free(l->name);
    free(l->color);
    free(l->description);
    memset(l, 0, sizeof(*l));
}

void label_array_free(Label *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        label_free(&arr[i]);
    free(arr);
}

int api_label_list(ApiClient *a, const char *owner, const char *repo,
                   Label **out, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/labels", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(Label), (void (*)(const JsonValue *, void *))parse_label,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_label_create(ApiClient *a, const char *owner, const char *repo,
                     const CreateLabelOpts *opts, Label *out)
{
    if (!opts || !opts->name || !opts->color) {
        set_error(a, "name and color are required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "name", opts->name);
    json_object_set_string(body, "color", opts->color);
    if (opts->description)
        json_object_set_string(body, "description", opts->description);
    if (opts->exclusive_set)
        json_object_set_bool(body, "exclusive", opts->exclusive_val);
    if (opts->is_archived_set)
        json_object_set_bool(body, "is_archived", opts->is_archived_val);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/labels", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);

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

    parse_label(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_label_get(ApiClient *a, const char *owner, const char *repo,
                  int64_t id, Label *out)
{
    char *path = build_path(a, "/repos/%s/%s/labels/%lld", owner, repo, (long long)id);
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

    parse_label(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_label_edit(ApiClient *a, const char *owner, const char *repo,
                   int64_t id, int name_set, const char *name,
                   int color_set, const char *color,
                   int desc_set, const char *description, Label *out)
{
    JsonValue *body = json_object_new();
    if (name_set)
        json_object_set_string(body, "name", name);
    if (color_set)
        json_object_set_string(body, "color", color);
    if (desc_set)
        json_object_set_string(body, "description", description);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/labels/%lld", owner, repo, (long long)id);
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
            parse_label(parsed, out);
        json_free(parsed);
    }

    http_response_free(&resp);
    return API_OK;
}

int api_label_delete(ApiClient *a, const char *owner, const char *repo,
                     int64_t id)
{
    char *path = build_path(a, "/repos/%s/%s/labels/%lld", owner, repo, (long long)id);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

/* ===== Milestones ===== */

void milestone_free(Milestone *m)
{
    if (!m)
        return;
    free(m->title);
    free(m->description);
    free(m->state);
    free(m->due_on);
    free(m->created_at);
    free(m->updated_at);
    memset(m, 0, sizeof(*m));
}

void milestone_array_free(Milestone *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        milestone_free(&arr[i]);
    free(arr);
}

int api_milestone_list(ApiClient *a, const char *owner, const char *repo,
                       const char *state, Milestone **out, size_t *count)
{
    char *path;
    if (state && state[0]) {
        QueryParam params[] = { { "state", state } };
        path = build_path_q(a, "/repos/%s/%s/milestones", params, 1, owner, repo);
    } else {
        path = build_path(a, "/repos/%s/%s/milestones", owner, repo);
    }

    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(Milestone), (void (*)(const JsonValue *, void *))parse_milestone,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_milestone_create(ApiClient *a, const char *owner, const char *repo,
                         const CreateMilestoneOpts *opts, Milestone *out)
{
    if (!opts || !opts->title) {
        set_error(a, "title is required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "title", opts->title);
    if (opts->description)
        json_object_set_string(body, "description", opts->description);
    if (opts->state)
        json_object_set_string(body, "state", opts->state);
    if (opts->due_on)
        json_object_set_string(body, "due_on", opts->due_on);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/milestones", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);

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

    parse_milestone(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_milestone_get(ApiClient *a, const char *owner, const char *repo,
                      int64_t id, Milestone *out)
{
    char *path = build_path(a, "/repos/%s/%s/milestones/%lld", owner, repo, (long long)id);
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

    parse_milestone(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_milestone_edit(ApiClient *a, const char *owner, const char *repo,
                       int64_t id, int title_set, const char *title,
                       int desc_set, const char *description,
                       int state_set, const char *state,
                       int due_set, const char *due_on, Milestone *out)
{
    JsonValue *body = json_object_new();
    if (title_set)
        json_object_set_string(body, "title", title);
    if (desc_set)
        json_object_set_string(body, "description", description);
    if (state_set)
        json_object_set_string(body, "state", state);
    if (due_set)
        json_object_set_string(body, "due_on", due_on);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/milestones/%lld", owner, repo, (long long)id);
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
            parse_milestone(parsed, out);
        json_free(parsed);
    }

    http_response_free(&resp);
    return API_OK;
}

int api_milestone_delete(ApiClient *a, const char *owner, const char *repo,
                         int64_t id)
{
    char *path = build_path(a, "/repos/%s/%s/milestones/%lld", owner, repo, (long long)id);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

/* ===== Pull Requests ===== */

void pullrequest_free(PullRequest *p)
{
    if (!p)
        return;
    free(p->title);
    free(p->body);
    free(p->state);
    free(p->merged_at);
    free(p->closed_at);
    free(p->created_at);
    free(p->updated_at);
    free(p->html_url);
    free(p->diff_url);
    free(p->patch_url);
    free(p->merge_commit_sha);
    free(p->base_ref);
    free(p->head_ref);
    memset(p, 0, sizeof(*p));
}

void pullrequest_array_free(PullRequest *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        pullrequest_free(&arr[i]);
    free(arr);
}

int api_pr_list(ApiClient *a, const char *owner, const char *repo,
                const char *state, int limit, PullRequest **out, size_t *count)
{
    QueryParam params[3];
    size_t pc = 0;
    if (state && state[0])
        params[pc++] = (QueryParam){ "state", state };
    if (limit > 0) {
        static char lim[16];
        snprintf(lim, sizeof(lim), "%d", limit);
        params[pc++] = (QueryParam){ "limit", lim };
    }

    char *path = build_path_q(a, "/repos/%s/%s/pulls", params, pc, owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(PullRequest), (void (*)(const JsonValue *, void *))parse_pullrequest,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_pr_create(ApiClient *a, const char *owner, const char *repo,
                  const CreatePullRequestOpts *opts, PullRequest *out)
{
    if (!opts || !opts->head) {
        set_error(a, "head branch is required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    if (opts->title)
        json_object_set_string(body, "title", opts->title);
    if (opts->body)
        json_object_set_string(body, "body", opts->body);
    json_object_set_string(body, "head", opts->head);
    if (opts->base)
        json_object_set_string(body, "base", opts->base);
    if (opts->assignee)
        json_object_set_string(body, "assignee", opts->assignee);
    if (opts->milestone > 0)
        json_object_set_number(body, "milestone", (double)opts->milestone);
    if (opts->labels && opts->label_count > 0) {
        JsonValue *arr = json_array_new();
        for (size_t i = 0; i < opts->label_count; i++)
            json_array_push(arr, json_number_new((double)opts->labels[i]));
        json_object_set(body, "labels", arr);
    }

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/pulls", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);

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

    parse_pullrequest(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_pr_get(ApiClient *a, const char *owner, const char *repo,
               int number, PullRequest *out)
{
    char *path = build_path(a, "/repos/%s/%s/pulls/%d", owner, repo, number);
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

    parse_pullrequest(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_pr_edit(ApiClient *a, const char *owner, const char *repo,
                int number, const EditPullRequestOpts *opts, PullRequest *out)
{
    JsonValue *body = json_object_new();
    if (opts->title_set)
        json_object_set_string(body, "title", opts->title);
    if (opts->body_set)
        json_object_set_string(body, "body", opts->body);
    if (opts->base_set)
        json_object_set_string(body, "base", opts->base);
    if (opts->state_set)
        json_object_set_string(body, "state", opts->state);
    if (opts->allow_maintainer_edit_set)
        json_object_set_bool(body, "allow_maintainer_edit", opts->allow_maintainer_edit_val);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/pulls/%d", owner, repo, number);
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
            parse_pullrequest(parsed, out);
        json_free(parsed);
    }

    http_response_free(&resp);
    return API_OK;
}

int api_pr_merge(ApiClient *a, const char *owner, const char *repo,
                 int number, const MergePullRequestOpts *opts)
{
    if (!opts || !opts->do_style) {
        set_error(a, "merge style is required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "Do", opts->do_style);
    if (opts->merge_title)
        json_object_set_string(body, "MergeTitleField", opts->merge_title);
    if (opts->merge_message)
        json_object_set_string(body, "MergeMessageField", opts->merge_message);
    if (opts->delete_branch_after_merge)
        json_object_set_bool(body, "delete_branch_after_merge", true);
    if (opts->force_merge)
        json_object_set_bool(body, "force_merge", true);
    if (opts->merge_when_checks_succeed)
        json_object_set_bool(body, "merge_when_checks_succeed", true);
    if (opts->head_commit_id)
        json_object_set_string(body, "head_commit_id", opts->head_commit_id);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/pulls/%d/merge", owner, repo, number);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);
    http_response_free(&resp);
    return err;
}

int api_pr_cancel_merge(ApiClient *a, const char *owner, const char *repo,
                        int number)
{
    char *path = build_path(a, "/repos/%s/%s/pulls/%d/merge", owner, repo, number);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

int api_pr_is_merged(ApiClient *a, const char *owner, const char *repo,
                     int number, int *out)
{
    char *path = build_path(a, "/repos/%s/%s/pulls/%d/merge", owner, repo, number);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err == API_OK) {
        *out = 1;
        http_response_free(&resp);
        return API_OK;
    }
    if (err == API_ERR_NOT_FOUND) {
        *out = 0;
        http_response_free(&resp);
        return API_OK;
    }

    http_response_free(&resp);
    return err;
}

int api_pr_diff(ApiClient *a, const char *owner, const char *repo,
                int number, int patch, char **out, size_t *out_len)
{
    char *path = build_path(a, "/repos/%s/%s/pulls/%d.%s",
                            owner, repo, number, patch ? "patch" : "diff");
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    *out = resp.body;
    *out_len = resp.body_len;
    resp.body = NULL;
    http_response_free(&resp);
    return API_OK;
}

/* ===== Commits ===== */

void commit_free(Commit *c)
{
    if (!c)
        return;
    free(c->sha);
    free(c->created);
    free(c->html_url);
    free(c->message);
    free(c->author_name);
    free(c->author_email);
    free(c->committer_name);
    free(c->committer_email);
    memset(c, 0, sizeof(*c));
}

void commit_array_free(Commit *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        commit_free(&arr[i]);
    free(arr);
}

void commitstatus_free(CommitStatus *s)
{
    if (!s)
        return;
    free(s->status);
    free(s->context);
    free(s->description);
    free(s->target_url);
    free(s->created_at);
    free(s->updated_at);
    memset(s, 0, sizeof(*s));
}

void commitstatus_array_free(CommitStatus *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        commitstatus_free(&arr[i]);
    free(arr);
}

int api_commit_list(ApiClient *a, const char *owner, const char *repo,
                    const char *sha, const char *path_param,
                    int limit, Commit **out, size_t *count)
{
    QueryParam params[4];
    size_t pc = 0;
    if (sha && sha[0])
        params[pc++] = (QueryParam){ "sha", sha };
    if (path_param && path_param[0])
        params[pc++] = (QueryParam){ "path", path_param };
    if (limit > 0) {
        static char lim[16];
        snprintf(lim, sizeof(lim), "%d", limit);
        params[pc++] = (QueryParam){ "limit", lim };
    }

    char *path = build_path_q(a, "/repos/%s/%s/commits", params, pc, owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(Commit), (void (*)(const JsonValue *, void *))parse_commit,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_commit_get(ApiClient *a, const char *owner, const char *repo,
                   const char *sha, Commit *out)
{
    char *path = build_path(a, "/repos/%s/%s/commits/%s", owner, repo, sha);
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

    parse_commit(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_commit_status(ApiClient *a, const char *owner, const char *repo,
                      const char *ref, CommitStatus **out, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/commits/%s/status", owner, repo, ref);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    /* The combined status response is an object with a "statuses" array */
    const char *json_err = NULL;
    JsonValue *parsed = json_parse(resp.body, &json_err);
    http_response_free(&resp);

    if (!parsed || !json_is_object(parsed)) {
        json_free(parsed);
        set_error(a, "failed to parse API response");
        return API_ERR_UNKNOWN;
    }

    JsonValue *statuses = json_object_lookup(parsed, "statuses");
    if (!statuses || !json_is_array(statuses)) {
        json_free(parsed);
        *out = NULL;
        *count = 0;
        return API_OK;
    }

    size_t n = json_array_count(statuses);
    CommitStatus *arr = calloc(n, sizeof(CommitStatus));
    if (!arr && n > 0) {
        json_free(parsed);
        set_error(a, "out of memory");
        return API_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < n; i++)
        parse_commitstatus(json_array_get(statuses, i), &arr[i]);

    *out = arr;
    *count = n;
    json_free(parsed);
    return API_OK;
}

int api_commit_compare(ApiClient *a, const char *owner, const char *repo,
                       const char *basehead, Commit **out, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/compare/%s", owner, repo, basehead);
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

    JsonValue *commits_arr = json_object_lookup(parsed, "commits");
    if (!commits_arr || !json_is_array(commits_arr)) {
        json_free(parsed);
        *out = NULL;
        *count = 0;
        return API_OK;
    }

    size_t n = json_array_count(commits_arr);
    Commit *arr = calloc(n, sizeof(Commit));
    if (!arr && n > 0) {
        json_free(parsed);
        set_error(a, "out of memory");
        return API_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < n; i++)
        parse_commit(json_array_get(commits_arr, i), &arr[i]);

    *out = arr;
    *count = n;
    json_free(parsed);
    return API_OK;
}

/* ===== File Contents ===== */

void content_entry_free(ContentEntry *e)
{
    if (!e)
        return;
    free(e->type);
    free(e->name);
    free(e->path);
    free(e->sha);
    free(e->encoding);
    free(e->content);
    free(e->download_url);
    free(e->html_url);
    free(e->git_url);
    free(e->last_commit_sha);
    memset(e, 0, sizeof(*e));
}

void content_entry_array_free(ContentEntry *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        content_entry_free(&arr[i]);
    free(arr);
}

int api_content_list(ApiClient *a, const char *owner, const char *repo,
                     const char *ref, ContentEntry **out, size_t *count)
{
    char *path;
    if (ref && ref[0]) {
        QueryParam params[] = { { "ref", ref } };
        path = build_path_q(a, "/repos/%s/%s/contents", params, 1, owner, repo);
    } else {
        path = build_path(a, "/repos/%s/%s/contents", owner, repo);
    }

    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(ContentEntry),
                      (void (*)(const JsonValue *, void *))parse_content_entry,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_content_get(ApiClient *a, const char *owner, const char *repo,
                    const char *filepath, const char *ref, ContentEntry *out)
{
    char *path;
    if (ref && ref[0]) {
        QueryParam params[] = { { "ref", ref } };
        path = build_path_q(a, "/repos/%s/%s/contents/%s", params, 1, owner, repo, filepath);
    } else {
        path = build_path(a, "/repos/%s/%s/contents/%s", owner, repo, filepath);
    }

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

    parse_content_entry(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_content_create(ApiClient *a, const char *owner, const char *repo,
                       const char *filepath, const char *message,
                       const char *content_b64, const char *branch,
                       const char *new_branch)
{
    if (!message) {
        set_error(a, "commit message is required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    if (content_b64)
        json_object_set_string(body, "content", content_b64);
    json_object_set_string(body, "message", message);
    if (branch)
        json_object_set_string(body, "branch", branch);
    if (new_branch)
        json_object_set_string(body, "new_branch", new_branch);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/contents/%s", owner, repo, filepath);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);
    http_response_free(&resp);
    return err;
}

int api_content_update(ApiClient *a, const char *owner, const char *repo,
                       const char *filepath, const char *message,
                       const char *content_b64, const char *sha,
                       const char *branch, const char *new_branch)
{
    if (!message || !sha) {
        set_error(a, "commit message and sha are required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    if (content_b64)
        json_object_set_string(body, "content", content_b64);
    json_object_set_string(body, "message", message);
    json_object_set_string(body, "sha", sha);
    if (branch)
        json_object_set_string(body, "branch", branch);
    if (new_branch)
        json_object_set_string(body, "new_branch", new_branch);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/contents/%s", owner, repo, filepath);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_PUT, path, body_str, &resp);
    free(path);
    free(body_str);
    http_response_free(&resp);
    return err;
}

int api_content_delete(ApiClient *a, const char *owner, const char *repo,
                       const char *filepath, const char *message,
                       const char *sha, const char *branch,
                       const char *new_branch)
{
    if (!message || !sha) {
        set_error(a, "commit message and sha are required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "message", message);
    json_object_set_string(body, "sha", sha);
    if (branch)
        json_object_set_string(body, "branch", branch);
    if (new_branch)
        json_object_set_string(body, "new_branch", new_branch);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/contents/%s", owner, repo, filepath);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, body_str, &resp);
    free(path);
    free(body_str);
    http_response_free(&resp);
    return err;
}

int api_content_raw(ApiClient *a, const char *owner, const char *repo,
                    const char *filepath, const char *ref,
                    char **out, size_t *out_len)
{
    char *path;
    if (ref && ref[0]) {
        QueryParam params[] = { { "ref", ref } };
        path = build_path_q(a, "/repos/%s/%s/raw/%s", params, 1, owner, repo, filepath);
    } else {
        path = build_path(a, "/repos/%s/%s/raw/%s", owner, repo, filepath);
    }

    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    *out = resp.body;
    *out_len = resp.body_len;
    resp.body = NULL;
    http_response_free(&resp);
    return API_OK;
}

/* ===== Deploy Keys ===== */

void deploykey_free(DeployKey *k)
{
    if (!k)
        return;
    free(k->title);
    free(k->key);
    free(k->fingerprint);
    free(k->created_at);
    memset(k, 0, sizeof(*k));
}

void deploykey_array_free(DeployKey *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        deploykey_free(&arr[i]);
    free(arr);
}

int api_key_list(ApiClient *a, const char *owner, const char *repo,
                 DeployKey **out, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/keys", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(DeployKey),
                      (void (*)(const JsonValue *, void *))parse_deploykey,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_key_add(ApiClient *a, const char *owner, const char *repo,
                const CreateKeyOpts *opts, DeployKey *out)
{
    if (!opts || !opts->title || !opts->key) {
        set_error(a, "title and key are required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "title", opts->title);
    json_object_set_string(body, "key", opts->key);
    json_object_set_bool(body, "read_only", opts->read_only);

    char *body_str = json_serialize(body, false);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/keys", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);

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

    parse_deploykey(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_key_get(ApiClient *a, const char *owner, const char *repo,
                int64_t id, DeployKey *out)
{
    char *path = build_path(a, "/repos/%s/%s/keys/%lld", owner, repo, (long long)id);
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

    parse_deploykey(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_key_delete(ApiClient *a, const char *owner, const char *repo,
                   int64_t id)
{
    char *path = build_path(a, "/repos/%s/%s/keys/%lld", owner, repo, (long long)id);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

/* ===== Collaborators ===== */

void user_free(User *u)
{
    if (!u)
        return;
    free(u->login);
    free(u->full_name);
    free(u->email);
    free(u->html_url);
    memset(u, 0, sizeof(*u));
}

void user_array_free(User *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        user_free(&arr[i]);
    free(arr);
}

int api_collaborator_list(ApiClient *a, const char *owner, const char *repo,
                          User **out, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/collaborators", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(User),
                      (void (*)(const JsonValue *, void *))parse_user,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_collaborator_add(ApiClient *a, const char *owner, const char *repo,
                         const char *username, const char *permission)
{
    JsonValue *body = json_object_new();
    if (permission)
        json_object_set_string(body, "permission", permission);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/collaborators/%s", owner, repo, username);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_PUT, path, body_str, &resp);
    free(path);
    free(body_str);
    http_response_free(&resp);
    return err;
}

int api_collaborator_remove(ApiClient *a, const char *owner, const char *repo,
                            const char *username)
{
    char *path = build_path(a, "/repos/%s/%s/collaborators/%s", owner, repo, username);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

int api_collaborator_perms(ApiClient *a, const char *owner, const char *repo,
                           const char *username, char *perm_out, size_t perm_sz)
{
    char *path = build_path(a, "/repos/%s/%s/collaborators/%s/permission",
                            owner, repo, username);
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

    char *perm = json_dup_string(parsed, "permission");
    if (perm) {
        strncpy(perm_out, perm, perm_sz - 1);
        perm_out[perm_sz - 1] = '\0';
        free(perm);
    } else {
        perm_out[0] = '\0';
    }

    json_free(parsed);
    return API_OK;
}

/* ===== Forks ===== */

int api_fork_list(ApiClient *a, const char *owner, const char *repo,
                  Repo **out, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/forks", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(Repo), (void (*)(const JsonValue *, void *))parse_repo,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_fork_create(ApiClient *a, const char *owner, const char *repo,
                    const char *name, const char *org, Repo *out)
{
    JsonValue *body = json_object_new();
    if (name)
        json_object_set_string(body, "name", name);
    if (org)
        json_object_set_string(body, "organization", org);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/forks", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
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

/* ===== Webhooks ===== */

void hook_free(Hook *h)
{
    if (!h)
        return;
    free(h->type);
    free(h->url);
    free(h->content_type);
    free(h->branch_filter);
    free(h->authorization_header);
    memset(h, 0, sizeof(*h));
}

void hook_array_free(Hook *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        hook_free(&arr[i]);
    free(arr);
}

int api_hook_list(ApiClient *a, const char *owner, const char *repo,
                  Hook **out, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/hooks", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(Hook),
                      (void (*)(const JsonValue *, void *))parse_hook,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_hook_create(ApiClient *a, const char *owner, const char *repo,
                    const CreateHookOpts *opts, Hook *out)
{
    if (!opts || !opts->type || !opts->url) {
        set_error(a, "type and url are required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "type", opts->type);
    json_object_set_bool(body, "active", opts->active);

    JsonValue *config = json_object_new();
    json_object_set_string(config, "url", opts->url);
    if (opts->content_type)
        json_object_set_string(config, "content_type", opts->content_type);
    if (opts->secret)
        json_object_set_string(config, "secret", opts->secret);
    json_object_set(body, "config", config);

    if (opts->events) {
        JsonValue *arr = json_array_new();
        for (size_t i = 0; opts->events[i]; i++)
            json_array_push(arr, json_string_new(opts->events[i]));
        json_object_set(body, "events", arr);
    }

    if (opts->branch_filter)
        json_object_set_string(body, "branch_filter", opts->branch_filter);
    if (opts->authorization_header)
        json_object_set_string(body, "authorization_header", opts->authorization_header);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/hooks", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);

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

    parse_hook(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_hook_get(ApiClient *a, const char *owner, const char *repo,
                 int64_t id, Hook *out)
{
    char *path = build_path(a, "/repos/%s/%s/hooks/%lld", owner, repo, (long long)id);
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

    parse_hook(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_hook_delete(ApiClient *a, const char *owner, const char *repo,
                    int64_t id)
{
    char *path = build_path(a, "/repos/%s/%s/hooks/%lld", owner, repo, (long long)id);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

int api_hook_test(ApiClient *a, const char *owner, const char *repo,
                  int64_t id)
{
    char *path = build_path(a, "/repos/%s/%s/hooks/%lld/tests", owner, repo, (long long)id);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

/* ===== Wiki ===== */

void wikipage_free(WikiPage *w)
{
    if (!w)
        return;
    free(w->title);
    free(w->content_base64);
    free(w->html_url);
    free(w->sub_url);
    free(w->last_commit_sha);
    memset(w, 0, sizeof(*w));
}

void wikipage_array_free(WikiPage *arr, size_t count)
{
    if (!arr)
        return;
    for (size_t i = 0; i < count; i++)
        wikipage_free(&arr[i]);
    free(arr);
}

int api_wiki_list(ApiClient *a, const char *owner, const char *repo,
                  WikiPage **out, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/wiki/pages", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(WikiPage),
                      (void (*)(const JsonValue *, void *))parse_wikipage,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_wiki_create(ApiClient *a, const char *owner, const char *repo,
                    const CreateWikiPageOpts *opts, WikiPage *out)
{
    if (!opts || !opts->title) {
        set_error(a, "title is required");
        return API_ERR_VALIDATION;
    }

    JsonValue *body = json_object_new();
    json_object_set_string(body, "title", opts->title);
    if (opts->content_base64)
        json_object_set_string(body, "content_base64", opts->content_base64);
    if (opts->message)
        json_object_set_string(body, "message", opts->message);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/wiki/new", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, body_str, &resp);
    free(path);
    free(body_str);

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

    parse_wikipage(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_wiki_get(ApiClient *a, const char *owner, const char *repo,
                 const char *page_name, WikiPage *out)
{
    char *path = build_path(a, "/repos/%s/%s/wiki/page/%s", owner, repo, page_name);
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

    parse_wikipage(parsed, out);
    json_free(parsed);
    return API_OK;
}

int api_wiki_edit(ApiClient *a, const char *owner, const char *repo,
                  const char *page_name, const char *content_b64,
                  const char *message, WikiPage *out)
{
    JsonValue *body = json_object_new();
    if (content_b64)
        json_object_set_string(body, "content_base64", content_b64);
    if (message)
        json_object_set_string(body, "message", message);

    char *body_str = json_serialize(body, true);
    json_free(body);

    char *path = build_path(a, "/repos/%s/%s/wiki/page/%s", owner, repo, page_name);
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
            parse_wikipage(parsed, out);
        json_free(parsed);
    }

    http_response_free(&resp);
    return API_OK;
}

int api_wiki_delete(ApiClient *a, const char *owner, const char *repo,
                    const char *page_name)
{
    char *path = build_path(a, "/repos/%s/%s/wiki/page/%s", owner, repo, page_name);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

/* ===== Repo misc ===== */

int api_repo_watch(ApiClient *a, const char *owner, const char *repo)
{
    char *path = build_path(a, "/repos/%s/%s/subscription", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_PUT, path, "{}", &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

int api_repo_unwatch(ApiClient *a, const char *owner, const char *repo)
{
    char *path = build_path(a, "/repos/%s/%s/subscription", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_DELETE, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

int api_repo_is_watching(ApiClient *a, const char *owner, const char *repo,
                         int *out)
{
    char *path = build_path(a, "/repos/%s/%s/subscription", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err == API_OK) {
        *out = 1;
        http_response_free(&resp);
        return API_OK;
    }
    if (err == API_ERR_NOT_FOUND) {
        *out = 0;
        http_response_free(&resp);
        return API_OK;
    }

    http_response_free(&resp);
    return err;
}

int api_repo_stargazers(ApiClient *a, const char *owner, const char *repo,
                        User **out, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/stargazers", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_GET, path, NULL, &resp);
    free(path);

    if (err != API_OK) {
        http_response_free(&resp);
        return err;
    }

    err = parse_array(a, resp.body, sizeof(User),
                      (void (*)(const JsonValue *, void *))parse_user,
                      (void **)out, count);
    http_response_free(&resp);
    return err;
}

int api_repo_languages(ApiClient *a, const char *owner, const char *repo,
                       char ***langs, int64_t **bytes, size_t *count)
{
    char *path = build_path(a, "/repos/%s/%s/languages", owner, repo);
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

    size_t n = json_object_count(parsed);
    char **l = calloc(n, sizeof(char *));
    int64_t *b = calloc(n, sizeof(int64_t));
    if ((!l || !b) && n > 0) {
        free(l);
        free(b);
        json_free(parsed);
        set_error(a, "out of memory");
        return API_ERR_UNKNOWN;
    }

    for (size_t i = 0; i < n; i++) {
        l[i] = strdup(json_object_key(parsed, i));
        JsonValue *v = json_object_get(parsed, i);
        b[i] = (v && json_is_number(v)) ? (int64_t)json_number(v) : 0;
    }

    *langs = l;
    *bytes = b;
    *count = n;
    json_free(parsed);
    return API_OK;
}

int api_repo_mirror_sync(ApiClient *a, const char *owner, const char *repo)
{
    char *path = build_path(a, "/repos/%s/%s/mirror-sync", owner, repo);
    HttpResponse resp;
    ApiError err = do_request(a, HTTP_POST, path, NULL, &resp);
    free(path);
    http_response_free(&resp);
    return err;
}

/* ===== Current user ===== */

int api_user_get_current(ApiClient *a, User *out)
{
    char *path = build_path(a, "/user");
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
        set_error(a, "failed to parse user response");
        return API_ERR_UNKNOWN;
    }

    parse_user(parsed, out);
    json_free(parsed);
    return API_OK;
}
