include(GetGitRevisionDescription)
include(CheckCXXCompilerFlag)

# for erasure and compressor plugins
set(CEPH_INSTALL_PKGLIBDIR ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME})
set(CEPH_INSTALL_FULL_PKGLIBDIR ${CMAKE_INSTALL_FULL_LIBDIR}/${PROJECT_NAME})
# for mgr plugins
set(CEPH_INSTALL_DATADIR ${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME})
# so libceph-common can be found
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
if(NOT CMAKE_INSTALL_RPATH)
  set(CMAKE_INSTALL_RPATH "${CEPH_INSTALL_FULL_PKGLIBDIR}")
endif()

# to be compatible with configure_files shared with autoconfig
set(bindir ${CMAKE_INSTALL_FULL_BINDIR})
set(sbindir ${CMAKE_INSTALL_FULL_SBINDIR})
set(libdir ${CMAKE_INSTALL_FULL_LIBDIR})
set(sysconfdir ${CMAKE_INSTALL_FULL_SYSCONFDIR})
set(libexecdir ${CMAKE_INSTALL_FULL_LIBEXECDIR})
set(pkgdatadir ${CMAKE_INSTALL_FULL_DATADIR})
set(datadir ${CEPH_INSTALL_DATADIR})
set(prefix ${CMAKE_INSTALL_PREFIX})

# the src/.git_version file may be written out by make-dist; otherwise
# we pull the git version from .git
option(ENABLE_GIT_VERSION "build Ceph with git version string" ON)
if(ENABLE_GIT_VERSION)
  get_git_head_revision(GIT_REFSPEC CEPH_GIT_VER)
  git_describe(CEPH_GIT_NICE_VER_WITH_V --always)
  # remove leading 'v'
  string(SUBSTRING ${CEPH_GIT_NICE_VER_WITH_V} 1 -1 CEPH_GIT_NICE_VER)
  #if building from a source tarball via make-dist
  if(${CEPH_GIT_VER} STREQUAL "GITDIR-NOTFOUND")
    message(STATUS "Ceph/.git directory not found, parsing ${CMAKE_CURRENT_SOURCE_DIR}/.git_version for CEPH_GIT_VER and CEPH_GIT_NICE_VER")
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/.git_version CEPH_GIT_SHA_AND_TAG)
    list(GET CEPH_GIT_SHA_AND_TAG 0 CEPH_GIT_VER)
    list(GET CEPH_GIT_SHA_AND_TAG 1 CEPH_GIT_NICE_VER)
  endif(${CEPH_GIT_VER} STREQUAL "GITDIR-NOTFOUND")
else(ENABLE_GIT_VERSION)
  set(CEPH_GIT_VER "no_version")
  set(CEPH_GIT_NICE_VER "Development")
endif(ENABLE_GIT_VERSION)

# the src/ceph_release file is 3 lines,
#   <release number, e.g. '12' for luminous>
#   <release name, e.g. 'luminous'>
#   <release type: 'dev' for x.0.z, 'rc' or x.1.z, or 'stable' or x.2.z>
# note that the release name is semi-redundant and must match CEPH_RELEASE_*
# definitions in include/rados.h and common/ceph_strings.c.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/ceph_release CEPH_RELEASE_FILE)
list(GET CEPH_RELEASE_FILE 0 CEPH_RELEASE)
list(GET CEPH_RELEASE_FILE 1 CEPH_RELEASE_NAME)
list(GET CEPH_RELEASE_FILE 2 CEPH_RELEASE_TYPE)

# Common infrastructure
configure_file(
  ${CMAKE_SOURCE_DIR}/src/ceph_ver.h.in.cmake
  ${CMAKE_BINARY_DIR}/src/include/ceph_ver.h
  @ONLY)

add_definitions(
  -DHAVE_CONFIG_H
  -D__CEPH__
  -D_REENTRANT
  -D_THREAD_SAFE
  -D__STDC_FORMAT_MACROS
  -D_FILE_OFFSET_BITS=64
  -DBOOST_ASIO_DISABLE_THREAD_KEYWORD_EXTENSION)
