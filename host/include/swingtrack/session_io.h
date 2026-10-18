// Reading and writing session folders.
//
// A session folder contains:
//   raw.csv      t_ms,ax,ay,az,gx,gy,gz            (optional, can be large)
//   swings.csv   idx,set,t_start_ms,duration_ms,peak_dps,peak_g,axis
//   markers.csv  set,t_ms
#pragma once

#include <filesystem>
#include <iosfwd>
#include <vector>

#include "swingtrack/types.h"

namespace swingtrack {

namespace fs = std::filesystem;

void writeRawHeader(std::ostream &out);
void writeRawRow(std::ostream &out, const RawSample &r);
void writeSwingHeader(std::ostream &out);
void writeSwingRow(std::ostream &out, const Swing &s);
void writeMarkerHeader(std::ostream &out);
void writeMarkerRow(std::ostream &out, const SetMarker &m);

// Writes whichever parts of the session are non-empty. Throws on I/O errors.
void saveSession(const fs::path &dir, const Session &session);

// Loads every file that exists in `dir`. Throws if the folder is missing or a
// file is malformed.
Session loadSession(const fs::path &dir);

}  // namespace swingtrack
