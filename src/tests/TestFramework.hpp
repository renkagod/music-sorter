#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <cmath>
#include <sstream>

namespace TestFramework {

struct TestCase {
    std::string name;
    std::string suite;
    std::function<void()> func;
};

class TestRegistry {
public:
    static TestRegistry& Instance() {
        static TestRegistry instance;
        return instance;
    }

    void Register(const std::string& suite, const std::string& name, std::function<void()> func) {
        m_tests.push_back({ name, suite, func });
    }

    const std::vector<TestCase>& GetTests() const { return m_tests; }

private:
    TestRegistry() = default;
    std::vector<TestCase> m_tests;
};

struct TestContext {
    int assertionsRun = 0;
    int assertionsFailed = 0;
    std::vector<std::string> failureMessages;
};

inline TestContext g_ctx;

struct TestRegistrar {
    TestRegistrar(const std::string& suite, const std::string& name, std::function<void()> func) {
        TestRegistry::Instance().Register(suite, name, func);
    }
};

} // namespace TestFramework

#define TF_CONCAT_INNER(a, b) a##b
#define TF_CONCAT(a, b) TF_CONCAT_INNER(a, b)

#define TEST_CASE_IMPL(suite, name, uid) \
    static void TF_CONCAT(test_func_, uid)(); \
    static TestFramework::TestRegistrar TF_CONCAT(test_registrar_, uid)(suite, name, TF_CONCAT(test_func_, uid)); \
    static void TF_CONCAT(test_func_, uid)()

#define TEST_CASE(suite, name) TEST_CASE_IMPL(suite, name, __COUNTER__)

#define ASSERT_TRUE(expr) \
    do { \
        TestFramework::g_ctx.assertionsRun++; \
        if (!(expr)) { \
            TestFramework::g_ctx.assertionsFailed++; \
            std::stringstream ss; \
            ss << "  [FAIL] " << __FILE__ << ":" << __LINE__ << " -> Expected TRUE: " #expr; \
            TestFramework::g_ctx.failureMessages.push_back(ss.str()); \
        } \
    } while(0)

#define ASSERT_FALSE(expr) \
    do { \
        TestFramework::g_ctx.assertionsRun++; \
        if (expr) { \
            TestFramework::g_ctx.assertionsFailed++; \
            std::stringstream ss; \
            ss << "  [FAIL] " << __FILE__ << ":" << __LINE__ << " -> Expected FALSE: " #expr; \
            TestFramework::g_ctx.failureMessages.push_back(ss.str()); \
        } \
    } while(0)

#define ASSERT_EQ(actual, expected) \
    do { \
        TestFramework::g_ctx.assertionsRun++; \
        auto valActual = (actual); \
        auto valExpected = (expected); \
        if (!(valActual == valExpected)) { \
            TestFramework::g_ctx.assertionsFailed++; \
            std::stringstream ss; \
            ss << "  [FAIL] " << __FILE__ << ":" << __LINE__ << " -> ASSERT_EQ failed: " #actual " (" << valActual << ") != " #expected " (" << valExpected << ")"; \
            TestFramework::g_ctx.failureMessages.push_back(ss.str()); \
        } \
    } while(0)

#define ASSERT_NE(actual, expected) \
    do { \
        TestFramework::g_ctx.assertionsRun++; \
        auto valActual = (actual); \
        auto valExpected = (expected); \
        if (valActual == valExpected) { \
            TestFramework::g_ctx.assertionsFailed++; \
            std::stringstream ss; \
            ss << "  [FAIL] " << __FILE__ << ":" << __LINE__ << " -> ASSERT_NE failed: " #actual " == " #expected; \
            TestFramework::g_ctx.failureMessages.push_back(ss.str()); \
        } \
    } while(0)

#define ASSERT_STR_EQ(actual, expected) \
    do { \
        TestFramework::g_ctx.assertionsRun++; \
        std::string sActual = (actual); \
        std::string sExpected = (expected); \
        if (sActual != sExpected) { \
            TestFramework::g_ctx.assertionsFailed++; \
            std::stringstream ss; \
            ss << "  [FAIL] " << __FILE__ << ":" << __LINE__ << " -> String mismatch:\n    Actual:   \"" << sActual << "\"\n    Expected: \"" << sExpected << "\""; \
            TestFramework::g_ctx.failureMessages.push_back(ss.str()); \
        } \
    } while(0)

#define ASSERT_NEAR(actual, expected, epsilon) \
    do { \
        TestFramework::g_ctx.assertionsRun++; \
        double dActual = (double)(actual); \
        double dExpected = (double)(expected); \
        double dEps = (double)(epsilon); \
        if (std::abs(dActual - dExpected) > dEps) { \
            TestFramework::g_ctx.assertionsFailed++; \
            std::stringstream ss; \
            ss << "  [FAIL] " << __FILE__ << ":" << __LINE__ << " -> ASSERT_NEAR failed: |" << dActual << " - " << dExpected << "| > " << dEps; \
            TestFramework::g_ctx.failureMessages.push_back(ss.str()); \
        } \
    } while(0)

#define ASSERT_GE(actual, expected) \
    do { \
        TestFramework::g_ctx.assertionsRun++; \
        if (!((actual) >= (expected))) { \
            TestFramework::g_ctx.assertionsFailed++; \
            std::stringstream ss; \
            ss << "  [FAIL] " << __FILE__ << ":" << __LINE__ << " -> Expected " #actual " >= " #expected; \
            TestFramework::g_ctx.failureMessages.push_back(ss.str()); \
        } \
    } while(0)

#define ASSERT_LE(actual, expected) \
    do { \
        TestFramework::g_ctx.assertionsRun++; \
        if (!((actual) <= (expected))) { \
            TestFramework::g_ctx.assertionsFailed++; \
            std::stringstream ss; \
            ss << "  [FAIL] " << __FILE__ << ":" << __LINE__ << " -> Expected " #actual " <= " #expected; \
            TestFramework::g_ctx.failureMessages.push_back(ss.str()); \
        } \
    } while(0)
