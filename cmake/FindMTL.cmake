include(FindPackageHandleStandardArgs)

find_package(PkgConfig)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(MTL IMPORTED_TARGET GLOBAL mtl)
  endif ()
set(MTL_LIBRARIES ${MTL_LIBRARIES} CACHE STRING "The Media-Transport-Library libraries." FORCE)
add_library(MTL ALIAS PkgConfig::MTL)

find_package_handle_standard_args(MTL DEFAULT_MSG MTL_LIBRARIES)