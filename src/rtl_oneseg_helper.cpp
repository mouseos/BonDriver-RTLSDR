#include "oneseg_iq.h"
#include "rtl_device.h"

#include <windows.h>

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <cmath>

namespace {

constexpr std::uint32_t kFrameMagic = 0x3147534f;
constexpr std::size_t kWindowBytes = 2 * 4'194'304;
// Keep roughly 0.52 s of overlap for the time/byte deinterleavers while
// allowing the 2.06 s decode window about 1.55 s of CPU time to finish.
constexpr std::size_t kAdvanceBytes = kWindowBytes * 3 / 4;
constexpr std::size_t kFirstBytes = 3 * 1024 * 1024;

bool write_all(HANDLE handle, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t offset = 0; offset < size;) {
        DWORD written = 0;
        if (!WriteFile(handle, bytes + offset,
                       static_cast<DWORD>(size - offset), &written, nullptr) ||
            !written) return false;
        offset += written;
    }
    return true;
}

class Receiver {
public:
    explicit Receiver(HANDLE output) : output_(output) {}

    void on_iq(const unsigned char* data, std::uint32_t size) {
        raw_.insert(raw_.end(), data, data + size);
        if (!warmup_queued_ && raw_.size() >= kFirstBytes) {
            enqueue(raw_.begin(), raw_.begin() + kFirstBytes);
            warmup_queued_ = true;
        }
        while (raw_.size() >= kWindowBytes) {
            enqueue(raw_.begin(), raw_.begin() + kWindowBytes);
            raw_.erase(raw_.begin(), raw_.begin() + kAdvanceBytes);
        }
    }

    void decode_loop() {
        std::vector<std::uint8_t> previous;
        for (;;) {
            std::vector<std::uint8_t> iq;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return !windows_.empty(); });
                iq = std::move(windows_.front());
                windows_.pop_front();
            }
            try {
                oneseg::IqDecodeStats stats;
                auto result = oneseg::decode_iq_mode3(iq.data(), iq.size(), &stats);
                const std::size_t current_count = result.ts.size() / 188;
                const std::size_t previous_count = previous.size() / 188;
                std::vector<int> offsets;
                for (std::size_t i = 0; i < current_count; ++i) {
                    const auto* packet = result.ts.data() + i * 188;
                    const unsigned pid = ((packet[1] & 0x1f) << 8) | packet[2];
                    if (pid < 0x20 || pid == 0x1fff) continue;
                    for (std::size_t j = 0; j < previous_count; ++j) {
                        if (std::memcmp(packet, previous.data() + j * 188,
                                        188) == 0) {
                            offsets.push_back(static_cast<int>(j) -
                                              static_cast<int>(i));
                            break;
                        }
                    }
                }
                std::size_t start = 0;
                if (offsets.size() >= 4) {
                    std::sort(offsets.begin(), offsets.end());
                    const int shift = offsets[offsets.size() / 2];
                    start = static_cast<std::size_t>(std::clamp(
                        static_cast<int>(previous_count) - shift,
                        0, static_cast<int>(current_count))) * 188;
                }
                previous = std::move(result.ts);
                if (start >= previous.size()) continue;
                const float rho = stats.cyclic_prefix_correlation;
                const float quality = rho > 0 && rho < 1
                    ? std::clamp(10.0f * std::log10(rho / (1.0f - rho)),
                                 0.0f, 50.0f) : 0.0f;
                struct Frame { std::uint32_t magic, bytes; float quality; };
                const Frame frame{kFrameMagic,
                                  static_cast<std::uint32_t>(previous.size() - start),
                                  quality};
                if (!write_all(output_, &frame, sizeof(frame)) ||
                    !write_all(output_, previous.data() + start, frame.bytes))
                    ExitProcess(1);
            } catch (...) {
                // An unsupported transmission mode yields no TS for this window.
            }
        }
    }

private:
    template<class It> void enqueue(It begin, It end) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            windows_.emplace_back(begin, end);
            while (windows_.size() > 3) windows_.pop_front();
        }
        ready_.notify_one();
    }
    HANDLE output_;
    std::vector<std::uint8_t> raw_;
    bool warmup_queued_ = false;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::vector<std::uint8_t>> windows_;
};

void callback(unsigned char* data, std::uint32_t size, void* context) {
    static_cast<Receiver*>(context)->on_iq(data, size);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 2;
    const unsigned channel = std::wcstoul(argv[1], nullptr, 10);
    const int gain = std::wcstol(argv[3], nullptr, 10);
    RtlDevice device;
    if (!device.open(argv[2], gain) || !device.tune(channel)) return 1;
    Receiver receiver(GetStdHandle(STD_OUTPUT_HANDLE));
    std::thread decoder([&receiver] { receiver.decode_loop(); });
    decoder.detach();
    return device.run_async(callback, &receiver) ? 0 : 1;
}
