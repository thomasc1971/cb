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
