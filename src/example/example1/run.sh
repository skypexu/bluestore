#first, cd to build directory, set compressor path
export LD_LIBRARY_PATH=`pwd`/lib
#run it
bin/example1 -i 1 --conf `pwd`/ceph-bluestore.conf 
