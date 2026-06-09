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
