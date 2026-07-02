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

static void set_body(MockResponse *r, const char *s)
{
    strncpy(r->body, s, sizeof(r->body) - 1);
    r->body[sizeof(r->body) - 1] = '\0';
}

static const char *RUN_LIST_JSON =
    "{\"total_count\":2,\"workflow_runs\":["
    "{\"id\":5218403,\"index_in_repo\":2,\"title\":\"Add CI\","
    "\"status\":\"success\",\"event\":\"push\",\"workflow_id\":\"build.yml\","
    "\"prettyref\":\"master\",\"commit_sha\":\"86920ea\","
    "\"html_url\":\"https://codeberg.org/thomasc/cb/actions/runs/2\","
    "\"created\":\"2026-07-02T13:44:17+02:00\",\"started\":\"2026-07-02T13:46:21+02:00\","
    "\"stopped\":\"2026-07-02T13:47:19+02:00\"},"
    "{\"id\":5218298,\"index_in_repo\":1,\"title\":\"Initial CI\","
    "\"status\":\"failure\",\"event\":\"push\",\"workflow_id\":\"build.yml\","
    "\"prettyref\":\"master\",\"commit_sha\":\"32cb444\","
    "\"html_url\":\"https://codeberg.org/thomasc/cb/actions/runs/1\","
    "\"created\":\"2026-07-02T13:37:17+02:00\",\"started\":\"\",\"stopped\":\"\"}"
    "]}";

static const char *RUN_SINGLE_JSON =
    "{\"id\":5218403,\"index_in_repo\":2,\"title\":\"Add CI\","
    "\"status\":\"success\",\"event\":\"push\",\"workflow_id\":\"build.yml\","
    "\"prettyref\":\"master\",\"commit_sha\":\"86920ea\","
    "\"html_url\":\"https://codeberg.org/thomasc/cb/actions/runs/2\","
    "\"created\":\"2026-07-02T13:44:17+02:00\",\"started\":\"2026-07-02T13:46:21+02:00\","
    "\"stopped\":\"2026-07-02T13:47:19+02:00\"}";

static const char *RUNNER_LIST_JSON =
    "[{\"id\":1,\"name\":\"codeberg-small\",\"uuid\":\"abc-123\","
    "\"status\":\"online\",\"version\":\"5.0.0\"},"
    "{\"id\":2,\"name\":\"codeberg-tiny\",\"uuid\":\"def-456\","
    "\"status\":\"offline\",\"version\":\"5.0.0\"}]";

static const char *SECRET_LIST_JSON =
    "{\"data\":[{\"name\":\"RELEASE_TOKEN\"},{\"name\":\"DOCKER_PASSWORD\"}]}";

static const char *VARIABLE_LIST_JSON =
    "[{\"name\":\"BUILD_OPTS\",\"data\":\"-j4\"},"
    "{\"name\":\"DEPLOY_TARGET\",\"data\":\"staging\"}]";

static const char *VARIABLE_SINGLE_JSON =
    "{\"name\":\"BUILD_OPTS\",\"data\":\"-j4\"}";

/* ===== Run list ===== */

static void test_action_run_list_success(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/cb/actions/runs", .status = 200
    };
    set_body(&resp, RUN_LIST_JSON);
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    ActionRun *runs = NULL;
    size_t count = 0;
    int rc = api_action_run_list(&a, "thomasc", "cb", &runs, &count);
    ASSERT_EQ(rc, API_OK);
    ASSERT_EQ((int)count, 2);
    ASSERT_EQ((long long)runs[0].id, 5218403LL);
    ASSERT_EQ((long long)runs[0].index_in_repo, 2LL);
    ASSERT_STR_EQ(runs[0].title, "Add CI");
    ASSERT_STR_EQ(runs[0].status, "success");
    ASSERT_STR_EQ(runs[0].event, "push");
    ASSERT_STR_EQ(runs[0].workflow_id, "build.yml");
    ASSERT_STR_EQ(runs[0].prettyref, "master");
    ASSERT_STR_EQ(runs[1].status, "failure");
    ASSERT_STR_EQ(runs[1].started, "");
    action_run_array_free(runs, count);
    api_client_free(&a);
    teardown_server();
}

static void test_action_run_list_empty(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/cb/actions/runs", .status = 200
    };
    set_body(&resp, "{\"total_count\":0,\"workflow_runs\":[]}");
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    ActionRun *runs = NULL;
    size_t count = 0;
    int rc = api_action_run_list(&a, "thomasc", "cb", &runs, &count);
    ASSERT_EQ(rc, API_OK);
    ASSERT_EQ((int)count, 0);
    ASSERT_NULL(runs);
    action_run_array_free(runs, count);
    api_client_free(&a);
    teardown_server();
}

