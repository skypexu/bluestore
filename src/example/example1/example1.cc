#include "common/ceph_argparse.h"
#include "global/global_init.h" 
#include "os/ObjectStore.h"
#include "global/global_context.h"
#include "perfglue/heap_profiler.h"
#include "common/errno.h"
#include <unistd.h>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <semaphore.h>

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_

using std::string;

static void generic_usage(bool is_server)
{                                                                               
	std::cout <<
		"  --conf/-c FILE    read configuration from the given configuration file" << std::endl <<
		(is_server ?
		 "  --id/-i ID        set ID portion of my name" :
		 "  --id ID           set ID portion of my name") << std::endl <<
		"  --name/-n TYPE.ID set name" << std::endl <<
		//    "  --cluster NAME    set cluster name (default: ceph)" << std::endl <<
		//    "  --setuser USER    set uid to user or uid (and gid to user's gid)" << std::endl <<
		//    "  --setgroup GROUP  set gid to group or gid" << std::endl <<
		"  --version         show version and quit" << std::endl
		<< std::endl;
}

static void usage()
{
	std::cout << "usage: example1 -i <ID> [flags]\n";
	generic_usage(true);
}

#define COLLECTION_COUNT 10

// struct to store ObjectStore collection info
struct MyCollection {
	spg_t pg;
	coll_t cid;
	ObjectStore::CollectionHandle ch;
	std::unique_ptr<std::mutex> lock;

	static constexpr int64_t MIN_POOL_ID = 0x0000ffffffffffff;

	MyCollection(const spg_t& pg, ObjectStore::CollectionHandle _ch)
		: pg(pg), cid(pg), ch(_ch),
	lock(new std::mutex) {                                                  
	}  
};

struct Object {                                                                 
  ghobject_t oid;
  MyCollection& coll;

  Object(const char* name, MyCollection& coll)
    : oid(hobject_t(name, "", CEPH_NOSNAP, coll.pg.ps(), coll.pg.pool(), "")),
      coll(coll) {}
};

struct Result {
	sem_t sem_;
	int result_;

	Result() {
		sem_init(&sem_, 0, 0);
		result_ = -1;
	}

	~Result() {
		sem_destroy(&sem_);
	}

	void wait() {
		while(sem_wait(&sem_) == -1 && errno == EINTR)
			;
	}

	void signal(int r) {
		result_ = r;
		sem_post(&sem_);
	}

	int result() const {
		return result_;
	}
};


std::vector<MyCollection> collections;

int destroy_collections(
	std::unique_ptr<ObjectStore>& os,
	std::vector<MyCollection>& collections);

int init_collections(std::unique_ptr<ObjectStore>& os,                          
                      uint64_t pool,                                            
                      std::vector<MyCollection>& collections,
                      uint64_t count)
{
	ceph_assert(count > 0);
	collections.reserve(count);

	const int split_bits = cbits(count - 1);

	{
		// propagate Superblock object to ensure proper functioning of tools that
		// need it. E.g. ceph-objectstore-tool
		coll_t cid(coll_t::meta());
		bool exists = os->collection_exists(cid);
		if (!exists) {
			auto ch = os->create_new_collection(cid);

			OSDSuperblock superblock;
			bufferlist bl;
			encode(superblock, bl);

			ObjectStore::Transaction t;
			t.create_collection(cid, split_bits);
			t.write(cid, OSD_SUPERBLOCK_GOBJECT, 0, bl.length(), bl);
			int r = os->queue_transaction(ch, std::move(t));

			if (r < 0) {
				std::cerr << "Failure to write OSD superblock: " << cpp_strerror(-r) << std::endl;
				return r;
			}
		}
	}

	for (uint32_t i = 0; i < count; i++) {
		auto pg = spg_t{pg_t{i, pool}};
		coll_t cid(pg);

		bool exists = os->collection_exists(cid);
		auto ch = exists ?
			os->open_collection(cid) :
			os->create_new_collection(cid) ;

		collections.emplace_back(pg, ch);

		ObjectStore::Transaction t;
		auto& coll = collections.back();
		if (!exists) {
			t.create_collection(coll.cid, split_bits);
			ghobject_t pgmeta_oid(coll.pg.make_pgmeta_oid());
			t.touch(coll.cid, pgmeta_oid);
			int r = os->queue_transaction(coll.ch, std::move(t));
			if (r) {
				std::cerr << "Engine init failed with " << cpp_strerror(-r) << std::endl;
				destroy_collections(os, collections);
				return r;
			}
		}
	}
	return 0;
}

