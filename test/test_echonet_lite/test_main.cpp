#include "EchonetLite.hpp"
#include <cstddef>
#include <unity.h>
#include <variant>
#include <vector>

void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}

EchonetLiteFrame frame_GetInstantaneousPower() {
  const uint8_t INSTANTANEOUS_POWER = static_cast<uint8_t>(
      ElectricityMeter::EchonetLiteEPC::Measured_instantaneous_power);
  EchonetLiteFrame frame{
      .ehd = EchonetLiteEHD,
      .tid = EchonetLiteTransactionId({0x12, 0x34}),
      .edata =
          {
              .seoj = HomeController::EchonetLiteEOJ,   // Home controller
              .deoj = ElectricityMeter::EchonetLiteEOJ, // Electricity meter
              .esv = EchonetLiteESV::Get,               // Get要求
              .opc = 1,                                 // 1つ
              .props =
                  {
                      {
                          .epc = INSTANTANEOUS_POWER, // instant watt
                          .pdc = 0,                   // EDT=0
                          .edt = {},                  // EDT is nothing
                      },
                  },
          },
  };
  return frame;
}

std::vector<uint8_t> octets_GetInstantaneousPower() {
  std::vector<uint8_t> ocetets{
      0x10, // EHD#0
      0x81, // EHD#1
      0x12, // TID#0
      0x34, // TID#1
            /** EDATA **/
      0x05, // SEOJ#0
      0xFF, // SEOJ#1
      0x01, // SEO1#2
      0x02, // DEOJ#0
      0x88, // DEOJ#1
      0x01, // DEO1#2
      0x62, // ESV
      0x01, // OPC
            /** PROPS **/
      0xE7, // EPC
      0x00, // EDT
  };
  return ocetets;
}

void test_deserialize(void) {
  auto source_frame = frame_GetInstantaneousPower();
  //
  std::vector<uint8_t> octets;
  auto result =
      EchonetLite::serializeFromEchonetLiteFrame(octets, source_frame);
  TEST_ASSERT_NOT_NULL(std::get_if<EchonetLite::SerializeOk>(&result));
  //
  TEST_ASSERT_EQUAL_MEMORY(octets.data(), octets_GetInstantaneousPower().data(),
                           octets.size());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_deserialize);
  return UNITY_END();
}
