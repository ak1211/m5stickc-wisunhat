#include "EchonetLite.hpp"
#include <unity.h>
#include <variant>
#include <vector>

namespace test_cumlative_amount_of_power {
EchonetLiteFrame frame_Get_request_CumlativeAmountOfPower() {
  const uint8_t EPC = static_cast<uint8_t>(
      ElectricityMeter::EchonetLiteEPC::
          Cumulative_amounts_of_electric_energy_measured_at_fixed_time);
  EchonetLiteFrame frame{
      .ehd = EchonetLiteEHD,
      .tid = EchonetLiteTransactionId({0x12, 0x34}),
      .edata =
          {
              .seoj = HomeController::EchonetLiteEOJ,   // Home controller
              .deoj = ElectricityMeter::EchonetLiteEOJ, // Electricity meter
              .esv = EchonetLiteESV::Get,               // Get
              .opc = 1,                                 // 1つ
              .props =
                  {
                      {
                          .epc = EPC, // cumlative amount of power
                          .pdc = 0,   // EDT=0
                          .edt = {},  // EDT is nothing
                      },
                  },
          },
  };
  return frame;
}

std::vector<uint8_t> octets_Get_request_CumlativeAmountOfPower() {
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
      0xEA, // EPC
      0x00, // PDC
            // EDT is nothing
  };
  return ocetets;
}

EchonetLiteFrame frame_Get_responce_CumlativeAmountOfPower() {
  const uint8_t EPC = static_cast<uint8_t>(
      ElectricityMeter::EchonetLiteEPC::
          Cumulative_amounts_of_electric_energy_measured_at_fixed_time);
  EchonetLiteFrame frame{
      .ehd = EchonetLiteEHD,
      .tid = EchonetLiteTransactionId({0x12, 0x34}),
      .edata =
          {
              .seoj = ElectricityMeter::EchonetLiteEOJ, // Electricity meter
              .deoj = HomeController::EchonetLiteEOJ,   // Home controller
              .esv = EchonetLiteESV::Get_Res,           // Get responce
              .opc = 1,                                 // 1つ
              .props =
                  {
                      {
                          .epc = EPC, // cumlative amount of power
                          .pdc = 11,  // EDT=11
                          .edt =
                              {
                                  0x07, 0xE6, // EDT#0,1: year(0x07E6=2022)
                                  0x08,       // EDT#2: month(0x08=8)
                                  0x01,       // EDT#3: day(0x01=1)
                                  0x14,       // EDT#4: hour(0x14=20)
                                  0x00,       // EDT#5: min(00)
                                  0x00,       // EDT#6: sec(00)
                                  0x00,       // EDT#7: 00
                                  0x01,       // EDT#8: 01
                                  0x2C,       // EDT#9: (0x2C=44)
                                  0xC7,       // EDT#10: (0xC7=199)
                              },              // 1*65536
                                              // +44*256
                                              // +199
                                              // =76999
                      },
                  },
          },
  };
  return frame;
}

std::vector<uint8_t> octets_Get_responce_CumlativeAmountOfPower() {
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
      0x01, // OPC
            /** PROPS#0 **/
      0xEA, // EPC
      0x0B, // PDC
      0x07, // EDT#0
      0xE6, // EDT#1
      0x08, // EDT#2
      0x01, // EDT#3
      0x14, // EDT#4
      0x00, // EDT#5
      0x00, // EDT#6
      0x00, // EDT#7
      0x01, // EDT#8
      0x2C, // EDT#9
      0xC7, // EDT#10

  };
  return ocetets;
}

void test_serialize_deserialize1(void) {
  auto source_frame = frame_Get_request_CumlativeAmountOfPower();
  //
  std::vector<uint8_t> octets;
  auto result =
      EchonetLite::serializeFromEchonetLiteFrame(octets, source_frame);
  TEST_ASSERT_NOT_NULL(std::get_if<EchonetLite::SerializeOk>(&result));
  //
  TEST_ASSERT_TRUE(octets == octets_Get_request_CumlativeAmountOfPower());
}

void test_serialize_deserialize2(void) {
  auto source_frame = frame_Get_responce_CumlativeAmountOfPower();
  //
  std::vector<uint8_t> octets;
  auto result =
      EchonetLite::serializeFromEchonetLiteFrame(octets, source_frame);
  TEST_ASSERT_NOT_NULL(std::get_if<EchonetLite::SerializeOk>(&result));
  //
  TEST_ASSERT_TRUE(octets == octets_Get_responce_CumlativeAmountOfPower());
}

void test_runner() {
  UNITY_BEGIN();
  RUN_TEST(test_serialize_deserialize1);
  RUN_TEST(test_serialize_deserialize2);
  UNITY_END();
}
} // namespace test_cumlative_amount_of_power
