#include "bon_abi.h"
#include "oneseg_core_c.h"
#include "oneseg_iq.h"
#include "rtl_device.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr DWORD kChunkPackets = 16;
constexpr DWORD kChunkBytes = kChunkPackets * 188;
HMODULE g_module = nullptr;

std::wstring config_path() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(g_module, path, MAX_PATH);
    std::wstring result(path);
    const auto dot = result.find_last_of(L'.');
    if (dot != std::wstring::npos) result.resize(dot);
    return result + L".ini";
}

bool read_file(const std::wstring& path, std::vector<std::uint8_t>& output) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool okay = GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
                size.QuadPart <= 128 * 1024 * 1024;
    if (okay) {
        output.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD bytes_read = 0;
        okay = ReadFile(file, output.data(), static_cast<DWORD>(output.size()),
                        &bytes_read, nullptr) && bytes_read == output.size();
    }
    CloseHandle(file);
    return okay;
}

class Driver final : public IBonDriver2 {
public:
    const BOOL OpenTuner() override {
        if (open_) return TRUE;
        const auto ini = config_path();
        wchar_t mode[32]{};
        GetPrivateProfileStringW(L"Source", L"Mode", L"Live", mode,
                                 32, ini.c_str());
        if (_wcsicmp(mode, L"Replay") != 0) {
        wchar_t library[MAX_PATH]{};
        GetPrivateProfileStringW(L"Source", L"RtlSdrLibrary", L"rtlsdr.dll",
                                 library, MAX_PATH, ini.c_str());
            if (!library[0] || !device_.open(library,
                    GetPrivateProfileIntW(L"Source", L"GainTenthsDb", 197,
                                          ini.c_str()))) return FALSE;
            live_ = true;
            open_ = true;
            worker_ = std::thread([this] { capture_loop(); });
            decoder_ = std::thread([this] { decode_loop(); });
            return TRUE;
        }
        // Recorded replay requires an explicit mode so a failed USB open
        // cannot silently replay old video.
        wchar_t input[MAX_PATH]{};
        GetPrivateProfileStringW(L"Source", L"ViterbiFile", L"", input,
                                 MAX_PATH, ini.c_str());
        DWORD channel = GetPrivateProfileIntW(L"Source", L"PhysicalChannel", 25,
                                             ini.c_str());
        if (!input[0] || channel < 13 || channel > 62) return FALSE;
        std::vector<std::uint8_t> bytes;
        if (!read_file(input, bytes)) return FALSE;
        int status = 0;
        oneseg_result* decoded = oneseg_recover_transport(bytes.data(), bytes.size(), &status);
        if (!decoded || status != 0) return FALSE;
        std::size_t size = 0;
        const auto* ts = oneseg_result_ts(decoded, &size);
        bool okay = ts && size >= 188 && size % 188 == 0;
        if (okay) ts_.assign(ts, ts + size);
        oneseg_result_destroy(decoded);
        if (!okay) return FALSE;
        physical_channel_ = channel;
        cursor_ = 0;
        open_ = true;
        return TRUE;
    }

