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

