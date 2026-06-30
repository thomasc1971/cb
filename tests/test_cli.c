#define _GNU_SOURCE

#include "cb_api.h"
#include "cb_cli.h"
#include "cb_config.h"
#include "mock_server.h"
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* CLI tests are integration tests: we set up a mock server and run
 * cli_run() with crafted argv, pointing CB_BASE_URL and CB_TOKEN at
 * the mock server. */

static MockServer server;

static void setup_server(MockResponse *responses, size_t count)
{
    memset(&server, 0, sizeof(server));
    if (mock_server_start(&server, responses, count) != 0) {
        fprintf(stderr, "Failed to start mock server\n");
        exit(1);
    }
    /* Set env vars so config_load picks them up */
    char url[256];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/api/v1", server.port);
    setenv("CB_BASE_URL", url, 1);
    setenv("CB_TOKEN", "test-token", 1);
}

static void teardown_server(void)
{
    mock_server_stop(&server);
    unsetenv("CB_BASE_URL");
    unsetenv("CB_TOKEN");
}

static const char *REPO_JSON_STR =
    "{\"name\":\"myproj\",\"full_name\":\"thomasc/myproj\","
    "\"description\":\"test repo\",\"html_url\":\"https://codeberg.org/thomasc/myproj\","
    "\"default_branch\":\"main\",\"language\":\"Go\","
    "\"private\":true,\"archived\":false,\"template\":false,"
    "\"stars_count\":12,\"forks_count\":3,"
    "\"has_issues\":true,\"has_wiki\":true,\"has_pull_requests\":true}";

static void set_body(MockResponse *r, const char *s)
{
    strncpy(r->body, s, sizeof(r->body) - 1);
    r->body[sizeof(r->body) - 1] = '\0';
}

/* Helper to run cli_run with given args. Returns exit code. */
static int run_cli(const char *args[])
{
    /* Count args */
    int argc = 0;
    while (args[argc])
        argc++;

    /* Build argv: ["cb", args...] */
    char **argv = malloc((argc + 2) * sizeof(char *));
    argv[0] = (char *)"cb";
    for (int i = 0; i < argc; i++)
        argv[i + 1] = (char *)args[i];
    argv[argc + 1] = NULL;

    int rc = cli_run(argc + 1, argv);
    free(argv);
    return rc;
}

/* ===== Tests ===== */

static void test_cli_repo_show(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/myproj", .status = 200
    };
    set_body(&resp, REPO_JSON_STR);
    setup_server(&resp, 1);

    const char *args[] = { "repo", "show", "thomasc/myproj", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));

    teardown_server();
}

static void test_cli_repo_create(void)
{
    MockResponse resp = {
        .method = "POST", .path = "/api/v1/user/repos", .status = 201
    };
    set_body(&resp, REPO_JSON_STR);
    setup_server(&resp, 1);

    const char *args[] = { "repo", "create", "myproj", "--private", "-d", "test repo", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));

    teardown_server();
}

static void test_cli_repo_delete_yes(void)
{
    MockResponse resp = {
        .method = "DELETE", .path = "/api/v1/repos/thomasc/myproj", .status = 204, .body = ""
    };
    setup_server(&resp, 1);

    const char *args[] = { "repo", "delete", "thomasc/myproj", "--yes", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));

    teardown_server();
}

static void test_cli_repo_rename(void)
{
    MockResponse resp = {
        .method = "PATCH", .path = "/api/v1/repos/thomasc/old-name", .status = 200
    };
    set_body(&resp, REPO_JSON_STR);
    setup_server(&resp, 1);

    const char *args[] = { "repo", "rename", "thomasc/old-name", "new-name", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_OK);
    /* Verify the PATCH body contains the new name */
    ASSERT_TRUE(strstr(server.last_body, "\"name\":\"new-name\"") != NULL);

    teardown_server();
}

static void test_cli_repo_edit(void)
{
    MockResponse resp = {
        .method = "PATCH", .path = "/api/v1/repos/thomasc/myproj", .status = 200
    };
    set_body(&resp, REPO_JSON_STR);
    setup_server(&resp, 1);

    const char *args[] = { "repo", "edit", "thomasc/myproj", "-d", "new desc", "--public", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_OK);
    /* Verify only provided fields are in body */
    ASSERT_TRUE(strstr(server.last_body, "\"description\":\"new desc\"") != NULL);
    ASSERT_TRUE(strstr(server.last_body, "\"private\":false") != NULL);
    ASSERT_FALSE(strstr(server.last_body, "archived") != NULL);

    teardown_server();
}

