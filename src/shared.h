#ifndef SHARED_H

#include <libmill.h>

typedef const char *error;

int stringmatchlen(const char *pattern, int patternLen,
        const char *string, int stringLen, int nocase);

void panic(const char *str);

typedef struct client_t client;

client *client_new_pipe();
client *client_new_unix(unixsock unix);
client *client_new_tcp(unixsock tcp);
void client_free(client *c);

error client_read_command(client *c);
int client_command_argc(client *c);
const char *client_command_argv(client *c, int i);


void client_write(client *c, void *data, int n);
void client_write_bulk(client *c, void *data, int n);
void client_write_multibulk(client *c, int n);
void client_write_int(client *c, int n);
void client_flush(client *c);

enum client_type {CLIENT_PIPE, CLIENT_TCP, CLIENT_UNIX};

#endif // SHARED_H
