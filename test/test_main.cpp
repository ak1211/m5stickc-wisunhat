#include <unity.h>

void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}

void test_always_ok(void) { TEST_ASSERT_EQUAL(true, true); }

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_always_ok);
  return UNITY_END();
}