    void CloseTuner() override {
        stop_ = true;
        device_.cancel_async();
        iq_ready_.notify_all();
        if (worker_.joinable()) worker_.join();
        if (decoder_.joinable()) decoder_.join();
        device_.close();
        live_ = false;
        stop_ = false;
        open_ = false;
        selected_ = false;
        ts_.clear();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.clear();
            iq_queue_.clear();
            selected_channel_ = 0;
        }
        cursor_ = 0;
    }

    const BOOL SetChannel(BYTE channel) override {
        if (channel < 13 || channel > 52) return FALSE;
        return SetChannel(0, channel - 13);
    }

    const float GetSignalLevel() override {
        return live_ ? signal_level_.load() : (selected_ ? 1.0f : 0.0f);
    }

    const DWORD WaitTsStream(DWORD timeout = 0) override {
        if (!open_ || !selected_) return WAIT_ABANDONED;
        if (live_) {
            const auto start = GetTickCount64();
            do {
                if (GetReadyCount()) return WAIT_OBJECT_0;
                Sleep(10);
            } while (timeout && GetTickCount64() - start < timeout);
            return WAIT_TIMEOUT;
        }
        return GetReadyCount() ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
    }

    const DWORD GetReadyCount() override {
        if (!open_ || !selected_) return 0;
        if (!live_) return ts_.empty() ? 0 : 1;
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<DWORD>(queue_.size() / 188);
    }

    const BOOL GetTsStream(BYTE* dst, DWORD* size, DWORD* remain) override {
        BYTE* source = nullptr;
        if (!GetTsStream(&source, size, remain)) return FALSE;
        if (dst && source && size) std::memcpy(dst, source, *size);
        return TRUE;
    }

    const BOOL GetTsStream(BYTE** dst, DWORD* size, DWORD* remain) override {
        if (!dst || !size || !remain || !open_ || !selected_) return FALSE;
        *dst = nullptr;
        *size = 0;
        *remain = 0;
        if (live_) {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto count = std::min<std::size_t>(kChunkBytes, queue_.size() / 188 * 188);
            for (std::size_t i = 0; i < count; ++i) {
                chunk_[i] = queue_.front();
                queue_.pop_front();
            }
            *dst = chunk_.data();
            *size = static_cast<DWORD>(count);
            *remain = static_cast<DWORD>(queue_.size() / 188);
            return TRUE;
        }
        const std::size_t packets = ts_.size() / 188;
        if (!packets) return FALSE;
        for (DWORD i = 0; i < kChunkPackets; ++i) {
            std::memcpy(chunk_.data() + i * 188,
                        ts_.data() + (cursor_++ % packets) * 188, 188);
        }
        *dst = chunk_.data();
        *size = kChunkBytes;
        return TRUE;
    }

    void PurgeTsStream() override {
        cursor_ = 0;
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

    void Release() override {
        CloseTuner();
        delete bon_struct_;
        delete this;
    }

    void SetBonStruct(BonStruct* value) { bon_struct_ = value; }

    LPCTSTR GetTunerName() override { return TEXT("RTL-SDR OneSeg"); }
    const BOOL IsTunerOpening() override { return open_ ? TRUE : FALSE; }
    LPCTSTR EnumTuningSpace(DWORD space) override {
        return space == 0 ? TEXT("UHF one-seg") : nullptr;
    }
    LPCTSTR EnumChannelName(DWORD space, DWORD channel) override {
        if (space != 0 || channel >= (live_ ? 40u : 1u)) return nullptr;
        channel_name_ = TEXT("UHF ") +
                        std::to_wstring(live_ ? 13 + channel : physical_channel_);
        return channel_name_.c_str();
    }
    const BOOL SetChannel(DWORD space, DWORD channel) override {
        if (!open_ || space != 0 || channel >= (live_ ? 40u : 1u)) return FALSE;
        selected_ = true;
        PurgeTsStream();
        if (live_) {
            signal_level_ = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                selected_channel_ = 13 + channel;
                ++generation_;
                iq_queue_.clear();
            }
            device_.cancel_async();
        }
        current_channel_ = channel;
        return TRUE;
    }
    const DWORD GetCurSpace() override { return selected_ ? 0 : 0xFFFFFFFFu; }
    const DWORD GetCurChannel() override {
        return selected_ ? current_channel_ : 0xFFFFFFFFu;
    }