if(Boost_VERSION VERSION_GREATER_EQUAL 1.74)
  add_definitions(-DBOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT)
endif()

if(LINUX)
  add_definitions("-D_GNU_SOURCE")
endif()

add_compile_options(
  -Wall
  -fno-strict-aliasing
  -fsigned-char)

if(NOT MSVC)
  add_compile_options(
    -Wtype-limits
    -Wignored-qualifiers
    -Wpointer-arith
    -Werror=format-security
    -Winit-self
    -Wno-unknown-pragmas)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-ignored-qualifiers>)
endif()

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-ftemplate-depth-1024>)

CHECK_CXX_COMPILER_FLAG("-Wpessimizing-move" COMPILER_SUPPORTS_PESSIMIZING_MOVE)
if(COMPILER_SUPPORTS_PESSIMIZING_MOVE)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wpessimizing-move>)
endif()
CHECK_CXX_COMPILER_FLAG("-Wredundant-move" COMPILER_SUPPORTS_REDUNDANT_MOVE)
if(COMPILER_SUPPORTS_REDUNDANT_MOVE)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wredundant-move>)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL GNU)
  if(WIN32)
    # require >= gcc-10 for compiling the windows port
    set(minimum_gcc_version 10)
  else()
    # require >= gcc-11 for compiling the whole tree
    set(minimum_gcc_version 10)
  endif()
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS minimum_gcc_version)
    message(FATAL_ERROR "C++20 support requires a minimum GCC version of ${minimum_gcc_version}.")
  endif()
  if(MINGW)
    # The MINGW headers are missing some "const" qualifiers.
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
  else()
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -rdynamic")
  endif()
  string(PREPEND CMAKE_CXX_FLAGS_DEBUG "-O0 ")
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wstrict-null-sentinel>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>)
  # cmake does not add '-pie' for executables even if
  # CMAKE_POSITION_INDEPENDENT_CODE is TRUE.
  if(EXE_LINKER_USE_PIE)
    if (NOT WITH_OSD_INSTRUMENT_FUNCTIONS AND NOT HAVE_SEASTAR)
      string(APPEND CMAKE_EXE_LINKER_FLAGS " -pie")
    endif()
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL Clang)
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12) # require >= clang-12
    message(FATAL_ERROR "C++20 support requires a minimum Clang version of 12.")
  endif()
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_EXPORTS_C_FLAG}")
  string(APPEND CMAKE_LINKER_FLAGS " -rdynamic -export-dynamic ${CMAKE_EXE_EXPORTS_C_FLAG}")
  string(PREPEND CMAKE_CXX_FLAGS_DEBUG "-g ")
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-inconsistent-missing-override>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-mismatched-tags>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-private-field>)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-address-of-packed-member>)
  add_compile_options(
    -Wno-unused-function
    -Wno-unused-local-typedef
    -Wno-varargs
    -Wno-gnu-designator
    -Wno-missing-braces
    -Wno-parentheses
    -Wno-deprecated-register)
  if(FREEBSD)
    # Need to use the GNU binutils linker to get versioning right.
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -fuse-ld=/usr/local/bin/ld -Wno-unused-command-line-argument")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -fuse-ld=/usr/local/bin/ld -Wno-unused-command-line-argument")
  endif()
  if(APPLE)
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -undefined dynamic_lookup")
  endif()
endif(CMAKE_CXX_COMPILER_ID STREQUAL GNU)

if(WITH_CEPH_DEBUG_MUTEX)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-DCEPH_DEBUG_MUTEX>)
endif()

