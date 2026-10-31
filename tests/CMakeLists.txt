add_executable(tst_placeholder tst_placeholder.cpp)

target_link_libraries(tst_placeholder PRIVATE
    Qt6::Test
)

add_test(NAME tst_placeholder COMMAND tst_placeholder)
