#pragma once

#include <cstdint>

namespace network {

/**
 * @brief RTP 包头定义（RFC 3550）
 */
#pragma pack(push, 1)
struct RTPHeader {
  uint8_t version : 2;
  uint8_t padding : 1;
  uint8_t extension : 1;
  uint8_t csrc_count : 4;

  uint8_t marker : 1;
  uint8_t payload_type : 7;

  uint16_t sequence_number;
  uint32_t timestamp;
  uint32_t ssrc;
};
#pragma pack(pop)

}  // namespace network
