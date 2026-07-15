#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    tests_run++; \
    test_##name(); \
    printf("  ✓ %s\n", #name); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ✗ FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  ✗ FAIL: %s:%d: expected %d, got %d\n", \
               __FILE__, __LINE__, (int)(b), (int)(a)); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("  ✗ FAIL: %s:%d: expected \"%s\", got \"%s\"\n", \
               __FILE__, __LINE__, (b), (a)); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_STR_CONTAINS(haystack, needle) do { \
    if (strstr((haystack), (needle)) == NULL) { \
        printf("  ✗ FAIL: %s:%d: \"%s\" not found in \"%s\"\n", \
               __FILE__, __LINE__, (needle), (haystack)); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define REPORT() do { \
    printf("\n%d tests, %d failed\n", tests_run, tests_failed); \
    return tests_failed; \
} while(0)

#endif
