#include "rtl_device.h"

RtlDevice::~RtlDevice() { close(); }

bool RtlDevice::open(const std::wstring& library, int gain_tenths_db) {
    close();
    library_ = library;
    module_ = library.size() > 2 && library[1] == L':'
        ? LoadLibraryExW(library.c_str(), nullptr,
                         LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)
        : LoadLibraryW(library.c_str());
    if (!module_) return false;
    auto symbol = [this](const char* name) { return GetProcAddress(module_, name); };
    open_ = reinterpret_cast<Open>(symbol("rtlsdr_open"));
    close_ = reinterpret_cast<Close>(symbol("rtlsdr_close"));
    rate_ = reinterpret_cast<SetU32>(symbol("rtlsdr_set_sample_rate"));
    frequency_ = reinterpret_cast<SetU32>(symbol("rtlsdr_set_center_freq"));
    gain_mode_ = reinterpret_cast<SetInt>(symbol("rtlsdr_set_tuner_gain_mode"));
    gain_ = reinterpret_cast<SetInt>(symbol("rtlsdr_set_tuner_gain"));
    digital_agc_ = reinterpret_cast<SetInt>(symbol("rtlsdr_set_agc_mode"));
    vendor_if_ = reinterpret_cast<SetInt>(symbol("rtlsdr_set_vendor_isdb_if"));
    fc0013_if_gain_reg_ = reinterpret_cast<SetInt>(symbol("rtlsdr_set_fc0013_if_gain_reg"));
    vendor_fc0013_mode_ = reinterpret_cast<Reset>(symbol("rtlsdr_set_vendor_fc0013_mode"));
    reset_ = reinterpret_cast<Reset>(symbol("rtlsdr_reset_buffer"));
    read_async_ = reinterpret_cast<ReadAsync>(symbol("rtlsdr_read_async"));
    cancel_async_ = reinterpret_cast<CancelAsync>(symbol("rtlsdr_cancel_async"));
    if (!open_ || !close_ || !rate_ || !frequency_ || !gain_mode_ ||
        !gain_ || !digital_agc_ || !vendor_if_ || !fc0013_if_gain_reg_ ||
        !vendor_fc0013_mode_ || !reset_ || !read_async_ || !cancel_async_ ||
        open_(&device_, 0) != 0) {
        close();
        return false;
    }
    gain_tenths_db_ = gain_tenths_db;
    if (rate_(device_, 2'031'746) != 0 ||
        vendor_fc0013_mode_(device_) != 0 ||
        gain_mode_(device_, 1) != 0 || gain_(device_, gain_tenths_db_) != 0 ||
        digital_agc_(device_, 2) != 0) {
        close();
        return false;
    }
    return true;
}

bool RtlDevice::reopen() {
    if (!module_ || !open_ || !close_) return false;
    if (device_) close_(device_);
    device_ = nullptr;
    if (open_(&device_, 0) != 0) return false;
    if (rate_(device_, 2'031'746) != 0 ||
        vendor_fc0013_mode_(device_) != 0 ||
        gain_mode_(device_, 1) != 0 || gain_(device_, gain_tenths_db_) != 0 ||
        digital_agc_(device_, 2) != 0) return false;
    return true;
}

bool RtlDevice::tune(unsigned physical_channel) {
    if (!device_ || physical_channel < 13 || physical_channel > 52) return false;
    const auto hz = static_cast<std::uint32_t>(473'142'857 +
                         (physical_channel - 13) * 6'000'000);
    const auto tuner_hz = hz - 600'000;
    const int tuner_gain = physical_channel == 13 || physical_channel == 15
        ? 71 : gain_tenths_db_;
    const int if_gain_reg = physical_channel == 13 || physical_channel == 15
        ? 0x0f : 0x0a;
    return frequency_(device_, tuner_hz) == 0 &&
           gain_(device_, tuner_gain) == 0 &&
           vendor_if_(device_, -600) == 0 &&
           fc0013_if_gain_reg_(device_, if_gain_reg) == 0 &&
           reset_(device_) == 0;
}

bool RtlDevice::run_async(Callback callback, void* context) {
    return device_ && read_async_(device_, callback, context, 0, 262'144) == 0;
}

void RtlDevice::cancel_async() {
    if (device_ && cancel_async_) cancel_async_(device_);
}

void RtlDevice::close() {
    if (device_ && close_) close_(device_);
    device_ = nullptr;
    if (module_) FreeLibrary(module_);
    module_ = nullptr;
}
