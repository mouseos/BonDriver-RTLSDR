#include "bon_abi.h"
#include "oneseg_core_c.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr DWORD kChunkPackets = 16;
constexpr DWORD kChunkBytes = kChunkPackets * 188;
constexpr DWORD kPacketRate = 210;
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
        next_tick_ = GetTickCount64();
        open_ = true;
        return TRUE;
    }

    void CloseTuner() override {
        open_ = false;
        selected_ = false;
        ts_.clear();
        cursor_ = 0;
        chunk_size_ = 0;
    }

    const BOOL SetChannel(BYTE channel) override {
        return channel == physical_channel_ ? SetChannel(0, 0) : FALSE;
    }

    const float GetSignalLevel() override { return selected_ ? 1.0f : 0.0f; }

    const DWORD WaitTsStream(DWORD timeout = 0) override {
        if (!open_ || !selected_) return WAIT_ABANDONED;
        const auto now = GetTickCount64();
        if (now >= next_tick_) return WAIT_OBJECT_0;
        const auto needed = static_cast<DWORD>(std::min<ULONGLONG>(next_tick_ - now, 0xFFFFFFFFu));
        if (timeout && timeout < needed) {
            Sleep(timeout);
            return WAIT_TIMEOUT;
        }
        Sleep(needed);
        return WAIT_OBJECT_0;
    }

    const DWORD GetReadyCount() override {
        return open_ && selected_ && GetTickCount64() >= next_tick_ ? 1 : 0;
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
        if (GetTickCount64() < next_tick_) return TRUE;
        const std::size_t packets = ts_.size() / 188;
        if (!packets) return FALSE;
        for (DWORD i = 0; i < kChunkPackets; ++i) {
            std::memcpy(chunk_.data() + i * 188,
                        ts_.data() + (cursor_++ % packets) * 188, 188);
        }
        chunk_size_ = kChunkBytes;
        *dst = chunk_.data();
        *size = chunk_size_;
        next_tick_ = GetTickCount64() + 1000 * kChunkPackets / kPacketRate;
        return TRUE;
    }

    void PurgeTsStream() override {
        cursor_ = 0;
        chunk_size_ = 0;
        next_tick_ = GetTickCount64();
    }

    void Release() override {
        delete bon_struct_;
        delete this;
    }

    void SetBonStruct(BonStruct* value) { bon_struct_ = value; }

    LPCTSTR GetTunerName() override { return TEXT("RTL-SDR OneSeg Replay"); }
    const BOOL IsTunerOpening() override { return open_ ? TRUE : FALSE; }
    LPCTSTR EnumTuningSpace(DWORD space) override {
        return space == 0 ? TEXT("UHF one-seg replay") : nullptr;
    }
    LPCTSTR EnumChannelName(DWORD space, DWORD channel) override {
        if (space != 0 || channel != 0) return nullptr;
        channel_name_ = TEXT("UHF ") + std::to_wstring(physical_channel_);
        return channel_name_.c_str();
    }
    const BOOL SetChannel(DWORD space, DWORD channel) override {
        if (!open_ || space != 0 || channel != 0) return FALSE;
        selected_ = true;
        PurgeTsStream();
        return TRUE;
    }
    const DWORD GetCurSpace() override { return selected_ ? 0 : 0xFFFFFFFFu; }
    const DWORD GetCurChannel() override {
        return selected_ ? 0 : 0xFFFFFFFFu;
    }

private:
    bool open_ = false;
    bool selected_ = false;
    DWORD physical_channel_ = 25;
    std::wstring channel_name_;
    std::vector<std::uint8_t> ts_;
    std::size_t cursor_ = 0;
    ULONGLONG next_tick_ = 0;
    DWORD chunk_size_ = 0;
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
