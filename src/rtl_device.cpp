#include "rtl_device.h"

RtlDevice::~RtlDevice() { close(); }

bool RtlDevice::open(const std::wstring& library, int gain_tenths_db) {
    close();
    module_ = LoadLibraryW(library.c_str());
    if (!module_) return false;
    auto symbol = [this](const char* name) { return GetProcAddress(module_, name); };
    open_ = reinterpret_cast<Open>(symbol("rtlsdr_open"));
    close_ = reinterpret_cast<Close>(symbol("rtlsdr_close"));
    rate_ = reinterpret_cast<SetU32>(symbol("rtlsdr_set_sample_rate"));
    frequency_ = reinterpret_cast<SetU32>(symbol("rtlsdr_set_center_freq"));
    gain_mode_ = reinterpret_cast<SetInt>(symbol("rtlsdr_set_tuner_gain_mode"));
    gain_ = reinterpret_cast<SetInt>(symbol("rtlsdr_set_tuner_gain"));
    reset_ = reinterpret_cast<Reset>(symbol("rtlsdr_reset_buffer"));
    read_ = reinterpret_cast<ReadSync>(symbol("rtlsdr_read_sync"));
    if (!open_ || !close_ || !rate_ || !frequency_ || !gain_mode_ ||
        !gain_ || !reset_ || !read_ || open_(&device_, 0) != 0) {
        close();
        return false;
    }
    gain_tenths_db_ = gain_tenths_db;
    if (rate_(device_, 2'031'746) != 0 || gain_mode_(device_, 1) != 0 ||
        gain_(device_, gain_tenths_db_) != 0) {
        close();
        return false;
    }
    return true;
}

bool RtlDevice::tune(unsigned physical_channel) {
    if (!device_ || physical_channel < 13 || physical_channel > 52) return false;
    const auto hz = static_cast<std::uint32_t>(473'142'857 +
                         (physical_channel - 13) * 6'000'000);
    return frequency_(device_, hz) == 0 && reset_(device_) == 0;
}

bool RtlDevice::read(std::vector<std::uint8_t>& bytes) {
    if (!device_) return false;
    // 4 Mi complex samples give about 2.06 seconds of I/Q per decode.
    bytes.resize(2 * 4'194'304);
    for (std::size_t offset = 0; offset < bytes.size();) {
        int received = 0;
        constexpr int kBlock = 262'144;
        if (read_(device_, bytes.data() + offset, kBlock, &received) != 0 ||
            received <= 0 || received > kBlock) return false;
        offset += static_cast<std::size_t>(received);
    }
    return true;
}

void RtlDevice::close() {
    if (device_ && close_) close_(device_);
    device_ = nullptr;
    if (module_) FreeLibrary(module_);
    module_ = nullptr;
}