int destroy_collections(
		std::unique_ptr<ObjectStore>& os,
		std::vector<MyCollection>& collections)
{
	ObjectStore::Transaction t;
	bool failed = false;
	// remove our collections
	for (auto& coll : collections) {
		ghobject_t pgmeta_oid(coll.pg.make_pgmeta_oid());
		t.remove(coll.cid, pgmeta_oid);
		t.remove_collection(coll.cid);
		int r = os->queue_transaction(coll.ch, std::move(t));
		if (r && !failed) {
			derr << "Engine cleanup failed with " << cpp_strerror(-r) << dendl;
			failed = true;
		}
	}
	return 0;
}

int create_object(std::unique_ptr<ObjectStore>& os, Object& o, size_t file_size)
{
	ObjectStore::Transaction t;

	struct stat st;                                                           
        int ret = os->stat(o.coll.ch, o.oid, &st);     
	printf("stat: %d\n", ret);
	t.touch(o.coll.cid, o.oid);
	t.truncate(o.coll.cid, o.oid, file_size);

	Result res;
	t.register_on_applied(                                          
                                new LambdaContext([&] (int r) {                 
                                        res.signal(r);                           
                                        })                                      
                                );      
	ret = os->queue_transaction(o.coll.ch, std::move(t));
	res.wait();
	return ret;
}

#define WAIT_COMMIT 0
#define WAIT_APPLIED 1
int write_object(std::unique_ptr<ObjectStore>& os, Object& o, off_t off, char *buf, size_t len, int wait)
{
	bufferlist bl;

    	bl.push_back(buffer::copy(buf, len));
	
	ObjectStore::Transaction t;
	Result r1;

	t.write(o.coll.cid, o.oid, off, len, bl, 0);
	if (wait == WAIT_COMMIT) {
		t.register_on_commit(
				new LambdaContext([&] (int r) {
					r1.signal(r);
					})
				);
	} else {
		t.register_on_applied(
				new LambdaContext([&] (int r) {
					r1.signal(r);
					})
				);
	}

	int r = os->queue_transaction(o.coll.ch, std::move(t));
	if (r) {
		std::cerr << "can not queue transaction" << __func__ << endl;
		return r;
	}

	r1.wait();
	return r1.result();
}

int clone_range(std::unique_ptr<ObjectStore>& os,
		Object& o1, Object& o2, uint64_t srcoff, uint64_t srclen,
		uint64_t dstoff, int wait)
{
	ObjectStore::Transaction t;
	Result r1;

	t.clone_range(o1.coll.cid, o1.oid, o2.oid, srcoff, srclen, dstoff);
	if (wait == WAIT_COMMIT) {
		t.register_on_commit(
			new LambdaContext([&] (int r) {
				r1.signal(r);
			})
		);
	} else {
		t.register_on_applied(
			new LambdaContext([&] (int r) {
				r1.signal(r);
			})
		);
	}

	int r = os->queue_transaction(o1.coll.ch, std::move(t));
	if (r) {
		printf("%s: can not queue transaction\n", __func__);
		return r;
	}

	r1.wait();
	return r1.result();
}
	
int benchmark_clone_range(std::unique_ptr<ObjectStore>& os, MyCollection &coll, int id, int repeat,
	int block_size)
{
	char name[128];

	snprintf(name, sizeof(name), "object_%d_0", id);
	Object o1(name, coll);
	snprintf(name, sizeof(name), "object_%d_1", id);
	Object o2(name, coll);

	char buf[block_size];
	memset(buf, 0, sizeof(buf));
	strcpy(buf, "hello world");
	int count = repeat;
	int ret = 0;
	auto start = ceph_clock_now();
	while (count-- > 0) {
		for (int i = 0; i < 128; ++i) {
			ret = write_object(os, o1, i * block_size, buf, block_size, WAIT_COMMIT);
			if (ret) {
				printf("can not write object: %d\n", ret);
				exit(1);
			}

			ret = write_object(os, o2, i * block_size, buf, block_size, WAIT_COMMIT);
			if (ret) {
				printf("can not write object: %d\n", ret);
				exit(1);
			}
		}
	}
	auto end = ceph_clock_now();
	auto elapsed1 = (double)(end - start);

	count = repeat;
	start = ceph_clock_now();
	while (count-- > 0) {
		for (int i = 0; i < 128; ++i) {
			ret = write_object(os, o1, i * block_size, buf, block_size, WAIT_COMMIT);
			if (ret) {
				printf("can not write object: %d\n", ret);
				exit(1);
			}

			ret = clone_range(os, o1, o2, i * block_size, block_size, i * block_size, WAIT_COMMIT);
			if (ret) {
				printf("can not clone_range: %d\n", ret);
				exit(1);
			}
		}
	}
	end = ceph_clock_now();
	auto elapsed2 = (double)(end - start);
	printf("thread %d: double write: %f(s), clone_range: %f(s)\n", id, elapsed1, elapsed2);
	return 0;
}

