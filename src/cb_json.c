#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "cb_json.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== Parser ===== */

typedef struct
{
    const char *p;
    const char *end;
    const char *error;
} Parser;

static JsonValue *parse_value(Parser *ps);

static void skip_ws(Parser *ps)
{
    while (ps->p < ps->end) {
        char c = *ps->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            ps->p++;
        else
            break;
    }
}

static bool match_literal(Parser *ps, const char *lit)
{
    size_t len = strlen(lit);
    if ((size_t)(ps->end - ps->p) < len)
        return false;
    if (strncmp(ps->p, lit, len) != 0)
        return false;
    ps->p += len;
    return true;
}

static JsonValue *new_value(JsonType type)
{
    JsonValue *v = calloc(1, sizeof(JsonValue));
    if (!v)
        return NULL;
    v->type = type;
    return v;
}

static JsonValue *parse_null(Parser *ps)
{
    if (!match_literal(ps, "null")) {
        ps->error = "expected 'null'";
        return NULL;
    }
    return new_value(JSON_NULL);
}

static JsonValue *parse_true(Parser *ps)
{
    if (!match_literal(ps, "true")) {
        ps->error = "expected 'true'";
        return NULL;
    }
    JsonValue *v = new_value(JSON_BOOL);
    if (v)
        v->boolean = true;
    return v;
}

static JsonValue *parse_false(Parser *ps)
{
    if (!match_literal(ps, "false")) {
        ps->error = "expected 'false'";
        return NULL;
    }
    JsonValue *v = new_value(JSON_BOOL);
    if (v)
        v->boolean = false;
    return v;
}

static JsonValue *parse_number(Parser *ps)
{
    const char *start = ps->p;
    if (ps->p < ps->end && *ps->p == '-')
        ps->p++;
    while (ps->p < ps->end && ((*ps->p >= '0' && *ps->p <= '9') ||
                               *ps->p == '.' || *ps->p == 'e' || *ps->p == 'E' ||
                               *ps->p == '+' || *ps->p == '-'))
        ps->p++;

    size_t len = ps->p - start;
    char *buf = malloc(len + 1);
    if (!buf)
        return NULL;
    memcpy(buf, start, len);
    buf[len] = '\0';

    char *endptr = NULL;
    double val = strtod(buf, &endptr);
    if (endptr != buf + len) {
        ps->error = "invalid number";
        free(buf);
        return NULL;
    }
    free(buf);

    JsonValue *v = new_value(JSON_NUMBER);
    if (v)
        v->number = val;
    return v;
}

static char *parse_string_raw(Parser *ps)
{
    if (ps->p >= ps->end || *ps->p != '"') {
        ps->error = "expected '\"'";
        return NULL;
    }
    ps->p++; /* skip opening quote */

    /* First pass: compute decoded length */
    size_t cap = 16;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return NULL;

    while (ps->p < ps->end && *ps->p != '"') {
        char decoded;
        if (*ps->p == '\\') {
            ps->p++;
            if (ps->p >= ps->end) {
                ps->error = "unterminated escape";
                free(buf);
                return NULL;
            }
            char esc = *ps->p++;
            switch (esc) {
            case '"':
                decoded = '"';
                break;
            case '\\':
                decoded = '\\';
                break;
            case '/':
                decoded = '/';
                break;
            case 'n':
                decoded = '\n';
                break;
            case 't':
                decoded = '\t';
                break;
            case 'r':
                decoded = '\r';
                break;
            case 'b':
                decoded = '\b';
                break;
            case 'f':
                decoded = '\f';
                break;
            case 'u':
            {
                /* Parse 4 hex digits for \uXXXX */
                if (ps->end - ps->p < 4) {
                    ps->error = "incomplete \\u escape";
                    free(buf);
                    return NULL;
                }
                char hex[5] = { 0 };
                memcpy(hex, ps->p, 4);
                ps->p += 4;
                unsigned int cp = (unsigned int)strtoul(hex, NULL, 16);
                /* Encode as UTF-8 */
                if (cp < 0x80) {
                    decoded = (char)cp;
                } else {
                    /* Multi-byte: need space */
                    if (cp < 0x800) {
                        if (len + 2 > cap) {
                            cap = (len + 2) * 2;
                            buf = realloc(buf, cap);
                        }
                        buf[len++] = (char)(0xC0 | (cp >> 6));
                        buf[len++] = (char)(0x80 | (cp & 0x3F));
                        continue;
                    } else {
                        if (len + 3 > cap) {
                            cap = (len + 3) * 2;
                            buf = realloc(buf, cap);
                        }
                        buf[len++] = (char)(0xE0 | (cp >> 12));
                        buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        buf[len++] = (char)(0x80 | (cp & 0x3F));
                        continue;
                    }
                }
                break;
            }
            default:
                ps->error = "invalid escape character";
                free(buf);
                return NULL;
            }
        } else {
            decoded = *ps->p++;
        }

        if (len + 1 > cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf)
                return NULL;
        }
        buf[len++] = decoded;
    }

    if (ps->p >= ps->end) {
        ps->error = "unterminated string";
        free(buf);
        return NULL;
    }
    ps->p++; /* skip closing quote */

    buf = realloc(buf, len + 1);
    if (!buf) {
        return NULL;
    }
    buf[len] = '\0';
    return buf;
}

