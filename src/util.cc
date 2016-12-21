#include "server.h"

int remove_directory(const char *path, int remove_parent){
	DIR *d = opendir(path);
	size_t path_len = strlen(path);
	int r = -1;
	if (d){
		struct dirent *p;
		r = 0;
		while (!r && (p=readdir(d))){
			int r2 = -1;
			char *buf;
			size_t len;
			if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")){
			  continue;
			}
			len = path_len + strlen(p->d_name) + 2; 
			buf = (char*)malloc(len);
			if (!buf){
				err(1, "malloc");
			}
			struct stat statbuf;
            snprintf(buf, len, "%s/%s", path, p->d_name);
			if (!stat(buf, &statbuf)){
				if (S_ISDIR(statbuf.st_mode)){
					r2 = remove_directory(buf, 1);
                }else{
					r2 = unlink(buf);
				}
            }
			free(buf);
			r = r2;
		}
		closedir(d);
	}
	if (!r&&remove_parent){
	   r = rmdir(path);
	}
	return r;
}
