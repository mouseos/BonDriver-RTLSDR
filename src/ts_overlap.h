#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

inline std::size_t ts_overlap_bytes(const std::vector<std::uint8_t>& previous,
                                    const std::vector<std::uint8_t>& current) {
    constexpr std::size_t packet_size = 188;
    const std::size_t previous_count = previous.size() / packet_size;
    const std::size_t current_count = current.size() / packet_size;
    // The short startup capture and first full capture start at the same I/Q
    // sample. Their decoded TS streams share a prefix, not a suffix.
    if (previous_count > 0 && previous_count <= 128 &&
        current_count >= previous_count + 200) {
        std::size_t matches = 0, candidates = 0;
        for (std::size_t i = 0; i < previous_count; ++i) {
            const auto* packet = current.data() + i * packet_size;
            const unsigned pid = ((packet[1] & 0x1f) << 8) | packet[2];
            if (pid < 0x20 || pid == 0x1fff) continue;
            ++candidates;
            if (std::memcmp(packet, previous.data() + i * packet_size,
                            packet_size) == 0) ++matches;
        }
        if (matches >= 4 && matches * 2 >= candidates)
            return previous_count * packet_size;
    }
    const std::size_t available = previous_count < current_count
        ? previous_count : current_count;
    const std::size_t limit = available < 64 ? available : 64;
    std::size_t best_overlap = 0, best_matches = 0;
    for (std::size_t overlap = 1; overlap <= limit; ++overlap) {
        std::size_t matches = 0;
        for (std::size_t i = 0; i < overlap; ++i) {
            const auto* packet = current.data() + i * packet_size;
            const unsigned pid = ((packet[1] & 0x1f) << 8) | packet[2];
            if (pid < 0x20 || pid == 0x1fff) continue;
            const auto* candidate = previous.data() +
                (previous_count - overlap + i) * packet_size;
            if (std::memcmp(packet, candidate, packet_size) == 0) ++matches;
        }
        if (matches > best_matches) {
            best_matches = matches;
            best_overlap = overlap;
        }
    }
    return best_matches >= 4 && best_matches * 2 >= best_overlap
        ? best_overlap * packet_size : 0;
}
