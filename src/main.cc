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

const char *parse_telnet_command(){
	size_t i = buf_idx;
	size_t z = buf_len+buf_idx;
	if (i >= z){
		return "incomplete";
	}
	args_count = 0;
	size_t s = i;
	bool first = true;
	for (;i<z;i++){
		if (buf[i]=='\'' || buf[i]=='\"'){
			if (!first){
				return "Protocol error: unbalanced quotes in request";
			}
			char c = buf[i];
			i++;
			s = i;
			for (;i<z;i++){
				if (buf[i] == c){
					// TODO: make sure next character is a space or line break.
					if (i+1>=z||buf[i+1]==' '||buf[i+1]=='\r'||buf[i+1]=='\n'){
						if (args_count >= MAX_ARGS){
							return "Protocol error: too many arguments";
						}
						args[args_count] = buf+s;
						args_size[args_count] = i-s;
						args_count++;
						//printf("[%s]\n", std::string(buf+s, i-s).c_str());
					}else{
						return "Protocol error: unbalanced quotes in request";
					}
					break;
				}
			}
			i++;
			continue;
		}
		if (buf[i] == '\n'){
			if (!first){
				size_t e;
				if (i>s && buf[i-1] == '\r'){
					e = i-1;
				}else{
					e = i;
				}
				if (args_count >= MAX_ARGS){
					return "Protocol error: too many arguments";
				}
				args[args_count] = buf+s;
				args_size[args_count] = e-s;
				args_count++;
			//	printf("[%s]\n", std::string(buf+s, e-s).c_str());
			}
			i++;
			buf_len -= i-buf_idx;
			if (buf_len == 0){
				buf_idx = 0;
			}else{
				buf_idx = i;
			}
			return NULL;
		}
		if (buf[i] == ' '){
			if (!first){
				if (args_count >= MAX_ARGS){
					return "Protocol error: too many arguments";
				}
				args[args_count] = buf+s;
				args_size[args_count] = i-s;
				args_count++;
				//printf("[%s]\n", std::string(buf+s, i-s).c_str());
				first = true;
			}
		}else{
			if (first){
				s = i;
				first = false;
			}
		}
	}

	return "incomplete";
}

// parse_command parses the current command in the buffer.
// returns NULL if there is no error.
// returns "incomplete" if there is more data to read.
// returns non-empty string on error.
const char *parse_command(){
	size_t i = buf_idx;
	size_t z = buf_len+buf_idx;
	if (i >= z){
		return "incomplete";
	}
	if (buf[i] != '*'){
		return parse_telnet_command();
	}
	i++;
	size_t s = i;
	for (;i < z;i++){
		if (buf[i]=='\n'){
			if (buf[i-1] !='\r'){
				return "Protocol error: invalid multibulk length";
			}
			buf[i-1] = 0;
			args_count = atoi(buf+s);
			buf[i-1] = '\r';
			if (args_count <= 0){
				if (args_count < 0 || i-s != 2){
					return "Protocol error: invalid multibulk length";
				}
			}
			i++;
			break;
		}
	}
	if (args_count > MAX_ARGS){
		return "Protocol error: too many arguments";
	}
	if (i >= z){
		return "incomplete";
	}
	for (int j=0;j<args_count;j++){
		if (i >= z){
			return "incomplete";
		}
		if (buf[i] != '$'){
			return expected_got('$', buf[i]);
		}
		i++;
		int nsiz = 0;
		size_t s = i;
		for (;i < z;i++){
			if (buf[i]=='\n'){
				if (buf[i-1] !='\r'){
					return "Protocol error: invalid bulk length";
				}
				buf[i-1] = 0;
				nsiz = atoi(buf+s);
				buf[i-1] = '\r';
				if (nsiz <= 0){
					if (nsiz < 0 || i-s != 2){
						return "Protocol error: invalid bulk length";
					}
				}
				i++;
				if (z-i < nsiz+2){
					return "incomplete";
				}
				s = i;
				if (buf[s+nsiz] != '\r'){
					return "Protocol error: invalid bulk data";
				}
				if (buf[s+nsiz+1] != '\n'){
					return "Protocol error: invalid bulk data";
				}
				args[j] = buf+s;
				args_size[j] = nsiz;
				i += nsiz+2;
				break;
			}
		}
	}
	buf_len -= i-buf_idx;
	if (buf_len == 0){
		buf_idx = 0;
	}else{
		buf_idx = i;
	}
	return NULL;
}

// read_command reads the next command from stdin
// returns empty string if a command has been read.
// returns non-empty string on error.
const char *read_command(){
	const char *err = parse_command();
	if (err == NULL){
		return NULL;
	}
	if (strcmp(err, "incomplete")!=0){
		return err;
	}
	// read into buffer
	if (buf_cap-buf_len == 0){
		size_t ncap;
		if (buf_cap==0){
			ncap = BUF_START_SIZE;
		}else{
			ncap = buf_cap*2;
		}
		buf = (char*)realloc(buf, ncap);
		if (!buf){
			return "out of memory";
		}
		buf_cap = ncap;
	}
	size_t n = read(STDIN_FILENO, buf+(buf_idx+buf_len), buf_cap-(buf_idx+buf_len));
	if (n <= 0){
		if (n == 0){
			if (buf_len>0){
				return "Protocol error: incomplete command";
			}
			return "eof";
		}
		return strerror(n);
	}
	buf_len+=n;
	return read_command();
}

