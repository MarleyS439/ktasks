include(ECMAddTests)

ecm_add_tests(
    databasetest.cpp
    icscodectest.cpp
    naturalparsertest.cpp
    recurrencetest.cpp
    mergetest.cpp
    LINK_LIBRARIES ktaskscore Qt6::Test
)

ecm_add_test(synctest.cpp mockcaldavserver.cpp
    TEST_NAME synctest
    LINK_LIBRARIES ktaskscore Qt6::Test Qt6::Network
)

ecm_add_test(apptest.cpp
    TEST_NAME apptest
    LINK_LIBRARIES ktasksapp KF6::I18nQml Qt6::Test
)
target_compile_definitions(apptest PRIVATE KTASKS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Manual end-to-end check against a real CalDAV server; see the README.
add_executable(livesync livesync.cpp)
target_link_libraries(livesync ktaskscore)
