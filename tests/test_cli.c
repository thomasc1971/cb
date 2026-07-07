#include "config.h"
#include "cb_api.h"
#include "cb_cli.h"
#include "cb_compat.h"
#include "cb_config.h"
#include "mock_server.h"
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    cb_setenv("CB_BASE_URL", url, 1);
    cb_setenv("CB_TOKEN", "test-token", 1);
}

static void teardown_server(void)
{
    mock_server_stop(&server);
    cb_unsetenv("CB_BASE_URL");
    cb_unsetenv("CB_TOKEN");
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
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");

    const char *args[] = { "bogus", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_USAGE);

    cb_unsetenv("CB_TOKEN");
}

static void test_cli_missing_args(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");

    const char *args[] = { "repo", "show", NULL };
    int rc = run_cli(args);
    /* Missing args should be a usage error, but since it goes through cli_run
     * which loads config first, it might be CLI_USAGE or CLI_ERR.
     * Either way it shouldn't be CLI_OK. */
    ASSERT_NEQ(rc, CLI_OK);

    cb_unsetenv("CB_TOKEN");
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

/* ===== Help tests ===== */

/* Run cli_run with stdout captured into a buffer. Returns exit code. */
static int run_cli_captured(const char *args[], char *buf, size_t bufsize)
{
    memset(buf, 0, bufsize);
    fflush(stdout);

    /* Save original stdout fd and redirect to a tmpfile */
    int saved_fd = cb_dup(fileno(stdout));
    if (saved_fd < 0)
        return -1;

    FILE *tmpf = tmpfile();
    if (!tmpf) {
        cb_close(saved_fd);
        return -1;
    }

    if (cb_dup2(fileno(tmpf), fileno(stdout)) < 0) {
        fclose(tmpf);
        cb_close(saved_fd);
        return -1;
    }

    int argc = 0;
    while (args[argc])
        argc++;
    char **argv = malloc((argc + 2) * sizeof(char *));
    argv[0] = (char *)"cb";
    for (int i = 0; i < argc; i++)
        argv[i + 1] = (char *)args[i];
    argv[argc + 1] = NULL;

    int rc = cli_run(argc + 1, argv);
    free(argv);

    fflush(stdout);

    /* Restore original stdout */
    cb_dup2(saved_fd, fileno(stdout));
    cb_close(saved_fd);

    /* Read captured output from tmpfile */
    fseek(tmpf, 0, SEEK_SET);
    size_t n = fread(buf, 1, bufsize - 1, tmpf);
    buf[n] = '\0';
    fclose(tmpf);
    return rc;
}

static void test_help_top_level(void)
{
    char buf[4096];
    const char *args[] = { "--help", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "cb — Codeberg") != NULL);
    ASSERT_TRUE(strstr(buf, "--help, -h") != NULL);
}

static void test_help_top_level_short(void)
{
    char buf[4096];
    const char *args[] = { "-h", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "cb — Codeberg") != NULL);
}

static void test_help_repo(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");
    char buf[4096];
    const char *args[] = { "repo", "--help", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "Repository management.") != NULL);
    ASSERT_TRUE(strstr(buf, "create") != NULL);
    ASSERT_TRUE(strstr(buf, "topic") != NULL);
    cb_unsetenv("CB_TOKEN");
}

static void test_help_repo_short(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");
    char buf[4096];
    const char *args[] = { "repo", "-h", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "Repository management.") != NULL);
    cb_unsetenv("CB_TOKEN");
}

static void test_help_repo_create(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");
    char buf[4096];
    const char *args[] = { "repo", "create", "--help", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "cb repo create <name>") != NULL);
    ASSERT_TRUE(strstr(buf, "--private") != NULL);
    ASSERT_TRUE(strstr(buf, "--license") != NULL);
    cb_unsetenv("CB_TOKEN");
}

static void test_help_repo_edit(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");
    char buf[4096];
    const char *args[] = { "repo", "edit", "-h", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "cb repo edit [owner/]repo") != NULL);
    ASSERT_TRUE(strstr(buf, "--website") != NULL);
    ASSERT_TRUE(strstr(buf, "--allow-squash") != NULL);
    cb_unsetenv("CB_TOKEN");
}

static void test_help_repo_delete(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");
    char buf[4096];
    const char *args[] = { "repo", "delete", "--help", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "cb repo delete") != NULL);
    ASSERT_TRUE(strstr(buf, "--yes") != NULL);
    cb_unsetenv("CB_TOKEN");
}

static void test_help_repo_rename(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");
    char buf[4096];
    const char *args[] = { "repo", "rename", "--help", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "cb repo rename") != NULL);
    cb_unsetenv("CB_TOKEN");
}

static void test_help_repo_show(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");
    char buf[4096];
    const char *args[] = { "repo", "show", "--help", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "cb repo show") != NULL);
    cb_unsetenv("CB_TOKEN");
}

static void test_help_repo_list(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");
    char buf[4096];
    const char *args[] = { "repo", "list", "--help", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "cb repo list") != NULL);
    ASSERT_TRUE(strstr(buf, "--org") != NULL);
    cb_unsetenv("CB_TOKEN");
}

static void test_help_repo_transfer(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");
    char buf[4096];
    const char *args[] = { "repo", "transfer", "--help", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "cb repo transfer") != NULL);
    cb_unsetenv("CB_TOKEN");
}

