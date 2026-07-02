#include "config.h"
#include "cb_api.h"
#include "mock_server.h"
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void make_client(ApiClient *a)
{
    char base_url[256];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%d/api/v1", server.port);
    api_client_init(a, base_url, "test-token");
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

/* ===== Show ===== */

static void test_repo_show_success(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/myproj", .status = 200
    };
    set_body(&resp, REPO_JSON_STR);
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    Repo r;
    int rc = api_repo_show(&a, "thomasc", "myproj", &r);
    ASSERT_EQ(rc, API_OK);
    ASSERT_STR_EQ(r.name, "myproj");
    ASSERT_STR_EQ(r.full_name, "thomasc/myproj");
    ASSERT_STR_EQ(r.description, "test repo");
    ASSERT_STR_EQ(r.html_url, "https://codeberg.org/thomasc/myproj");
    ASSERT_STR_EQ(r.default_branch, "main");
    ASSERT_STR_EQ(r.language, "Go");
    ASSERT_TRUE(r.private);
    ASSERT_FALSE(r.archived);
    ASSERT_EQ(r.stars, 12);
    ASSERT_EQ(r.forks, 3);
    ASSERT_TRUE(r.has_issues);
    repo_free(&r);
    api_client_free(&a);
    teardown_server();
}

static void test_repo_show_404(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/missing", .status = 404, .body = "{\"message\":\"repository not found\"}"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    Repo r;
    int rc = api_repo_show(&a, "thomasc", "missing", &r);
    ASSERT_EQ(rc, API_ERR_NOT_FOUND);
    ASSERT_TRUE(strlen(a.last_error) > 0);
    api_client_free(&a);
    teardown_server();
}

static void test_repo_show_401(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/myproj", .status = 401, .body = "{\"message\":\"invalid token\"}"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    Repo r;
    int rc = api_repo_show(&a, "thomasc", "myproj", &r);
    ASSERT_EQ(rc, API_ERR_AUTH);
    api_client_free(&a);
    teardown_server();
}

/* ===== Create ===== */

static void test_repo_create_success(void)
{
    MockResponse resp = {
        .method = "POST", .path = "/api/v1/user/repos", .status = 201
    };
    set_body(&resp, REPO_JSON_STR);
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    CreateRepoOpts opts = {
        .name = "myproj",
        .description = "test repo",
        .private_set = 1,
        .private_val = 1,
        .default_branch = "main"
    };
    Repo r;
    int rc = api_repo_create(&a, &opts, &r);
    ASSERT_EQ(rc, API_OK);
    ASSERT_STR_EQ(r.name, "myproj");
    ASSERT_TRUE(r.private);
    repo_free(&r);
    api_client_free(&a);
    teardown_server();
}

static void test_repo_create_org(void)
{
    MockResponse resp = {
        .method = "POST", .path = "/api/v1/orgs/myorg/repos", .status = 201
    };
    set_body(&resp, REPO_JSON_STR);
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    CreateRepoOpts opts = {
        .name = "myproj",
        .org = "myorg"
    };
    Repo r;
    int rc = api_repo_create(&a, &opts, &r);
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));
    repo_free(&r);
    api_client_free(&a);
    teardown_server();
}

