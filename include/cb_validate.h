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

#ifndef CB_VALIDATE_H
#define CB_VALIDATE_H

#include <stddef.h>

/* Validation return codes */
#define VALIDATE_OK  0
#define VALIDATE_ERR -1

/* Returns VALIDATE_OK if name is valid, VALIDATE_ERR otherwise.
 * error_out (optional) receives a descriptive error message. */
int validate_repo_name(const char *name, char *error_out, size_t error_sz);

/* Returns VALIDATE_OK if description length is within limits. */
int validate_description(const char *desc, char *error_out, size_t error_sz);

/* Returns VALIDATE_OK if website URL length is within limits. */
int validate_website(const char *url, char *error_out, size_t error_sz);

/* Returns VALIDATE_OK if merge style is a valid enum value. */
int validate_merge_style(const char *style, char *error_out, size_t error_sz);

/* Parse "owner/repo" or "repo" (owner defaults to empty string).
 * owner_out and repo_out are caller-provided buffers.
 * Returns VALIDATE_OK on success, VALIDATE_ERR if format is invalid.
 * If no slash is present, owner_out is set to empty string and repo_out gets the full input.
 * If slash is present but owner or repo is empty, returns VALIDATE_ERR.
 */
int validate_owner_repo(const char *str, char *owner_out, size_t owner_sz,
                        char *repo_out, size_t repo_sz, char *error_out, size_t error_sz);

int validate_tag_name(const char *name, char *error_out, size_t error_sz);
int validate_branch_name(const char *name, char *error_out, size_t error_sz);
int validate_issue_title(const char *title, char *error_out, size_t error_sz);
int validate_label_color(const char *color, char *error_out, size_t error_sz);
int validate_permission(const char *perm, char *error_out, size_t error_sz);
int validate_sha(const char *sha, char *error_out, size_t error_sz);
int validate_org_name(const char *name, char *error_out, size_t error_sz);
int validate_visibility(const char *vis, char *error_out, size_t error_sz);

#endif /* CB_VALIDATE_H */
