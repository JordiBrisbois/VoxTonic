#pragma once

namespace voxtonic::mumble_link {

// True when the current map is competitive (structured PvP or WvW), read from
// GW2's MumbleLink shared memory. Falls back to false (PvE) when the link is
// unavailable, so callers can also offer a manual override.
bool isCompetitive();

// Current mount index from MumbleLink (0 = on foot, 1+ = mounted). Cached
// briefly (separately from isCompetitive) and returns 0 when the link is
// unavailable.
int mountIndex();

} // namespace voxtonic::mumble_link

