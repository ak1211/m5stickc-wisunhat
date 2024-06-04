#include "EchonetLite.hpp"
#include <unity.h>
#include <variant>
#include <vector>

namespace test_instant_power {
void test_runner();
}
namespace test_instant_current {
void test_runner();
}

void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}

int runUnityTests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_instant_power::test_runner);
  RUN_TEST(test_instant_current::test_runner);
  return UNITY_END();
}

int main(void) { return runUnityTests(); }
