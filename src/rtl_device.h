#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

struct rtlsdr_dev;

class RtlDevice {
public:
    ~RtlDevice();
    bool open(const std::wstring& library, int gain_tenths_db);
    bool reopen();
    bool tune(unsigned physical_channel);
    using Callback = void (*)(unsigned char*, std::uint32_t, void*);
    bool run_async(Callback callback, void* context);
    void cancel_async();
    void close();
private:
    using Open = int (*)(rtlsdr_dev**, std::uint32_t);
    using Close = int (*)(rtlsdr_dev*);
    using SetU32 = int (*)(rtlsdr_dev*, std::uint32_t);
    using SetInt = int (*)(rtlsdr_dev*, int);
    using Reset = int (*)(rtlsdr_dev*);
    using ReadAsync = int (*)(rtlsdr_dev*, Callback, void*, std::uint32_t, std::uint32_t);
    using CancelAsync = int (*)(rtlsdr_dev*);
    HMODULE module_ = nullptr;
    rtlsdr_dev* device_ = nullptr;
    Open open_ = nullptr;
    Close close_ = nullptr;
    SetU32 rate_ = nullptr;
    SetU32 frequency_ = nullptr;
    SetInt gain_mode_ = nullptr;
    SetInt gain_ = nullptr;
    SetInt digital_agc_ = nullptr;
    SetInt vendor_if_ = nullptr;
    SetInt fc0013_if_gain_reg_ = nullptr;
    Reset vendor_fc0013_mode_ = nullptr;
    Reset reset_ = nullptr;
    ReadAsync read_async_ = nullptr;
    CancelAsync cancel_async_ = nullptr;
    int gain_tenths_db_ = 197;
    std::wstring library_;
};
