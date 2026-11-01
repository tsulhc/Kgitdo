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

add_executable(tst_authtransport tst_authtransport.cpp)
target_link_libraries(tst_authtransport PRIVATE
    kgitdoauth
    Qt6::Test
)
add_test(NAME tst_authtransport COMMAND tst_authtransport)
set_tests_properties(tst_authtransport PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_authhttpmapping tst_authhttpmapping.cpp)
target_link_libraries(tst_authhttpmapping PRIVATE
    kgitdoauth
    Qt6::Test
)
add_test(NAME tst_authhttpmapping COMMAND tst_authhttpmapping)
set_tests_properties(tst_authhttpmapping PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_walletstore tst_walletstore.cpp)
target_link_libraries(tst_walletstore PRIVATE
    kgitdoauth
    Qt6::Test
)
add_test(NAME tst_walletstore COMMAND tst_walletstore)
set_tests_properties(tst_walletstore PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_issuequery tst_issuequery.cpp)
target_link_libraries(tst_issuequery PRIVATE
    kgitdoissues
    Qt6::Test
)
add_test(NAME tst_issuequery COMMAND tst_issuequery)
set_tests_properties(tst_issuequery PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_githubclient tst_githubclient.cpp)
target_link_libraries(tst_githubclient PRIVATE
    kgitdoissues
    Qt6::Test
)
add_test(NAME tst_githubclient COMMAND tst_githubclient)
set_tests_properties(tst_githubclient PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_issuemodel tst_issuemodel.cpp)
target_link_libraries(tst_issuemodel PRIVATE
    kgitdoissues
    Qt6::Test
)
add_test(NAME tst_issuemodel COMMAND tst_issuemodel)
set_tests_properties(tst_issuemodel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_issuecontroller tst_issuecontroller.cpp)
target_link_libraries(tst_issuecontroller PRIVATE
    kgitdoissues
    Qt6::Test
)
add_test(NAME tst_issuecontroller COMMAND tst_issuecontroller)
set_tests_properties(tst_issuecontroller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_qmlsecurity tst_qmlsecurity.cpp)
target_link_libraries(tst_qmlsecurity PRIVATE Qt6::Test)
target_compile_definitions(tst_qmlsecurity PRIVATE KGITDO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME tst_qmlsecurity COMMAND tst_qmlsecurity)