const char *exec_set(){
	if (args_count!=3){
		return "wrong number of arguments for 'set' command";
	}	
	std::string key(args[1], args_size[1]);
	std::string value(args[2], args_size[2]);
	rocksdb::WriteOptions write_options;
	write_options.sync = true;
	rocksdb::Status s = db->Put(write_options, key, value);
	if (!s.ok()){
		fatal(s.ToString().c_str());
	}
	write_out("+OK\r\n", 5);
	return NULL;
}

const char *exec_get(){
	if (args_count!=2){
		return "wrong number of arguments for 'get' command";
	}	
	std::string key(args[1], args_size[1]);
	std::string value;
	rocksdb::ReadOptions read_options;
	rocksdb::Status s = db->Get(read_options, key, &value);
	if (!s.ok()){
		if (s.IsNotFound()){
			write_out("$-1\r\n", 5);
			return NULL;
		}
		fatal(s.ToString().c_str());
	}

	output_clear();
	output_write_bulk(value.data(), value.size());
	output_flush();

	return NULL;
}

const char *exec_del(){
	if (args_count!=2){
		return "wrong number of arguments for 'del' command";
	}
	std::string key(args[1], args_size[1]);
	std::string value; 
	rocksdb::Status s = db->Get(rocksdb::ReadOptions(), key, &value);
	if (!s.ok()){
		if (s.IsNotFound()){
			write_out(":0\r\n", 4);
			return NULL;
		}
		fatal(s.ToString().c_str());
	}
	rocksdb::WriteOptions write_options;
	write_options.sync = true;
	s = db->Delete(write_options, key);
	if (!s.ok()){
		fatal(s.ToString().c_str());
	}
	write_out(":1\r\n", 4);
	return NULL;
}

const char *exec_quit(){
	write_out("+OK\r\n", 5);
	exit(0);
	return NULL;
}

const char *exec_keys(){
	if (args_count!=2){
		return "wrong number of arguments for 'keys' command";
	}

	output_clear();
	output_write("012345678901234567890123456789", 30);

	int count = 0;
	rocksdb::Iterator* it = db->NewIterator(rocksdb::ReadOptions());
	for (it->SeekToFirst(); it->Valid(); it->Next()) {
		rocksdb::Slice key = it->key();
		if (stringmatchlen(args[1], args_size[1], key.data(), key.size(), 1)){
			output_write_bulk(key.data(), key.size());
			count++;	
		}
	}
	rocksdb::Status s = it->status();
	if (!s.ok()){
		fatal(s.ToString().c_str());	
	}
	delete it;
	
	// fill in the header and write from offset.
	char nb[32];
	sprintf(nb, "*%d\r\n", count);
	int nbn = strlen(nb);
	memcpy(output+30-nbn, nb, nbn);
	write_out(output+30-nbn, output_len-30+nbn);
	return NULL;
}

const char *handle_command(){
	if (args_count==0){
		return NULL;
	}
	if (args_size[0] == 3 && 
		(args[0][0] == 'S' || args[0][0] == 's') &&
		(args[0][1] == 'E' || args[0][1] == 'e') &&
		(args[0][2] == 'T' || args[0][2] == 't')){
		return exec_set();
	}else if (args_size[0] == 3 &&
		(args[0][0] == 'G' || args[0][0] == 'g') &&
		(args[0][1] == 'E' || args[0][1] == 'e') &&
		(args[0][2] == 'T' || args[0][2] == 't')){
		return exec_get();
	}else if (args_size[0] == 3 &&
		(args[0][0] == 'D' || args[0][0] == 'd') &&
		(args[0][1] == 'E' || args[0][1] == 'e') &&
		(args[0][2] == 'L' || args[0][2] == 'l')){
		return exec_del();
	}else if (args_size[0] == 4 &&
		(args[0][0] == 'Q' || args[0][0] == 'q') &&
		(args[0][1] == 'U' || args[0][1] == 'u') &&
		(args[0][2] == 'I' || args[0][2] == 'i') &&
		(args[0][3] == 'T' || args[0][3] == 't')){
		return exec_quit();
	}else if (args_size[0] == 4 &&
		(args[0][0] == 'K' || args[0][0] == 'k') &&
		(args[0][1] == 'E' || args[0][1] == 'e') &&
		(args[0][2] == 'Y' || args[0][2] == 'y') &&
		(args[0][3] == 'S' || args[0][3] == 's')){
		return exec_keys();
	}
	return err_unknown_command(args[0], args_size[0]);
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

int main(int argc, char *argv[]){
	/*
	go(worker(4, "a"));
    go(worker(2, "b"));
    go(worker(3, "c"));
    msleep(now() + 100);
    return 0;
*/
	const char *dir = "data";
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
	int opened = 0;
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
}

