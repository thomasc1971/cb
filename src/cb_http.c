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

#include "cb_http.h"
#include "cb_compat.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/time.h>
#endif

#ifdef HAVE_LIBTLS
#include <tls.h>
#endif

const char *http_method_str(HttpMethod m)
{
    switch (m) {
    case HTTP_GET:
        return "GET";
    case HTTP_POST:
        return "POST";
    case HTTP_PUT:
        return "PUT";
    case HTTP_PATCH:
        return "PATCH";
    case HTTP_DELETE:
        return "DELETE";
    default:
        return "UNKNOWN";
    }
}

int http_client_init(HttpClient *c, const char *host, int port, int use_tls,
                     const char *token)
{
    memset(c, 0, sizeof(*c));
    c->host = strdup(host);
    if (!c->host)
        return -1;
    c->port = port;
    c->use_tls = use_tls;
    c->timeout_sec = 30;
    if (token) {
        c->token = strdup(token);
        if (!c->token) {
            free(c->host);
            return -1;
        }
    }
    return 0;
}

void http_client_free(HttpClient *c)
{
    if (!c)
        return;
    free(c->host);
    free(c->token);
    memset(c, 0, sizeof(*c));
}

void http_response_free(HttpResponse *resp)
{
    if (!resp)
        return;
    free(resp->body);
    resp->body = NULL;
    resp->body_len = 0;
}

/* Plain HTTP implementation using POSIX sockets */

static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n <= 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, char **body_out, size_t *len_out)
{
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return -1;

    for (;;) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) {
                free(buf);
                return -1;
            }
            buf = tmp;
        }
        ssize_t n = recv(fd, buf + len, cap - len - 1, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            free(buf);
            return -1;
        }
        if (n == 0)
            break;
        len += (size_t)n;
    }
    buf[len] = '\0';
    *body_out = buf;
    *len_out = len;
    return 0;
}

static int parse_response(const char *raw, size_t raw_len, HttpResponse *resp)
{
    /* Parse status line: HTTP/1.1 XXX ... */
    if (raw_len < 12) {
        snprintf(resp->error, sizeof(resp->error), "response too short");
        return -1;
    }

    /* Find status code */
    const char *p = raw;
    p = memchr(p, ' ', raw_len);
    if (!p) {
        snprintf(resp->error, sizeof(resp->error), "malformed status line");
        return -1;
    }
    p++;
    resp->status = atoi(p);

    /* Find end of headers */
    const char *body_start = NULL;
    const char *header_end = NULL;
    for (size_t i = 0; i + 3 < raw_len; i++) {
        if (raw[i] == '\r' && raw[i + 1] == '\n' && raw[i + 2] == '\r' && raw[i + 3] == '\n') {
            header_end = raw + i;
            body_start = raw + i + 4;
            break;
        }
    }
    if (!body_start) {
        for (size_t i = 0; i + 1 < raw_len; i++) {
            if (raw[i] == '\n' && raw[i + 1] == '\n') {
                header_end = raw + i;
                body_start = raw + i + 2;
                break;
            }
        }
    }
    if (!body_start) {
        snprintf(resp->error, sizeof(resp->error), "no header terminator");
        return -1;
    }

    /* Check for chunked transfer encoding */
    int is_chunked = 0;
    {
        const char *hdr = raw;
        const char *hend = header_end ? header_end : body_start;
        while (hdr < hend) {
            const char *eol = memchr(hdr, '\r', (size_t)(hend - hdr));
            if (!eol)
                eol = memchr(hdr, '\n', (size_t)(hend - hdr));
            if (!eol)
                eol = hend; /* last line before terminator */
            size_t line_len = (size_t)(eol - hdr);
            if (line_len > 18 && strncasecmp(hdr, "Transfer-Encoding:", 18) == 0) {
                const char *val = hdr + 18;
                while (val < eol && (*val == ' ' || *val == '\t'))
                    val++;
                if ((size_t)(eol - val) >= 7 && strncasecmp(val, "chunked", 7) == 0)
                    is_chunked = 1;
            }
            if (eol >= hend)
                break;
            hdr = eol + 1;
            if (*hdr == '\n')
                hdr++;
        }
    }

    const char *raw_body = body_start;
    size_t raw_body_len = raw_len - (size_t)(body_start - raw);

    if (is_chunked) {
        /* Decode chunked transfer encoding */
        size_t out_cap = raw_body_len;
        char *out = malloc(out_cap + 1);
        if (!out) {
            snprintf(resp->error, sizeof(resp->error), "out of memory");
            return -1;
        }
        size_t out_len = 0;
        const char *cp = raw_body;
        const char *cp_end = raw_body + raw_body_len;

        while (cp < cp_end) {
            /* Read chunk size line (hex) */
            const char *eol = memchr(cp, '\r', (size_t)(cp_end - cp));
            if (!eol)
                eol = memchr(cp, '\n', (size_t)(cp_end - cp));
            if (!eol)
                break;

            char sizebuf[32] = { 0 };
            size_t slen = (size_t)(eol - cp);
            if (slen >= sizeof(sizebuf))
                slen = sizeof(sizebuf) - 1;
            memcpy(sizebuf, cp, slen);
            /* Strip any chunk extensions (after ';') */
            char *semi = strchr(sizebuf, ';');
            if (semi)
                *semi = '\0';

            unsigned long chunk_size = strtoul(sizebuf, NULL, 16);
            /* Skip past the size line (CRLF) */
            cp = eol;
            if (*cp == '\r')
                cp++;
            if (cp < cp_end && *cp == '\n')
                cp++;

            if (chunk_size == 0)
                break;

            if (cp + chunk_size > cp_end)
                chunk_size = (unsigned long)(cp_end - cp);

            if (out_len + chunk_size > out_cap) {
                out_cap = (out_len + chunk_size) * 2;
                char *tmp = realloc(out, out_cap + 1);
                if (!tmp) {
                    free(out);
                    snprintf(resp->error, sizeof(resp->error), "out of memory");
                    return -1;
                }
                out = tmp;
            }
            memcpy(out + out_len, cp, chunk_size);
            out_len += chunk_size;
            cp += chunk_size;

            /* Skip trailing CRLF after chunk data */
            if (cp < cp_end && *cp == '\r')
                cp++;
            if (cp < cp_end && *cp == '\n')
                cp++;
        }

        out[out_len] = '\0';
        resp->body = out;
        resp->body_len = out_len;
        return 0;
    }

    /* Non-chunked: copy body as-is */
    resp->body = malloc(raw_body_len + 1);
    if (!resp->body) {
        snprintf(resp->error, sizeof(resp->error), "out of memory");
        return -1;
    }
    memcpy(resp->body, raw_body, raw_body_len);
    resp->body[raw_body_len] = '\0';
    resp->body_len = raw_body_len;
    return 0;
}

