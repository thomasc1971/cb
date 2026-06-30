#include "cb_http.h"
#include "mock_server.h"
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static MockServer server;

static void setup_server(MockResponse *responses, size_t count)
{
    memset(&server, 0, sizeof(server));
    if (mock_server_start(&server, responses, count) != 0) {
        fprintf(stderr, "Failed to start mock server\n");
        exit(1);
    }
}

static void teardown_server(void)
{
    mock_server_stop(&server);
}

static void test_get_200(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/test", .status = 200, .body = "{\"hello\":\"world\"}"
    };
    setup_server(&resp, 1);

    HttpClient c;
    http_client_init(&c, "127.0.0.1", server.port, 0, "test-token");
    HttpResponse r;
    int rc = http_request(&c, HTTP_GET, "/test", NULL, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.status, 200);
    ASSERT_STR_EQ(r.body, "{\"hello\":\"world\"}");
    ASSERT_TRUE(mock_server_all_matched(&server));
    http_response_free(&r);
    http_client_free(&c);
    teardown_server();
}

static void test_get_sends_auth_header(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/auth", .status = 200, .body = "{}", .auth_header = "token test-token-123"
    };
    setup_server(&resp, 1);

    HttpClient c;
    http_client_init(&c, "127.0.0.1", server.port, 0, "test-token-123");
    HttpResponse r;
    int rc = http_request(&c, HTTP_GET, "/auth", NULL, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.status, 200);
    ASSERT_TRUE(mock_server_all_matched(&server));
    http_response_free(&r);
    http_client_free(&c);
    teardown_server();
}

static void test_post_sends_body(void)
{
    MockResponse resp = {
        .method = "POST", .path = "/create", .status = 201, .body = "{\"id\":1}"
    };
    setup_server(&resp, 1);

    HttpClient c;
    http_client_init(&c, "127.0.0.1", server.port, 0, "tok");
    HttpResponse r;
    const char *body = "{\"name\":\"test\"}";
    int rc = http_request(&c, HTTP_POST, "/create", body, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.status, 201);
    ASSERT_STR_EQ(r.body, "{\"id\":1}");
    /* Verify the server received the body */
    ASSERT_STR_EQ(server.last_body, "{\"name\":\"test\"}");
    /* Verify Content-Type was set */
    ASSERT_TRUE(strstr(server.last_content_type, "application/json") != NULL);
    ASSERT_TRUE(mock_server_all_matched(&server));
    http_response_free(&r);
    http_client_free(&c);
    teardown_server();
}

static void test_patch_sends_body(void)
{
    MockResponse resp = {
        .method = "PATCH", .path = "/edit", .status = 200, .body = "{\"ok\":true}"
    };
    setup_server(&resp, 1);

    HttpClient c;
    http_client_init(&c, "127.0.0.1", server.port, 0, "tok");
    HttpResponse r;
    int rc = http_request(&c, HTTP_PATCH, "/edit", "{\"private\":true}", &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.status, 200);
    ASSERT_STR_EQ(server.last_body, "{\"private\":true}");
    ASSERT_TRUE(mock_server_all_matched(&server));
    http_response_free(&r);
    http_client_free(&c);
    teardown_server();
}

static void test_delete_returns_204(void)
{
    MockResponse resp = {
        .method = "DELETE", .path = "/rm", .status = 204, .body = ""
    };
    setup_server(&resp, 1);

    HttpClient c;
    http_client_init(&c, "127.0.0.1", server.port, 0, "tok");
    HttpResponse r;
    int rc = http_request(&c, HTTP_DELETE, "/rm", NULL, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.status, 204);
    ASSERT_TRUE(mock_server_all_matched(&server));
    http_response_free(&r);
    http_client_free(&c);
    teardown_server();
}

static void test_get_404(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/missing", .status = 404, .body = "{\"message\":\"not found\"}"
    };
    setup_server(&resp, 1);

    HttpClient c;
    http_client_init(&c, "127.0.0.1", server.port, 0, "tok");
    HttpResponse r;
    int rc = http_request(&c, HTTP_GET, "/missing", NULL, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.status, 404);
    ASSERT_TRUE(strstr(r.body, "not found") != NULL);
    http_response_free(&r);
    http_client_free(&c);
    teardown_server();
}

static void test_get_403_with_body(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/forbidden", .status = 403, .body = "{\"message\":\"token does not have required scope\"}"
    };
    setup_server(&resp, 1);

    HttpClient c;
    http_client_init(&c, "127.0.0.1", server.port, 0, "tok");
    HttpResponse r;
    int rc = http_request(&c, HTTP_GET, "/forbidden", NULL, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.status, 403);
    ASSERT_TRUE(strstr(r.body, "scope") != NULL);
    http_response_free(&r);
    http_client_free(&c);
    teardown_server();
}

static void test_connection_refused(void)
{
    /* Use a port that's almost certainly not listening */
    HttpClient c;
    http_client_init(&c, "127.0.0.1", 1, 0, "tok");
    HttpResponse r;
    int rc = http_request(&c, HTTP_GET, "/test", NULL, &r);
    ASSERT_EQ(rc, -1);
    ASSERT_EQ(r.status, 0);
    ASSERT_TRUE(strlen(r.error) > 0);
    http_response_free(&r);
    http_client_free(&c);
}

static void test_no_token_no_auth_header(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/public", .status = 200, .body = "{}"
    };
    setup_server(&resp, 1);

    HttpClient c;
    http_client_init(&c, "127.0.0.1", server.port, 0, NULL);
    HttpResponse r;
    int rc = http_request(&c, HTTP_GET, "/public", NULL, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.status, 200);
    /* Auth header should be empty when no token is set */
    ASSERT_EQ((long long)strlen(server.last_auth), 0);
    http_response_free(&r);
    http_client_free(&c);
    teardown_server();
}

static void test_large_response(void)
{
    /* Build a large JSON body (>4KB to test buffer growth) */
    static char large_body[6000];
    memset(large_body, 0, sizeof(large_body));
    strcpy(large_body, "{\"data\":\"");
    for (size_t i = 0; i < 5000; i++)
        strcat(large_body, "x");
    strcat(large_body, "\"}");

    MockResponse resp = {
        .method = "GET", .path = "/large", .status = 200, .body = ""
    };
    strncpy(resp.body, large_body, sizeof(resp.body) - 1);

    setup_server(&resp, 1);

    HttpClient c;
    http_client_init(&c, "127.0.0.1", server.port, 0, "tok");
    HttpResponse r;
    int rc = http_request(&c, HTTP_GET, "/large", NULL, &r);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(r.status, 200);
    ASSERT_TRUE(r.body_len > 4000);
    http_response_free(&r);
    http_client_free(&c);
    teardown_server();
}

int main(int argc, char *argv[])
{
    test_parse_args(argc, argv);
    printf("Running HTTP client tests:\n");

    RUN_TEST(test_get_200);
    RUN_TEST(test_get_sends_auth_header);
    RUN_TEST(test_post_sends_body);
    RUN_TEST(test_patch_sends_body);
    RUN_TEST(test_delete_returns_204);
    RUN_TEST(test_get_404);
    RUN_TEST(test_get_403_with_body);
    RUN_TEST(test_connection_refused);
    RUN_TEST(test_no_token_no_auth_header);
    RUN_TEST(test_large_response);

    TEST_SUMMARY();
}