static void test_action_run_list_404(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/missing/actions/runs", .status = 404, .body = "{\"message\":\"repository not found\"}"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    ActionRun *runs = NULL;
    size_t count = 0;
    int rc = api_action_run_list(&a, "thomasc", "missing", &runs, &count);
    ASSERT_EQ(rc, API_ERR_NOT_FOUND);
    api_client_free(&a);
    teardown_server();
}

/* ===== Run show ===== */

static void test_action_run_show_success(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/cb/actions/runs/5218403", .status = 200
    };
    set_body(&resp, RUN_SINGLE_JSON);
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    ActionRun run;
    int rc = api_action_run_show(&a, "thomasc", "cb", 5218403, &run);
    ASSERT_EQ(rc, API_OK);
    ASSERT_EQ((long long)run.id, 5218403LL);
    ASSERT_STR_EQ(run.title, "Add CI");
    ASSERT_STR_EQ(run.status, "success");
    ASSERT_STR_EQ(run.workflow_id, "build.yml");
    action_run_free(&run);
    api_client_free(&a);
    teardown_server();
}

static void test_action_run_show_404(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/cb/actions/runs/999", .status = 404, .body = "{\"message\":\"run not found\"}"
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    ActionRun run;
    int rc = api_action_run_show(&a, "thomasc", "cb", 999, &run);
    ASSERT_EQ(rc, API_ERR_NOT_FOUND);
    api_client_free(&a);
    teardown_server();
}

/* ===== Runner list ===== */

static void test_action_runner_list_success(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/cb/actions/runners", .status = 200
    };
    set_body(&resp, RUNNER_LIST_JSON);
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    ActionRunner *runners = NULL;
    size_t count = 0;
    int rc = api_action_runner_list(&a, "thomasc", "cb", &runners, &count);
    ASSERT_EQ(rc, API_OK);
    ASSERT_EQ((int)count, 2);
    ASSERT_EQ((long long)runners[0].id, 1LL);
    ASSERT_STR_EQ(runners[0].name, "codeberg-small");
    ASSERT_STR_EQ(runners[0].status, "online");
    ASSERT_STR_EQ(runners[1].status, "offline");
    action_runner_array_free(runners, count);
    api_client_free(&a);
    teardown_server();
}

/* ===== Dispatch ===== */

static void test_action_dispatch_success(void)
{
    MockResponse resp = {
        .method = "POST",
        .path = "/api/v1/repos/thomasc/cb/actions/workflows/build.yml/dispatches",
        .status = 204,
        .body = ""
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    int rc = api_action_dispatch(&a, "thomasc", "cb", "build.yml", "master");
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));
    api_client_free(&a);
    teardown_server();
}

static void test_action_dispatch_null_ref(void)
{
    MockResponse resp = {
        .method = "POST",
        .path = "/api/v1/repos/thomasc/cb/actions/workflows/build.yml/dispatches",
        .status = 204,
        .body = ""
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    int rc = api_action_dispatch(&a, "thomasc", "cb", "build.yml", NULL);
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));
    api_client_free(&a);
    teardown_server();
}

/* ===== Secret list ===== */

static void test_action_secret_list_success(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/cb/actions/secrets", .status = 200
    };
    set_body(&resp, SECRET_LIST_JSON);
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    ActionSecret *secrets = NULL;
    size_t count = 0;
    int rc = api_action_secret_list(&a, "thomasc", "cb", &secrets, &count);
    ASSERT_EQ(rc, API_OK);
    ASSERT_EQ((int)count, 2);
    ASSERT_STR_EQ(secrets[0].name, "RELEASE_TOKEN");
    ASSERT_STR_EQ(secrets[1].name, "DOCKER_PASSWORD");
    action_secret_array_free(secrets, count);
    api_client_free(&a);
    teardown_server();
}

/* ===== Secret set ===== */

static void test_action_secret_set_success(void)
{
    MockResponse resp = {
        .method = "PUT", .path = "/api/v1/repos/thomasc/cb/actions/secrets/MY_SECRET", .status = 201, .body = ""
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    int rc = api_action_secret_set(&a, "thomasc", "cb", "MY_SECRET", "secret-value");
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));
    api_client_free(&a);
    teardown_server();
}

static void test_action_secret_set_null_value(void)
{
    ApiClient a;
    make_client(&a);
    int rc = api_action_secret_set(&a, "thomasc", "cb", "MY_SECRET", NULL);
    ASSERT_EQ(rc, API_ERR_VALIDATION);
    api_client_free(&a);
}

/* ===== Secret delete ===== */

static void test_action_secret_delete_success(void)
{
    MockResponse resp = {
        .method = "DELETE", .path = "/api/v1/repos/thomasc/cb/actions/secrets/MY_SECRET", .status = 204, .body = ""
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    int rc = api_action_secret_delete(&a, "thomasc", "cb", "MY_SECRET");
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));
    api_client_free(&a);
    teardown_server();
}

/* ===== Variable list ===== */