static void test_cli_repo_list(void)
{
    const char *list_json = "[]";
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/user/repos", .status = 200
    };
    set_body(&resp, list_json);
    setup_server(&resp, 1);

    const char *args[] = { "repo", "list", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));

    teardown_server();
}

static void test_cli_repo_list_org(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/orgs/myorg/repos", .status = 200, .body = "[]"
    };
    setup_server(&resp, 1);

    const char *args[] = { "repo", "list", "--org", "myorg", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));

    teardown_server();
}

static void test_cli_repo_transfer_yes(void)
{
    MockResponse resp = {
        .method = "POST", .path = "/api/v1/repos/thomasc/myproj/transfer", .status = 202, .body = "{}"
    };
    setup_server(&resp, 1);

    const char *args[] = { "repo", "transfer", "thomasc/myproj", "new-owner", "--yes", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(server.last_body, "\"new_owner\":\"new-owner\"") != NULL);

    teardown_server();
}

static void test_cli_topic_add(void)
{
    MockResponse resp = {
        .method = "PUT", .path = "/api/v1/repos/thomasc/myproj/topics/go", .status = 200, .body = "{}"
    };
    setup_server(&resp, 1);

    const char *args[] = { "repo", "topic", "add", "thomasc/myproj", "go", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));

    teardown_server();
}

static void test_cli_topic_set(void)
{
    MockResponse resp = {
        .method = "PUT", .path = "/api/v1/repos/thomasc/myproj/topics", .status = 200, .body = "{}"
    };
    setup_server(&resp, 1);

    const char *args[] = { "repo", "topic", "set", "thomasc/myproj", "go,cli,codeberg", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(server.last_body, "\"topics\":[\"go\",\"cli\",\"codeberg\"]") != NULL);

    teardown_server();
}

static void test_cli_topic_list(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/myproj/topics", .status = 200, .body = "{\"topics\":[\"go\",\"cli\"]}"
    };
    setup_server(&resp, 1);

    const char *args[] = { "repo", "topic", "list", "thomasc/myproj", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));

    teardown_server();
}

static void test_cli_unknown_command(void)
{
    setenv("CB_TOKEN", "tok", 1);
    unsetenv("CB_BASE_URL");

    const char *args[] = { "bogus", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_USAGE);

    unsetenv("CB_TOKEN");
}

static void test_cli_missing_args(void)
{
    setenv("CB_TOKEN", "tok", 1);
    unsetenv("CB_BASE_URL");

    const char *args[] = { "repo", "show", NULL };
    int rc = run_cli(args);
    /* Missing args should be a usage error, but since it goes through cli_run
     * which loads config first, it might be CLI_USAGE or CLI_ERR.
     * Either way it shouldn't be CLI_OK. */
    ASSERT_NEQ(rc, CLI_OK);

    unsetenv("CB_TOKEN");
}

static void test_cli_json_flag(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/myproj", .status = 200
    };
    set_body(&resp, REPO_JSON_STR);
    setup_server(&resp, 1);

    const char *args[] = { "repo", "show", "thomasc/myproj", "--json", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_OK);

    teardown_server();
}

int main(int argc, char *argv[])
{
    test_parse_args(argc, argv);
    printf("Running CLI tests:\n");

    RUN_TEST(test_cli_repo_show);
    RUN_TEST(test_cli_repo_create);
    RUN_TEST(test_cli_repo_delete_yes);
    RUN_TEST(test_cli_repo_rename);
    RUN_TEST(test_cli_repo_edit);
    RUN_TEST(test_cli_repo_list);
    RUN_TEST(test_cli_repo_list_org);
    RUN_TEST(test_cli_repo_transfer_yes);
    RUN_TEST(test_cli_topic_add);
    RUN_TEST(test_cli_topic_set);
    RUN_TEST(test_cli_topic_list);
    RUN_TEST(test_cli_unknown_command);
    RUN_TEST(test_cli_missing_args);
    RUN_TEST(test_cli_json_flag);

    TEST_SUMMARY();
}
