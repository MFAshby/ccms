/*
 Author: José Bollo <jobol@nonadev.net>
 Author: José Bollo <jose.bollo@iot.bzh>

 https://gitlab.com/jobol/mustach

 SPDX-License-Identifier: ISC
*/

#ifndef _mustach_json_c_h_included_
#define _mustach_json_c_h_included_

#include <json-c/json.h>

/**
 * fmustach_json_c - Renders the mustache 'template' in 'file' for 'root'.
 *
 * @template: the template string to instanciate
 * @root:     the root json object to render
 * @file:     the file where to write the result
 *
 * Returns 0 in case of success, -1 with errno set in case of system error
 * a other negative value in case of error.
 */
extern int fmustach_json_c(const char *template, struct json_object *root, FILE *file);

/**
 * fmustach_json_c - Renders the mustache 'template' in 'fd' for 'root'.
 *
 * @template: the template string to instanciate
 * @root:     the root json object to render
 * @fd:       the file descriptor number where to write the result
 *
 * Returns 0 in case of success, -1 with errno set in case of system error
 * a other negative value in case of error.
 */
extern int fdmustach_json_c(const char *template, struct json_object *root, int fd);


/**
 * fmustach_json_c - Renders the mustache 'template' in 'result' for 'root'.
 *
 * @template: the template string to instanciate
 * @root:     the root json object to render
 * @result:   the pointer receiving the result when 0 is returned
 * @size:     the size of the returned result
 *
 * Returns 0 in case of success, -1 with errno set in case of system error
 * a other negative value in case of error.
 */
extern int mustach_json_c(const char *template, struct json_object *root, char **result, size_t *size);

/**
 * umustach_json_c - Renders the mustache 'template' for 'root' to custom writer 'writecb' with 'closure'.
 *
 * @template: the template string to instanciate
 * @root:     the root json object to render
 * @writecb:  the function that write values
 * @closure:  the closure for the write function
 *
 * Returns 0 in case of success, -1 with errno set in case of system error
 * a other negative value in case of error.
 */
typedef int (*mustach_json_c_write_cb)(void*closure, const char*buffer, size_t size);
extern int umustach_json_c(const char *template, struct json_object *root, mustach_json_c_write_cb writecb, void *closure);

#endif

