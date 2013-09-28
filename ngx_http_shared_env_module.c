/*
nginx module for shared hosting environment
Copyright (C) 2013  Arran Schlosberg
https://github.com/aschlosberg/ngx-shared-env

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <ndk.h>
#include "ngx_http_shared_env_module.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>

#define NGX_SHARED_ENV_FPM_PORT_BASE 40000
#define NGX_SHARED_ENV_SHARED_DIR "/var/www/public/"
#define NGX_SHARED_ENV_PUBLIC_DIR "_public"
#define NGX_SHARED_ENV_OWNER_CACHE "/usr/local/nginx/conf/ownercache/"
#define NGX_SHARED_ENV_MAX_USERNAME_LEN 64

static ndk_set_var_t ngx_http_shared_env_set_dir_filter = {
	NDK_SET_VAR_VALUE,
	ngx_http_shared_env_set_dir,
	1,
	NULL
};

static ndk_set_var_t ngx_http_shared_env_set_owner_filter = {
	NDK_SET_VAR_VALUE,
	ngx_http_shared_env_set_owner,
	1,
	NULL
};

static ndk_set_var_t ngx_http_shared_env_set_fpm_port_filter = {
	NDK_SET_VAR_VALUE,
	ngx_http_shared_env_set_fpm_port,
	1,
	NULL
};

static ndk_set_var_t ngx_http_shared_env_read_file_filter = {
	NDK_SET_VAR_VALUE,
	ngx_http_shared_env_read_file,
	1,
	NULL
};

static ngx_command_t  ngx_http_shared_env_commands[] = {
	{
		ngx_string ("set_shared_env_directory"),
		NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_SIF_CONF|NGX_CONF_TAKE2,
		ndk_set_var_value,
		0,
		0,
		&ngx_http_shared_env_set_dir_filter
	},
	{
		ngx_string ("set_shared_env_owner"),
		NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_SIF_CONF|NGX_CONF_TAKE2,
		ndk_set_var_value,
		0,
		0,
		&ngx_http_shared_env_set_owner_filter
	},
	{
		ngx_string ("set_shared_env_fpm_port"),
		NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_SIF_CONF|NGX_CONF_TAKE2,
		ndk_set_var_value,
		0,
		0,
		&ngx_http_shared_env_set_fpm_port_filter
	},
	{
		ngx_string ("set_shared_env_file_contents"),
		NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_SIF_CONF|NGX_CONF_TAKE2,
		ndk_set_var_value,
		0,
		0,
		&ngx_http_shared_env_read_file_filter
	},
	ngx_null_command
};


static ngx_http_module_t  ngx_http_shared_env_module_ctx = {
	NULL,                                 /* preconfiguration */
	NULL,                                 /* postconfiguration */

	NULL,                                  /* create main configuration */
	NULL,                                  /* init main configuration */

	NULL,                                  /* create server configuration */
	NULL,                                  /* merge server configuration */

	NULL,     /* create location configuration */
	NULL       /*  merge location configuration */
};


ngx_module_t  ngx_http_shared_env_module = {
	NGX_MODULE_V1,
	&ngx_http_shared_env_module_ctx,          /* module context */
	ngx_http_shared_env_commands,             /* module directives */
	NGX_HTTP_MODULE,                        /* module type */
	NULL,                                   /* init master */
	NULL,                                   /* init module */
	NULL,                                   /* init process */
	NULL,                                   /* init thread */
	NULL,                                   /* exit thread */
	NULL,                                   /* exit process */
	NULL,                                   /* exit master */
	NGX_MODULE_V1_PADDING
};

/*
 * Domains are mapped to the filesystem by the similarity between directory and domain hierarchy:
 * www.example.com -> com/example/www
 *
 * A state machine finds all . occurrences in the domain and then:
 * 1. Places a / in the relevant position of the output
 * 2. Copies the sub-domain since the last . to the relevant output memory
 */
ngx_int_t ngx_http_shared_env_set_dir(ngx_http_request_t *r, ngx_str_t *res, ngx_http_variable_value_t *v) {
	res->len = v->len;
	ndk_palloc_re(res->data, r->pool, res->len);

	int i, last;

	// keep track of the last time a . was found
	// initial value of -1 is a hack for one as the first character (although not possible in a domain, is important for other aspects)
	last = -1;
	for(i=0; i<=v->len; i++){
		if(i==v->len || v->data[i]=='.'){
			if(i!=v->len){
				res->data[v->len - i - 1] = '/';
			}
			memcpy(res->data+v->len-i, v->data+last+1, i-last-1);
			last = i;
		}
	}

	return NGX_OK;
}

/*
 * NGX_SHARED_ENV_SHARED_DIR contains a directory for each user, within which, the user's domains are mapped using ngx_http_shared_env_set_dir()
 * The public HTML directory is differentiated by a preceding _ as _public
 *
 * e.g. user2 has a domain www.example.com:
 *  /path/to/shared
 *  - user1
 *  - user2
 *  - - com
 *  - - - example
 *  - - - - www
 *  - - - - - _public
 *  - user3
 *
 *  The owner is found by iterating through each user's directory and checking for the existence of the public HTML directory.
 *
 *  This is cached in a file stored in NGX_SHARED_ENV_OWNER_CACHE : www.example.com -> NGX_SHARED_ENV_OWNER_CACHE/com.example.www
 */