static int do_plain_http(HttpClient *c, HttpMethod method, const char *path,
                         const char *body, HttpResponse *resp)
{
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", c->port);

    int gai_rc = getaddrinfo(c->host, port_str, &hints, &res);
    if (gai_rc != 0) {
        snprintf(resp->error, sizeof(resp->error), "DNS error: %s", gai_strerror(gai_rc));
        return -1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        snprintf(resp->error, sizeof(resp->error), "socket: %s", strerror(errno));
        freeaddrinfo(res);
        return -1;
    }

    /* Set timeout */
    struct timeval tv = { .tv_sec = c->timeout_sec, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        snprintf(resp->error, sizeof(resp->error), "connect: %s", strerror(errno));
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    /* Build request */
    char *request = NULL;
    size_t req_len = 0;
    FILE *f = cb_open_memstream(&request, &req_len);
    if (!f) {
        snprintf(resp->error, sizeof(resp->error), "open_memstream failed");
        close(fd);
        return -1;
    }

    fprintf(f, "%s %s HTTP/1.1\r\n", http_method_str(method), path);
    fprintf(f, "Host: %s:%d\r\n", c->host, c->port);
    fprintf(f, "Connection: close\r\n");
    if (c->token) {
        fprintf(f, "Authorization: token %s\r\n", c->token);
    }
    if (body) {
        size_t body_len = strlen(body);
        fprintf(f, "Content-Type: application/json\r\n");
        fprintf(f, "Content-Length: %zu\r\n", body_len);
        fprintf(f, "\r\n");
        fwrite(body, 1, body_len, f);
    } else {
        fprintf(f, "\r\n");
    }
    cb_close_memstream(f);

    /* Send request */
    if (send_all(fd, request, req_len) < 0) {
        snprintf(resp->error, sizeof(resp->error), "send failed: %s", strerror(errno));
        free(request);
        close(fd);
        return -1;
    }
    free(request);

    /* Receive response */
    char *raw = NULL;
    size_t raw_len = 0;
    if (recv_all(fd, &raw, &raw_len) < 0) {
        snprintf(resp->error, sizeof(resp->error), "recv failed: %s", strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);

    int rc = parse_response(raw, raw_len, resp);
    free(raw);
    return rc;
}

/* TLS implementation using libtls */
#ifdef HAVE_LIBTLS
static int do_tls_http(HttpClient *c, HttpMethod method, const char *path,
                       const char *body, HttpResponse *resp)
{
    struct tls *ctx = tls_client();
    if (!ctx) {
        snprintf(resp->error, sizeof(resp->error), "tls_client() failed");
        return -1;
    }

    struct tls_config *config = tls_config_new();
    if (!config) {
        tls_free(ctx);
        snprintf(resp->error, sizeof(resp->error), "tls_config_new() failed");
        return -1;
    }
    tls_config_insecure_noverifycert(config);
    tls_config_insecure_noverifyname(config);

    if (tls_configure(ctx, config) < 0) {
        snprintf(resp->error, sizeof(resp->error), "tls_configure: %s", tls_error(ctx));
        tls_config_free(config);
        tls_free(ctx);
        return -1;
    }
    tls_config_free(config);

    /* Resolve and connect TCP socket ourselves so we can poll() on it */
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", c->port);

    if (getaddrinfo(c->host, port_str, &hints, &res) != 0) {
        snprintf(resp->error, sizeof(resp->error), "DNS resolution failed for %s", c->host);
        tls_free(ctx);
        return -1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        snprintf(resp->error, sizeof(resp->error), "socket: %s", strerror(errno));
        freeaddrinfo(res);
        tls_free(ctx);
        return -1;
    }

    struct timeval tv = { .tv_sec = c->timeout_sec, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        snprintf(resp->error, sizeof(resp->error), "connect: %s", strerror(errno));
        close(fd);
        freeaddrinfo(res);
        tls_free(ctx);
        return -1;
    }
    freeaddrinfo(res);

    if (tls_connect_socket(ctx, fd, c->host) < 0) {
        snprintf(resp->error, sizeof(resp->error), "tls_connect_socket: %s", tls_error(ctx));
        close(fd);
        tls_free(ctx);
        return -1;
    }

    /* Build request */
    char *request = NULL;
    size_t req_len = 0;
    FILE *f = open_memstream(&request, &req_len);
    if (!f) {
        snprintf(resp->error, sizeof(resp->error), "open_memstream failed");
        tls_close(ctx);
        tls_free(ctx);
        return -1;
    }

    fprintf(f, "%s %s HTTP/1.1\r\n", http_method_str(method), path);
    fprintf(f, "Host: %s:%d\r\n", c->host, c->port);
    fprintf(f, "Connection: close\r\n");
    if (c->token) {
        fprintf(f, "Authorization: token %s\r\n", c->token);
    }
    if (body) {
        size_t body_len = strlen(body);
        fprintf(f, "Content-Type: application/json\r\n");
        fprintf(f, "Content-Length: %zu\r\n", body_len);
        fprintf(f, "\r\n");
        fwrite(body, 1, body_len, f);
    } else {
        fprintf(f, "\r\n");
    }
    fclose(f);

    /* Send */
    size_t sent = 0;
    while (sent < req_len) {
        ssize_t n = tls_write(ctx, request + sent, req_len - sent);
        if (n == TLS_WANT_POLLIN || n == TLS_WANT_POLLOUT) {
            struct pollfd pfd = { .fd = fd, .events = (n == TLS_WANT_POLLIN) ? POLLIN : POLLOUT };
            poll(&pfd, 1, c->timeout_sec * 1000);
            continue;
        }
        if (n < 0) {
            snprintf(resp->error, sizeof(resp->error), "tls_write: %s", tls_error(ctx));
            free(request);
            tls_close(ctx);
            tls_free(ctx);
            close(fd);
            return -1;
        }
        if (n == 0)
            break;
        sent += (size_t)n;
    }
    free(request);

    /* Read response */
    size_t cap = 4096;
    size_t len = 0;
    char *raw = malloc(cap);
    if (!raw) {
        tls_close(ctx);
        tls_free(ctx);
        return -1;
    }

    for (;;) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *tmp = realloc(raw, cap);
            if (!tmp) {
                free(raw);
                tls_close(ctx);
                tls_free(ctx);
                close(fd);
                return -1;
            }
            raw = tmp;
        }
        ssize_t n = tls_read(ctx, raw + len, cap - len - 1);
        if (n == TLS_WANT_POLLIN || n == TLS_WANT_POLLOUT) {
            struct pollfd pfd = { .fd = fd, .events = (n == TLS_WANT_POLLIN) ? POLLIN : POLLOUT };
            poll(&pfd, 1, c->timeout_sec * 1000);
            continue;
        }
        if (n < 0) {
            snprintf(resp->error, sizeof(resp->error), "tls_read: %s", tls_error(ctx));
            free(raw);
            tls_close(ctx);
            tls_free(ctx);
            close(fd);
            return -1;
        }
        if (n == 0)
            break;
        len += (size_t)n;
    }
    raw[len] = '\0';

    tls_close(ctx);
    tls_free(ctx);
    close(fd);

    int rc = parse_response(raw, len, resp);
    free(raw);
    return rc;
}
#endif /* HAVE_LIBTLS */

int http_request(HttpClient *c, HttpMethod method, const char *path,
                 const char *body, HttpResponse *resp)
{
    memset(resp, 0, sizeof(*resp));

    if (!c || !path) {
        snprintf(resp->error, sizeof(resp->error), "null argument");
        return -1;
    }

#ifdef HAVE_LIBTLS
    if (c->use_tls) {
        return do_tls_http(c, method, path, body, resp);
    }
#endif
    (void)body;
    return do_plain_http(c, method, path, body, resp);
}
