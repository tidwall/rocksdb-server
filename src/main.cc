#include <cassert>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "rocksdb/db.h"
#include <libmill.h>
#include "shared.h"

const int MAX_ARGS = 64;
const int BUF_START_SIZE = 64;

rocksdb::DB* db = 0;

char *buf = NULL;   // buffer 
size_t buf_cap = 0; // buffer capacity
size_t buf_len = 0; // buffer length (from index)
size_t buf_idx = 0; // buffer index

int args_count = 0;         // number of command arguments
char *args[MAX_ARGS];       // command argument data
size_t args_size[MAX_ARGS]; // size of each command argument

void fatal(const char *s){
	fprintf(stdout, "-ERR %s\r\n", s);
	exit(-1);
}

void write_out(const char *s, int n){
	if (write(STDOUT_FILENO, s, n) != n){
		exit(-1);
	}
}

char *output = NULL;
size_t output_cap = 0;
size_t output_len = 0;

void output_req(size_t siz){
	if (output_cap < siz){
		while (output_cap < siz){
			if (output_cap == 0){
				output_cap = 1;
			}else{
				output_cap *= 2;
			}
		}
		output = (char*)realloc(output, output_cap);
		if (!output){
			fatal("out of memory");
		}
	}
}

void output_clear(){
	output_len = 0;
}

void output_write(const char *data, int n){
	output_req(output_len+n);
	memcpy(output+output_len, data, n);	
	output_len+=n;
}

void output_write_byte(char c){
	output_req(output_len+1);
	output[output_len++] = c;
}

void output_write_bulk(const char *data, int n){
	char h[32];
	sprintf(h, "$%d\r\n", n);
	output_write(h, strlen(h));
	output_write(data, n);
	output_write_byte('\r');
	output_write_byte('\n');
}

void output_write_multibulk(int n){
	char h[32];
	sprintf(h, "*%d\r\n", n);
	output_write(h, strlen(h));
}

void output_write_int(int n){
	char h[32];
	sprintf(h, ":%d\r\n", n);
	output_write(h, strlen(h));
}

void output_flush(){
	write_out(output, output_len);
}


static char expected_got_str[64];
static const char *expected_got(char c1, char c2){
	sprintf(expected_got_str, "Protocol error: expected '%c', got '%c'", c1, c2);
	return expected_got_str;
}

char *err_str = NULL;
const char *err_unknown_command(const char *name, int count){
	if (err_str!=NULL){
		free(err_str);
	}
	err_str = (char*)malloc(strlen(name)+64);
	err_str[0] = 0;
	strcat(err_str, "unknown command '");
	strncat(err_str, name, count);
	strcat(err_str, "'");
	
	return err_str;
}

error server_exec_set(client *c){
	const char **argv = client_argv(c);
	int *argl = client_argl(c);
	int argc = client_argc(c);
	if (argc!=3){
		return "wrong number of arguments for 'set' command";
	}	
	std::string key(argv[1], argl[1]);
	std::string value(argv[2], argl[2]);
	rocksdb::WriteOptions write_options;
	write_options.sync = true;
	rocksdb::Status s = db->Put(write_options, key, value);
	if (!s.ok()){
		panic(s.ToString().c_str());
	}
	client_clear(c);
	client_write(c, "+OK\r\n", 5);
	client_flush(c);
	return NULL;
}

error server_exec_get(client *c){
	const char **argv = client_argv(c);
	int *argl = client_argl(c);
	int argc = client_argc(c);
	if (argc!=2){
		return "wrong number of arguments for 'get' command";
	}	
	std::string key(argv[1], argl[1]);
	std::string value;
	rocksdb::ReadOptions read_options;
	rocksdb::Status s = db->Get(read_options, key, &value);
	if (!s.ok()){
		if (s.IsNotFound()){
			client_clear(c);
			client_write(c, "$-1\r\n", 5);
			client_flush(c);
			return NULL;
		}
		panic(s.ToString().c_str());
	}
	client_clear(c);
	client_write_bulk(c, value.data(), value.size());
	client_flush(c);
	return NULL;
}

