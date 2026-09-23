cmake_minimum_required(VERSION 3.16)
project(BonDriver_RTLSDR_OneSeg LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "BonDriver requires Windows")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "TVTest uses MSVC C++ RTTI for IBonDriver2; build this BonDriver with MSVC")
endif()

set(ONESEG_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../oneseg-core" CACHE PATH
    "Path to the rtl-sdr-oneseg-core checkout")
if(NOT EXISTS "${ONESEG_CORE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Clone rtl-sdr-oneseg-core and set ONESEG_CORE_DIR")
endif()

add_subdirectory("${ONESEG_CORE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/oneseg-core")
add_library(BonDriver_RTLSDR_OneSeg SHARED src/driver.cpp)
target_compile_features(BonDriver_RTLSDR_OneSeg PRIVATE cxx_std_17)
target_compile_definitions(BonDriver_RTLSDR_OneSeg PRIVATE UNICODE _UNICODE)
target_link_libraries(BonDriver_RTLSDR_OneSeg PRIVATE oneseg_core)
set_target_properties(BonDriver_RTLSDR_OneSeg PROPERTIES PREFIX "" OUTPUT_NAME "BonDriver_RTLSDR_OneSeg")

add_executable(bondriver_probe src/bondriver_probe.cpp)
target_compile_features(bondriver_probe PRIVATE cxx_std_17)
target_compile_definitions(bondriver_probe PRIVATE UNICODE _UNICODE)
