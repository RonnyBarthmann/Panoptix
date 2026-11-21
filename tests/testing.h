/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Winziges Test-Gerüst (keine Abhängigkeit). Nutzung:
 *   TEST(name) { CHECK(cond); CHECK_EQ_INT(a, b); CHECK_NEAR(a, b, eps); }
 * Registrierung: in tests/test_main.c via RUN(name). */
#ifndef PNX_TESTING_H
#define PNX_TESTING_H

#include <math.h>
#include <stdio.h>

extern int g_test_failures;
extern int g_test_checks;

#define TEST(name) void test_##name(void)
#define CHECK(cond)                                                                               \
    do {                                                                                          \
        g_test_checks++;                                                                          \
        if (!(cond)) {                                                                            \
            g_test_failures++;                                                                    \
            printf("  FEHLER %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
        }                                                                                         \
    } while (0)
#define CHECK_EQ_INT(a, b)                                                                        \
    do {                                                                                          \
        long long va_ = (long long)(a), vb_ = (long long)(b);                                      \
        g_test_checks++;                                                                          \
        if (va_ != vb_) {                                                                         \
            g_test_failures++;                                                                    \
            printf("  FEHLER %s:%d: %s == %s  (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_); \
        }                                                                                         \
    } while (0)
#define CHECK_NEAR(a, b, eps)                                                                     \
    do {                                                                                          \
        double va_ = (double)(a), vb_ = (double)(b);                                               \
        g_test_checks++;                                                                          \
        if (fabs(va_ - vb_) > (double)(eps)) {                                                    \
            g_test_failures++;                                                                    \
            printf("  FEHLER %s:%d: %s ~ %s  (%f vs %f)\n", __FILE__, __LINE__, #a, #b, va_, vb_);  \
        }                                                                                         \
    } while (0)

#endif
