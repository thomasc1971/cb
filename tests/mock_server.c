#include "config.h"
#include "mock_server.h"
#include "cb_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void handle_client (MockServer *s, cb_socket_t clientfd)
{
  char buf[8192];
  ssize_t n = recv (clientfd, buf, sizeof (buf) - 1, 0);
  if (n <= 0) {
    cb_close_socket (clientfd);
    return;
  }
  buf[n] = '\0';

  /* Parse request line: METHOD PATH HTTP/1.1 */
  char method[8] = { 0 };
  char path[256] = { 0 };
  sscanf (buf, "%7s %255s", method, path);

  /* Capture for test inspection */
  snprintf (s->last_method, sizeof (s->last_method), "%s", method);
  snprintf (s->last_path, sizeof (s->last_path), "%s", path);

  /* Extract headers */
  char *auth = NULL;
  char *content_type = NULL;
  char *body = NULL;

  /* Find end of headers */
  char *header_end = strstr (buf, "\r\n\r\n");
  if (header_end) {
    body = header_end + 4;
    /* Search for Authorization header */
    char *line = buf;
    while (line < header_end) {
      char *eol = strstr (line, "\r\n");
      if (!eol)
        break;
      if (strncasecmp (line, "Authorization:", 14) == 0) {
        char *v = line + 14;
        while (*v == ' ')
          v++;
        strncpy (s->last_auth, v, (size_t)(eol - v) < sizeof (s->last_auth) ? (size_t)(eol - v) : sizeof (s->last_auth) - 1);
        auth = s->last_auth;
      }
      if (strncasecmp (line, "Content-Type:", 13) == 0) {
        char *v = line + 13;
        while (*v == ' ')
          v++;
        strncpy (s->last_content_type, v, (size_t)(eol - v) < sizeof (s->last_content_type) ? (size_t)(eol - v) : sizeof (s->last_content_type) - 1);
        content_type = s->last_content_type;
      }
      line = eol + 2;
    }
  } else {
    header_end = strstr (buf, "\n\n");
    if (header_end) {
      body = header_end + 2;
      char *line = buf;
      while (line < header_end) {
        char *eol = strchr (line, '\n');
        if (!eol)
          break;
        if (strncasecmp (line, "Authorization:", 14) == 0) {
          char *v = line + 14;
          while (*v == ' ')
            v++;
          strncpy (s->last_auth, v, (size_t)(eol - v) < sizeof (s->last_auth) ? (size_t)(eol - v) : sizeof (s->last_auth) - 1);
          auth = s->last_auth;
        }
        if (strncasecmp (line, "Content-Type:", 13) == 0) {
          char *v = line + 13;
          while (*v == ' ')
            v++;
          strncpy (s->last_content_type, v, (size_t)(eol - v) < sizeof (s->last_content_type) ? (size_t)(eol - v) : sizeof (s->last_content_type) - 1);
          content_type = s->last_content_type;
        }
        line = eol + 1;
      }
    }
  }

  if (body) {
    strncpy (s->last_body, body, sizeof (s->last_body) - 1);
  }

  (void)auth;
  (void)content_type;

  /* Find matching response */
  MockResponse *match = NULL;
  for (size_t i = 0; i < s->response_count; i++) {
    if (!s->responses[i].matched && strcmp (s->responses[i].method, method) == 0 && strcmp (s->responses[i].path, path) == 0) {
      match = &s->responses[i];
      break;
    }
  }

  char resp[8192];
  if (match) {
    match->matched = true;
    /* Check auth if required */
    if (match->auth_header[0] != '\0') {
      if (!auth || strcmp (auth, match->auth_header) != 0) {
        snprintf (resp, sizeof (resp),
                  "HTTP/1.1 401 Unauthorized\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: 27\r\n"
                  "Connection: close\r\n"
                  "\r\n"
                  "{\"message\":\"unauthorized\"}");
        send (clientfd, resp, strlen (resp), 0);
        cb_close_socket (clientfd);
        return;
      }
    }
    size_t body_len = strlen (match->body);
    snprintf (resp, sizeof (resp),
              "HTTP/1.1 %d\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %zu\r\n"
              "Connection: close\r\n"
              "\r\n"
              "%s",
              match->status, body_len, match->body);
  } else {
    snprintf (resp, sizeof (resp),
              "HTTP/1.1 404 Not Found\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: 27\r\n"
              "Connection: close\r\n"
              "\r\n"
              "{\"message\":\"not found\"}");
  }

  send (clientfd, resp, strlen (resp), 0);
  cb_close_socket (clientfd);
}

static void *server_thread (void *arg)
{
  MockServer *s = (MockServer *)arg;
  while (s->running) {
    struct sockaddr_in client;
    socklen_t client_len = sizeof (client);
    cb_socket_t clientfd = accept (s->sockfd, (struct sockaddr *)&client, &client_len);
    if (clientfd == CB_INVALID_SOCKET) {
      if (s->running)
        continue;
      break;
    }
    handle_client (s, clientfd);
  }
  return NULL;
}

int mock_server_start (MockServer *s, MockResponse *responses, size_t count)
{
  memset (s, 0, sizeof (*s));
  s->responses = responses;
  s->response_count = count;

#ifdef _WIN32
  if (cb_wsa_startup () < 0)
    return -1;
#endif

  s->sockfd = socket (AF_INET, SOCK_STREAM, 0);
  if (s->sockfd == CB_INVALID_SOCKET)
    return -1;

  int opt = 1;
  setsockopt (s->sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof (opt));

  struct sockaddr_in addr;
  memset (&addr, 0, sizeof (addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr ("127.0.0.1");
  addr.sin_port = 0; /* ephemeral port */

  if (bind (s->sockfd, (struct sockaddr *)&addr, sizeof (addr)) == CB_SOCKET_ERROR) {
    cb_close_socket (s->sockfd);
    return -1;
  }

  socklen_t addr_len = sizeof (addr);
  getsockname (s->sockfd, (struct sockaddr *)&addr, &addr_len);
  s->port = ntohs (addr.sin_port);

  if (listen (s->sockfd, 5) == CB_SOCKET_ERROR) {
    cb_close_socket (s->sockfd);
    return -1;
  }

  s->started = true;
  s->running = true;
  if (pthread_create (&s->thread, NULL, server_thread, s) != 0) {
    cb_close_socket (s->sockfd);
    return -1;
  }

  return 0;
}

void mock_server_stop (MockServer *s)
{
  if (!s->started)
    return;
  s->running = false;
  shutdown (s->sockfd, CB_SHUT_RDWR);
  cb_close_socket (s->sockfd);
  pthread_join (s->thread, NULL);
  s->started = false;
}

bool mock_server_all_matched (MockServer *s)
{
  for (size_t i = 0; i < s->response_count; i++) {
    if (!s->responses[i].matched)
      return false;
  }
  return true;
}
