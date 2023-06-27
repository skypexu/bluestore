

# - Find libcap
# Find the cap library and includes
#
# CAP_INCLUDE_DIR - where to find blkid.h, etc.
# CAP_LIBRARIES - List of libraries when using blkid.
# CAP_FOUND - True if blkid found.

find_path(CAP_INCLUDE_DIR sys/capability.h)

find_library(CAP_LIBRARIES cap)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(cap DEFAULT_MSG CAP_LIBRARIES CAP_INCLUDE_DIR)

mark_as_advanced(CAP_LIBRARIES CAP_INCLUDE_DIR)
