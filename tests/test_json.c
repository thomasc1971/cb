#include "cb_json.h"
#include "test_helpers.h"
#include <stdlib.h>

/* ===== Parsing tests ===== */

static void test_parse_null(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("null", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_null(v));
    ASSERT_NULL(err);
    json_free(v);
}

static void test_parse_true(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("true", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_bool(v));
    ASSERT_TRUE(json_bool(v));
    json_free(v);
}

static void test_parse_false(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("false", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_bool(v));
    ASSERT_FALSE(json_bool(v));
    json_free(v);
}

static void test_parse_integer(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("42", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_number(v));
    ASSERT_EQ((int)json_number(v), 42);
    json_free(v);
}

static void test_parse_negative(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("-1", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_number(v));
    ASSERT_EQ((int)json_number(v), -1);
    json_free(v);
}

static void test_parse_float(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("3.14", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_number(v));
    ASSERT_TRUE(json_number(v) > 3.13 && json_number(v) < 3.15);
    json_free(v);
}

static void test_parse_string(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("\"hello\"", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_string(v));
    ASSERT_STR_EQ(json_string(v), "hello");
    json_free(v);
}

static void test_parse_empty_string(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("\"\"", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_string(v));
    ASSERT_STR_EQ(json_string(v), "");
    json_free(v);
}

static void test_parse_escaped_string(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("\"hello\\nworld\"", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_string(v));
    ASSERT_STR_EQ(json_string(v), "hello\nworld");
    json_free(v);
}

static void test_parse_escaped_tab_quote_backslash(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("\"\\t\\\"\\\\\"", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_string(v));
    ASSERT_STR_EQ(json_string(v), "\t\"\\");
    json_free(v);
}

static void test_parse_empty_array(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("[]", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_array(v));
    ASSERT_EQ((long long)json_array_count(v), 0);
    json_free(v);
}

static void test_parse_array_of_numbers(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("[1, 2, 3]", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_array(v));
    ASSERT_EQ((long long)json_array_count(v), 3);
    ASSERT_EQ((int)json_number(json_array_get(v, 0)), 1);
    ASSERT_EQ((int)json_number(json_array_get(v, 1)), 2);
    ASSERT_EQ((int)json_number(json_array_get(v, 2)), 3);
    json_free(v);
}

static void test_parse_mixed_array(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("[1, \"two\", true, null]", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_array(v));
    ASSERT_EQ((long long)json_array_count(v), 4);
    ASSERT_TRUE(json_is_number(json_array_get(v, 0)));
    ASSERT_TRUE(json_is_string(json_array_get(v, 1)));
    ASSERT_TRUE(json_is_bool(json_array_get(v, 2)));
    ASSERT_TRUE(json_is_null(json_array_get(v, 3)));
    json_free(v);
}

static void test_parse_empty_object(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("{}", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_object(v));
    ASSERT_EQ((long long)json_object_count(v), 0);
    json_free(v);
}

static void test_parse_simple_object(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("{\"name\": \"test\", \"private\": true}", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_object(v));
    ASSERT_EQ((long long)json_object_count(v), 2);
    JsonValue *name = json_object_lookup(v, "name");
    ASSERT_NOT_NULL(name);
    ASSERT_STR_EQ(json_string(name), "test");
    JsonValue *priv = json_object_lookup(v, "private");
    ASSERT_NOT_NULL(priv);
    ASSERT_TRUE(json_bool(priv));
    json_free(v);
}

static void test_parse_nested(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("{\"a\": {\"b\": [1, 2]}}", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_object(v));
    JsonValue *a = json_object_lookup(v, "a");
    ASSERT_NOT_NULL(a);
    ASSERT_TRUE(json_is_object(a));
    JsonValue *b = json_object_lookup(a, "b");
    ASSERT_NOT_NULL(b);
    ASSERT_TRUE(json_is_array(b));
    ASSERT_EQ((long long)json_array_count(b), 2);
    json_free(v);
}

