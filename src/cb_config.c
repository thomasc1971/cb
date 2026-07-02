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
#include "cb_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Trim leading/trailing whitespace in-place. Returns pointer to trimmed string. */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    if (*s == '\0')
        return s;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        *end-- = '\0';
    return s;
}

/* Parse a quoted value: "value" -> value (unquote) */
static void unquote(char *s)
{
    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

static void load_file(Config *cfg, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *trimmed = trim(line);
        /* Skip comments and empty lines */
        if (*trimmed == '\0' || *trimmed == '#')
            continue;

        /* Find '=' */
        char *eq = strchr(trimmed, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char *key = trim(trimmed);
        char *val = trim(eq + 1);
        unquote(val);

        if (strcmp(key, "token") == 0 && val[0] != '\0' && !cfg->token_set) {
            strncpy(cfg->token, val, sizeof(cfg->token) - 1);
            cfg->token[sizeof(cfg->token) - 1] = '\0';
            cfg->token_set = 1;
        } else if (strcmp(key, "base_url") == 0 && val[0] != '\0') {
            strncpy(cfg->base_url, val, sizeof(cfg->base_url) - 1);
            cfg->base_url[sizeof(cfg->base_url) - 1] = '\0';
        }
    }
    fclose(f);
}

int config_load(Config *cfg, const char *config_path, char *error_out, size_t error_sz)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->base_url, DEFAULT_BASE_URL, sizeof(cfg->base_url) - 1);
    cfg->base_url[sizeof(cfg->base_url) - 1] = '\0';

    /* 1. Load from file first (lowest priority) */
    if (config_path)
        load_file(cfg, config_path);

    /* 2. Environment variables override file */
    const char *env_token = getenv("CB_TOKEN");
    if (env_token && env_token[0] != '\0') {
        strncpy(cfg->token, env_token, sizeof(cfg->token) - 1);
        cfg->token[sizeof(cfg->token) - 1] = '\0';
        cfg->token_set = 1;
    }

    const char *env_url = getenv("CB_BASE_URL");
    if (env_url && env_url[0] != '\0') {
        strncpy(cfg->base_url, env_url, sizeof(cfg->base_url) - 1);
        cfg->base_url[sizeof(cfg->base_url) - 1] = '\0';
    }

    /* 3. Check token is set */
    if (!cfg->token_set) {
        snprintf(error_out, error_sz,
                 "no token found. Set CB_TOKEN env var or create config file with token = \"...\"");
        return -1;
    }

    return 0;
}

void config_apply_cli_override(Config *cfg, const char *cli_base_url)
{
    if (!cli_base_url || cli_base_url[0] == '\0')
        return;
    strncpy(cfg->base_url, cli_base_url, sizeof(cfg->base_url) - 1);
    cfg->base_url[sizeof(cfg->base_url) - 1] = '\0';
}

int config_parse_url(const char *url, char *host_out, size_t host_sz,
                     int *port_out, int *use_tls_out,
                     char *path_prefix_out, size_t prefix_sz)
{
    if (!url || !host_out || !port_out || !use_tls_out || !path_prefix_out)
        return -1;

    /* Parse scheme */
    int use_tls = 0;
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) {
        use_tls = 1;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        use_tls = 0;
        p += 7;
    } else {
        return -1;
    }

    /* Default port */
    int port = use_tls ? 443 : 80;

    /* Extract host (and optional port) */
    const char *host_start = p;
    const char *slash = strchr(p, '/');
    const char *colon = NULL;

    /* Find colon before slash (port separator) */
    const char *scan = p;
    while (*scan && *scan != '/' && *scan != ':')
        scan++;
    if (*scan == ':')
        colon = scan;

    size_t host_len;
    if (colon) {
        host_len = (size_t)(colon - host_start);
        port = atoi(colon + 1);
    } else {
        host_len = slash ? (size_t)(slash - host_start) : strlen(host_start);
    }

    if (host_len == 0 || host_len >= host_sz)
        return -1;

    memcpy(host_out, host_start, host_len);
    host_out[host_len] = '\0';

    *port_out = port;
    *use_tls_out = use_tls;

    /* Extract path prefix */
    if (slash) {
        size_t prefix_len = strlen(slash);
        if (prefix_len >= prefix_sz)
            prefix_len = prefix_sz - 1;
        memcpy(path_prefix_out, slash, prefix_len);
        path_prefix_out[prefix_len] = '\0';
    } else {
        path_prefix_out[0] = '\0';
    }

    return 0;
}
