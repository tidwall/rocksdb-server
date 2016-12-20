#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <rocksdb/db.h>

#ifndef ROCKSDB_VERSION
#define ROCKSDB_VERSION "0.0"
#endif

#ifndef LIBUV_VERSION
#define LIBUV_VERSION "0.0.0"
#endif

#ifndef SERVER_VERSION
#define SERVER_VERSION "0.0.0"
#endif

// db is the globally shared database. 
rocksdb::DB* db = NULL;
bool nosync = false;
int nprocs = 1;

uv_loop_t *loop = NULL;
const char *ERR_INCOMPLETE = "incomplete";
const char *ERR_QUIT = "quit";

int stringmatchlen(const char *pattern, int patternLen,
        const char *string, int stringLen, int nocase);

typedef const char *error;

typedef struct client2_t {
	uv_tcp_t tcp;	// should always be the first element.
	uv_write_t req;
	uv_work_t worker;
	uv_stream_t *server;
	int must_close;
	char *buf;
	int buf_cap;
	int buf_len;
	int buf_idx;
	const char **args;
	int args_cap;
	int args_len;
	int *args_size;
	char *tmp_err;
	char *output;
	int output_len;
	int output_cap;
	int output_offset;
} client2;

client2 *client2_new(){
	client2 *c = (client2*)calloc(1, sizeof(client2));
	if (!c){
		err(1, "malloc");
	}
	c->worker.data = c; // self reference
	return c;
}

void client2_free(client2 *c){
	if (!c){
		return;
	}
	if (c->buf){
		free(c->buf);
	}
	if (c->args){
		free(c->args);
	}
	if (c->args_size){
		free(c->args_size);
	}
	if (c->output){
		free(c->output);
	}
	if (c->tmp_err){
		free(c->tmp_err);
	}
	free(c);
}

static void on_close(uv_handle_t *stream){
	client2_free((client2*)stream);
}

void client2_close(client2 *c){
	uv_close((uv_handle_t *)&c->tcp, on_close);
}

static void get_buffer(uv_handle_t *handle, size_t size, uv_buf_t *buf){
	client2 *c = (client2*)handle;
	if (c->buf_cap-c->buf_idx-c->buf_len < size){
		while (c->buf_cap-c->buf_idx-c->buf_len < size){
			if (c->buf_cap==0){
				c->buf_cap=1;
			}else{
				c->buf_cap*=2;
			}
		}
		c->buf = (char*)realloc(c->buf, c->buf_cap);
		if (!c->buf){
			err(1, "malloc");
		}
	}
	buf->base = c->buf+c->buf_idx+c->buf_len;
	buf->len = size;
}



static void client_err_alloc(client2 *c, int n){
	if (c->tmp_err){
		free(c->tmp_err);
	}
	c->tmp_err = (char*)malloc(n);
	if (!c->tmp_err){
		panic("out of memory");
	}
	memset(c->tmp_err, 0, n);
}

static error client_err_expected_got(client2 *c, char c1, char c2){
	client_err_alloc(c, 64);
	sprintf(c->tmp_err, "Protocol error: expected '%c', got '%c'", c1, c2);
	return c->tmp_err;
}

error client_err_unknown_command(client2 *c, const char *name, int count){
	client_err_alloc(c, count+64);
	c->tmp_err[0] = 0;
	strcat(c->tmp_err, "unknown command '");
	strncat(c->tmp_err, name, count);
	strcat(c->tmp_err, "'");
	return c->tmp_err;
}
static void client_append_arg(client2 *c, const char *data, int nbyte){
	if (c->args_cap==c->args_len){
		if (c->args_cap==0){
			c->args_cap=1;
		}else{
			c->args_cap*=2;
		}
		c->args = (const char**)realloc(c->args, c->args_cap*sizeof(const char *));
		if (!c->args){
			panic("out of memory");
		}
		c->args_size = (int*)realloc(c->args_size, c->args_cap*sizeof(int));
		if (!c->args_size){
			panic("out of memory");
		}
	}
	c->args[c->args_len] = data;
	c->args_size[c->args_len] = nbyte;
	c->args_len++;
}