static void test_parse_whitespace(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("  {  \"x\"  :  1  }  ", &err);
    ASSERT_NOT_NULL(v);
    ASSERT_TRUE(json_is_object(v));
    JsonValue *x = json_object_lookup(v, "x");
    ASSERT_NOT_NULL(x);
    ASSERT_EQ((int)json_number(x), 1);
    json_free(v);
}

static void test_parse_missing_key(void)
{
    const char *err = NULL;
    JsonValue *v = json_object_lookup(NULL, "nope");
    ASSERT_NULL(v);
    (void)err;
}

/* ===== Malformed input tests ===== */

static void test_parse_incomplete_object(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("{", &err);
    ASSERT_NULL(v);
    ASSERT_NOT_NULL(err);
}

static void test_parse_trailing_comma_array(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("[1,]", &err);
    ASSERT_NULL(v);
    ASSERT_NOT_NULL(err);
}

static void test_parse_missing_value(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("{\"a\"}", &err);
    ASSERT_NULL(v);
    ASSERT_NOT_NULL(err);
}

static void test_parse_garbage(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("tru", &err);
    ASSERT_NULL(v);
    ASSERT_NOT_NULL(err);
}

static void test_parse_empty_string_input(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse("", &err);
    ASSERT_NULL(v);
    ASSERT_NOT_NULL(err);
}

static void test_parse_null_input(void)
{
    const char *err = NULL;
    JsonValue *v = json_parse(NULL, &err);
    ASSERT_NULL(v);
}

/* ===== Serialization tests ===== */

static void test_serialize_null(void)
{
    JsonValue *v = json_null_new();
    char *s = json_serialize(v, false);
    ASSERT_STR_EQ(s, "null");
    free(s);
    json_free(v);
}

static void test_serialize_bool(void)
{
    JsonValue *t = json_bool_new(true);
    char *s = json_serialize(t, false);
    ASSERT_STR_EQ(s, "true");
    free(s);
    json_free(t);

    JsonValue *f = json_bool_new(false);
    s = json_serialize(f, false);
    ASSERT_STR_EQ(s, "false");
    free(s);
    json_free(f);
}

static void test_serialize_number(void)
{
    JsonValue *v = json_number_new(42);
    char *s = json_serialize(v, false);
    ASSERT_STR_EQ(s, "42");
    free(s);
    json_free(v);
}

static void test_serialize_string(void)
{
    JsonValue *v = json_string_new("hello");
    char *s = json_serialize(v, false);
    ASSERT_STR_EQ(s, "\"hello\"");
    free(s);
    json_free(v);
}

static void test_serialize_escaped_string(void)
{
    JsonValue *v = json_string_new("hello\nworld");
    char *s = json_serialize(v, false);
    ASSERT_STR_EQ(s, "\"hello\\nworld\"");
    free(s);
    json_free(v);
}

static void test_serialize_array(void)
{
    JsonValue *arr = json_array_new();
    json_array_push(arr, json_number_new(1));
    json_array_push(arr, json_number_new(2));
    char *s = json_serialize(arr, false);
    ASSERT_STR_EQ(s, "[1,2]");
    free(s);
    json_free(arr);
}

static void test_serialize_empty_array(void)
{
    JsonValue *arr = json_array_new();
    char *s = json_serialize(arr, false);
    ASSERT_STR_EQ(s, "[]");
    free(s);
    json_free(arr);
}

static void test_serialize_object(void)
{
    JsonValue *obj = json_object_new();
    json_object_set_string(obj, "name", "test");
    json_object_set_bool(obj, "private", true);
    char *s = json_serialize(obj, false);
    /* Keys should appear in insertion order */
    ASSERT_STR_EQ(s, "{\"name\":\"test\",\"private\":true}");
    free(s);
    json_free(obj);
}

