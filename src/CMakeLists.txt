find_package(nmos-cpp REQUIRED)



add_library(nmos-client STATIC node_implementation.cpp node.cpp)


# 设置库的公共头文件目录
target_include_directories(nmos-client PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# add_executable(
#     nmos-client

#     main.cpp
#     node_implementation.cpp
#     node.cpp
# )

target_link_libraries(nmos-client nmos-cpp::compile-settings
nmos-cpp::nmos-cpp)
# #安装目标和头文件
install(TARGETS nmos-client
        EXPORT nmos-clientConfig
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin)
        install(FILES node.h node_implementation.h DESTINATION include)

        # 导出配置文件
install(EXPORT nmos-clientConfig
FILE nmos-clientConfig.cmake
DESTINATION lib/cmake/nmos-client)


export(TARGETS nmos-client FILE nmos-clientConfig.cmake)