static error client_parse_telnet_command(client2 *c){
	size_t i = c->buf_idx;
	size_t z = c->buf_len+c->buf_idx;
	if (i >= z){
		return ERR_INCOMPLETE;
	}
	c->args_len = 0;
	size_t s = i;
	bool first = true;
	for (;i<z;i++){
		if (c->buf[i]=='\'' || c->buf[i]=='\"'){
			if (!first){
				return "Protocol error: unbalanced quotes in request";
			}
			char b = c->buf[i];
			i++;
			s = i;
			for (;i<z;i++){
				if (c->buf[i] == b){
					if (i+1>=z||c->buf[i+1]==' '||c->buf[i+1]=='\r'||c->buf[i+1]=='\n'){
						client_append_arg(c, c->buf+s, i-s);
						i--;
					}else{
						return "Protocol error: unbalanced quotes in request";
					}
					break;
				}
			}
			i++;
			continue;
		}
		if (c->buf[i] == '\n'){
			if (!first){
				size_t e;
				if (i>s && c->buf[i-1] == '\r'){
					e = i-1;
				}else{
					e = i;
				}
				client_append_arg(c, c->buf+s, e-s);
			}
			i++;
			c->buf_len -= i-c->buf_idx;
			if (c->buf_len == 0){
				c->buf_idx = 0;
			}else{
				c->buf_idx = i;
			}
			return NULL;
		}
		if (c->buf[i] == ' '){
			if (!first){
				client_append_arg(c, c->buf+s, i-s);
				first = true;
			}
		}else{
			if (first){
				s = i;
				first = false;
			}
		}
	}
	return ERR_INCOMPLETE;
}

static error client_read_command(client2 *c){
	c->args_len = 0;
	size_t i = c->buf_idx;
	size_t z = c->buf_idx+c->buf_len;
	if (i >= z){
		return ERR_INCOMPLETE;
	}
	if (c->buf[i] != '*'){
		return client_parse_telnet_command(c);
	}
	i++;
	int args_len = 0;
	size_t s = i;
	for (;i < z;i++){
		if (c->buf[i]=='\n'){
			if (c->buf[i-1] !='\r'){
				return "Protocol error: invalid multibulk length";
			}
			c->buf[i-1] = 0;
			args_len = atoi(c->buf+s);
			c->buf[i-1] = '\r';
			if (args_len <= 0){
				if (args_len < 0 || i-s != 2){
					return "Protocol error: invalid multibulk length";
				}
			}
			i++;
			break;
		}
	}
	if (i >= z){
		return ERR_INCOMPLETE;
	}
	for (int j=0;j<args_len;j++){
		if (i >= z){
			return ERR_INCOMPLETE;
		}
		if (c->buf[i] != '$'){
			return client_err_expected_got(c, '$', c->buf[i]);
		}
		i++;
		int nsiz = 0;
		size_t s = i;
		for (;i < z;i++){
			if (c->buf[i]=='\n'){
				if (c->buf[i-1] !='\r'){
					return "Protocol error: invalid bulk length";
				}
				c->buf[i-1] = 0;
				nsiz = atoi(c->buf+s);
				c->buf[i-1] = '\r';
				if (nsiz <= 0){
					if (nsiz < 0 || i-s != 2){
						return "Protocol error: invalid bulk length";
					}
				}
				i++;
				if (z-i < nsiz+2){
					return ERR_INCOMPLETE;
				}
				s = i;
				if (c->buf[s+nsiz] != '\r'){
					return "Protocol error: invalid bulk data";
				}
				if (c->buf[s+nsiz+1] != '\n'){
					return "Protocol error: invalid bulk data";
				}
				client_append_arg(c, c->buf+s, nsiz);
				i += nsiz+2;
				break;
			}
		}
	}
	c->buf_len -= i-c->buf_idx;
	if (c->buf_len == 0){
		c->buf_idx = 0;
	}else{
		c->buf_idx = i;
	}
	return NULL;
}

