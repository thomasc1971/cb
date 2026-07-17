#ifndef MOCK_SERVER_H
#define MOCK_SERVER_H

#include "cb_compat.h"
#include <pthread.h>
#include <stdbool.h>

/* A minimal HTTP mock server for testing.
 * Listens on an ephemeral port on localhost (plain HTTP, no TLS).
 * The test can configure canned responses per path+method.
 */

typedef struct
{
  char method[8];        /* "GET", "POST", etc. */
  char path[256];        /* e.g. "/api/v1/repos/owner/repo" */
  int status;            /* HTTP status to return */
  char body[4096];       /* Body to return */
  char auth_header[256]; /* Expected auth header value (empty = don't check) */
  bool matched;          /* Set when this response is consumed */
} MockResponse;

typedef struct
{
  int port;
  cb_socket_t sockfd;
  pthread_t thread;
  MockResponse* responses;
  size_t response_count;
  bool running;
  bool started;
  /* Captured request info (last request) */
  char last_method[8];
  char last_path[256];
  char last_body[4096];
  char last_auth[256];
  char last_content_type[128];
} MockServer;

/* Start the mock server. Returns 0 on success. */
int mock_server_start (MockServer* s, MockResponse* responses, size_t count);

/* Stop the mock server. */
void mock_server_stop (MockServer* s);

/* Check if all responses were matched. Returns true if all matched. */
bool mock_server_all_matched (MockServer* s);

#endif /* MOCK_SERVER_H */