static void test_action_variable_list_success(void)
{
    MockResponse resp = {
        .method = "GET", .path = "/api/v1/repos/thomasc/cb/actions/variables", .status = 200
    };
    set_body(&resp, VARIABLE_LIST_JSON);
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    ActionVariable *vars = NULL;
    size_t count = 0;
    int rc = api_action_variable_list(&a, "thomasc", "cb", &vars, &count);
    ASSERT_EQ(rc, API_OK);
    ASSERT_EQ((int)count, 2);
    ASSERT_STR_EQ(vars[0].name, "BUILD_OPTS");
    ASSERT_STR_EQ(vars[0].data, "-j4");
    ASSERT_STR_EQ(vars[1].name, "DEPLOY_TARGET");
    ASSERT_STR_EQ(vars[1].data, "staging");
    action_variable_array_free(vars, count);
    api_client_free(&a);
    teardown_server();
}

/* ===== Variable show ===== */

static void test_action_variable_show_success(void)
{
    MockResponse resp = {
        .method = "GET",
        .path = "/api/v1/repos/thomasc/cb/actions/variables/BUILD_OPTS",
        .status = 200
    };
    set_body(&resp, VARIABLE_SINGLE_JSON);
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    ActionVariable var;
    int rc = api_action_variable_show(&a, "thomasc", "cb", "BUILD_OPTS", &var);
    ASSERT_EQ(rc, API_OK);
    ASSERT_STR_EQ(var.name, "BUILD_OPTS");
    ASSERT_STR_EQ(var.data, "-j4");
    action_variable_free(&var);
    api_client_free(&a);
    teardown_server();
}

/* ===== Variable set ===== */

static void test_action_variable_set_create(void)
{
    /* PUT returns 404 (doesn't exist), then POST creates it */
    MockResponse responses[2];
    memset(responses, 0, sizeof(responses));
    strncpy(responses[0].method, "PUT", sizeof(responses[0].method) - 1);
    strncpy(responses[0].path, "/api/v1/repos/thomasc/cb/actions/variables/NEW_VAR",
            sizeof(responses[0].path) - 1);
    responses[0].status = 404;
    strncpy(responses[0].body, "{\"message\":\"not found\"}", sizeof(responses[0].body) - 1);
    strncpy(responses[1].method, "POST", sizeof(responses[1].method) - 1);
    strncpy(responses[1].path, "/api/v1/repos/thomasc/cb/actions/variables/NEW_VAR",
            sizeof(responses[1].path) - 1);
    responses[1].status = 201;
    responses[1].body[0] = '\0';
    setup_server(responses, 2);

    ApiClient a;
    make_client(&a);
    int rc = api_action_variable_set(&a, "thomasc", "cb", "NEW_VAR", "hello");
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));
    api_client_free(&a);
    teardown_server();
}

static void test_action_variable_set_update(void)
{
    MockResponse resp = {
        .method = "PUT",
        .path = "/api/v1/repos/thomasc/cb/actions/variables/EXISTING",
        .status = 204,
        .body = ""
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    int rc = api_action_variable_set(&a, "thomasc", "cb", "EXISTING", "updated");
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));
    api_client_free(&a);
    teardown_server();
}

static void test_action_variable_set_null_value(void)
{
    ApiClient a;
    make_client(&a);
    int rc = api_action_variable_set(&a, "thomasc", "cb", "VAR", NULL);
    ASSERT_EQ(rc, API_ERR_VALIDATION);
    api_client_free(&a);
}

/* ===== Variable delete ===== */

static void test_action_variable_delete_success(void)
{
    MockResponse resp = {
        .method = "DELETE",
        .path = "/api/v1/repos/thomasc/cb/actions/variables/MY_VAR",
        .status = 204,
        .body = ""
    };
    setup_server(&resp, 1);

    ApiClient a;
    make_client(&a);
    int rc = api_action_variable_delete(&a, "thomasc", "cb", "MY_VAR");
    ASSERT_EQ(rc, API_OK);
    ASSERT_TRUE(mock_server_all_matched(&server));
    api_client_free(&a);
    teardown_server();
}

int main(int argc, char *argv[])
{
    test_parse_args(argc, argv);
    printf("Running actions tests:\n");

    RUN_TEST(test_action_run_list_success);
    RUN_TEST(test_action_run_list_empty);
    RUN_TEST(test_action_run_list_404);
    RUN_TEST(test_action_run_show_success);
    RUN_TEST(test_action_run_show_404);
    RUN_TEST(test_action_runner_list_success);
    RUN_TEST(test_action_dispatch_success);
    RUN_TEST(test_action_dispatch_null_ref);
    RUN_TEST(test_action_secret_list_success);
    RUN_TEST(test_action_secret_set_success);
    RUN_TEST(test_action_secret_set_null_value);
    RUN_TEST(test_action_secret_delete_success);
    RUN_TEST(test_action_variable_list_success);
    RUN_TEST(test_action_variable_show_success);
    RUN_TEST(test_action_variable_set_create);
    RUN_TEST(test_action_variable_set_update);
    RUN_TEST(test_action_variable_set_null_value);
    RUN_TEST(test_action_variable_delete_success);

    TEST_SUMMARY();
}
