add_executable(tst_placeholder tst_placeholder.cpp)

target_link_libraries(tst_placeholder PRIVATE
    Qt6::Test
)

add_test(NAME tst_placeholder COMMAND tst_placeholder)

add_executable(tst_deviceflowprotocol tst_deviceflowprotocol.cpp)
target_link_libraries(tst_deviceflowprotocol PRIVATE
    kgitdoauth
    Qt6::Test
)
add_test(NAME tst_deviceflowprotocol COMMAND tst_deviceflowprotocol)
set_tests_properties(tst_deviceflowprotocol PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_authsession tst_authsession.cpp)
target_link_libraries(tst_authsession PRIVATE
    kgitdoauth
    Qt6::Test
)
add_test(NAME tst_authsession COMMAND tst_authsession)
set_tests_properties(tst_authsession PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