static void test_serialize_empty_object(void)
{
    JsonValue *obj = json_object_new();
    char *s = json_serialize(obj, false);
    ASSERT_STR_EQ(s, "{}");
    free(s);
    json_free(obj);
}

/* ===== Critical: omit_null behavior ===== */

static void test_serialize_omit_null(void)
{
    JsonValue *obj = json_object_new();
    json_object_set_string(obj, "name", "test");
    json_object_set_null(obj, "description"); /* should be omitted */
    json_object_set_bool(obj, "private", true);
    char *s = json_serialize(obj, true);
    ASSERT_STR_EQ(s, "{\"name\":\"test\",\"private\":true}");
    free(s);
    json_free(obj);
}

static void test_serialize_no_omit_null(void)
{
    JsonValue *obj = json_object_new();
    json_object_set_string(obj, "name", "test");
    json_object_set_null(obj, "description");
    char *s = json_serialize(obj, false);
    ASSERT_STR_EQ(s, "{\"name\":\"test\",\"description\":null}");
    free(s);
    json_free(obj);
}

/* ===== Builder + round-trip ===== */

static void test_roundtrip_object(void)
{
    JsonValue *obj = json_object_new();
    json_object_set_string(obj, "name", "myproj");
    json_object_set_number(obj, "stars", 12);
    json_object_set_bool(obj, "private", false);

    char *s = json_serialize(obj, false);
    ASSERT_NOT_NULL(s);

    const char *err = NULL;
    JsonValue *parsed = json_parse(s, &err);
    ASSERT_NOT_NULL(parsed);
    ASSERT_TRUE(json_is_object(parsed));
    ASSERT_STR_EQ(json_string(json_object_lookup(parsed, "name")), "myproj");
    ASSERT_EQ((int)json_number(json_object_lookup(parsed, "stars")), 12);
    ASSERT_FALSE(json_bool(json_object_lookup(parsed, "private")));

    free(s);
    json_free(parsed);
    json_free(obj);
}

int main(int argc, char *argv[])
{
    test_parse_args(argc, argv);
    printf("Running JSON parser tests:\n");

    RUN_TEST(test_parse_null);
    RUN_TEST(test_parse_true);
    RUN_TEST(test_parse_false);
    RUN_TEST(test_parse_integer);
    RUN_TEST(test_parse_negative);
    RUN_TEST(test_parse_float);
    RUN_TEST(test_parse_string);
    RUN_TEST(test_parse_empty_string);
    RUN_TEST(test_parse_escaped_string);
    RUN_TEST(test_parse_escaped_tab_quote_backslash);
    RUN_TEST(test_parse_empty_array);
    RUN_TEST(test_parse_array_of_numbers);
    RUN_TEST(test_parse_mixed_array);
    RUN_TEST(test_parse_empty_object);
    RUN_TEST(test_parse_simple_object);
    RUN_TEST(test_parse_nested);
    RUN_TEST(test_parse_whitespace);
    RUN_TEST(test_parse_missing_key);

    RUN_TEST(test_parse_incomplete_object);
    RUN_TEST(test_parse_trailing_comma_array);
    RUN_TEST(test_parse_missing_value);
    RUN_TEST(test_parse_garbage);
    RUN_TEST(test_parse_empty_string_input);
    RUN_TEST(test_parse_null_input);

    RUN_TEST(test_serialize_null);
    RUN_TEST(test_serialize_bool);
    RUN_TEST(test_serialize_number);
    RUN_TEST(test_serialize_string);
    RUN_TEST(test_serialize_escaped_string);
    RUN_TEST(test_serialize_array);
    RUN_TEST(test_serialize_empty_array);
    RUN_TEST(test_serialize_object);
    RUN_TEST(test_serialize_empty_object);

    RUN_TEST(test_serialize_omit_null);
    RUN_TEST(test_serialize_no_omit_null);

    RUN_TEST(test_roundtrip_object);

    TEST_SUMMARY();
}
