#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

struct rtlsdr_dev;

class RtlDevice {
public:
    ~RtlDevice();
    bool open(const std::wstring& library, int gain_tenths_db);
    bool tune(unsigned physical_channel);
    bool read(std::vector<std::uint8_t>& bytes);
    void close();
private:
    using Open = int (*)(rtlsdr_dev**, std::uint32_t);
    using Close = int (*)(rtlsdr_dev*);
    using SetU32 = int (*)(rtlsdr_dev*, std::uint32_t);
    using SetInt = int (*)(rtlsdr_dev*, int);
    using Reset = int (*)(rtlsdr_dev*);
    using ReadSync = int (*)(rtlsdr_dev*, void*, int, int*);
    HMODULE module_ = nullptr;
    rtlsdr_dev* device_ = nullptr;
    Open open_ = nullptr;
    Close close_ = nullptr;
    SetU32 rate_ = nullptr;
    SetU32 frequency_ = nullptr;
    SetInt gain_mode_ = nullptr;
    SetInt gain_ = nullptr;
    Reset reset_ = nullptr;
    ReadSync read_ = nullptr;
    int gain_tenths_db_ = 197;
};