include(CheckCCompilerFlag)
if(CMAKE_CXX_COMPILER_ID STREQUAL GNU)
  CHECK_C_COMPILER_FLAG("-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2" HAS_FORTIFY_SOURCE)
  if(NOT CMAKE_BUILD_TYPE STREQUAL Debug)
    if(HAS_FORTIFY_SOURCE)
      add_definitions(
        -U_FORTIFY_SOURCE
        -D_FORTIFY_SOURCE=2)
    endif()
  endif()
    CHECK_C_COMPILER_FLAG(-fstack-protector-strong HAS_STACK_PROTECT)
    if (HAS_STACK_PROTECT)
      add_compile_options(-fstack-protector-strong)
      if(WIN32)
        add_link_options(-fstack-protector-strong)
      endif(WIN32)
    endif()
endif(CMAKE_CXX_COMPILER_ID STREQUAL GNU)

CHECK_C_COMPILER_FLAG("-D_GLIBCXX_ASSERTIONS" HAS_GLIBCXX_ASSERTIONS)
if(HAS_GLIBCXX_ASSERTIONS AND CMAKE_BUILD_TYPE STREQUAL Debug)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-D_GLIBCXX_ASSERTIONS>)
endif()

include(SIMDExt)
if(HAVE_INTEL)
  if(APPLE)
    set(object_format "macho64")
  else()
    set(object_format "elf64")
  endif()
  set(CMAKE_ASM_FLAGS "-f ${object_format}")
  set(CMAKE_ASM_COMPILER ${PROJECT_SOURCE_DIR}/src/nasm-wrapper)
  if(NOT WIN32)
    # The native tools might be located even when cross compiling, which
    # might not work in this case (especially when targeting Windows).
    include(CheckNasm)
    check_nasm_support(${object_format}
      HAVE_NASM_X64
      HAVE_NASM_X64_AVX2
      HAVE_NASM_X64_AVX512)
  endif()
endif()

# require c++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
# we use `asm()` to inline assembly, so enable the GNU extension
set(CMAKE_C_EXTENSIONS ON)
set(C_STANDARD_REQUIRED ON)

