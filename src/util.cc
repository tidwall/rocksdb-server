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

int pattern_limits(const char *pattern, int pattern_len, char **start, int *start_len, char **end, int *end_len) {
	int n = pattern_len;
	if (pattern_len==1&&pattern[0]=='*'){
		*start = NULL;
		*start_len = 0;
		*end = NULL;
		*end_len = 0;
		return 1;
	}
	*start = (char*)calloc(1, n*2);
	*end = (char*)calloc(1, n*2);
	if (!*start||!*end){
		err(1, "malloc");
	}
	int i = 0;
	int j = 0;
	int star = 1;
	for (;i<n;i++){
		switch (pattern[i]){
			default:
				(*start)[j] = pattern[i];
				(*end)[j] = pattern[i];
				j++;
				break;
			case '*':
				star = 1;
				goto break_loop;
			case '?':case '[':case '\\':
				goto break_loop;
		}
	}
break_loop:
	*start_len = j;
	*end_len = j;
	if (j == 0){
		return star;
	}
	if ((unsigned char)((*end)[*end_len-1])==0xFF){
		(*end)[*end_len] = 0;
		*end_len = *end_len + 1;
	}else{
		(*end)[j-1]++;
	}
	return 0;
}

// atop returns a positive integer. invalid or negative integers return -1.
int atop(const char* str, int len){
    int ret = 0;
    for(int i = 0; i < len; i++){
		if (str[i] < '0' || str[i] > '9'){
			return -1;
		}
		ret = ret * 10 + (str[i] - '0');
    }
    return ret;
}
