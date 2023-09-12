set(osd_srcs   
    osd_types.cc)

add_library(osd STATIC ${osd_srcs})
#target_link_libraries(osd
#  PUBLIC dmclock::dmclock Boost::MPL
#  PRIVATE os heap_profiler cpu_profiler fmt::fmt ${CMAKE_DL_LIBS})
