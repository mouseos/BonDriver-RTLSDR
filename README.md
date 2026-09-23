# BonDriver for RTL-SDR One-seg

Windows x64 BonDriver2 DLL for integration testing with TVTest. This initial driver reads **recorded Viterbi bytes from a physical RTL-SDR one-seg capture**, calls the shared [RTL-SDR one-seg core](https://github.com/mouseos/rtl-sdr-oneseg-core), and replays the recovered 188-byte TS to TVTest. No GNU Radio runtime or proprietary Realtek DLL is used.

**Current limit:** This is a replay driver. It does not yet tune the USB device or demodulate live I/Q. The shared core currently starts at Viterbi output; the portable live OFDM/Viterbi pipeline and BonDriver USB adapter remain to be implemented. TVTest seeing this driver is an integration test, not proof of live reception or video playback.

The x64 MSVC build was loaded by TVTest 0.10.0 on 2026-09-24. It displayed the captured service, but replayed the same short, damaged scene. Channel scanning exposed fewer channels than the vendor application because this build enumerates only the one recorded physical channel. These observations are expected for the replay backend and do not validate live USB tuning.

## Build

Clone this repository next to `rtl-sdr-oneseg-core`, or pass its path explicitly. **Build with MSVC x64.** TVTest performs an MSVC `dynamic_cast` on the BonDriver object, so a MinGW C++ DLL can crash TVTest even when a simple exported-function probe succeeds.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DONESEG_CORE_DIR=D:\path\to\rtl-sdr-oneseg-core
cmake --build build --config Release
```

Use a **64-bit** compiler for 64-bit TVTest. Copy `BonDriver_RTLSDR_OneSeg.dll` next to `TVTest.exe`. Copy `BonDriver_RTLSDR_OneSeg.ini.example` to `BonDriver_RTLSDR_OneSeg.ini` and set `ViterbiFile` to an absolute path of a local packed Viterbi output file and `PhysicalChannel` to its physical UHF channel. This repo contains no recordings.

Then start TVTest with `/d BonDriver_RTLSDR_OneSeg.dll`. The DLL reports one tuning space with one channel named after the configured physical UHF channel; its BonDriver2 channel index is 0. It loops the short recovered TS sample at a nominal one-seg packet rate. The current short sample may not include enough PSI or keyframes for TVTest to show video.

BonDriver ABI was checked against the [TvtPlay BonDriver_Pipe source headers](https://github.com/xtne6f/TvtPlay/tree/work/BonDriver_Pipe_src). TVTest invocation follows [TVTest's documentation](https://github.com/DBCTRADO/TVTest/blob/develop/doc/TVTest.txt).
