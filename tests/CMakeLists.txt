find_package(nmos-cpp REQUIRED)

add_executable(refactor_seams_smoke
    refactor_seams_smoke.cpp
    ../src/node_activation_auto_resolver.cpp
    ../src/node_callback_dispatcher.cpp
    ../src/node_connection_validation.cpp
    ../src/node_connection_transport_params.cpp
    ../src/node_implementation.cpp
    ../src/node_sdp_service.cpp
    ../src/node_server_runtime.cpp
    ../src/node_settings.cpp
    ../src/node_stream_store.cpp
    ../src/node_stream_store_ids.cpp
    ../src/node_stream_store_receiver_ids.cpp
    ../src/daemon/config.cpp
    ../src/daemon/dto.cpp
    ../src/daemon/video_format.cpp
    ../src/daemon/ws_client.cpp)

target_include_directories(refactor_seams_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/daemon)

target_link_libraries(refactor_seams_smoke PRIVATE
    nmos-cpp::compile-settings
    nmos-cpp::nmos-cpp)

add_test(NAME refactor_seams_smoke COMMAND refactor_seams_smoke)

add_executable(registry_client_test
    registry_client_test.cpp
    ../src/daemon/registry_client.cpp
    ../src/daemon/sdp_format.cpp
    ../src/daemon/video_format.cpp)
target_include_directories(registry_client_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(registry_client_test PRIVATE
    nmos-cpp::compile-settings nmos-cpp::nmos-cpp)
add_test(NAME registry_client_test COMMAND registry_client_test)
set_tests_properties(registry_client_test PROPERTIES TIMEOUT 30)

add_executable(sdp_format_test
    sdp_format_test.cpp
    ../src/daemon/sdp_format.cpp
    ../src/daemon/video_format.cpp)
target_include_directories(sdp_format_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sdp_format_test PRIVATE
    nmos-cpp::compile-settings nmos-cpp::nmos-cpp)
add_test(NAME sdp_format_test COMMAND sdp_format_test)