static JsonValue *parse_string(Parser *ps)
{
    char *s = parse_string_raw(ps);
    if (!s)
        return NULL;
    JsonValue *v = new_value(JSON_STRING);
    if (!v) {
        free(s);
        return NULL;
    }
    v->string = s;
    return v;
}

static JsonValue *parse_array(Parser *ps)
{
    ps->p++; /* skip '[' */
    skip_ws(ps);

    JsonValue *arr = new_value(JSON_ARRAY);
    if (!arr)
        return NULL;

    /* Empty array */
    if (ps->p < ps->end && *ps->p == ']') {
        ps->p++;
        return arr;
    }

    size_t cap = 4;
    arr->array.items = malloc(cap * sizeof(JsonValue *));
    if (!arr->array.items) {
        json_free(arr);
        return NULL;
    }

    for (;;) {
        skip_ws(ps);
        JsonValue *item = parse_value(ps);
        if (!item) {
            json_free(arr);
            return NULL;
        }

        if (arr->array.count >= cap) {
            cap *= 2;
            JsonValue **tmp = realloc(arr->array.items, cap * sizeof(JsonValue *));
            if (!tmp) {
                json_free(item);
                json_free(arr);
                return NULL;
            }
            arr->array.items = tmp;
        }
        arr->array.items[arr->array.count++] = item;

        skip_ws(ps);
        if (ps->p >= ps->end) {
            ps->error = "unterminated array";
            json_free(arr);
            return NULL;
        }
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == ']') {
            ps->p++;
            break;
        }
        ps->error = "expected ',' or ']'";
        json_free(arr);
        return NULL;
    }

    return arr;
}

static JsonValue *parse_object(Parser *ps)
{
    ps->p++; /* skip '{' */
    skip_ws(ps);

    JsonValue *obj = new_value(JSON_OBJECT);
    if (!obj)
        return NULL;

    /* Empty object */
    if (ps->p < ps->end && *ps->p == '}') {
        ps->p++;
        return obj;
    }

    size_t cap = 4;
    obj->object.keys = malloc(cap * sizeof(char *));
    obj->object.values = malloc(cap * sizeof(JsonValue *));
    if (!obj->object.keys || !obj->object.values) {
        json_free(obj);
        return NULL;
    }

    for (;;) {
        skip_ws(ps);
        char *key = parse_string_raw(ps);
        if (!key) {
            json_free(obj);
            return NULL;
        }

        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != ':') {
            ps->error = "expected ':' after key";
            free(key);
            json_free(obj);
            return NULL;
        }
        ps->p++; /* skip ':' */

        skip_ws(ps);
        JsonValue *val = parse_value(ps);
        if (!val) {
            free(key);
            json_free(obj);
            return NULL;
        }

        if (obj->object.count >= cap) {
            cap *= 2;
            char **tmpk = realloc(obj->object.keys, cap * sizeof(char *));
            JsonValue **tmpv = realloc(obj->object.values, cap * sizeof(JsonValue *));
            if (!tmpk || !tmpv) {
                free(key);
                json_free(val);
                json_free(obj);
                return NULL;
            }
            obj->object.keys = tmpk;
            obj->object.values = tmpv;
        }
        obj->object.keys[obj->object.count] = key;
        obj->object.values[obj->object.count] = val;
        obj->object.count++;

        skip_ws(ps);
        if (ps->p >= ps->end) {
            ps->error = "unterminated object";
            json_free(obj);
            return NULL;
        }
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == '}') {
            ps->p++;
            break;
        }
        ps->error = "expected ',' or '}'";
        json_free(obj);
        return NULL;
    }

    return obj;
}

static JsonValue *parse_value(Parser *ps)
{
    skip_ws(ps);
    if (ps->p >= ps->end) {
        ps->error = "unexpected end of input";
        return NULL;
    }

    char c = *ps->p;
    switch (c) {
    case 'n':
        return parse_null(ps);
    case 't':
        return parse_true(ps);
    case 'f':
        return parse_false(ps);
    case '"':
        return parse_string(ps);
    case '[':
        return parse_array(ps);
    case '{':
        return parse_object(ps);
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        return parse_number(ps);
    default:
        ps->error = "unexpected character";
        return NULL;
    }
}

