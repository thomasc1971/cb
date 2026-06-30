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

#ifndef CB_HTTP_H
#define CB_HTTP_H

#include <stddef.h>

typedef enum
{
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_PATCH,
    HTTP_DELETE
} HttpMethod;

typedef struct
{
    int status;      /* HTTP status code (0 on network error) */
    char *body;      /* Response body (null-terminated, caller frees) */
    size_t body_len; /* Response body length */
    char error[256]; /* Error message (empty on success) */
} HttpResponse;

typedef struct
{
    char *host;      /* e.g. "codeberg.org" */
    int port;        /* e.g. 443 or 80 */
    int use_tls;     /* 1 = TLS, 0 = plain */
    char *token;     /* Authorization token (may be NULL) */
    int timeout_sec; /* Connection/read timeout (default 30) */
} HttpClient;

/* Initialize client. Returns 0 on success, -1 on error. */
int http_client_init(HttpClient *c, const char *host, int port, int use_tls,
                     const char *token);

/* Free client resources. */
void http_client_free(HttpClient *c);

/* Perform an HTTP request.
 * path: e.g. "/api/v1/repos/owner/repo"
 * body: request body (may be NULL for GET/DELETE)
 * Returns 0 on success (even for 4xx/5xx — check response.status).
 * Returns -1 on network/connection error (check response.error).
 */
int http_request(HttpClient *c, HttpMethod method, const char *path,
                 const char *body, HttpResponse *resp);

/* Free response body. */
void http_response_free(HttpResponse *resp);

/* Convert method enum to string. */
const char *http_method_str(HttpMethod m);

#endif /* CB_HTTP_H */
