cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBJBIG_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_jbig.c
)

SET(LIBJBIG_LIBS
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libjbig.a
)

SET(LIBJBIG_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(libjbig
        "${LIBJBIG_SRC}"
        "${LIBJBIG_LIBS}"
        []
        ""
        "${LIBJBIG_INC}"
        ""
        "1")
