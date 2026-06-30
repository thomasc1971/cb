#ifndef CB_TEST_HELPERS_H
#define CB_TEST_HELPERS_H

#include <stdio.h>
#include <string.h>

static int test_pass_count = 0;
static int test_fail_count = 0;
static int verbose = 0;

#define ASSERT_TRUE(expr)                                                      \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            test_fail_count++;                                                 \
            return;                                                            \
        }                                                                      \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b)                                                          \
    do {                                                                         \
        if ((a) != (b)) {                                                        \
            fprintf(stderr, "  FAIL: %s:%d: %s == %s (%lld != %lld)\n",          \
                    __FILE__, __LINE__, #a, #b, (long long)(a), (long long)(b)); \
            test_fail_count++;                                                   \
            return;                                                              \
        }                                                                        \
    } while (0)

#define ASSERT_NEQ(a, b)                                                         \
    do {                                                                         \
        if ((a) == (b)) {                                                        \
            fprintf(stderr, "  FAIL: %s:%d: %s != %s (%lld == %lld)\n",          \
                    __FILE__, __LINE__, #a, #b, (long long)(a), (long long)(b)); \
            test_fail_count++;                                                   \
            return;                                                              \
        }                                                                        \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                                 \
    do {                                                                    \
        const char *_sa = (a);                                              \
        const char *_sb = (b);                                              \
        if (_sa == NULL || _sb == NULL || strcmp(_sa, _sb) != 0) {          \
            fprintf(stderr, "  FAIL: %s:%d: %s == %s (\"%s\" != \"%s\")\n", \
                    __FILE__, __LINE__, #a, #b,                             \
                    _sa ? _sa : "(null)", _sb ? _sb : "(null)");            \
            test_fail_count++;                                              \
            return;                                                         \
        }                                                                   \
    } while (0)

#define ASSERT_NULL(expr)                                      \
    do {                                                       \
        if ((expr) != NULL) {                                  \
            fprintf(stderr, "  FAIL: %s:%d: %s is not NULL\n", \
                    __FILE__, __LINE__, #expr);                \
            test_fail_count++;                                 \
            return;                                            \
        }                                                      \
    } while (0)

#define ASSERT_NOT_NULL(expr)                              \
    do {                                                   \
        if ((expr) == NULL) {                              \
            fprintf(stderr, "  FAIL: %s:%d: %s is NULL\n", \
                    __FILE__, __LINE__, #expr);            \
            test_fail_count++;                             \
            return;                                        \
        }                                                  \
    } while (0)

#define RUN_TEST(fn)                      \
    do {                                  \
        int _before = test_fail_count;    \
        fn();                             \
        if (test_fail_count == _before) { \
            printf("  PASS: %s\n", #fn);  \
            test_pass_count++;            \
        } else {                          \
            printf("  FAIL: %s\n", #fn);  \
        }                                 \
    } while (0)

#define TEST_SUMMARY()                            \
    do {                                          \
        printf("\n%d passed, %d failed\n",        \
               test_pass_count, test_fail_count); \
        return test_fail_count > 0 ? 1 : 0;       \
    } while (0)

static inline void test_parse_args(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "-v") == 0)
            verbose = 1;
}

#endif /* CB_TEST_HELPERS_H */
