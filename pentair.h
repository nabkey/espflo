#pragma once
//
// Pentair RS-485 frame helper for ESPHome.
//
// Frame layout sent to a SuperFlo VST / IntelliFlo family pump:
//
//   FF 00 FF | A5 00 <dst> <src> <cmd> <len> <data...> | <ckHi> <ckLo>
//   \__preamble_/ \__________ checksummed body __________/  \_checksum_/
//
// The checksum is the 16-bit sum of every byte from the leading A5 through
// the final data byte, transmitted big-endian. The three preamble bytes
// (FF 00 FF) are NOT included in the checksum.
//
// Addresses used by this project:
//   dst = 0x60  -> pump set to keypad "Pump Address 1" (0x61 = address 2)
//   src = 0x21  -> the address this controller announces itself as
//
// This is a template so it compiles under both the arduino and esp-idf
// frameworks without needing to name ESPHome's UART class explicitly.

#include <cstdint>
#include <vector>

template<typename UART>
void send_pentair(UART *uart, uint8_t dst, uint8_t src, uint8_t cmd,
                  const std::vector<uint8_t> &data) {
  std::vector<uint8_t> body = {0xA5, 0x00, dst, src, cmd,
                               static_cast<uint8_t>(data.size())};
  for (uint8_t b : data) body.push_back(b);

  uint16_t sum = 0;
  for (uint8_t b : body) sum += b;

  std::vector<uint8_t> frame = {0xFF, 0x00, 0xFF};
  for (uint8_t b : body) frame.push_back(b);
  frame.push_back(static_cast<uint8_t>(sum >> 8));
  frame.push_back(static_cast<uint8_t>(sum & 0xFF));

  uart->write_array(frame);
  uart->flush();
}
