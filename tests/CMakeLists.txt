find_package(nmos-cpp REQUIRED)

add_executable(refactor_seams_smoke
    refactor_seams_smoke.cpp
    ../src/node_callback_dispatcher.cpp
    ../src/node_connection_transport_params.cpp
    ../src/node_implementation.cpp
    ../src/node_sdp_service.cpp
    ../src/node_server_runtime.cpp
    ../src/node_settings.cpp
    ../src/node_stream_store.cpp
    ../src/node_stream_store_ids.cpp
    ../src/node_stream_store_receiver_ids.cpp
    ../src/daemon/video_format.cpp)

target_include_directories(refactor_seams_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/daemon)

target_link_libraries(refactor_seams_smoke PRIVATE
    nmos-cpp::compile-settings
    nmos-cpp::nmos-cpp)

add_test(NAME refactor_seams_smoke COMMAND refactor_seams_smoke)
