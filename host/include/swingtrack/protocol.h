// Parser for the firmware's line-based serial protocol (see swing_tracker.ino).
#pragma once

#include <string>

#include "swingtrack/types.h"

namespace swingtrack {

enum class MessageType { Raw, Swing, Marker, Info, Invalid };

struct Message {
  MessageType type = MessageType::Invalid;
  RawSample raw;      // valid when type == Raw
  Swing swing;        // valid when type == Swing
  SetMarker marker;   // valid when type == Marker
  std::string text;   // info text, or an error description when Invalid
};

// Parses one line (with or without a trailing "\r" / "\n").
Message parseLine(const std::string &line);

}  // namespace swingtrack
