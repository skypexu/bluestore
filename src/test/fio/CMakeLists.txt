# ObjectStore
add_library(fio_ceph_objectstore SHARED fio_ceph_objectstore.cc)
target_link_libraries(fio_ceph_objectstore fio)

target_link_libraries(fio_ceph_objectstore os global)
install(TARGETS fio_ceph_objectstore DESTINATION lib)

