#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <rocksdb/db.h>
#include <libmill.h>
#include "shared.h"

#ifndef ROCKSDB_VERSION
#define ROCKSDB_VERSION "0.0"
#endif

#ifndef SERVER_VERSION
#define SERVER_VERSION "0.0.0"
#endif

// db is the globally shared database. 
rocksdb::DB* db = NULL;
bool nosync = false;
int nprocs = 1;

// handle_client runs a client lifecycle from a coroutine
coroutine void handle_client(client *c, int idx) {
	//fprintf(stderr, "client[%d]: started\n", idx);
	error err = client_run(c);
	client_close(c);
	client_free(c);
	//fprintf(stderr, "client[%d]: ended, err: %s\n", idx, err);
}

// run_pipe_server starts the stdin->stdout IO server
int run_pipe_server(){
	fprintf(stderr, "00000:M 01 Jan 00:00:00.000 * The server is now ready to accept commands from stdin\n");
	client *c = client_new_pipe();
	handle_client(c, 0);
	return 0;
}

static void forks(){
    for (int i = 1; i < nprocs; ++i) {
        pid_t pid = mfork();
        if(pid < 0) {
           perror("Can't create new process");
		   exit(1);
        }
        if(pid == 0) {
            break;
        }
    }
}
// run_tcp_server starts the TCP server on the specified port
int run_tcp_server(int port){
	fprintf(stderr, "00000:M 01 Jan 00:00:00.000 * The server is now ready to accept connections on port %d\n", port);
	ipaddr addr = iplocal(NULL, port, 0);
	tcpsock ls = tcplisten(addr, 10);
	if (!ls){
		perror("tcplisten");
		exit(1);
	}
	forks();
	int idx = 0;
	while(1) {
		tcpsock s = tcpaccept(ls, -1);
		client *c = client_new_tcp(s);
		go(handle_client(c, idx));
		idx++;
	}
	return 0;
}

// run_unix_server starts the Unix Domains server on the specified path
int run_unix_server(const char *path){
	fprintf(stderr, "00000:M 01 Jan 00:00:00.000 * The server is now ready to accept connections at \"%s\"\n", path);
	unixsock ls = unixlisten(path, -1);
	if (!ls){
		perror("unixlisten");
		exit(1);
	}
	forks();
	int idx = 0;
	while(1) {
		unixsock s = unixaccept(ls, -1);
		client *c = client_new_unix(s);
		go(handle_client(c, idx));
		idx++;
	}
	return 0;
}

// main is the process entry point
int main(int argc, char *argv[]){
	const char *dir = "data";
	const char *unix_path = "";
	bool unix_path_provided = false;
	int tcp_port = 5555;
	bool tcp_port_provided = false;
	bool cpus_provided = false;
	bool piped = false;
	for (int i=1;i<argc;i++){
		if (strcmp(argv[i], "-h")==0||
			strcmp(argv[i], "--help")==0||
			strcmp(argv[i], "-?")==0){
			fprintf(stdout, "RocksDB version " ROCKSDB_VERSION ", Server version " SERVER_VERSION "\n");
			fprintf(stdout, "usage: %s [-d data_path] [-u unix_bind] [-p tcp_port] [--nosync]\n", argv[0]);
			return 0;
		}else if (strcmp(argv[i], "--version")==0){
			fprintf(stdout, "RocksDB version " ROCKSDB_VERSION ", Server version " SERVER_VERSION "\n");
			return 0;
		}else if (strcmp(argv[i], "-d")==0){
			if (i+1 == argc){
				fprintf(stderr, "argument missing after: \"%s\"\n", argv[i]);
				return 1;
			}
			dir = argv[++i];
		}else if (strcmp(argv[i], "--nosync")==0){
			nosync = true;
		}else if (strcmp(argv[i], "-u")==0){
			if (i+1 == argc){
				fprintf(stderr, "argument missing after: \"%s\"\n", argv[i]);
				return 1;
			}
			unix_path = argv[++i];
			unix_path_provided = true;
		}else if (strcmp(argv[i], "-p")==0){
			if (i+1 == argc){
				fprintf(stderr, "argument missing after: \"%s\"\n", argv[i]);
				return 1;
			}
			tcp_port = atoi(argv[i+1]);
			if (!tcp_port){
				fprintf(stderr, "invalid option '%s' for argument: \"%s\"\n", argv[i+1], argv[i]);
			}
			i++;
			tcp_port_provided = true;
		}else if (strcmp(argv[i], "-")==0){
			piped = true;
		}else if (strcmp(argv[i], "--nprocs")==0){
			if (i+1 == argc){
				fprintf(stderr, "argument missing after: \"%s\"\n", argv[i]);
				return 1;
			}
			nprocs = atoi(argv[i+1]);
			if (nprocs <= 0 || nprocs > 128){
				fprintf(stderr, "invalid option '%s' for argument: \"%s\"\n", argv[i+1], argv[i]);
			}
			i++;
			if (nprocs != 1){
				cpus_provided = true;
			}
		}else{
			fprintf(stderr, "unknown option argument: \"%s\"\n", argv[i]);
			return 1;
		}
	}
	fprintf(stderr, "00000:M 01 Jan 00:00:00.000 # Server started, RocksDB version " ROCKSDB_VERSION ", Server version " SERVER_VERSION "\n");
	rocksdb::Options options;
	options.create_if_missing = true;
	rocksdb::Status s = rocksdb::DB::Open(options, dir, &db);
	if (!s.ok()){
		panic(s.ToString().c_str());
	}
	if (unix_path_provided){
		return run_unix_server(unix_path);
	}
	if (piped){
		return run_pipe_server();
	}
	return run_tcp_server(tcp_port);
}
