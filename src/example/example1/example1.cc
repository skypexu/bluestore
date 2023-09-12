#include "common/ceph_argparse.h"
#include "global/global_init.h" 
#include "os/ObjectStore.h"
#include "global/global_context.h"
#include "perfglue/heap_profiler.h"
#include <unistd.h>
#include <iostream>
#include <map>
#include <memory>
#include <string>

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
	printf("%p\n", os.get());
	int ret = os->mkfs();
	printf("mkfs ret=%d\n", ret);
	ret = os->mount();
	printf("mount ret=%d\n", ret);
	ret = os->umount();
	printf("umount ret=%d\n", ret);
}