error server_exec_del(client *c){
	const char **argv = client_argv(c);
	int *argl = client_argl(c);
	int argc = client_argc(c);
	if (argc!=2){
		return "wrong number of arguments for 'del' command";
	}
	std::string key(argv[1], argl[1]);
	std::string value; 
	rocksdb::Status s = db->Get(rocksdb::ReadOptions(), key, &value);
	if (!s.ok()){
		if (s.IsNotFound()){
			client_clear(c);
			client_write(c, ":0\r\n", 4);
			client_flush(c);
			return NULL;
		}
		panic(s.ToString().c_str());
	}
	rocksdb::WriteOptions write_options;
	write_options.sync = true;
	s = db->Delete(write_options, key);
	if (!s.ok()){
		panic(s.ToString().c_str());
	}
	client_clear(c);
	client_write(c, ":1\r\n", 4);
	client_flush(c);
	return NULL;
}

error server_exec_quit(client *c){
	client_clear(c);
	client_write(c, "+OK\r\n", 5);
	client_flush(c);
	exit(0);
	return NULL;
}

error server_exec_keys(client *c){
	return NULL;
	/*
	const char **argv = client_argv(c);
	int *argl = client_argl(c);
	int argc = client_argc(c);
	if (argc!=2){
		return "wrong number of arguments for 'keys' command";
	}

	client_clear(c);
	client_write(c, "012345678901234567890123456789", 30);

	int count = 0;
	rocksdb::Iterator* it = db->NewIterator(rocksdb::ReadOptions());
	for (it->SeekToFirst(); it->Valid(); it->Next()) {
		rocksdb::Slice key = it->key();
		if (stringmatchlen(args[1], args_size[1], key.data(), key.size(), 1)){
			client_write_bulk(c, key.data(), key.size());
			count++;	
		}
	}
	rocksdb::Status s = it->status();
	if (!s.ok()){
		panic(s.ToString().c_str());	
	}
	delete it;
	
	// fill in the header and write from offset.
	char nb[32];
	sprintf(nb, "*%d\r\n", count);
	int nbn = strlen(nb);
	memcpy(output+30-nbn, nb, nbn);
	write_out(output+30-nbn, output_len-30+nbn);
	return NULL;
	*/
}

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


error server_handle_command(client *c){
	const char **argv = client_argv(c);
	int *argl = client_argl(c);
	int argc = client_argc(c);
	if (argc==0){
		return NULL;
	}
	if (argl[0] == 3 && 
		(argv[0][0] == 'S' || argv[0][0] == 's') &&
		(argv[0][1] == 'E' || argv[0][1] == 'e') &&
		(argv[0][2] == 'T' || argv[0][2] == 't')){
		return server_exec_set(c);
	}else if (argl[0] == 3 &&
		(argv[0][0] == 'G' || argv[0][0] == 'g') &&
		(argv[0][1] == 'E' || argv[0][1] == 'e') &&
		(argv[0][2] == 'T' || argv[0][2] == 't')){
		return server_exec_get(c);
	}else if (argl[0] == 3 &&
		(argv[0][0] == 'D' || argv[0][0] == 'd') &&
		(argv[0][1] == 'E' || argv[0][1] == 'e') &&
		(argv[0][2] == 'L' || argv[0][2] == 'l')){
		return server_exec_del(c);
	}else if (argl[0] == 4 &&
		(argv[0][0] == 'Q' || argv[0][0] == 'q') &&
		(argv[0][1] == 'U' || argv[0][1] == 'u') &&
		(argv[0][2] == 'I' || argv[0][2] == 'i') &&
		(argv[0][3] == 'T' || argv[0][3] == 't')){
		return server_exec_quit(c);
	}else if (argl[0] == 4 &&
		(argv[0][0] == 'K' || argv[0][0] == 'k') &&
		(argv[0][1] == 'E' || argv[0][1] == 'e') &&
		(argv[0][2] == 'Y' || argv[0][2] == 'y') &&
		(argv[0][3] == 'S' || argv[0][3] == 's')){
		return server_exec_keys(c);
	}
	return err_unknown_command(argv[0], argl[0]);
}

int server_run(client_type type){
	client *c = client_new_pipe();
	for (;;){
		const char *err = client_read_command(c);
		if (err){
			if (strcmp(err, "eof")==0){
				return 0;
			}
			fprintf(stdout, "-ERR %s\r\n", err);
			return 1;
		}
		err = server_handle_command(c);
		if (err){
			if (fprintf(stdout, "-ERR %s\r\n", err) < 0){
				return 1;
			}
		}
	}
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

