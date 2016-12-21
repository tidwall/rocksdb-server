#include "server.h"

error exec_set(client *c){
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
		err(1, "%s", s.ToString().c_str());
	}
	client_write(c, "+OK\r\n", 5);
	return NULL;
}

error exec_get(client *c){
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
		err(1, "%s", s.ToString().c_str());
	}
	client_write_bulk(c, value.data(), value.size());
	return NULL;
}

error exec_del(client *c){
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
		err(1, "%s", s.ToString().c_str());
	}
	rocksdb::WriteOptions write_options;
	write_options.sync = !nosync;
	s = db->Delete(write_options, key);
	if (!s.ok()){
		err(1, "%s", s.ToString().c_str());
	}
	client_write(c, ":1\r\n", 4);
	return NULL;
}

error exec_quit(client *c){
	client_write(c, "+OK\r\n", 5);
	return ERR_QUIT;
}

error exec_flushdb(client *c){
	if (c->args_len!=1){
		return "wrong number of arguments for 'flushdb' command";
	}
	flushdb();
	client_write(c, "+OK\r\n", 5);
	return NULL;
}

error exec_keys(client *c){
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
		err(1, "%s", s.ToString().c_str());	
	}
	delete it;

	// fill in the header and write from offset.
	char nb[32];
	sprintf(nb, "*%d\r\n", count);
	int nbn = strlen(nb);
	memcpy(c->output+30-nbn, nb, nbn);
	c->output_offset = 30-nbn;

	return NULL;
}

static bool iscmd(client *c, const char *cmd){
	int i = 0;
	for (;i<c->args_size[0];i++){
		if (c->args[0][i] != cmd[i] && c->args[0][i] != cmd[i]-32){
			return false;
		}
	}
	return !cmd[i];
}

error exec_command(client *c){
	if (c->args_len==0||(c->args_len==1&&c->args_size[0]==0)){
		return NULL;
	}
	if (iscmd(c, "set")){
		return exec_set(c);
	}else if (iscmd(c, "get")){
		return exec_get(c);
	}else if (iscmd(c, "del")){
		return exec_del(c);
	}else if (iscmd(c, "quit")){
		return exec_quit(c);
	}else if (iscmd(c, "keys")){
		return exec_keys(c);
	}else if (iscmd(c, "flushdb")){
		return exec_flushdb(c);
	}
	return client_err_unknown_command(c, c->args[0], c->args_size[0]);
}