include(CheckCXXSourceCompiles)
CHECK_CXX_SOURCE_COMPILES("
#include <map>
using Map = std::map<int, int>;
int main() {
  Map m;
  m.merge(Map{});
}
" HAVE_STDLIB_MAP_SPLICING)

## Handle diagnostics color if compiler supports them.
CHECK_C_COMPILER_FLAG("-fdiagnostics-color=always"
  COMPILER_SUPPORTS_DIAGNOSTICS_COLOR)

set(DIAGNOSTICS_COLOR "auto"
  CACHE STRING "Used if the C/C++ compiler supports the -fdiagnostics-color option. May have one of three values -- 'auto' (default), 'always', or 'never'. If set to 'always' and the compiler supports the option, 'make [...] | less -R' will make visible diagnostics colorization of compiler output.")

if(COMPILER_SUPPORTS_DIAGNOSTICS_COLOR)
  add_compile_options(
    "$<$<COMPILE_LANGUAGE:C>:-fdiagnostics-color=${DIAGNOSTICS_COLOR}>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fdiagnostics-color=${DIAGNOSTICS_COLOR}>")
endif()

if(MINGW)
  # Fedora and RHEL set CMAKE_DL_LIBS on MINGW, which we must un-set here.
  # Details at https://bugzilla.redhat.com/show_bug.cgi?id=2127529
  set(CMAKE_DL_LIBS "")
endif()

set(EXTRALIBS ${CMAKE_DL_LIBS})
if(HAVE_POSIX_TIMERS)
  list(APPEND EXTRALIBS ${RT_LIBRARY})
endif()
if(LINUX OR APPLE)
  set(LIB_RESOLV resolv)
  list(APPEND EXTRALIBS ${LIB_RESOLV})
endif()

if(${ENABLE_COVERAGE})
  find_program(HAVE_GCOV gcov)
  if(NOT HAVE_GCOV)
    message(FATAL_ERROR "Coverage Enabled but gcov Not Found")
  endif()
  add_compile_options(
    -fprofile-arcs
    -ftest-coverage
    -O0)
  list(APPEND EXTRALIBS gcov)
endif(${ENABLE_COVERAGE})

add_subdirectory(json_spirit)

include_directories(SYSTEM "${CMAKE_SOURCE_DIR}/src/xxHash")

# use the rapidjson headers from s3select's submodule
if(NOT TARGET RapidJSON::RapidJSON)
  add_library(RapidJSON::RapidJSON INTERFACE IMPORTED)
  set_target_properties(RapidJSON::RapidJSON PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/s3select/rapidjson/include")
endif()

option(WITH_FMT_HEADER_ONLY "use header-only version of fmt library" OFF)
set(WITH_FMT_VERSION "7.0.0" CACHE
  STRING "build with fmt version")
find_package(fmt ${WITH_FMT_VERSION} QUIET)
if(fmt_FOUND)
  include_directories(SYSTEM "${fmt_INCLUDE_DIR}")
else()
  message(STATUS "Could not find fmt, will build it")
  set(old_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS FALSE)
  add_subdirectory(fmt)
  set(BUILD_SHARED_LIBS ${old_BUILD_SHARED_LIBS})
  unset(old_BUILD_SHARED_LIBS)
  target_compile_definitions(fmt PUBLIC
    $<$<BOOL:${WIN32}>:FMT_USE_TZSET=0>)
  include_directories(SYSTEM "${CMAKE_SOURCE_DIR}/src/fmt/include")
endif()

# in osd/PeeringState.h, the number of elements in PeeringState::Active::reactions
# is now 21 which exceeds the default value of BOOST_MPL_LIMIT_VECTOR_SIZE, which
# is 20. so we need to override it. see
# https://www.boost.org/doc/libs/1_74_0/libs/mpl/doc/refmanual/limit-list-size.html
# link with this library, if your code includes osd/PeeringState.h (indirectly)
add_library(Boost::MPL INTERFACE IMPORTED)
set_target_properties(Boost::MPL PROPERTIES
  INTERFACE_COMPILE_DEFINITIONS
    "BOOST_MPL_CFG_NO_PREPROCESSED_HEADERS;BOOST_MPL_LIMIT_LIST_SIZE=30")

include(CheckTypeSize)
set(CMAKE_EXTRA_INCLUDE_FILES "sys/time.h")
CHECK_TYPE_SIZE(suseconds_t SUSECONDS_T)
unset(CMAKE_EXTRA_INCLUDE_FILES)

set(libcommon_files
  ${CMAKE_BINARY_DIR}/src/include/ceph_ver.h
  ceph_ver.c
  global/global_context.cc
  xxHash/xxhash.c
  common/error_code.cc
  common/tracer.cc
  log/Log.cc
  osd/osd_types.cc
  )

set_source_files_properties(ceph_ver.c
  APPEND PROPERTY OBJECT_DEPENDS ${CMAKE_BINARY_DIR}/src/include/ceph_ver.h)
add_library(common-objs OBJECT ${libcommon_files})
target_compile_definitions(common-objs PRIVATE
  $<TARGET_PROPERTY:fmt::fmt,INTERFACE_COMPILE_DEFINITIONS>)
add_dependencies(common-objs legacy-option-headers)

CHECK_C_COMPILER_FLAG("-fvar-tracking-assignments" HAS_VTA)
add_subdirectory(common)
add_subdirectory(msg)
add_subdirectory(arch)
add_subdirectory(extblkdev)

set(ceph_common_objs
  $<TARGET_OBJECTS:common-common-objs>
  $<TARGET_OBJECTS:common-options-objs>
  $<TARGET_OBJECTS:common-msg-objs>
  $<TARGET_OBJECTS:common_buffer_obj>
  $<TARGET_OBJECTS:common_texttable_obj>
  $<TARGET_OBJECTS:compressor_objs>
  $<TARGET_OBJECTS:common-objs>
  $<TARGET_OBJECTS:common_mountcephfs_objs>
  )
set(ceph_common_deps
  json_spirit 
#  erasure_code
  extblkdev
  arch
  crc32
  ${LIB_RESOLV}
  Boost::thread
  Boost::system
  Boost::random
  Boost::program_options
  Boost::date_time
  Boost::iostreams
  StdFilesystem::filesystem
  fmt::fmt
  ${BLKID_LIBRARIES}
  ${Backtrace_LIBRARIES}
  ${BLKIN_LIBRARIES}
  ${CRYPTO_LIBS}
  ${GSSAPI_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
  ${CMAKE_DL_LIBS})
if(HAVE_UDEV)
  list(APPEND ceph_common_deps ${UDEV_LIBRARIES})
endif()

if(NOT WITH_SYSTEM_BOOST)
  list(APPEND ceph_common_deps ${ZLIB_LIBRARIES})
endif()

if(HAVE_QATZIP)
  list(APPEND ceph_common_deps ${qatzip_LIBRARIES})
endif()

if(WITH_DPDK)
  list(APPEND ceph_common_deps common_async_dpdk)
endif()

if(WIN32)
  list(APPEND ceph_common_deps ws2_32 mswsock iphlpapi bcrypt)
  list(APPEND ceph_common_deps dlfcn_win32)
endif()

if(WITH_BLUESTORE_PMEM OR WITH_RBD_RWL)
  if(WITH_SYSTEM_PMDK)
    set(pmdk_COMPONENTS)
    if(WITH_BLUESTORE_PMEM)
      list(APPEND pmdk_COMPONENTS pmem)
    endif()
    if(WITH_RBD_RWL)
      list(APPEND pmdk_COMPONENTS pmemobj)
    endif()
    find_package(pmdk 1.8 REQUIRED COMPONENTS ${pmdk_COMPONENTS})
  else()
    include(Buildpmdk)
    if(WITH_BLUESTORE_PMEM)
      set(enable_ndctl ON)
      find_package(ndctl 63 REQUIRED)
      find_package(daxctl 63 REQUIRED)
    else()
      set(enable_ndctl OFF)
    endif()
    build_pmdk(${enable_ndctl})
  endif()
endif()

add_library(common STATIC ${ceph_common_objs})
target_link_libraries(common ${ceph_common_deps})
add_dependencies(common legacy-option-headers)

if (WIN32)
  # Statically building ceph-common on Windows fails. We're temporarily
  # reverting this: 22fefb2338cfc4fcb03ece3cbf77aa964a7f17f2
  add_library(ceph-common SHARED ${ceph_common_objs})
else()
  add_library(ceph-common ${CEPH_SHARED} ${ceph_common_objs})
endif()

target_link_libraries(ceph-common ${ceph_common_deps})
if(ENABLE_COVERAGE)
  target_link_libraries(ceph-common gcov)
endif(ENABLE_COVERAGE)

add_dependencies(ceph-common legacy-option-headers)

# appease dpkg-shlibdeps
set_target_properties(ceph-common PROPERTIES
  SOVERSION 2
  SKIP_RPATH TRUE)
if(NOT APPLE AND NOT FREEBSD)
  # Apple uses Mach-O, not ELF. so this option does not apply to APPLE.
  #
  # prefer the local symbol definitions when binding references to global
  # symbols. otherwise we could reference the symbols defined by the application
  # with the same name, instead of using the one defined in libceph-common.
  # in other words, we require libceph-common to use local symbols, even if redefined
  # in application".
  set_property(
    TARGET ceph-common
    APPEND APPEND_STRING
    PROPERTY LINK_FLAGS "-Wl,-Bsymbolic -Wl,-Bsymbolic-functions")
endif()

if(MINGW)
  install(
    TARGETS ceph-common
    RUNTIME
    DESTINATION ${CEPH_INSTALL_PKGLIBDIR})
else()
  install(
    TARGETS ceph-common
    LIBRARY
    DESTINATION ${CEPH_INSTALL_PKGLIBDIR}
    NAMELINK_SKIP)
endif()

if(${WITH_LTTNG})
  add_subdirectory(tracing)
  add_dependencies(common-objs oprequest-tp)
endif(${WITH_LTTNG})

add_subdirectory(global)

# heal_profiler
add_subdirectory(perfglue)

add_subdirectory(include)

# make rocksdb statically

if(NOT WITH_SYSTEM_ROCKSDB)
  include(BuildRocksDB)
  build_rocksdb()
endif(NOT WITH_SYSTEM_ROCKSDB)

# OSD/ObjectStore

include(TestBigEndian)
test_big_endian(CEPH_BIG_ENDIAN)
if(NOT CEPH_BIG_ENDIAN)
  set(CEPH_LITTLE_ENDIAN 1)
endif()

add_subdirectory(kv)
add_subdirectory(os)

if(NOT WIN32)
add_subdirectory(blk)
endif(NOT WIN32)

#add_subdirectory(erasure-code)

# Support/Tools
if(WITH_TESTS)
  option(WITH_SYSTEM_GTEST "require and build with system gtest and gmock" OFF)
  if(WITH_SYSTEM_GTEST)
    find_package(GTest REQUIRED)
    find_package(GMock REQUIRED)
  else()
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(googletest)
    add_library(GMock::GMock ALIAS gmock)
    add_library(GMock::Main ALIAS gmock_main)
    target_include_directories(gmock INTERFACE
      $<TARGET_PROPERTY:gtest,INTERFACE_INCLUDE_DIRECTORIES>)
    target_include_directories(gmock_main INTERFACE
      $<TARGET_PROPERTY:gtest,INTERFACE_INCLUDE_DIRECTORIES>)
    add_library(GTest::GTest ALIAS gtest)
    add_library(GTest::Main ALIAS gtest_main)
  endif()
endif(WITH_TESTS)

add_subdirectory(compressor)

#add_subdirectory(tools)

#if(WITH_TESTS)
  add_subdirectory(test)
#endif()

add_subdirectory(crypto)

# Now create a usable config.h
configure_file(
  ${CMAKE_SOURCE_DIR}/src/include/config-h.in.cmake
  ${CMAKE_BINARY_DIR}/include/acconfig.h
)

if(WITH_LTTNG)
  add_dependencies(vstart tracepoint_libraries)
endif(WITH_LTTNG)

if (IS_DIRECTORY "${PROJECT_SOURCE_DIR}/.git")
  add_custom_target(
    git-update
    COMMAND git submodule sync
    COMMAND git submodule update --force --init --recursive
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endif()

#add_subdirectory(script)

if (FALSE)
# add doxygen target
find_package(Doxygen)
if(DOXYGEN_FOUND)
  set(DOXYGEN_FILE_PATTERNS *.cc *.c *.cpp *.C *.cxx *.c++ *.CC *.H *.h *.hh *.hpp)
  set(DOXYGEN_SOURCE_BROWSER YES)
  set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
  # enabling clang slows down doxygen significantly
  set(DOXYGEN_CLANG_ASSISTED_PARSING NO)
  set(DOXYGEN_CLANG_DATABASE_PATH "${PROJECT_BINARY_DIR}")
  set(DOXYGEN_BUILTIN_STL_SUPPORT YES)
  set(DOXYGEN_RECURSIVE YES)
  set(DOXYGEN_QUIET YES)
  set(DOXYGEN_GENERATE_LATEX NO)
  set(DOXYGEN_GENERATE_XML NO)
  set(DOXYGEN_GENERATE_HTML YES)
  set(DOXYGEN_PROJECT_NAME Ceph)
  set(DOXYGEN_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/build-doc/doxygen")
  set(DOXYGEN_STRIP_FROM_PATH "src/")
  set(DOXYGEN_STRIP_FROM_INC_PATH "src/include")
  set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
  set(DOXYGEN_HAVE_DOT NO)
  set(DOXYGEN_VERBATIM_HEADERS YES)
  doxygen_add_docs(doxygen
    common
    compressor
    crypto
    erasure-code
    global
    include
    json_spirit
    log
    messages
    msg
    os
    perfglue

    COMMENT "Generate C++ documentation")
endif()
endif(FALSE)

#add_subdirectory(osd)
add_subdirectory(example)
