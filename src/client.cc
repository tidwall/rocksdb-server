#include "server.h"

client *client_new(){
	client *c = (client*)calloc(1, sizeof(client));
	if (!c){
		err(1, "malloc");
	}
	c->worker.data = c; // self reference
	return c;
}

void client_free(client *c){
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

void on_close(uv_handle_t *stream){
	client_free((client*)stream);
}

void client_close(client *c){
	uv_close((uv_handle_t *)&c->tcp, on_close);
}
inline void client_output_require(client *c, size_t siz){
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
void client_write(client *c, const char *data, int n){
	client_output_require(c, c->output_len+n);
	memcpy(c->output+c->output_len, data, n);	
	c->output_len+=n;
}

void client_clear(client *c){
	c->output_len = 0;
	c->output_offset = 0;
}

void client_write_byte(client *c, char b){
	client_output_require(c, c->output_len+1);
	c->output[c->output_len++] = b;
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

void client_write_error(client *c, error err){
	client_write(c, "-ERR ", 5);
	client_write(c, err, strlen(err));
	client_write_byte(c, '\r');
	client_write_byte(c, '\n');
}


void client_flush_offset(client *c, int offset){
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
void client_flush(client *c){
	client_flush_offset(c, 0);
}

void client_err_alloc(client *c, int n){
	if (c->tmp_err){
		free(c->tmp_err);
	}
	c->tmp_err = (char*)malloc(n);
	if (!c->tmp_err){
		err(1, "malloc");
	}
	memset(c->tmp_err, 0, n);
}

error client_err_expected_got(client *c, char c1, char c2){
	client_err_alloc(c, 64);
	sprintf(c->tmp_err, "Protocol error: expected '%c', got '%c'", c1, c2);
	return c->tmp_err;
}

error client_err_unknown_command(client *c, const char *name, int count){
	client_err_alloc(c, count+64);
	c->tmp_err[0] = 0;
	strcat(c->tmp_err, "unknown command '");
	strncat(c->tmp_err, name, count);
	strcat(c->tmp_err, "'");
	return c->tmp_err;
}
void client_append_arg(client *c, const char *data, int nbyte){
	if (c->args_cap==c->args_len){
		if (c->args_cap==0){
			c->args_cap=1;
		}else{
			c->args_cap*=2;
		}
		c->args = (const char**)realloc(c->args, c->args_cap*sizeof(const char *));
		if (!c->args){
			err(1, "malloc");
		}
		c->args_size = (int*)realloc(c->args_size, c->args_cap*sizeof(int));
		if (!c->args_size){
			err(1, "malloc");
		}
	}
	c->args[c->args_len] = data;
	c->args_size[c->args_len] = nbyte;
	c->args_len++;
}

error client_parse_telnet_command(client *c){
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

error client_read_command(client *c){
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

void client_print_args(client *c){
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

bool client_exec_commands(client *c){
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
