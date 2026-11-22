add_library(ktaskscore STATIC
    task.h task.cpp
    crypto.h crypto.cpp
    database.h database.cpp
    icscodec.h icscodec.cpp
    naturalparser.h naturalparser.cpp
    recurrence.h recurrence.cpp
    merge.h merge.cpp
    caldavclient.h caldavclient.cpp
    syncengine.h syncengine.cpp
    logging.h logging.cpp
)
target_include_directories(ktaskscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ktaskscore PUBLIC Qt6::Core Qt6::Sql Qt6::Network KF6::CalendarCore OpenSSL::Crypto)