JsonValue *json_parse(const char *str, const char **error_out)
{
    if (!str) {
        if (error_out)
            *error_out = "null input";
        return NULL;
    }

    Parser ps = {
        .p = str,
        .end = str + strlen(str),
        .error = NULL
    };

    JsonValue *v = parse_value(&ps);
    if (!v) {
        if (error_out)
            *error_out = ps.error ? ps.error : "parse error";
        return NULL;
    }

    /* Check for trailing content */
    skip_ws(&ps);
    if (ps.p != ps.end) {
        if (error_out)
            *error_out = "trailing content after value";
        json_free(v);
        return NULL;
    }

    if (error_out)
        *error_out = NULL;
    return v;
}

/* ===== Free ===== */

void json_free(JsonValue *v)
{
    if (!v)
        return;

    switch (v->type) {
    case JSON_STRING:
        free(v->string);
        break;
    case JSON_ARRAY:
        for (size_t i = 0; i < v->array.count; i++)
            json_free(v->array.items[i]);
        free(v->array.items);
        break;
    case JSON_OBJECT:
        for (size_t i = 0; i < v->object.count; i++) {
            free(v->object.keys[i]);
            json_free(v->object.values[i]);
        }
        free(v->object.keys);
        free(v->object.values);
        break;
    default:
        break;
    }
    free(v);
}

/* ===== Type checks ===== */

bool json_is_null(const JsonValue *v) { return v && v->type == JSON_NULL; }
bool json_is_bool(const JsonValue *v) { return v && v->type == JSON_BOOL; }
bool json_is_number(const JsonValue *v) { return v && v->type == JSON_NUMBER; }
bool json_is_string(const JsonValue *v) { return v && v->type == JSON_STRING; }
bool json_is_array(const JsonValue *v) { return v && v->type == JSON_ARRAY; }
bool json_is_object(const JsonValue *v) { return v && v->type == JSON_OBJECT; }

/* ===== Accessors ===== */

bool json_bool(const JsonValue *v)
{
    return v && v->type == JSON_BOOL ? v->boolean : false;
}

double json_number(const JsonValue *v)
{
    return v && v->type == JSON_NUMBER ? v->number : 0.0;
}

const char *json_string(const JsonValue *v)
{
    return v && v->type == JSON_STRING ? v->string : NULL;
}

/* ===== Array helpers ===== */

size_t json_array_count(const JsonValue *v)
{
    return v && v->type == JSON_ARRAY ? v->array.count : 0;
}

JsonValue *json_array_get(const JsonValue *v, size_t index)
{
    if (!v || v->type != JSON_ARRAY || index >= v->array.count)
        return NULL;
    return v->array.items[index];
}

/* ===== Object helpers ===== */

size_t json_object_count(const JsonValue *v)
{
    return v && v->type == JSON_OBJECT ? v->object.count : 0;
}

const char *json_object_key(const JsonValue *v, size_t index)
{
    if (!v || v->type != JSON_OBJECT || index >= v->object.count)
        return NULL;
    return v->object.keys[index];
}

JsonValue *json_object_get(const JsonValue *v, size_t index)
{
    if (!v || v->type != JSON_OBJECT || index >= v->object.count)
        return NULL;
    return v->object.values[index];
}

JsonValue *json_object_lookup(const JsonValue *v, const char *key)
{
    if (!v || v->type != JSON_OBJECT || !key)
        return NULL;
    for (size_t i = 0; i < v->object.count; i++) {
        if (strcmp(v->object.keys[i], key) == 0)
            return v->object.values[i];
    }
    return NULL;
}

/* ===== Builder API ===== */

JsonValue *json_null_new(void) { return new_value(JSON_NULL); }

JsonValue *json_bool_new(bool val)
{
    JsonValue *v = new_value(JSON_BOOL);
    if (v)
        v->boolean = val;
    return v;
}

JsonValue *json_number_new(double val)
{
    JsonValue *v = new_value(JSON_NUMBER);
    if (v)
        v->number = val;
    return v;
}

JsonValue *json_string_new(const char *val)
{
    JsonValue *v = new_value(JSON_STRING);
    if (!v)
        return NULL;
    v->string = strdup(val ? val : "");
    if (!v->string) {
        free(v);
        return NULL;
    }
    return v;
}

JsonValue *json_array_new(void)
{
    JsonValue *v = new_value(JSON_ARRAY);
    return v;
}

