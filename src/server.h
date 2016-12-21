#ifndef SERVER_H

#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <rocksdb/db.h>

extern rocksdb::DB* db;
extern bool nosync;
extern int nprocs;
extern uv_loop_t *loop;

extern const char *ERR_INCOMPLETE;
extern const char *ERR_QUIT;

int stringmatchlen(const char *pattern, int patternLen,
        const char *string, int stringLen, int nocase);

typedef const char *error;

typedef struct client_t {
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
} client;

client *client_new();
void client_free(client *c);
void client_close(client *c);

void client_write(client *c, const char *data, int n);
void client_clear(client *c);
void client_write_byte(client *c, char b);
void client_write_bulk(client *c, const char *data, int n);
void client_write_multibulk(client *c, int n);
void client_write_int(client *c, int n);
void client_write_error(client *c, error err);
void client_flush_offset(client *c, int offset);
void client_flush(client *c);
error client_err_expected_got(client *c, char c1, char c2);
error client_err_unknown_command(client *c, const char *name, int count);

bool client_exec_commands(client *c);

error exec_command(client *c);

#endif // SERVER_H

