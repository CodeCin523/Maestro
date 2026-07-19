add_executable(
    bench_maestro_logger
    bench_logger.c
)

target_link_libraries(
    bench_maestro_logger PRIVATE
    HARP::Harp
    Maestro
)

target_include_directories(
    bench_maestro_logger PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

add_test(
    NAME bench_maestro_logger
    COMMAND bench_maestro_logger
)


add_executable(
    test_maestro_logger
    test_logger.c
)

target_link_libraries(
    test_maestro_logger PRIVATE
    HARP::Harp
    Maestro
)

target_include_directories(
    test_maestro_logger PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

add_test(
    NAME test_maestro_logger
    COMMAND test_maestro_logger
)


add_executable(
    test_maestro_path
    test_path.c
)

target_link_libraries(
    test_maestro_path PRIVATE
    HARP::Harp
    Maestro
)

target_include_directories(
    test_maestro_path PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

add_test(
    NAME test_maestro_path
    COMMAND test_maestro_path
)


add_executable(
    test_maestro_window
    test_window.c
)

target_link_libraries(
    test_maestro_window PRIVATE
    HARP::Harp
    Maestro
)

target_include_directories(
    test_maestro_window PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

add_test(
    NAME test_maestro_window
    COMMAND test_maestro_window
)


add_executable(
    test_maestro_vulkan
    test_vulkan.c
)

target_link_libraries(
    test_maestro_vulkan PRIVATE
    HARP::Harp
    Maestro
    Vulkan::Vulkan
)

target_include_directories(
    test_maestro_vulkan PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

add_test(
    NAME test_maestro_vulkan
    COMMAND test_maestro_vulkan
)


add_executable(
    test_maestro_sequencer
    test_sequencer.c
)

target_link_libraries(
    test_maestro_sequencer PRIVATE
    HARP::Harp
    Maestro
    Vulkan::Vulkan
)

target_include_directories(
    test_maestro_sequencer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

add_test(
    NAME test_maestro_sequencer
    COMMAND test_maestro_sequencer
)


add_executable(
    test_maestro_surface
    test_surface.c
)

target_link_libraries(
    test_maestro_surface PRIVATE
    HARP::Harp
    Maestro
    Vulkan::Vulkan
)

target_include_directories(
    test_maestro_surface PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

add_test(
    NAME test_maestro_surface
    COMMAND test_maestro_surface
)


add_executable(
    test_maestro_swapchain
    test_swapchain.c
)

target_link_libraries(
    test_maestro_swapchain PRIVATE
    HARP::Harp
    Maestro
    Vulkan::Vulkan
)

target_include_directories(
    test_maestro_swapchain PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

add_test(
    NAME test_maestro_swapchain
    COMMAND test_maestro_swapchain
)

