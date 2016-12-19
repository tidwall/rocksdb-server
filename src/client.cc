#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libmill.h>
#include "shared.h"

typedef struct client_t {
	client_type type;
	tcpsock tcp;
	unixsock unix;

	char *output;
	int output_len;
	int output_cap;

	char *buf;   
	int buf_cap; 
	int buf_len; 
	int buf_idx;

	const char **args;
	int args_cap;
	int args_len;
	int *args_size;

	char tmp_err[0xFF]; // storing a temporary error

} client;

void client_free(client *c){
	if (c){
		if (c->output){
			free(c->output);
		}
		if (c->buf){
			free(c->buf);
		}
		if (c->args){
			free(c->args);
		}
		free(c);
	}
}

client *client_new_pipe(){
	client *c = (client*)malloc(sizeof(client));
	if (!c){
		return NULL;
	}
	memset(c, 0, sizeof(client));
	c->type = CLIENT_PIPE;
	return c;
}

client *client_new_unix(unixsock unix){
	client *c = (client*)malloc(sizeof(client));
	if (!c){
		return NULL;
	}
	memset(c, 0, sizeof(client));
	c->type = CLIENT_UNIX;
	c->unix = unix;
	return c;
}

client *client_new_tcp(unixsock tcp){
	client *c = (client*)malloc(sizeof(client));
	if (!c){
		return NULL;
	}
	memset(c, 0, sizeof(client));
	c->type = CLIENT_TCP;
	c->unix = tcp;
	return c;
}

error client_read(client *c, void *buf, int nbyte){
	int n;
	switch (c->type){
	default:
		n = -1;
		break;
	case CLIENT_PIPE:
		n = read(STDIN_FILENO, buf, nbyte);
		break;
	case CLIENT_TCP:
		n = tcprecv(c->tcp, buf, nbyte, -1);
		break;
	case CLIENT_UNIX:
		n = unixrecv(c->unix, buf, nbyte, -1);
		break;
	}
	if (n != nbyte){
		if (n == 0){
			return "eof";
		}
		return "bad read";
	}
	return NULL;
}

static const char *expected_got(char *buf, char c1, char c2){
	sprintf(buf, "Protocol error: expected '%c', got '%c'", c1, c2);
	return buf;
}
static void client_append_arg(client *c, const char *data, int nbyte){
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
static error client_parse_telnet_command(client *c){
	size_t i = c->buf_idx;
	size_t z = c->buf_len+c->buf_idx;
	if (i >= z){
		return "incomplete";
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
	return "incomplete";
}

int client_argc(client *c){
	return c->args_len;
}
int *client_argl(client *c){
	return c->args_size;
}
const char **client_argv(client *c){
	return c->args;
}

static error client_parse_command(client *c){
	size_t i = c->buf_idx;
	size_t z = c->buf_idx+c->buf_len;
	if (i >= z){
		return "incomplete";
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
		return "incomplete";
	}
	for (int j=0;j<args_len;j++){
		if (i >= z){
			return "incomplete";
		}
		if (c->buf[i] != '$'){
			return expected_got(c->tmp_err, '$', c->buf[i]);
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
					return "incomplete";
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

error client_read_command(client *c){
	const char *err = client_parse_command(c);
	if (err == NULL){
		return NULL;
	}
	if (strcmp(err, "incomplete") != 0){
		return err;
	}
	// read into buffer
	if (c->buf_cap-c->buf_len == 0){
		if (c->buf_cap==0){
			c->buf_cap = 1;
		}else{
			c->buf_cap*=2;
		}
		c->buf = (char*)realloc(c->buf, c->buf_cap);
		if (!c->buf){
			panic("out of memory");
		}
	//	memset(c->buf+c->buf_idx+c->buf_len, 0, c->buf_cap-c->buf_idx-c->buf_len);
	}
	size_t n = read(STDIN_FILENO, c->buf+c->buf_idx+c->buf_len, c->buf_cap-c->buf_idx-c->buf_len);
	if (n <= 0){
		if (n == 0){
			if (c->buf_len>0){
				return "Protocol error: incomplete command";
			}
			return "eof";
		}
		return strerror(n);
	}
	c->buf_len+=n;
	return client_read_command(c);
}

static inline void client_output_require(client *c, size_t siz){
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
			panic("out of memory");
		}
	}
}

void client_clear(client *c){
	c->output_len = 0;
}

static void client_write_byte(client *c, char b){
	client_output_require(c, c->output_len+1);
	c->output[c->output_len++] = b;
}

void client_write(client *c, const char *data, int n){
	client_output_require(c, c->output_len+n);
	memcpy(c->output+c->output_len, data, n);	
	c->output_len+=n;
}

void client_write_bulk(client *c, const char *data, int n){
	char h[32];
	sprintf(h, "$%d\r\n", n);
	client_write(c, h, strlen(h));
	client_write(c, data, n);
	client_write_byte(c, '\r');
	client_write_byte(c, '\n');
}

void client_write_multibulk(client *c, int n){
	char h[32];
	sprintf(h, "*%d\r\n", n);
	client_write(c, h, strlen(h));
}

void client_write_int(client *c, int n){
	char h[32];
	sprintf(h, ":%d\r\n", n);
	client_write(c, h, strlen(h));
}

// client_flush flushes the bytes to the client output stream. it does not
// check for errors because the client read operations handle socket errors.
void client_flush(client *c){
	if (c->output_len == 0){
		return;
	}
	switch (c->type){
	case CLIENT_PIPE:
		write(STDOUT_FILENO, c->output, c->output_len);
		break;
	case CLIENT_TCP:
		tcpsend(c->tcp, c->output, c->output_len, -1);
		break;
	case CLIENT_UNIX:
		unixsend(c->unix, c->output, c->output_len, -1);
		break;
	}
	c->output_len = 0;
}
