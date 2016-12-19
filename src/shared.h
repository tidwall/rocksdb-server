#ifndef SHARED_H

#include <libmill.h>


int stringmatchlen(const char *pattern, int patternLen,
        const char *string, int stringLen, int nocase);

void panic(const char *str);

typedef const char *error;

typedef struct client_t client;

enum client_type {CLIENT_PIPE, CLIENT_TCP, CLIENT_UNIX};

client *client_new_pipe();
client *client_new_unix(unixsock unix);
client *client_new_tcp(unixsock tcp);
void client_free(client *c);

error client_read_command(client *c);
int client_argc(client *c);
int *client_argl(client *c);
const char **client_argv(client *c);


void client_clear(client *c);
void client_write(client *c, const char *data, int n);
void client_write_bulk(client *c, const char *data, int n);
void client_write_multibulk(client *c, int n);
void client_write_int(client *c, int n);
void client_write_error(client *c, error err);
void client_flush(client *c);
void client_flush_offset(client *c, int offset);
error client_run(client *c);

// client_raw returns the raw output buffer.
char *client_raw(client *c);
int client_raw_len(client *c);

// creates a temporary error. use the result asap.
error client_err_unknown_command(client *c, const char *name, int count);
error client_err_expected_got(client *c, char c1, char c2);


error exec_get(client *c);
error exec_set(client *c);
error exec_del(client *c);
error exec_quit(client *c);
error exec_keys(client *c);
error exec_command(client *c);

#endif // SHARED_H

