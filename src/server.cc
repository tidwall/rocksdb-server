#include "server.h"

rocksdb::DB* db = NULL;
bool nosync = true;
int nprocs = 1;
uv_loop_t *loop = NULL;
bool inmem = false;
const char *dir = "data";

const char *ERR_INCOMPLETE = "incomplete";
const char *ERR_QUIT = "quit";

void get_buffer(uv_handle_t *handle, size_t size, uv_buf_t *buf){
	client *c = (client*)handle;
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

void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf){
	client *c = (client*)stream;
	if (nread < 0) {
		client_close(c);
		return;
	}
	c->buf_len += nread;
	client_clear(c);
	bool keep_alive = client_exec_commands(c);
	client_flush_offset(c, c->output_offset);
	if (!keep_alive){
		client_close(c);
	}
}

void on_accept_work(uv_work_t *worker) {
	client *c = (client*)worker->data;
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

void on_accept_work_done(uv_work_t *worker, int status) {
	client *c = (client*)worker->data;
	if (c->must_close){
		client_close(c);
	}
}

void on_accept(uv_stream_t *server, int status) {
	if (status == -1) {
		return;
	}
	client *c = client_new();
	c->server = server;
	if (0){
		// future thread-pool stuff, perhaps rip this out
		uv_queue_work(loop, &c->worker, on_accept_work, on_accept_work_done);
	}else{
		on_accept_work(&c->worker);
		on_accept_work_done(&c->worker, 0);
	}
}

void log(char c, const char *format, ...){
	time_t rawtime;
	struct tm *info;
	char tbuf[32];
	struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
	rawtime = spec.tv_sec;	
	info = localtime( &rawtime );
	strftime(tbuf,sizeof(tbuf),"%d %b %H:%M:%S", info);
	char buffer[512];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer,sizeof(buffer),format, args);
	char cc[32];
	if (isatty(1)){
		switch (c) {
		default: cc[0] = c; cc[1] = 0; 
		case '.': strcpy(cc, "\x1b[35m.\x1b[0m"); break;
		case '*': strcpy(cc, "\x1b[1m*\x1b[0m"); break;
		case '#': strcpy(cc, "\x1b[33m#\x1b[0m"); break;
		}
	}else{
		cc[0] = c; cc[1] = 0;
	}
	fprintf(stderr, "%d:M %s.%d %s %s\n", getpid(), tbuf, ((int)(spec.tv_nsec/1.0e6))%1000, cc, buffer);
	va_end(args);
}

void opendb(){
	rocksdb::Options options;
	options.create_if_missing = true;
	if (inmem){
		options.env = rocksdb::NewMemEnv(rocksdb::Env::Default());
	}
	rocksdb::Status s = rocksdb::DB::Open(options, dir, &db);
	if (!s.ok()){
		err(1, "%s", s.ToString().c_str());
	}
}

void flushdb(){
	delete db;
	if (remove_directory(dir, false)){
		err(1, "remove_directory");
	}
	opendb();
}

int main(int argc, char **argv) {
	int tcp_port = 5555;
	bool tcp_port_provided = false;
	const char *rocksdb_version = rocksdb::Version::GetRocksVersionAsString();
	for (int i=1;i<argc;i++){
		if (strcmp(argv[i], "-h")==0||
			strcmp(argv[i], "--help")==0||
			strcmp(argv[i], "-?")==0){
			fprintf(stdout, "RocksDB version " rocksdb_version ", Libuv version " uv_version_string() ", Server version " SERVER_VERSION "\n");
			fprintf(stdout, "usage: %s [-d data_path] [-p tcp_port] [--sync] [--inmem]\n", argv[0]);
			return 0;
		}else if (strcmp(argv[i], "--version")==0){
			fprintf(stdout, "Server version " SERVER_VERSION "\n");
			return 0;
		}else if (strcmp(argv[i], "-d")==0){
			if (i+1 == argc){
				fprintf(stderr, "argument missing after: \"%s\"\n", argv[i]);
				return 1;
			}
			dir = argv[++i];
		}else if (strcmp(argv[i], "--sync")==0){
			nosync = false;
		}else if (strcmp(argv[i], "--inmem")==0){
			inmem = true;
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
		}else{
			fprintf(stderr, "unknown option argument: \"%s\"\n", argv[i]);
			return 1;
		}
	}
	log('#', "Server started, Server version " SERVER_VERSION);
	opendb();

	uv_tcp_t server;
	loop = uv_default_loop();

	struct sockaddr_in addr;
	uv_ip4_addr("0.0.0.0", tcp_port, &addr);

	uv_tcp_init(loop, &server);
	uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);

	int r = uv_listen((uv_stream_t *)&server, -1, on_accept);
	if (r) {
		err(1, "uv_listen");
	}
	log('*', "The server is now ready to accept connections on port %d", tcp_port);
	return uv_run(loop, UV_RUN_DEFAULT);
}
