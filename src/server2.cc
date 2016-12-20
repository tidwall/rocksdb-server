/*
 * Copyright (c) 2011, Jason Ish
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *     
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * libevent echo server example.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* For inet_ntoa. */
#include <arpa/inet.h>

/* Required by event.h. */
#include <sys/time.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>

/* Libevent. */
#include <event.h>

/* Port to listen on. */
//#define SERVER_PORT 5555

/**
 * A struct for client specific data, in this simple case the only
 * client specific data is the read event.
 */
struct client {
	struct event ev_read;
	struct event ev_write;
	char *write_buf;
	int write_buf_len;
	int write_buf_cap;
	int write_buf_idx;
};

/**
 * Set a socket to non-blocking mode.
 */
int
setnonblock(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0)
		return -1;

        return 0;
}

/**
 * This function will be called by libevent when the client socket is
 * ready for reading.
 */
void
on_read(int fd, short ev, void *arg)
{
	struct client *client = (struct client *)arg;
	u_char buf[8196];
	int len, wlen;

    len = read(fd, buf, sizeof(buf));
	if (len <= 0){
        close(fd);
		event_del(&client->ev_read);
		if (client->write_buf){
			free(client->write_buf);
		}
		free(client);
		if (len < 0){
			printf("Socket failure, disconnecting client: %s",
				 strerror(errno));
		}
		return;
	}
	event_add(&client->ev_write, NULL);
return;
	len = 5;
	memcpy(buf, "$-1\r\n", len);//+OK\r\n", 5);

	if (client->write_buf_cap-client->write_buf_idx-client->write_buf_len<len){
		while (client->write_buf_cap-client->write_buf_idx-client->write_buf_len<len){
			if (client->write_buf_cap==0){
				client->write_buf_cap = 1;
			}else{
				client->write_buf_cap *= 2;
			}
		}
		printf("alloc\n");
		client->write_buf = (char*)realloc(client->write_buf, client->write_buf_cap);
		if (!client->write_buf){
			err(1, "malloc");
		}
	}
	memcpy(client->write_buf+client->write_buf_idx+client->write_buf_len, buf, len);
	client->write_buf_len += len;


	/* Since we now have data that needs to be written back to the
	 * client, add a write event. */
	event_add(&client->ev_write, NULL);
}
/**
 * This function will be called by libevent when the client socket is
 * ready for writing.
 */
void on_write(int fd, short ev, void *arg) {
	struct client *client = (struct client *)arg;
	struct bufferq *bufferq;
	int len;

if (1){
	len = write(fd, ":-1\r\n", 5);//client->write_buf+client->write_buf_idx, client->write_buf_len);
	if (len == -1) {
		if (errno == EINTR || errno == EAGAIN) {
			/* The write was interrupted by a signal or we
			 * were not able to write any data to it,
			 * reschedule and return. */
			event_add(&client->ev_write, NULL);
			return;
		}else {
			/* Some other socket error occurred, exit. */
			warn("write");
			return;
		}
	} else if (len < client->write_buf_len) {
		err(1, "too little data");
	}
	return;
}
	if (client->write_buf_len == 0){
		return;
	}
	len = write(fd, client->write_buf+client->write_buf_idx, client->write_buf_len);
	if (len == -1) {
		if (errno == EINTR || errno == EAGAIN) {
			/* The write was interrupted by a signal or we
			 * were not able to write any data to it,
			 * reschedule and return. */
			event_add(&client->ev_write, NULL);
			return;
		}else {
			/* Some other socket error occurred, exit. */
			warn("write");
			return;
		}
	} else if (len < client->write_buf_len) {
		/* Not all the data was written, update the offset and
		 * reschedule the write event. */
		client->write_buf_idx += len;
		client->write_buf_len -= len;
		event_add(&client->ev_write, NULL);
		return;
	}
	client->write_buf_idx = 0;
	client->write_buf_len = 0;
}
// on_accept will be called by libevent when there is a connection
// ready to be accepted.
void on_accept(int fd, short ev, void *arg){
	int client_fd;
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	struct client *client;

	// Accept the new connection.
	client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);
	if (client_fd == -1) {
		warn("accept failed");
		return;
	}

	// Set the client socket to non-blocking mode.
	if (setnonblock(client_fd) < 0){
		warn("failed to set client socket non-blocking");
	}

	/* We've accepted a new client, allocate a client object to
	 * maintain the state of this client. */
	client = (struct client*)calloc(1, sizeof(*client));
	if (client == NULL){
		err(1, "malloc");
	}

	/* Setup the read event, libevent will call on_read() whenever
	 * the clients socket becomes read ready.  We also make the
	 * read event persistent so we don't have to re-add after each
	 * read. */
	event_set(&client->ev_read, client_fd, EV_READ|EV_PERSIST, on_read, client);

	/* Setting up the event does not activate, add the event so it
	 * becomes active. */
	event_add(&client->ev_read, NULL);

	/* Create the write event, but don't add it until we have
	 * something to write. */
	event_set(&client->ev_write, client_fd, EV_WRITE, on_write, client);

	//printf("Accepted connection from %s\n", inet_ntoa(client_addr.sin_addr));
}