private:
    struct Window {
        unsigned channel;
        unsigned generation;
        std::vector<std::uint8_t> iq;
    };

    static void on_iq(unsigned char* data, std::uint32_t size, void* context) {
        static_cast<Driver*>(context)->receive_iq(data, size);
    }

    void receive_iq(const unsigned char* data, std::uint32_t size) {
        if (stop_) { device_.cancel_async(); return; }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (capture_generation_ != generation_) {
                device_.cancel_async();
                return;
            }
        }
        constexpr std::size_t kWindowBytes = 2 * 4'194'304;
        constexpr std::size_t kAdvanceBytes = kWindowBytes / 2;
        raw_.insert(raw_.end(), data, data + size);
        while (raw_.size() >= kWindowBytes) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (capture_generation_ != generation_) {
                    device_.cancel_async();
                    return;
                }
                iq_queue_.push_back({capture_channel_, capture_generation_,
                                     std::vector<std::uint8_t>(raw_.begin(),
                                                               raw_.begin() + kWindowBytes)});
                while (iq_queue_.size() > 3) iq_queue_.pop_front();
            }
            iq_ready_.notify_one();
            raw_.erase(raw_.begin(), raw_.begin() + kAdvanceBytes);
        }
    }

    void capture_loop() {
        while (!stop_) {
            unsigned requested = 0;
            unsigned generation = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                requested = selected_channel_;
                generation = generation_;
            }
            if (!requested) { Sleep(10); continue; }
            if (!device_.tune(requested)) { Sleep(100); continue; }
            raw_.clear();
            capture_generation_ = generation;
            capture_channel_ = requested;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation != generation_) continue;
            }
            device_.run_async(on_iq, this);
            if (!stop_) Sleep(10);
        }
    }

    void decode_loop() {
        std::vector<std::uint8_t> previous_ts;
        unsigned previous_generation = 0;
        while (!stop_) {
            Window window{};
            {
                std::unique_lock<std::mutex> lock(mutex_);
                iq_ready_.wait(lock, [this] { return stop_ || !iq_queue_.empty(); });
                if (stop_) break;
                window = std::move(iq_queue_.front());
                iq_queue_.pop_front();
            }
            if (window.generation != previous_generation) {
                previous_ts.clear();
                previous_generation = window.generation;
            }
            try {
                oneseg::IqDecodeStats stats;
                auto result = oneseg::decode_iq_mode3(window.iq.data(),
                                                       window.iq.size(), &stats);
                std::lock_guard<std::mutex> lock(mutex_);
                if (window.generation != generation_ ||
                    window.channel != selected_channel_) continue;
                // TVTest labels GetSignalLevel in dB. Approximate a quality
                // ratio from repeated-prefix correlation; this is not a
                // calibrated RF C/N measurement.
                const float rho = stats.cyclic_prefix_correlation;
                signal_level_ = result.stats.accepted && rho > 0 && rho < 1
                    ? std::clamp(10.0f * std::log10(rho / (1.0f - rho)),
                                 0.0f, 50.0f)
                    : 0.0f;
                std::size_t append_from = 0;
                // Align the one-second overlap using many packet matches.
                // Repeated PSI packets can match far from the true boundary,
                // so a single last match is not sufficient.
                std::vector<int> offsets;
                const auto current_packets = result.ts.size() / 188;
                const auto previous_packets = previous_ts.size() / 188;
                for (std::size_t i = 0; i < current_packets; ++i) {
                    const auto* packet = result.ts.data() + i * 188;
                    const unsigned pid = ((packet[1] & 0x1f) << 8) | packet[2];
                    if (pid < 0x20 || pid == 0x1fff) continue;
                    for (std::size_t j = 0; j < previous_packets; ++j) {
                        if (std::memcmp(packet, previous_ts.data() + j * 188,
                                        188) == 0) {
                            offsets.push_back(static_cast<int>(j) -
                                              static_cast<int>(i));
                            break;
                        }
                    }
                }
                if (offsets.size() >= 4) {
                    std::sort(offsets.begin(), offsets.end());
                    const int shift = offsets[offsets.size() / 2];
                    const int cutoff = std::clamp(
                        static_cast<int>(previous_packets) - shift,
                        0, static_cast<int>(current_packets));
                    append_from = static_cast<std::size_t>(cutoff) * 188;
                }
                for (std::size_t i = append_from; i < result.ts.size(); ++i)
                    queue_.push_back(result.ts[i]);
                previous_ts = std::move(result.ts);
                while (queue_.size() > 188 * 2500) queue_.pop_front();
            } catch (...) { signal_level_ = 0; }
        }
    }

    bool open_ = false;
    bool selected_ = false;
    bool live_ = false;
    std::atomic<bool> stop_{false};
    std::atomic<float> signal_level_{0};
    RtlDevice device_;
    std::thread worker_;
    std::thread decoder_;
    mutable std::mutex mutex_;
    std::condition_variable iq_ready_;
    std::deque<Window> iq_queue_;
    std::vector<std::uint8_t> raw_;
    unsigned capture_generation_ = 0;
    unsigned capture_channel_ = 0;
    std::deque<std::uint8_t> queue_;
    unsigned selected_channel_ = 0;
    unsigned generation_ = 0;
    DWORD current_channel_ = 0;
    DWORD physical_channel_ = 25;
    std::wstring channel_name_;
    std::vector<std::uint8_t> ts_;
    std::size_t cursor_ = 0;
    std::array<BYTE, kChunkBytes> chunk_{};
    BonStruct* bon_struct_ = nullptr;
};

