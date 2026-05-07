find_package(nmos-cpp REQUIRED)

set(NMOS_CLIENT_CORE_SOURCES
    node_implementation.cpp
    node.cpp
    daemon/video_format.cpp)

set(NMOS_SYNC_DAEMON_SOURCES
    daemon/app.cpp
    daemon/config.cpp
    daemon/dto.cpp
    daemon/node_runtime.cpp
    daemon/snapshot_client.cpp
    daemon/ws_client.cpp
    daemon/reconcile_engine.cpp
    daemon/state_store.cpp
    daemon/http_debug_server.cpp)

add_executable(nmos-sync-daemon
    daemon/main.cpp
    ${NMOS_CLIENT_CORE_SOURCES}
    ${NMOS_SYNC_DAEMON_SOURCES})

target_include_directories(nmos-sync-daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/daemon)

target_link_libraries(nmos-sync-daemon PRIVATE
    nmos-cpp::compile-settings
    nmos-cpp::nmos-cpp)

install(TARGETS nmos-sync-daemon
        RUNTIME DESTINATION opt/nmos-daemon/bin)

install(FILES
        ${CMAKE_SOURCE_DIR}/packaging/daemon_config.json
        ${CMAKE_SOURCE_DIR}/packaging/node_config.json
        DESTINATION opt/nmos-daemon/config)

install(FILES
        ${CMAKE_SOURCE_DIR}/packaging/nmos-daemon.service
        DESTINATION etc/systemd/system)
