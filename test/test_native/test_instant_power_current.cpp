#include "EchonetLite.hpp"
#include <unity.h>
#include <variant>
#include <vector>

namespace test_instant_power_current {

EchonetLiteFrame frame_Get_responce_InstantaneousPower_Current() {
  const uint8_t INSTANTANEOUS_POWER = static_cast<uint8_t>(
      ElectricityMeter::EchonetLiteEPC::Measured_instantaneous_power);
  const uint8_t INSTANTANEOUS_CURRENT = static_cast<uint8_t>(
      ElectricityMeter::EchonetLiteEPC::Measured_instantaneous_currents);
  EchonetLiteFrame frame{
      .ehd = EchonetLiteEHD,
      .tid = EchonetLiteTransactionId({0x12, 0x34}),
      .edata =
          {
              .seoj = ElectricityMeter::EchonetLiteEOJ, // Electricity meter
              .deoj = HomeController::EchonetLiteEOJ,   // Home controller
              .esv = EchonetLiteESV::Get_Res,           // Get responce
              .opc = 2,                                 // 2つ
              .props =
                  {
                      {
                          .epc = INSTANTANEOUS_POWER,      // instant power
                          .pdc = 4,                        // EDT=4
                          .edt = {0x00, 0x00, 0x07, 0x90}, // (0x07=7)*256
                                                           // +(0x90=144)
                                                           // =1936W
                      },
                      {
                          .epc = INSTANTANEOUS_CURRENT,    // instant current
                          .pdc = 4,                        // EDT=4
                          .edt = {0x00, 0x2D, 0x00, 0x9C}, // R:(0x2D=45)
                                                           // T:(0x22=34)
                                                           // = R4.5A, T3.4A
                      },
                  },
          },
  };
  return frame;
}

std::vector<uint8_t> octets_Get_responce_InstantaneousPower_Current() {
  std::vector<uint8_t> ocetets{
      0x10, // EHD#0
      0x81, // EHD#1
      0x12, // TID#0
      0x34, // TID#1
            /** EDATA **/
      0x02, // SEOJ#0
      0x88, // SEOJ#1
      0x01, // SEO1#2
      0x05, // DEOJ#0
      0xFF, // DEOJ#1
      0x01, // DEO1#2
      0x72, // ESV
      0x02, // OPC
            /** PROPS#0 **/
      0xE7, // EPC(0xE7=instantaneous power)
      0x04, // PDC
      0x00, // EDT#0
      0x00, // EDT#1
      0x07, // EDT#2
      0x90, // EDT#3
            /** PROPS#1 **/
      0xE8, // EPC(0xE8=instantaneous current)
      0x04, // PDC
      0x00, // EDT#0
      0x2D, // EDT#1
      0x00, // EDT#2
      0x9C, // EDT#3
  };
  return ocetets;
}

void test_serialize_responce(void) {
  std::vector<uint8_t> octets;
  auto result = EchonetLite::serializeFromEchonetLiteFrame(
      octets, frame_Get_responce_InstantaneousPower_Current());
  TEST_ASSERT_NOT_NULL(std::get_if<EchonetLite::SerializeOk>(&result));
  //
  TEST_ASSERT_TRUE(octets == octets_Get_responce_InstantaneousPower_Current());
}

void test_deserialize_responce(void) {
  EchonetLiteFrame frame;
  auto result = EchonetLite::deserializeToEchonetLiteFrame(
      frame, octets_Get_responce_InstantaneousPower_Current());
  TEST_ASSERT_NOT_NULL(std::get_if<EchonetLite::DeserializeOk>(&result));
  TEST_ASSERT_TRUE(frame == frame_Get_responce_InstantaneousPower_Current());
}

void test_serialize_deserialize_responce(void) {
  auto source_frame = frame_Get_responce_InstantaneousPower_Current();
  //
  std::vector<uint8_t> octets;
  auto result =
      EchonetLite::serializeFromEchonetLiteFrame(octets, source_frame);
  TEST_ASSERT_NOT_NULL(std::get_if<EchonetLite::SerializeOk>(&result));
  //
  TEST_ASSERT_TRUE(octets == octets_Get_responce_InstantaneousPower_Current());
}

void test_runner() {
  UNITY_BEGIN();
  RUN_TEST(test_serialize_responce);
  RUN_TEST(test_deserialize_responce);
  RUN_TEST(test_serialize_deserialize_responce);
  UNITY_END();
}
} // namespace test_instant_power_current
