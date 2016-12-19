#include <sys/socket.h>
#include <sys/un.h>
#include <rocksdb/db.h>
#include <libmill.h>
#include "shared.h"

rocksdb::DB* db = 0;

coroutine void connection(int s) {
	char str[100];
    int n, done = 0;
    do {
        n = recv(s, str, 100, 0);
        if (n <= 0) {
            if (n < 0){
				perror("recv");
			}
            done = 1;
        }
        if (!done) {
            if (send(s, str, n, 0) < 0) {
                perror("send");
                done = 1;
            }
		}
    } while (!done);
    close(s);
}








coroutine void handle_tcpconn(tcpsock s) {
	tcpclose(s);
}

int run_server(const char *path){
	// accept on any.
	ipaddr addr = iplocal(NULL, 5555, 0);
	tcpsock ls = tcplisten(addr, 10);
	if (!ls){
		perror("tcplisten");
		exit(1);
	}
	while(1) {
        printf("Waiting for a connection...\n");
		tcpsock s = tcpaccept(ls, -1);
		go(handle_tcpconn(s));
	}
	return 0;

	return 0;
	int s, len;
	struct sockaddr_un local, remote;
	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s == -1){
		perror("socket");
		exit(1);
	}
	local.sun_family = AF_UNIX;
	strncpy(local.sun_path, path, 104);
    unlink(local.sun_path);
    if (bind(s, (struct sockaddr *)&local, sizeof(local)) == -1) {
        perror("bind");
        exit(1);
    }
	if (listen(s, 5) == -1) {
        perror("listen");
        exit(1);
    }
	for(;;) {
        int done, s2, n;
        printf("Waiting for a connection...\n");
        unsigned int t = sizeof(remote);
        if ((s2 = accept(s, (struct sockaddr *)&remote, &t)) == -1) {
            perror("accept");
            exit(1);
        }
        printf("Connected.\n");
		go(connection(s2));
		yield();
    }
	return 0;
}

coroutine void worker(int count, const char *text) {
    int i;
    for(i = 0; i != count; ++i) {
        printf("%s\n", text);
        msleep(now() + 10);
    }
}


int server_run(client_type type){
	client *c = client_new_pipe();
	client_run(c);
	client_free(c);
	return 0;
}


int main(int argc, char *argv[]){
	const char *dir = "data";
	rocksdb::Options options;
	options.create_if_missing = true;
	rocksdb::Status s = rocksdb::DB::Open(options, dir, &db);
	if (!s.ok()){
		panic(s.ToString().c_str());
	}
	return server_run(CLIENT_PIPE);
	return 0;
	/*
	go(worker(4, "a"));
    go(worker(2, "b"));
    go(worker(3, "c"));
    msleep(now() + 100);
    return 0;
*/
	const char *bind = "";
	for (int i=1;i<argc;i++){
		if (strcmp(argv[i], "-h")==0){
			fprintf(stdout, "rdbp - RocksDB Piper 0.1.0\n");
			fprintf(stdout, "usage: %s [-d data_path] [-b unix_bind]\n", argv[0]);
			return 0;
		}else if (strcmp(argv[i], "-d")==0){
			if (i+1 == argc){
				fprintf(stderr, "argument missing after: \"%s\"\n", argv[i]);
				return 1;
			}
			dir = argv[++i];
		}else if (strcmp(argv[i], "-b")==0){
			if (i+1 == argc){
				fprintf(stderr, "argument missing after: \"%s\"\n", argv[i]);
				return 1;
			}
			bind = argv[++i];
		}else{
			fprintf(stderr, "unknown option argument: \"%s\"\n", argv[i]);
			return 1;
		}
	}
	if (strcmp(bind, "")){
		return run_server(bind);
	}
	/*int opened = 0;

	for (;;){
		const char *err = read_command();
		if (err){
			if (strcmp(err, "eof")==0){
				return 0;
			}
			fprintf(stdout, "-ERR %s\r\n", err);
			return 1;
		}
		if (!opened){
			rocksdb::Options options;
			options.create_if_missing = true;
			rocksdb::Status s = rocksdb::DB::Open(options, dir, &db);
			if (!s.ok()){
				fatal(s.ToString().c_str());
			}
			opened = 1;
		}
		err = handle_command();
		if (err){
			if (fprintf(stdout, "-ERR %s\r\n", err) < 0){
				return 1;
			}
		}
	}
	*/
}