static inline void client_output_require(client2 *c, size_t siz){
	if (c->output_cap < siz){
		while (c->output_cap < siz){
			if (c->output_cap == 0){
				c->output_cap = 1;
			}else{
				c->output_cap *= 2;
			}
		}
		c->output = (char*)realloc(c->output, c->output_cap);
		if (!c->output){
			err(1, "malloc");
		}
	}
}
static void client_write(client2 *c, const char *data, int n){
	client_output_require(c, c->output_len+n);
	memcpy(c->output+c->output_len, data, n);	
	c->output_len+=n;
}

void client_clear(client2 *c){
	c->output_len = 0;
	c->output_offset = 0;
}

static void client_write_byte(client2 *c, char b){
	client_output_require(c, c->output_len+1);
	c->output[c->output_len++] = b;
}

void client_write_bulk(client2 *c, const char *data, int n){
	char h[32];
	sprintf(h, "$%d\r\n", n);
	client_write(c, h, strlen(h));
	client_write(c, data, n);
	client_write_byte(c, '\r');
	client_write_byte(c, '\n');
}

void client_write_multibulk(client2 *c, int n){
	char h[32];
	sprintf(h, "*%d\r\n", n);
	client_write(c, h, strlen(h));
}

void client_write_int(client2 *c, int n){
	char h[32];
	sprintf(h, ":%d\r\n", n);
	client_write(c, h, strlen(h));
}

void client_write_error(client2 *c, error err){
	client_write(c, "-ERR ", 5);
	client_write(c, err, strlen(err));
	client_write_byte(c, '\r');
	client_write_byte(c, '\n');
}


void client_flush_offset(client2 *c, int offset){
	if (c->output_len-offset <= 0){
		return;
	}
	uv_buf_t buf = {0};
	buf.base = c->output+offset;
	buf.len = c->output_len-offset;
	uv_write(&c->req, (uv_stream_t *)&c->tcp, &buf, 1, NULL);
	c->output_len = 0;
}


// client_flush flushes the bytes to the client output stream. it does not
// check for errors because the client read operations handle socket errors.
void client_flush(client2 *c){
	client_flush_offset(c, 0);
}

error exec_set(client2 *c){
	const char **argv = c->args;
	int *argl = c->args_size;
	int argc = c->args_len;
	if (argc!=3){
		return "wrong number of arguments for 'set' command";
	}	
	std::string key(argv[1], argl[1]);
	std::string value(argv[2], argl[2]);
	rocksdb::WriteOptions write_options;
	write_options.sync = !nosync;
	rocksdb::Status s = db->Put(write_options, key, value);
	if (!s.ok()){
		panic(s.ToString().c_str());
	}
	client_write(c, "+OK\r\n", 5);
	return NULL;
}

error exec_get(client2 *c){
	const char **argv = c->args;
	int *argl = c->args_size;
	int argc = c->args_len;
	if (argc!=2){
		return "wrong number of arguments for 'get' command";
	}	
	std::string key(argv[1], argl[1]);
	std::string value;
	rocksdb::ReadOptions read_options;
	rocksdb::Status s = db->Get(read_options, key, &value);
	if (!s.ok()){
		if (s.IsNotFound()){
			client_write(c, "$-1\r\n", 5);
			return NULL;
		}
		panic(s.ToString().c_str());
	}
	client_write_bulk(c, value.data(), value.size());
	return NULL;
}

