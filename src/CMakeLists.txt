set(xlisp_sources
    xlansi.c
    xlapi.c
    xlbcode.h
    xlcobj.c
    xlcom.c
    xlcontext.c
    xldbg.c
    xldmem.c
    xlfasl.c
    xlftab.c
    xlfun1.c
    xlfun2.c
    xlfun3.c
    xlimage.c
    xlinit.c
    xlint.c
    xlio.c
    xlitersq.c
    xlmain.c
    xlmath.c
    xlnthread.c
    xlsync.c
    xlobj.c
    xlosint.c
    xlprint.c
    xlread.c
    xlsym.c
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/xlisp.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/xlcontext.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/xlcompat.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/xlthread.h")

if(${CMAKE_SYSTEM} STREQUAL Windows)
    list(APPEND xlisp_sources msstuff.c)
else()
    list(APPEND xlisp_sources unstuff.c)
endif()

target_sources(xlisp PRIVATE ${xlisp_sources})
target_include_directories(xlisp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Enable position-independent code for TLS compatibility with shared libraries
set_target_properties(xlisp PROPERTIES POSITION_INDEPENDENT_CODE ON)
