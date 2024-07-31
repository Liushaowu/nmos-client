include(FindPackageHandleStandardArgs)

find_package(PkgConfig)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(DPDK IMPORTED_TARGET GLOBAL libdpdk)
  endif ()
set(DPDK_LIBRARIES ${DPDK_LIBRARIES} CACHE STRING "The DPDK libraries." FORCE)
add_library(DPDK ALIAS PkgConfig::DPDK)

find_package_handle_standard_args(DPDK DEFAULT_MSG DPDK_LIBRARIES)