error exec_del(client2 *c){
	const char **argv = c->args;
	int *argl = c->args_size;
	int argc = c->args_len;
	if (argc!=2){
		return "wrong number of arguments for 'del' command";
	}
	std::string key(argv[1], argl[1]);
	std::string value; 
	rocksdb::Status s = db->Get(rocksdb::ReadOptions(), key, &value);
	if (!s.ok()){
		if (s.IsNotFound()){
			client_write(c, ":0\r\n", 4);
			return NULL;
		}
		panic(s.ToString().c_str());
	}
	rocksdb::WriteOptions write_options;
	write_options.sync = !nosync;
	s = db->Delete(write_options, key);
	if (!s.ok()){
		panic(s.ToString().c_str());
	}
	client_write(c, ":1\r\n", 4);
	return NULL;
}

error exec_quit(client2 *c){
	client_write(c, "+OK\r\n", 5);
	return ERR_QUIT;
}

error exec_keys(client2 *c){
	const char **argv = c->args;
	int *argl = c->args_size;
	int argc = c->args_len;
	if (argc!=2){
		return "wrong number of arguments for 'keys' command";
	}

	// to avoid double-buffering, prewrite some bytes and then we'll go back 
	// and fill it in with correctness.
	client_write(c, "012345678901234567890123456789", 30);

	int count = 0;
	rocksdb::Iterator* it = db->NewIterator(rocksdb::ReadOptions());
	for (it->SeekToFirst(); it->Valid(); it->Next()) {
		rocksdb::Slice key = it->key();
		if (stringmatchlen(argv[1], argl[1], key.data(), key.size(), 1)){
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
	memcpy(c->output+30-nbn, nb, nbn);
	c->output_offset = 30;
	return NULL;
}
error exec_command(client2 *c){
	const char **argv = c->args;
	int *argl = c->args_size;
	int argc = c->args_len;
	if (argc==0||(argc==1&&argl[0]==0)){
		return NULL;
	}
	if (argl[0] == 3 && 
		(argv[0][0] == 'S' || argv[0][0] == 's') &&
		(argv[0][1] == 'E' || argv[0][1] == 'e') &&
		(argv[0][2] == 'T' || argv[0][2] == 't')){
		return exec_set(c);
	}else if (argl[0] == 3 &&
		(argv[0][0] == 'G' || argv[0][0] == 'g') &&
		(argv[0][1] == 'E' || argv[0][1] == 'e') &&
		(argv[0][2] == 'T' || argv[0][2] == 't')){
		return exec_get(c);
	}else if (argl[0] == 3 &&
		(argv[0][0] == 'D' || argv[0][0] == 'd') &&
		(argv[0][1] == 'E' || argv[0][1] == 'e') &&
		(argv[0][2] == 'L' || argv[0][2] == 'l')){
		return exec_del(c);
	}else if (argl[0] == 4 &&
		(argv[0][0] == 'Q' || argv[0][0] == 'q') &&
		(argv[0][1] == 'U' || argv[0][1] == 'u') &&
		(argv[0][2] == 'I' || argv[0][2] == 'i') &&
		(argv[0][3] == 'T' || argv[0][3] == 't')){
		return exec_quit(c);
	}else if (argl[0] == 4 &&
		(argv[0][0] == 'K' || argv[0][0] == 'k') &&
		(argv[0][1] == 'E' || argv[0][1] == 'e') &&
		(argv[0][2] == 'Y' || argv[0][2] == 'y') &&
		(argv[0][3] == 'S' || argv[0][3] == 's')){
		return exec_keys(c);
	}
	return client_err_unknown_command(c, argv[0], argl[0]);
}
void client_print_args(client2 *c){
	printf("args[%d]:", c->args_len);
	for (int i=0;i<c->args_len;i++){
		printf(" [");
		for (int j=0;j<c->args_size[i];j++){
			printf("%c", c->args[i][j]);
		}
		printf("]");
	}
	printf("\n");
}
bool client_exec_commands(client2 *c){
	for (;;){
		error err = client_read_command(c);
		if (err != NULL){
			if ((char*)err == (char*)ERR_INCOMPLETE){
				return true;
			}
			client_write_error(c, err);
			return false;
		}
		err = exec_command(c);
		if (err != NULL){
			if (err == ERR_QUIT){
				return false;
			}
			client_write_error(c, err);
			return true;
		}
	}
	return true;
}

static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf){
	client2 *c = (client2*)stream;
	if (nread < 0) {
		client2_close(c);
		return;
	}
	c->buf_len += nread;
	client_clear(c);
	bool keep_alive = client_exec_commands(c);
	client_flush_offset(c, c->output_offset);
	if (!keep_alive){
		client2_close(c);
	}
}

static void on_accept_work(uv_work_t *worker) {
	client2 *c = (client2*)worker->data;
	uv_tcp_init(loop, &c->tcp);
	if (uv_accept(c->server, (uv_stream_t *)&c->tcp) == 0) {
		if (uv_read_start((uv_stream_t *)&c->tcp, get_buffer, on_read)){
			c->must_close = 1;
		}
		// read has started
	}else{
		c->must_close = 1;
	}
}

static void on_accept_work_done(uv_work_t *worker, int status) {
	client2 *c = (client2*)worker->data;
	if (c->must_close){
		client2_close(c);
	}
}

static void on_accept(uv_stream_t *server, int status) {
	if (status == -1) {
		return;
	}
	client2 *c = client2_new();
	c->server = server;
	if (0){
		// future thread-pool stuff, perhaps rip this out
		uv_queue_work(loop, &c->worker, on_accept_work, on_accept_work_done);
	}else{
		on_accept_work(&c->worker);
		on_accept_work_done(&c->worker, 0);
	}
}

int main(int argc, char **argv) {
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
			fprintf(stdout, "RocksDB version " ROCKSDB_VERSION ", Libuv version " LIBUV_VERSION ", Server version " SERVER_VERSION "\n");
			fprintf(stdout, "usage: %s [-d data_path] [-p tcp_port] [--nosync]\n", argv[0]);
			return 0;
		}else if (strcmp(argv[i], "--version")==0){
			fprintf(stdout, "RocksDB version " ROCKSDB_VERSION ", Libuv version " LIBUV_VERSION ", Server version " SERVER_VERSION "\n");
			return 0;
		}else if (strcmp(argv[i], "-d")==0){
			if (i+1 == argc){
				fprintf(stderr, "argument missing after: \"%s\"\n", argv[i]);
				return 1;
			}
			dir = argv[++i];
		}else if (strcmp(argv[i], "--nosync")==0){
			nosync = true;
	/*	}else if (strcmp(argv[i], "-u")==0){
			if (i+1 == argc){
				fprintf(stderr, "argument missing after: \"%s\"\n", argv[i]);
				return 1;
			}
			unix_path = argv[++i];
			unix_path_provided = true;*/
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
		/*}else if (strcmp(argv[i], "-")==0){
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
			}*/
		}else{
			fprintf(stderr, "unknown option argument: \"%s\"\n", argv[i]);
			return 1;
		}
	}
	fprintf(stderr, "00000:M 01 Jan 00:00:00.000 # Server started, RocksDB version " ROCKSDB_VERSION ", Libuv version " LIBUV_VERSION ", Server version " SERVER_VERSION "\n");
	rocksdb::Options options;
	options.create_if_missing = true;
	rocksdb::Status s = rocksdb::DB::Open(options, dir, &db);
	if (!s.ok()){
		err(1, "%s", s.ToString().c_str());
	}

	uv_tcp_t server;
	loop = uv_default_loop();

	struct sockaddr_in addr;
	uv_ip4_addr("0.0.0.0", 5555, &addr);

	uv_tcp_init(loop, &server);
	uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);

	int r = uv_listen((uv_stream_t *)&server, -1, on_accept);
	if (r) {
		err(1, "uv_listen");
	}
	return uv_run(loop, UV_RUN_DEFAULT);
}