static void test_help_repo_topic(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");
    char buf[4096];
    const char *args[] = { "repo", "topic", "--help", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "Manage repository topics.") != NULL);
    ASSERT_TRUE(strstr(buf, "add") != NULL);
    ASSERT_TRUE(strstr(buf, "set") != NULL);
    cb_unsetenv("CB_TOKEN");
}

static void test_help_topic_add(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");
    char buf[4096];
    const char *args[] = { "repo", "topic", "add", "--help", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "cb repo topic add") != NULL);
    cb_unsetenv("CB_TOKEN");
}

static void test_help_topic_set(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");
    char buf[4096];
    const char *args[] = { "repo", "topic", "set", "-h", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "cb repo topic set") != NULL);
    cb_unsetenv("CB_TOKEN");
}

static void test_cli_org_create(void)
{
    MockResponse resp = {
        .method = "POST", .path = "/api/v1/orgs", .status = 201
    };
    set_body(&resp, "{\"id\":42,\"name\":\"myorg\",\"full_name\":\"myorg\","
                    "\"description\":\"test org\",\"visibility\":\"public\","
                    "\"avatar_url\":\"https://codeberg.org/avatars/42\"}");
    setup_server(&resp, 1);

    const char *args[] = { "org", "create", "myorg", "-d", "test org", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));

    teardown_server();
}

static void test_cli_org_create_visibility(void)
{
    MockResponse resp = {
        .method = "POST", .path = "/api/v1/orgs", .status = 201
    };
    set_body(&resp, "{\"id\":43,\"name\":\"privorg\",\"full_name\":\"privorg\","
                    "\"visibility\":\"private\","
                    "\"avatar_url\":\"https://codeberg.org/avatars/43\"}");
    setup_server(&resp, 1);

    const char *args[] = { "org", "create", "privorg", "--visibility", "private", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));

    teardown_server();
}

static void test_help_org_create(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");
    char buf[4096];
    const char *args[] = { "org", "create", "--help", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "cb org create <name>") != NULL);
    ASSERT_TRUE(strstr(buf, "--visibility") != NULL);
    cb_unsetenv("CB_TOKEN");
}

static void test_cli_org_create_no_args(void)
{
    cb_setenv("CB_TOKEN", "tok", 1);
    cb_unsetenv("CB_BASE_URL");
    const char *args[] = { "org", "create", NULL };
    int rc = run_cli(args);
    ASSERT_EQ(rc, CLI_USAGE);
    cb_unsetenv("CB_TOKEN");
}

/* ===== --quiet tests ===== */

static void test_cli_quiet_list(void)
{
    const char *list_json =
        "[{\"name\":\"myproj\",\"full_name\":\"thomasc/myproj\","
        "\"description\":\"test\",\"html_url\":\"https://codeberg.org/thomasc/myproj\","
        "\"private\":true,\"archived\":false,\"stars_count\":12,\"forks_count\":3}]";
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/user/repos", .status = 200
    };
    set_body(&resp, list_json);
    setup_server(&resp, 1);

    char buf[4096];
    const char *args[] = { "--quiet", "repo", "list", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "thomasc/myproj") != NULL);
    ASSERT_FALSE(strstr(buf, "private") != NULL);
    ASSERT_FALSE(strstr(buf, "stars") != NULL);

    teardown_server();
}

static void test_cli_quiet_show(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/myproj", .status = 200
    };
    set_body(&resp, REPO_JSON_STR);
    setup_server(&resp, 1);

    char buf[4096];
    const char *args[] = { "--quiet", "repo", "show", "thomasc/myproj", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_EQ((int)strlen(buf), 0);

    teardown_server();
}

static void test_cli_quiet_json_list(void)
{
    const char *list_json =
        "[{\"name\":\"myproj\",\"full_name\":\"thomasc/myproj\","
        "\"description\":\"test\",\"html_url\":\"https://codeberg.org/thomasc/myproj\","
        "\"private\":true,\"archived\":false,\"stars_count\":12,\"forks_count\":3}]";
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/user/repos", .status = 200
    };
    set_body(&resp, list_json);
    setup_server(&resp, 1);

    char buf[4096];
    const char *args[] = { "--quiet", "--json", "repo", "list", NULL };
    int rc = run_cli_captured(args, buf, sizeof(buf));
    ASSERT_EQ(rc, CLI_OK);
    ASSERT_TRUE(strstr(buf, "thomasc/myproj") != NULL);
    ASSERT_TRUE(strstr(buf, "\"private\"") != NULL);

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

    RUN_TEST(test_help_top_level);
    RUN_TEST(test_help_top_level_short);
    RUN_TEST(test_help_repo);
    RUN_TEST(test_help_repo_short);
    RUN_TEST(test_help_repo_create);
    RUN_TEST(test_help_repo_edit);
    RUN_TEST(test_help_repo_delete);
    RUN_TEST(test_help_repo_rename);
    RUN_TEST(test_help_repo_show);
    RUN_TEST(test_help_repo_list);
    RUN_TEST(test_help_repo_transfer);
    RUN_TEST(test_help_repo_topic);
    RUN_TEST(test_help_topic_add);
    RUN_TEST(test_help_topic_set);

    RUN_TEST(test_cli_org_create);
    RUN_TEST(test_cli_org_create_visibility);
    RUN_TEST(test_help_org_create);
    RUN_TEST(test_cli_org_create_no_args);

    RUN_TEST(test_cli_quiet_list);
    RUN_TEST(test_cli_quiet_show);
    RUN_TEST(test_cli_quiet_json_list);

    TEST_SUMMARY();
}
