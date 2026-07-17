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

#ifndef CB_CONFIG_H
#define CB_CONFIG_H

#include <stddef.h>

#define DEFAULT_BASE_URL "https://codeberg.org/api/v1"
#define MAX_TOKEN_LEN    256
#define MAX_BASE_URL_LEN 256

typedef struct
{
  char token[MAX_TOKEN_LEN];
  char base_url[MAX_BASE_URL_LEN];
  int token_set; /* 1 if token was found from any source */
} Config;

/* Parse a base_url like "https://codeberg.org/api/v1" into host, port, use_tls, and path_prefix.
 * Returns 0 on success, -1 on error.
 * host_out: buffer of at least host_sz bytes (e.g. 256)
 * path_prefix_out: buffer of at least prefix_sz bytes (e.g. 256) - e.g. "/api/v1"
 */
int config_parse_url (const char* url, char* host_out, size_t host_sz,
                      int* port_out, int* use_tls_out,
                      char* path_prefix_out, size_t prefix_sz);

/* Load config from file + env vars.
 * config_path: path to config file (may be NULL to skip file loading)
 * Returns 0 on success, -1 on error (error message in error_out).
 */
int config_load (Config* cfg, const char* config_path, char* error_out, size_t error_sz);

/* Apply a CLI override for base_url. Only overrides if cli_base_url is non-NULL and non-empty. */
void config_apply_cli_override (Config* cfg, const char* cli_base_url);

#endif /* CB_CONFIG_H */