static void discard_cb(int severity, const char *msg){
	// This callback does nothing.
}

static int run_tcp_server(int port){
	int listen_fd;
	struct sockaddr_in listen_addr;
	int reuseaddr_on = 1;

	// The socket accept event.
	struct event ev_accept;

	// disbale logging
//	event_set_log_callback(discard_cb);

	// Initialize libevent
	event_init();

	// Create our listening socket. 
	// This is largely boiler plate code that I'll abstract away in the future.
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0){
		err(1, "socket");
	}
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, sizeof(reuseaddr_on)) == -1){
		err(1, "setsockopt");
	}
	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = INADDR_ANY;
	listen_addr.sin_port = htons(port);
	if (bind(listen_fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0){
		err(1, "bind");
	}
	if (listen(listen_fd, 5) < 0){
		err(1, "listen");
	}

	// Set the socket to non-blocking, this is essential in event
	// based programming with libevent.
	if (setnonblock(listen_fd) < 0){
		err(1, "failed to set server socket to non-blocking");
	}

	// We now have a listening socket, we create a read event to
	// be notified when a client connects.
	event_set(&ev_accept, listen_fd, EV_READ|EV_PERSIST, on_accept, NULL);
	event_add(&ev_accept, NULL);

	// Start the libevent event loop.
	event_dispatch();
	return 0;
}

int main3(int argc, char **argv) {
	const char *dir = "data";
	int tcp_port = 5555;
	bool tcp_port_provided = false;
	bool nosync = false;
	for (int i=1;i<argc;i++){
		if (strcmp(argv[i], "-h")==0||
			strcmp(argv[i], "--help")==0||
			strcmp(argv[i], "-?")==0){
			fprintf(stdout, "RocksDB version " ROCKSDB_VERSION ", Libevent version " LIBEVENT_VERSION ", Server version " SERVER_VERSION "\n");
			fprintf(stdout, "usage: %s [-d data_path] [-u unix_bind] [-p tcp_port] [--nosync]\n", argv[0]);
			return 0;
		}else if (strcmp(argv[i], "--version")==0){
			fprintf(stdout, "RocksDB version " ROCKSDB_VERSION ", Libevent version " LIBEVENT_VERSION ", Server version " SERVER_VERSION "\n");
			return 0;
		}else if (strcmp(argv[i], "-d")==0){
			if (i+1 == argc){
				fprintf(stderr, "argument missing after: \"%s\"\n", argv[i]);
				return 1;
			}
			dir = argv[++i];
		}else if (strcmp(argv[i], "--nosync")==0){
			nosync = true;
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

	fprintf(stderr, "00000:M 01 Jan 00:00:00.000 # Server started, RocksDB version " ROCKSDB_VERSION ", Libevent version " LIBEVENT_VERSION ", Server version " SERVER_VERSION "\n");
	return run_tcp_server(tcp_port);
}
