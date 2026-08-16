#pragma once

#include <array>
#include <cstdint>

namespace voxtonic::ids {

// Known cosmetic tonic transformation effect ids (Guild Wars 2 Wiki,
// "Transformation Effect"). Only one transformation can be active at a time,
// so scanning this whole list is cheap (~18 compares per buff entry, and only
// while the option is enabled).
inline constexpr std::array<std::uint32_t, 18> kKnownTonicIds {{
    17732, 20976, 24573, 25651, 28935, 34572, 37954, 38453, 50196,
    51219, 57740, 58417, 55397, 39339, 52798, 70230, 73215, 80128,
}};

} // namespace voxtonic::ids
