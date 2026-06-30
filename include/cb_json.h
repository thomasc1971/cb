#ifndef CB_JSON_H
#define CB_JSON_H

#include <stdbool.h>
#include <stddef.h>

typedef enum
{
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue
{
    JsonType type;
    union
    {
        bool boolean;
        double number;
        char *string;
        struct
        {
            struct JsonValue **items;
            size_t count;
        } array;
        struct
        {
            char **keys;
            struct JsonValue **values;
            size_t count;
        } object;
    };
} JsonValue;

/* Parser: returns NULL on failure. error_out (optional) receives a static error string. */
JsonValue *json_parse(const char *str, const char **error_out);

/* Deep free */
void json_free(JsonValue *v);

/* Type check helpers */
bool json_is_null(const JsonValue *v);
bool json_is_bool(const JsonValue *v);
bool json_is_number(const JsonValue *v);
bool json_is_string(const JsonValue *v);
bool json_is_array(const JsonValue *v);
bool json_is_object(const JsonValue *v);

/* Accessors */
bool json_bool(const JsonValue *v);
double json_number(const JsonValue *v);
const char *json_string(const JsonValue *v);

/* Array helpers */
size_t json_array_count(const JsonValue *v);
JsonValue *json_array_get(const JsonValue *v, size_t index);

/* Object helpers */
size_t json_object_count(const JsonValue *v);
const char *json_object_key(const JsonValue *v, size_t index);
JsonValue *json_object_get(const JsonValue *v, size_t index);
JsonValue *json_object_lookup(const JsonValue *v, const char *key);

/* --- Builder API (for constructing request bodies) --- */

JsonValue *json_null_new(void);
JsonValue *json_bool_new(bool val);
JsonValue *json_number_new(double val);
JsonValue *json_string_new(const char *val);
JsonValue *json_array_new(void);
void json_array_push(JsonValue *arr, JsonValue *item);
JsonValue *json_object_new(void);
void json_object_set(JsonValue *obj, const char *key, JsonValue *val);

/* Set helpers that create the value for you */
void json_object_set_string(JsonValue *obj, const char *key, const char *val);
void json_object_set_number(JsonValue *obj, const char *key, double val);
void json_object_set_bool(JsonValue *obj, const char *key, bool val);
void json_object_set_null(JsonValue *obj, const char *key);

/* Serialize to string (caller frees). omit_null: if true, skip object keys with null values. */
char *json_serialize(const JsonValue *v, bool omit_null);

#endif /* CB_JSON_H */