static void test_repo_create_conflict(void)
{
    MockResponse resp = {
        .method = "POST", .path = "/api/v1/user/repos", .status = 409, .body = "{\"message\":\"repository already exists\"}"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    CreateRepoOpts opts = { .name = "myproj" };
    Repo r;
    int rc = api_repo_create(&a, &opts, &r);
    ASSERT_EQ(rc, API_ERR_CONFLICT);
    api_client_free(&a);
    teardown_server();
}

static void test_repo_create_quota(void)
{
    MockResponse resp = {
        .method = "POST", .path = "/api/v1/user/repos", .status = 422, .body = "{\"message\":\"quota exceeded\"}"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    CreateRepoOpts opts = { .name = "myproj" };
    Repo r;
    int rc = api_repo_create(&a, &opts, &r);
    ASSERT_EQ(rc, API_ERR_QUOTA);
    api_client_free(&a);
    teardown_server();
}

/* ===== Delete ===== */

static void test_repo_delete_success(void)
{
    MockResponse resp = {
        .method = "DELETE", .path = "/api/v1/repos/thomasc/myproj", .status = 204, .body = ""
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    int rc = api_repo_delete(&a, "thomasc", "myproj");
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));
    api_client_free(&a);
    teardown_server();
}

static void test_repo_delete_404(void)
{
    MockResponse resp = {
        .method = "DELETE", .path = "/api/v1/repos/thomasc/missing", .status = 404, .body = "{\"message\":\"not found\"}"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    int rc = api_repo_delete(&a, "thomasc", "missing");
    ASSERT_EQ(rc, API_ERR_NOT_FOUND);
    api_client_free(&a);
    teardown_server();
}

/* ===== Edit (critical: omit unset fields) ===== */

static void test_repo_edit_only_provided_fields(void)
{
    MockResponse resp = {
        .method = "PATCH", .path = "/api/v1/repos/thomasc/myproj", .status = 200
    };
    set_body(&resp, REPO_JSON_STR);
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    EditRepoOpts opts = { 0 };
    opts.desc_set = 1;
    opts.description = "new description";
    opts.private_set = 1;
    opts.private_val = 0; /* public */

    Repo r;
    int rc = api_repo_edit(&a, "thomasc", "myproj", &opts, &r);
    ASSERT_EQ(rc, API_OK);

    /* Verify the body only contains the two fields we set */
    /* The JSON should NOT contain "archived", "has_issues", etc. */
    ASSERT_TRUE(strstr(server.last_body, "\"description\":\"new description\"") != NULL);
    ASSERT_TRUE(strstr(server.last_body, "\"private\":false") != NULL);
    ASSERT_FALSE(strstr(server.last_body, "archived") != NULL);
    ASSERT_FALSE(strstr(server.last_body, "has_issues") != NULL);
    ASSERT_FALSE(strstr(server.last_body, "has_wiki") != NULL);

    repo_free(&r);
    api_client_free(&a);
    teardown_server();
}

static void test_repo_edit_rename(void)
{
    MockResponse resp = {
        .method = "PATCH", .path = "/api/v1/repos/thomasc/old-name", .status = 200
    };
    set_body(&resp, REPO_JSON_STR);
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    EditRepoOpts opts = { 0 };
    opts.name_set = 1;
    opts.name = "new-name";

    Repo r;
    int rc = api_repo_edit(&a, "thomasc", "old-name", &opts, &r);
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(strstr(server.last_body, "\"name\":\"new-name\"") != NULL);
    repo_free(&r);
    api_client_free(&a);
    teardown_server();
}

/* ===== List ===== */

static void test_repo_list_success(void)
{
    const char *list_json =
        "[{\"name\":\"proj1\",\"full_name\":\"thomasc/proj1\",\"private\":false,"
        "\"stars_count\":5,\"forks_count\":1,\"archived\":false,\"template\":false,"
        "\"has_issues\":true,\"has_wiki\":true,\"has_pull_requests\":true,"
        "\"html_url\":\"https://codeberg.org/thomasc/proj1\","
        "\"default_branch\":\"main\",\"language\":\"C\",\"description\":\"one\"},"
        "{\"name\":\"proj2\",\"full_name\":\"thomasc/proj2\",\"private\":true,"
        "\"stars_count\":0,\"forks_count\":0,\"archived\":true,\"template\":false,"
        "\"has_issues\":false,\"has_wiki\":false,\"has_pull_requests\":true,"
        "\"html_url\":\"https://codeberg.org/thomasc/proj2\","
        "\"default_branch\":\"main\",\"language\":\"Go\",\"description\":\"two\"}]";

    MockResponse resp = {
        .method = "GET", .path = "/api/v1/user/repos", .status = 200
    };
    set_body(&resp, list_json);
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    Repo *repos;
    size_t count;
    int rc = api_repo_list(&a, NULL, 0, &repos, &count);
    ASSERT_EQ(rc, API_OK);
    ASSERT_EQ((long long)count, 2);
    ASSERT_STR_EQ(repos[0].name, "proj1");
    ASSERT_FALSE(repos[0].private);
    ASSERT_STR_EQ(repos[1].name, "proj2");
    ASSERT_TRUE(repos[1].private);
    ASSERT_TRUE(repos[1].archived);
    repo_array_free(repos, count);
    api_client_free(&a);
    teardown_server();
}

static void test_repo_list_empty(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/user/repos", .status = 200, .body = "[]"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    Repo *repos;
    size_t count;
    int rc = api_repo_list(&a, NULL, 0, &repos, &count);
    ASSERT_EQ(rc, API_OK);
    ASSERT_EQ((long long)count, 0);
    repo_array_free(repos, count);
    api_client_free(&a);
    teardown_server();
}

static void test_repo_list_org(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/orgs/myorg/repos", .status = 200, .body = "[]"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    Repo *repos;
    size_t count;
    int rc = api_repo_list(&a, "myorg", 1, &repos, &count);
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));
    repo_array_free(repos, count);
    api_client_free(&a);
    teardown_server();
}

/* ===== Transfer ===== */