ngx_int_t ngx_http_shared_env_set_owner(ngx_http_request_t *r, ngx_str_t *res, ngx_http_variable_value_t *v) {
	// first attempt to read it from the filesystem cache
	char cacheFile[v->len+1];
	snprintf(cacheFile, v->len+1, "%s", v->data);
	str_replace(&(cacheFile[0]), '/', '.');
	int cachePathLen = v->len+2+strlen(NGX_SHARED_ENV_OWNER_CACHE);
	char cachePath[cachePathLen];
	snprintf(cachePath, cachePathLen, "%s/%s", NGX_SHARED_ENV_OWNER_CACHE, cacheFile);

	FILE *fd = fopen(cachePath, "r");
	if(fd){
		fseek(fd, 0, SEEK_END);
		res->len = ftell(fd);
		rewind(fd);
		ndk_palloc_re(res->data, r->pool, res->len);
		if(!fread(res->data, sizeof(char), res->len, fd)){}  //throws a warning if return value is ignored and won't let nginx compile
		fclose(fd);
		return NGX_OK;
	}

	ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "Owner of (%s) not found in cache (file: %s)", v->data, cacheFile);

	//not in the filesystem cache
	struct dirent *e;
	struct stat sb;

	// for concatenation of various path elements with snprintf
	char fullPath[strlen(NGX_SHARED_ENV_SHARED_DIR)+v->len+NGX_SHARED_ENV_MAX_USERNAME_LEN];

	errno = 0;
	DIR *sharedDir = opendir(NGX_SHARED_ENV_SHARED_DIR);
	if(!sharedDir){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "set_shared_env_owner: could not open shared directory (opendir errno: %d)", errno);
		return NGX_ERROR;
	}

	bool found = false;
	while((e=readdir(sharedDir))!=NULL){
		// skip non-directories
		#ifdef _DIRENT_HAVE_D_TYPE // readdir returns a struct with d_type
			if(e->d_type!=DT_DIR){
				continue;
			}
		#else // have to use stat instead
			snprintf(fullPath, sizeof(fullPath), "%s%s", NGX_SHARED_ENV_SHARED_DIR, e->d_name);
			stat(fullPath, &sb);
			if(!S_ISDIR(sb.st_mode)){
				continue;
			}
		#endif

		// skip current and parent directories
		if(strcmp(e->d_name, ".")==0 || strcmp(e->d_name, "..")==0){
			continue;
		}

		// check for existence of full path
		snprintf(fullPath, sizeof(fullPath), "%s%s/%s/%s", NGX_SHARED_ENV_SHARED_DIR, e->d_name, v->data, NGX_SHARED_ENV_PUBLIC_DIR);
		if(stat(fullPath, &sb)==0){
			if(S_ISDIR(sb.st_mode)){
				found = true;
				res->len = (int) strlen(e->d_name);
				ndk_palloc_re(res->data, r->pool, res->len);
				memcpy(res->data, &(e->d_name), res->len);
				break;
			}
		}
	}

	if(found){
		// save to the filesystem cache
		fd = fopen(cachePath, "w");
		if(fd){
			if(!fwrite(res->data, sizeof(char), res->len, fd)){}
			fclose(fd);
		}
		else {
			ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "Could not cache owner (file: %s, fopen errno: %d)", cacheFile, errno);
		}
	}

	closedir(sharedDir);
	return NGX_OK;
}

/*
 * User's have PHP-FPM pools that listen on unique ports. The port is determined by the user's UID + NGX_SHARED_ENV_FPM_PORT_BASE.
 *
 * e.g. user 1001 has a pool listening on 127.0.0.1:41001
 */
ngx_int_t ngx_http_shared_env_set_fpm_port(ngx_http_request_t *r, ngx_str_t *res, ngx_http_variable_value_t *v) {
	char user[v->len+1]; //why do I need the +1? it truncates without - does snprintf use 1 character for \0 to make it a C-string?
	snprintf(user, v->len+1, "%s", v->data);

	errno = 0;
	struct passwd *pw = getpwnam(user);
	if(!pw){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "set_shared_env_fpm_port: could not get passwd data (user: %s, getpwnam errno: %d)", user, errno);
		return NGX_ERROR;
	}

	unsigned int port = NGX_SHARED_ENV_FPM_PORT_BASE + (unsigned int) pw->pw_uid;

	res->len = (int) ceil(log10(port));

	char portStr[res->len+1]; //leave space for the null character but ignore it later otherwise FPM complains of a bad port
	snprintf(portStr, res->len+1, "%d", port);

	ndk_palloc_re(res->data, r->pool, res->len);
	memcpy(res->data, &(portStr[0]), res->len);

	return NGX_OK;
}

/*
 * Read the contents of a file into a variable.
 */
ngx_int_t ngx_http_shared_env_read_file(ngx_http_request_t *r, ngx_str_t *res, ngx_http_variable_value_t *v) {
	char path[v->len+1];
	snprintf(path, v->len+1, "%s", v->data);

	FILE *fd = fopen(path, "r");
	if(!fd){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "set_shared_env_file_contents: could not open file (%s)", path);
		return NGX_ERROR;
	}

	fseek(fd, 0, SEEK_END);
	res->len = ftell(fd);
	rewind(fd);
	ndk_palloc_re(res->data, r->pool, res->len);
	if(!fread(res->data, sizeof(char), res->len, fd)){}  //throws a warning if return value is ignored and won't let nginx compile
	fclose(fd);

	return NGX_OK;
}

void str_replace(char *str, char from, char to){
	unsigned int len = strlen(str), i;
	for(i=0; i<len; i++){
		if(str[i]==from){
			str[i]=to;
		}
	}
}