void json_array_push(JsonValue *arr, JsonValue *item)
{
    if (!arr || arr->type != JSON_ARRAY || !item)
        return;
    if (arr->array.count == 0) {
        arr->array.items = malloc(4 * sizeof(JsonValue *));
        if (!arr->array.items)
            return;
    } else if ((arr->array.count & (arr->array.count - 1)) == 0) {
        /* Grow when count is a power of 2 */
        size_t newcap = arr->array.count * 2;
        JsonValue **tmp = realloc(arr->array.items, newcap * sizeof(JsonValue *));
        if (!tmp)
            return;
        arr->array.items = tmp;
    }
    arr->array.items[arr->array.count++] = item;
}

JsonValue *json_object_new(void)
{
    return new_value(JSON_OBJECT);
}

void json_object_set(JsonValue *obj, const char *key, JsonValue *val)
{
    if (!obj || obj->type != JSON_OBJECT || !key || !val)
        return;

    /* Check if key already exists */
    for (size_t i = 0; i < obj->object.count; i++) {
        if (strcmp(obj->object.keys[i], key) == 0) {
            json_free(obj->object.values[i]);
            obj->object.values[i] = val;
            return;
        }
    }

    if (obj->object.count == 0) {
        obj->object.keys = malloc(4 * sizeof(char *));
        obj->object.values = malloc(4 * sizeof(JsonValue *));
        if (!obj->object.keys || !obj->object.values)
            return;
    } else if ((obj->object.count & (obj->object.count - 1)) == 0) {
        size_t newcap = obj->object.count * 2;
        char **tmpk = realloc(obj->object.keys, newcap * sizeof(char *));
        JsonValue **tmpv = realloc(obj->object.values, newcap * sizeof(JsonValue *));
        if (!tmpk || !tmpv)
            return;
        obj->object.keys = tmpk;
        obj->object.values = tmpv;
    }
    obj->object.keys[obj->object.count] = strdup(key);
    obj->object.values[obj->object.count] = val;
    obj->object.count++;
}

void json_object_set_string(JsonValue *obj, const char *key, const char *val)
{
    json_object_set(obj, key, json_string_new(val));
}

void json_object_set_number(JsonValue *obj, const char *key, double val)
{
    json_object_set(obj, key, json_number_new(val));
}

void json_object_set_bool(JsonValue *obj, const char *key, bool val)
{
    json_object_set(obj, key, json_bool_new(val));
}

void json_object_set_null(JsonValue *obj, const char *key)
{
    json_object_set(obj, key, json_null_new());
}

/* ===== Serializer ===== */

static void serialize_string(const char *s, FILE *f)
{
    fputc('"', f);
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '"':
            fputs("\\\"", f);
            break;
        case '\\':
            fputs("\\\\", f);
            break;
        case '\n':
            fputs("\\n", f);
            break;
        case '\t':
            fputs("\\t", f);
            break;
        case '\r':
            fputs("\\r", f);
            break;
        case '\b':
            fputs("\\b", f);
            break;
        case '\f':
            fputs("\\f", f);
            break;
        default:
            if ((unsigned char)*p < 0x20)
                fprintf(f, "\\u%04x", (unsigned char)*p);
            else
                fputc(*p, f);
        }
    }
    fputc('"', f);
}

static void serialize_value(const JsonValue *v, FILE *f, bool omit_null)
{
    if (!v) {
        fputs("null", f);
        return;
    }

    switch (v->type) {
    case JSON_NULL:
        fputs("null", f);
        break;
    case JSON_BOOL:
        fputs(v->boolean ? "true" : "false", f);
        break;
    case JSON_NUMBER:
    {
        /* Print integer without decimals */
        if (v->number == floor(v->number) && fabs(v->number) < 1e15)
            fprintf(f, "%lld", (long long)v->number);
        else
            fprintf(f, "%g", v->number);
        break;
    }
    case JSON_STRING:
        serialize_string(v->string, f);
        break;
    case JSON_ARRAY:
        fputc('[', f);
        for (size_t i = 0; i < v->array.count; i++) {
            if (i > 0)
                fputc(',', f);
            serialize_value(v->array.items[i], f, omit_null);
        }
        fputc(']', f);
        break;
    case JSON_OBJECT:
        fputc('{', f);
        {
            bool first = true;
            for (size_t i = 0; i < v->object.count; i++) {
                if (omit_null && v->object.values[i]->type == JSON_NULL)
                    continue;
                if (!first)
                    fputc(',', f);
                first = false;
                serialize_string(v->object.keys[i], f);
                fputc(':', f);
                serialize_value(v->object.values[i], f, omit_null);
            }
        }
        fputc('}', f);
        break;
    }
}

char *json_serialize(const JsonValue *v, bool omit_null)
{
    char *result = NULL;
    size_t size = 0;
    FILE *f = open_memstream(&result, &size);
    if (!f)
        return NULL;
    serialize_value(v, f, omit_null);
    fclose(f);
    return result;
}
