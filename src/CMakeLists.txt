find_package(nmos-cpp REQUIRED)

set(NMOS_CLIENT_CORE_SOURCES
    node_activation_auto_resolver.cpp
    node_activation_connection.cpp
    node_activation_registration.cpp
    node_activation_transport.cpp
    node_activation_transportfile.cpp
    node_callback_dispatcher.cpp
    node_connection_transport_params.cpp
    node_event_bridge.cpp
    node_implementation.cpp
    node_lifecycle_controller.cpp
    node_ptp_clock_updater.cpp
    node_resource_controller.cpp
    node_resource_factory.cpp
    node_resource_factory_receivers.cpp
    node_resource_factory_senders.cpp
    node_resource_lifecycle.cpp
    node_runtime_interface_updater.cpp
    node_sdp_service.cpp
    node_server_runtime.cpp
    node_settings.cpp
    node_stream_store.cpp
    node_stream_store_ids.cpp
    node_stream_store_receiver_ids.cpp
    node.cpp
    daemon/video_format.cpp)

set(NMOS_SYNC_DAEMON_SOURCES
    daemon/app.cpp
    daemon/config.cpp
    daemon/dto.cpp
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

if(WIN32)
    target_link_libraries(nmos-sync-daemon PRIVATE ws2_32 iphlpapi)
endif()

if(UNIX)
    install(TARGETS nmos-sync-daemon
            RUNTIME DESTINATION opt/nmos-daemon/bin)

    install(FILES
            ${CMAKE_SOURCE_DIR}/packaging/daemon_config.json
            ${CMAKE_SOURCE_DIR}/packaging/node_config.json
            DESTINATION opt/nmos-daemon/config)

    install(FILES
            ${CMAKE_SOURCE_DIR}/packaging/nmos-daemon.service
            DESTINATION etc/systemd/system)
else()
    install(TARGETS nmos-sync-daemon
            RUNTIME DESTINATION .)

    install(FILES
            ${CMAKE_SOURCE_DIR}/config/windows/node_config.json
            DESTINATION .)
    install(FILES
            ${CMAKE_SOURCE_DIR}/config/windows/daemon_config.json
            DESTINATION .)
    install(FILES
            ${CMAKE_SOURCE_DIR}/config/windows/start.bat
            DESTINATION .)
    install(FILES
            ${CMAKE_SOURCE_DIR}/config/windows/config-ui.html
            DESTINATION .)
    install(FILES
            ${CMAKE_SOURCE_DIR}/config/windows/install-service.ps1
            ${CMAKE_SOURCE_DIR}/config/windows/uninstall-service.ps1
            DESTINATION .)
endif()