BOOL open(void* p) { return static_cast<Driver*>(p)->OpenTuner(); }
void close(void* p) { static_cast<Driver*>(p)->CloseTuner(); }
BOOL set_legacy(void* p, BYTE c) { return static_cast<Driver*>(p)->SetChannel(c); }
float level(void* p) { return static_cast<Driver*>(p)->GetSignalLevel(); }
DWORD wait(void* p, DWORD t) { return static_cast<Driver*>(p)->WaitTsStream(t); }
DWORD ready(void* p) { return static_cast<Driver*>(p)->GetReadyCount(); }
BOOL get_copy(void* p, BYTE* d, DWORD* s, DWORD* r) {
    return static_cast<Driver*>(p)->GetTsStream(d, s, r);
}
BOOL get_pointer(void* p, BYTE** d, DWORD* s, DWORD* r) {
    return static_cast<Driver*>(p)->GetTsStream(d, s, r);
}
void purge(void* p) { static_cast<Driver*>(p)->PurgeTsStream(); }
void release(void* p) { static_cast<Driver*>(p)->Release(); }
LPCTSTR name(void* p) { return static_cast<Driver*>(p)->GetTunerName(); }
BOOL is_open(void* p) { return static_cast<Driver*>(p)->IsTunerOpening(); }
LPCTSTR space(void* p, DWORD s) { return static_cast<Driver*>(p)->EnumTuningSpace(s); }
LPCTSTR channel(void* p, DWORD s, DWORD c) {
    return static_cast<Driver*>(p)->EnumChannelName(s, c);
}
BOOL set_channel(void* p, DWORD s, DWORD c) {
    return static_cast<Driver*>(p)->SetChannel(s, c);
}
DWORD cur_space(void* p) { return static_cast<Driver*>(p)->GetCurSpace(); }
DWORD cur_channel(void* p) { return static_cast<Driver*>(p)->GetCurChannel(); }

}  // namespace

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_module = module;
    return TRUE;
}

extern "C" __declspec(dllexport) IBonDriver* CreateBonDriver() {
    return new Driver;
}

extern "C" __declspec(dllexport) const BonStruct* CreateBonStruct() {
    auto* driver = new Driver;
    auto* structure = new BonStruct{driver, nullptr, open, close, set_legacy,
                                    level, wait, ready, get_copy, get_pointer,
                                    purge, release, name, is_open, space,
                                    channel, set_channel, cur_space, cur_channel};
    structure->end = structure + 1;
    driver->SetBonStruct(structure);
    return structure;
}