static void test_repo_transfer_success(void)
{
    MockResponse resp = {
        .method = "POST", .path = "/api/v1/repos/thomasc/myproj/transfer", .status = 202, .body = "{}"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    int rc = api_repo_transfer(&a, "thomasc", "myproj", "codeberg-org", NULL, 0);
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(strstr(server.last_body, "\"new_owner\":\"codeberg-org\"") != NULL);
    ASSERT_FALSE(strstr(server.last_body, "team_ids") != NULL);
    api_client_free(&a);
    teardown_server();
}

static void test_repo_transfer_with_teams(void)
{
    MockResponse resp = {
        .method = "POST", .path = "/api/v1/repos/thomasc/myproj/transfer", .status = 202, .body = "{}"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    int64_t teams[] = { 1, 2 };
    int rc = api_repo_transfer(&a, "thomasc", "myproj", "codeberg-org", teams, 2);
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(strstr(server.last_body, "\"new_owner\":\"codeberg-org\"") != NULL);
    ASSERT_TRUE(strstr(server.last_body, "\"team_ids\":[1,2]") != NULL);
    api_client_free(&a);
    teardown_server();
}

/* ===== Topics ===== */

static void test_topic_list(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/myproj/topics", .status = 200, .body = "{\"topics\":[\"go\",\"cli\",\"codeberg\"]}"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    char **topics;
    size_t count;
    int rc = api_topic_list(&a, "thomasc", "myproj", &topics, &count);
    ASSERT_EQ(rc, API_OK);
    ASSERT_EQ((long long)count, 3);
    ASSERT_STR_EQ(topics[0], "go");
    ASSERT_STR_EQ(topics[1], "cli");
    ASSERT_STR_EQ(topics[2], "codeberg");
    topic_array_free(topics, count);
    api_client_free(&a);
    teardown_server();
}

static void test_topic_set(void)
{
    MockResponse resp = {
        .method = "PUT", .path = "/api/v1/repos/thomasc/myproj/topics", .status = 200, .body = "{}"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    const char *topics[] = { "go", "cli", "codeberg" };
    int rc = api_topic_set(&a, "thomasc", "myproj", topics, 3);
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(strstr(server.last_body, "\"topics\":[\"go\",\"cli\",\"codeberg\"]") != NULL);
    api_client_free(&a);
    teardown_server();
}

static void test_topic_add(void)
{
    MockResponse resp = {
        .method = "PUT", .path = "/api/v1/repos/thomasc/myproj/topics/go", .status = 200, .body = "{}"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    int rc = api_topic_add(&a, "thomasc", "myproj", "go");
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));
    api_client_free(&a);
    teardown_server();
}

static void test_topic_remove(void)
{
    MockResponse resp = {
        .method = "DELETE", .path = "/api/v1/repos/thomasc/myproj/topics/go", .status = 204, .body = ""
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    int rc = api_topic_remove(&a, "thomasc", "myproj", "go");
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));
    api_client_free(&a);
    teardown_server();
}

/* ===== Scope error ===== */

static void test_scope_error(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/myproj", .status = 403, .body = "{\"message\":\"token does not have at least one of required scope(s): [write:organization]\"}"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    Repo r;
    int rc = api_repo_show(&a, "thomasc", "myproj", &r);
    ASSERT_EQ(rc, API_ERR_SCOPE);
    ASSERT_TRUE(strstr(a.last_error, "scope") != NULL);
    api_client_free(&a);
    teardown_server();
}

int main(int argc, char *argv[])
{
    test_parse_args(argc, argv);
    printf("Running API client tests:\n");

    RUN_TEST(test_repo_show_success);
    RUN_TEST(test_repo_show_404);
    RUN_TEST(test_repo_show_401);
    RUN_TEST(test_repo_create_success);
    RUN_TEST(test_repo_create_org);
    RUN_TEST(test_repo_create_conflict);
    RUN_TEST(test_repo_create_quota);
    RUN_TEST(test_repo_delete_success);
    RUN_TEST(test_repo_delete_404);
    RUN_TEST(test_repo_edit_only_provided_fields);
    RUN_TEST(test_repo_edit_rename);
    RUN_TEST(test_repo_list_success);
    RUN_TEST(test_repo_list_empty);
    RUN_TEST(test_repo_list_org);
    RUN_TEST(test_repo_transfer_success);
    RUN_TEST(test_repo_transfer_with_teams);
    RUN_TEST(test_topic_list);
    RUN_TEST(test_topic_set);
    RUN_TEST(test_topic_add);
    RUN_TEST(test_topic_remove);
    RUN_TEST(test_scope_error);

    TEST_SUMMARY();
}
