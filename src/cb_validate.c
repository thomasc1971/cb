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
#include "cb_validate.h"

#include <stdio.h>
#include <string.h>

int validate_repo_name(const char *name, char *error_out, size_t error_sz)
{
    if (!name || name[0] == '\0') {
        if (error_out)
            snprintf(error_out, error_sz, "repository name is empty");
        return VALIDATE_ERR;
    }

    size_t len = strlen(name);
    if (len > 100) {
        if (error_out)
            snprintf(error_out, error_sz, "repository name exceeds 100 characters");
        return VALIDATE_ERR;
    }

    /* AlphaDashDot: alphanumeric, dash, dot, underscore */
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_')) {
            if (error_out)
                snprintf(error_out, error_sz,
                         "repository name contains invalid character '%c' (allowed: alphanumeric, -, ., _)", c);
            return VALIDATE_ERR;
        }
    }

    return VALIDATE_OK;
}

int validate_description(const char *desc, char *error_out, size_t error_sz)
{
    if (!desc)
        return VALIDATE_OK; /* NULL = no description */
    if (strlen(desc) > 2048) {
        if (error_out)
            snprintf(error_out, error_sz, "description exceeds 2048 characters");
        return VALIDATE_ERR;
    }
    return VALIDATE_OK;
}

int validate_website(const char *url, char *error_out, size_t error_sz)
{
    if (!url)
        return VALIDATE_OK;
    if (strlen(url) > 1024) {
        if (error_out)
            snprintf(error_out, error_sz, "website URL exceeds 1024 characters");
        return VALIDATE_ERR;
    }
    return VALIDATE_OK;
}

int validate_merge_style(const char *style, char *error_out, size_t error_sz)
{
    if (!style) {
        if (error_out)
            snprintf(error_out, error_sz, "merge style is NULL");
        return VALIDATE_ERR;
    }

    const char *valid[] = {
        "merge", "rebase", "rebase-merge", "squash",
        "fast-forward-only", "manually-merged", "rebase-update-only", NULL
    };

    for (int i = 0; valid[i]; i++) {
        if (strcmp(style, valid[i]) == 0)
            return VALIDATE_OK;
    }

    if (error_out)
        snprintf(error_out, error_sz,
                 "invalid merge style '%s' (valid: merge, rebase, rebase-merge, squash, "
                 "fast-forward-only, manually-merged, rebase-update-only)",
                 style);
    return VALIDATE_ERR;
}

int validate_owner_repo(const char *str, char *owner_out, size_t owner_sz,
                        char *repo_out, size_t repo_sz, char *error_out, size_t error_sz)
{
    if (!str || str[0] == '\0') {
        if (error_out)
            snprintf(error_out, error_sz, "repository argument is empty");
        return VALIDATE_ERR;
    }

    /* Clear outputs */
    if (owner_out && owner_sz > 0)
        owner_out[0] = '\0';
    if (repo_out && repo_sz > 0)
        repo_out[0] = '\0';

    const char *slash = strchr(str, '/');
    if (!slash) {
        /* No slash: owner is empty, repo is the full string */
        if (repo_sz <= strlen(str)) {
            if (error_out)
                snprintf(error_out, error_sz, "repository name too long");
            return VALIDATE_ERR;
        }
        if (owner_out)
            owner_out[0] = '\0';
        strcpy(repo_out, str);
        return VALIDATE_OK;
    }

    /* Has slash: split into owner and repo */
    size_t owner_len = (size_t)(slash - str);
    const char *repo_start = slash + 1;

    if (owner_len == 0) {
        if (error_out)
            snprintf(error_out, error_sz, "owner is empty");
        return VALIDATE_ERR;
    }
    if (repo_start[0] == '\0') {
        if (error_out)
            snprintf(error_out, error_sz, "repository name is empty");
        return VALIDATE_ERR;
    }

    if (owner_sz <= owner_len) {
        if (error_out)
            snprintf(error_out, error_sz, "owner name too long");
        return VALIDATE_ERR;
    }
    if (repo_sz <= strlen(repo_start)) {
        if (error_out)
            snprintf(error_out, error_sz, "repository name too long");
        return VALIDATE_ERR;
    }

    memcpy(owner_out, str, owner_len);
    owner_out[owner_len] = '\0';
    strcpy(repo_out, repo_start);

    return VALIDATE_OK;
}
