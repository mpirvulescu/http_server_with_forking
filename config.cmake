set(PROJECT_NAME "template-c")
set(PROJECT_VERSION "1.0.0")
set(PROJECT_DESCRIPTION "Template C Project")
set(PROJECT_LANGUAGE "C")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Common compiler flags
set(STANDARD_FLAGS
        -D_POSIX_C_SOURCE=200809L
        -D_XOPEN_SOURCE=700
        #-D_GNU_SOURCE
        #-D_DARWIN_C_SOURCE
        #-D__BSD_VISIBLE
        -Werror
)

set(LIBRARY_TARGETS    handler)
set(EXECUTABLE_TARGETS server dbquery)

# handler -> libhandler.so
set(handler_SOURCES
        src/handler/handler.c)
set(handler_HEADERS
        include/handler.h)

# server binary
set(server_SOURCES
        src/main.c
        src/worker.c
        src/network.c
        src/utils.c)
set(server_HEADERS
        include/main.h
        include/worker.h
        include/network.h
        include/utils.h)

# dbquery binary
set(dbquery_SOURCES
        dbquery/dbquery.c)
set(dbquery_HEADERS "")

# Detect ndbm library — gdbm_compat on Linux, nothing needed on macOS/FreeBSD
find_library(GDBM_COMPAT_LIB NAMES gdbm_compat HINTS /usr/lib /usr/local/lib)
find_library(GDBM_LIB        NAMES gdbm         HINTS /usr/lib /usr/local/lib)
find_library(DL_LIB          NAMES dl            HINTS /usr/lib /usr/local/lib)

if(GDBM_COMPAT_LIB AND GDBM_LIB)
    message(STATUS "Found gdbm_compat: ${GDBM_COMPAT_LIB}")
    message(STATUS "Found gdbm:        ${GDBM_LIB}")
    set(NDBM_LIBS gdbm_compat gdbm)
else()
    message(STATUS "gdbm_compat not found, assuming ndbm is in libc")
    set(NDBM_LIBS "")
endif()

if(DL_LIB)
    message(STATUS "Found dl: ${DL_LIB}")
    set(DL_LIBS dl)
else()
    set(DL_LIBS "")
endif()

set(handler_LINK_LIBRARIES  ${NDBM_LIBS})
set(server_LINK_LIBRARIES   ${DL_LIBS})
set(dbquery_LINK_LIBRARIES  ${NDBM_LIBS})
