/*
nginx module for shared hosting environment
Copyright (C) 2013  Oonix Pty Ltd (oonix.com.au)
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

#include <ngx_core.h>
#include <ngx_config.h>
#include <ngx_http.h>

#ifndef NULL
#define NULL 0
#endif

#ifndef ngx_null_command
#define ngx_null_command 0
#endif

ngx_int_t ngx_http_shared_env_set_dir(ngx_http_request_t *r, ngx_str_t *res, ngx_http_variable_value_t *v);
ngx_int_t ngx_http_shared_env_set_owner(ngx_http_request_t *r, ngx_str_t *res, ngx_http_variable_value_t *v);
ngx_int_t ngx_http_shared_env_set_fpm_port(ngx_http_request_t *r, ngx_str_t *res, ngx_http_variable_value_t *v);
ngx_int_t ngx_http_shared_env_read_file(ngx_http_request_t *r, ngx_str_t *res, ngx_http_variable_value_t *v);
ngx_int_t ngx_http_shared_env_handler(ngx_http_request_t *r, ngx_str_t *res, ngx_http_variable_value_t *v);

void str_replace(char *str, char from, char to);