// test clone_range correctness
int test_clone_range(std::unique_ptr<ObjectStore>& os, MyCollection &coll)
{
	Object o1("object1", coll);
	Object o2("object2", coll);

	int ret = create_object(os, o1, 0);
	if (ret) {
		printf("can not create object1\n");
		exit(1);
	}
	ret = create_object(os, o2, 0);
	if (ret) {
		printf("can not create object2\n");
		exit(1);
	}

	char buf[64 * 1024];
	char buf2[64 * 1024]; // same size as buf
	memset(buf, 0, sizeof(buf));
	strcpy(buf, "hello world");

	ret = write_object(os, o1, 4096, buf, sizeof(buf), WAIT_COMMIT);
	if (ret) {
		printf("can not write object: %d\n", ret);
		exit(1);
	}
	bufferlist bl;
    	ret = os->read(o1.coll.ch, o1.oid, 4096, sizeof(buf), bl);
	if (ret != sizeof(buf)) {
		printf("can not read object: %d\n", ret);
		exit(1);
	}
	if (!bl.contents_equal(buf, sizeof(buf))) {
		printf("inconst data with buf\n");
		exit(1);
	}

	ret = clone_range(os, o1, o2, 4096, sizeof(buf), 0, WAIT_COMMIT);
	if (ret) {
		printf("clone range error, %d\n", ret);
		exit(1);
	}

	memcpy(buf2, buf, sizeof(buf));

	memset(buf, 0, sizeof(buf));
	strcpy(buf, "xxxx");

	// overwrite object1 
	ret = write_object(os, o1, 4096, buf, sizeof(buf), WAIT_COMMIT);
	if (ret) {
		printf("can not write object: %d\n", ret);
		exit(1);
	}

	bl.clear();

	// read object2
    	ret = os->read(o1.coll.ch, o2.oid, 0, sizeof(buf), bl);
	if (ret != sizeof(buf)) {
		printf("can not read object: %d\n", ret);
		exit(1);
	}

	// check if o2 is changed, should not!
	if (!bl.contents_equal(buf2, sizeof(buf2))) {
		printf("inconsistent data with buf2 after ovewritting o1\n");
		exit(1);
	}
	printf("test clone_range success\n");
	return 0;
}

int main(int argc, const char **argv)
{
	auto args = argv_to_vec(argc, argv);

	if (args.empty()) {
		std::cerr << argv[0] << ": -h or --help for usage" << std::endl;
		exit(1);
	}

	std::map<string,string> defaults = { };

	if (ceph_argparse_need_usage(args)) {
		usage();
		exit(0);
	}

	int nthread = 1;
	int block_ksize = 64;
	for (std::vector<const char*>::iterator i = args.begin(); i != args.end();) {
		if (ceph_argparse_double_dash(args, i)) {
			break;
		} else if (ceph_argparse_witharg(args, i, &nthread, std::cout, "--threads", (char*)NULL)) {
		} else if (ceph_argparse_witharg(args, i, &block_ksize, std::cout, "--block_ksize", (char*)NULL)) {
		} else {
			i++;
		}
	}
	printf("threads = %d\n", nthread);

	auto cct = global_init(
		NULL,
    		args, CEPH_ENTITY_TYPE_OSD,
		CODE_ENVIRONMENT_UTILITY,
//   		CODE_ENVIRONMENT_DAEMON,
		CINIT_FLAG_NO_DEFAULT_CONFIG_FILE);

	common_init_finish(g_ceph_context);

	std::unique_ptr<ObjectStore> os;

	os = ObjectStore::create(g_ceph_context,
//                           g_conf().get_val<std::string>("osd objectstore"),
			   "bluestore",
                           g_conf().get_val<std::string>("osd data"),
                           g_conf().get_val<std::string>("osd journal"));
	if (!os) {
		std::cerr << " can not open object store" << std::endl;
		exit(1);
	}

	int ret = os->mkfs();
	printf("mkfs ret=%d\n", ret);
	if (ret) {
		exit(1);
	}
	ret = os->mount();

	init_collections(os, MyCollection::MIN_POOL_ID,
			 collections, COLLECTION_COUNT);

	test_clone_range(os, collections[0]);

	std::vector<std::thread> thread_list(nthread);

	for (int i = 0; i < nthread; ++i) {
		thread_list[i] = std::move(
			std::thread([&] {
					benchmark_clone_range(os, collections[0], i, 100, block_ksize * 1024);
				}
			));
	}

	for (int i = 0; i < nthread; ++i) {
		thread_list[i].join();
	}

	// destruct MyCollection memory obj before umount,
	// otherwise there is illegal reference to object store
	collections.clear();

	ret = os->umount();
	printf("umount ret=%d\n", ret);
}
