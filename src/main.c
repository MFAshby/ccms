#include <json-c/json_tokener.h>
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>

#define DG_DYNARR_IMPLEMENTATION
#include <DG_dynarr.h>
#include <microhttpd.h>
#include <sqlite3.h>
#include <mustach.h>
#include <cmark.h>
#include <json-c/json.h>

/////////// Macros ////////////

/*
 * Exits the program with an error message if the error code isn't OK
 */
#define sqlite_check(db, function_call) {\
	int error_code = function_call; \
	if (error_code != SQLITE_OK) { \
		fprintf(stderr, "sqlite3 error at: %s:%d\n" \
		   "%s", __FILE__, __LINE__, sqlite3_errmsg(db)); \
		raise(SIGTERM); \
	} \
}

/*
 * Copies a resource embedded into the binary with ld into 
 * the heap, and adds a null terminator. 
 * Caller is responsible for freeing the memory.
 * Implemented as a macro, so that just the sanitized 
 * resource path is required.
 */
#define null_terminated_resource(resource) ({\
	char* res; \
	do { \
		extern char _binary_##resource ##_start[]; \
		extern char _binary_##resource##_end[]; \
		int len = _binary_##resource##_end - _binary_##resource##_start; \
		res = malloc(len + 1); \
		memcpy(res,  _binary_##resource##_start, len); \
		res[len] = '\0'; \
	} while(0); \
	res; \
})

//////////// Structures /////////////

/*
 * List of strings, useful sometimes.
 */
DA_TYPEDEF(char*, Strings);

/*
 * Structure representing a single navigation item
 */
typedef struct _NavItem {
	char* title;
	char* url;
} NavItem;

/*
 * Collection representing a list of nav items
 */
DA_TYPEDEF(NavItem, NavItems);

/*
 * Structure representing a translateable element of the theme
 * e.g. a tagline, powered-by, copyright notice, or similar.
 */
typedef struct _ThemeContentItem {
	char* name;
	char* value;
} ThemeContentItem;

/*
 * Collection represting a list of theme content items
 */
DA_TYPEDEF(ThemeContentItem, ThemeContentItems);

/* 
 * Structure containing all data required for loading a content page
 */
typedef struct _PageData {
	// The template page, from the current theme
	char* template;
	// Localizable content from the current theme
	ThemeContentItems content_items;

	// The title of the page
	char* title;
	// The actual content of the page, 
	char* content;
	char* language;

	// Navigation data
	NavItems nav_items;

	// These two are used by mustach for iterating over 
	// nav_items. Potentially they shouldn't be on this type,
	// but on some kind of wrapper.
	int isnav; 
	int navix; 

	// Error page information
	int isnotfound;
} PageData;

/*
 * Structure reprenting simple static content, e.g. images or CSS
 */
typedef struct _StaticResource {
	char* key;
	void* value;
	int value_size;
	char* content_type;
	bool isnotfound;
} StaticResource;

///// Types for API calls /////

/*
 * Represents a virtual host, i.e. a website 
 * hosted in the same ccms instance.
 */
typedef struct _Server {
	int id;
	char* hostname;
	int theme_id;
	bool is_default;
} Server;

DA_TYPEDEF(Server, Servers);

/*
 * Types for manipulating servers
 */
typedef struct _NewServer {
	bool valid;
	char* hostname;
	int theme_id;
} NewServer;

typedef struct _NewServerResponse {
	bool success;
	char* error_message;
	Server server;
} NewServerResponse;

/*
 * Structure representing a single page on a website.
 */
typedef struct _Page {
	int id;
	// Which server this page belongs to
	int server_id;
	// If the page has a parent, it appears in a navigation
	// sub menu
	int parent_page_id; 
	// The URL path of the page, e.g. /hello-world
	char* relative_path;
} Page;

DA_TYPEDEF(Page, Pages);

/*
 * Structures for manipulating pages
 */
typedef struct _NewPage {
	bool valid;
	int server_id;
	int parent_page_id; 
	char* relative_path;
} NewPage;

typedef struct _NewPageResponse {
	bool success;
	char* error_message;
	Page page;
} NewPageResponse;

/*
 * Structure representing the _content_ of a Page in a 
 * particular language.
 */
typedef struct _PageContent {
	int id;
	int page_id;
	char* language;
	char* title;
	char* content;
} PageContent;

DA_TYPEDEF(PageContent, PageContents);

/*
 * Structures for manipulating page contents.
 */
typedef struct _NewPageContent {
	bool valid;
	int page_id;
	char* language;
	char* title;
	char* content;
} NewPageContent;

typedef struct _NewPageContentResponse {
	bool success;
	char* error_message;
	PageContent content;
} NewPageContentResponse;

typedef struct _PatchPageContent {
	bool valid;
	int id;
	char* title;
	char* content;
} PatchPageContent;

typedef struct _PatchPageContentResponse {
	bool success;
	char* error_message;
} PatchPageContentResponse;

/*
 * Structure to encapsulate an HTTP response,
 * agnostic of any specific web server implementation.
 */
typedef struct _HttpResponse {
	int status_code;
	char* content;
	size_t content_length;
	char* content_type;
} HttpResponse;


///////////// Globals ////////////////
 
// The web server http_server_daemon
static struct MHD_Daemon* http_server_daemon;

// The database
static sqlite3* db;

///////////// Functions ///////////////

/**
 * Duplicates a null-terminated string.
 * Caller is responsible for freeing the 
 * string.
 */
char* strdup(const char* str) {
	size_t len = strlen(str)+1;
	char* r = (char*)malloc(len);
	memcpy(r, str, len);
	return r;
}

/*
 * Open the database and run the initialization script.
 */
void initialize_database(const char* database_path) {
	sqlite_check(db, sqlite3_open(database_path, &db));
	char* initial_script = null_terminated_resource(src_initial_sql);
	sqlite_check(db, sqlite3_exec(db, initial_script, NULL, NULL, NULL));
	free(initial_script);
}

/*
 * Serializes a json_object to a string and wraps it up in an 
 * HttpResponse for return to the http server library
 */
HttpResponse http_json_response(struct json_object* obj, int status_code) {
	char* jsonstr = (char*)json_object_to_json_string(obj);
	HttpResponse ret = {
		.content = strdup(jsonstr),
		.content_length = strlen(jsonstr),
		.content_type = strdup("application/json"),
		.status_code = status_code,
	};
	return ret;
}

/*
 * Wraps an error message in a JSON string, i.e:
 * { "error": "your message here" }
 * Caller is responsible for freeing the string.
 */
HttpResponse http_error_response(char* errmsg, int status_code) {
	struct json_object* obj = json_object_new_object();
	json_object_object_add(obj, "error", json_object_new_string(errmsg));
	HttpResponse ret = http_json_response(obj, status_code);
	json_object_put(obj);
	return ret;
}


///////////// Content /////////////////

/*
 * Finds the page contents, given a particular host, path, and language.
 */
PageData find_page_data(const char* host, 
		const char* path, 
		const char* lang) {
	printf("Looking for page with host %s path %s lang %s\n",
			host, 
			path,
			lang);
	sqlite3_stmt* stmt;
	int v;

	// Populate page data
	sqlite_check(db, sqlite3_prepare_v2(db, 
			"select t.template, t.id, pc.title, pc.content, pc.language "
			"from server s "
			"left outer join page p "
				"on s.id = p.server_id "
				"and p.relative_path = ? "
			"left outer join page_content pc "
				"on pc.page_id = p.id "
				"and pc.language = 'en' "
			"join theme t on t.id = s.theme_id "
			"where (s.hostname = ? or s.is_default) "
			"order by s.is_default ", -1, &stmt, NULL));
	sqlite_check(db, sqlite3_bind_text(stmt, 1, path, -1, NULL));
	sqlite_check(db, sqlite3_bind_text(stmt, 2, host, -1, NULL));
	v  = sqlite3_step(stmt);
	if (v == SQLITE_DONE) {
		printf("Couldn't find default server OOPS!\n");
		raise(SIGTERM);
	} else if (v != SQLITE_ROW) {
		sqlite_check(db, v);
	}
	const char* template = (const char*)sqlite3_column_text(stmt, 0);
	int theme_id = sqlite3_column_int(stmt, 1);
	const char* title = (const char*)sqlite3_column_text(stmt, 2);
	const char* content = (const char*)sqlite3_column_text(stmt, 3);
	const char* language = (const char*)sqlite3_column_text(stmt, 4);

	// Render content with markdown
	// The returned string is our own, we will free it later
	char* rendered_content = NULL;
	if (content != NULL) {
		rendered_content = cmark_markdown_to_html(content, strlen(content), 0);
	} else {
		rendered_content = strdup("Not found!"); // TODO customizable error page
	}

	// Construct the PageData
	ThemeContentItems tci = {0};
	NavItems ni = {0};
	PageData pl = {
		.template = strdup(template),
		.content_items= tci,
		.title = strdup(title != NULL ? title : "Not Found!"),
		.content = rendered_content,
		.nav_items = ni,
		.navix = 0,
		.isnav = 0,
		.language = strdup(language != NULL ? language : "en"), // Possibly should be server default language
		.isnotfound = content == NULL,
	};
	sqlite_check(db, sqlite3_finalize(stmt));

	// Fetch the theme contents
	sqlite_check(db, sqlite3_prepare_v2(db, 
				"select key, value "
				"from theme_content "
				"where theme_id = ? "
				"and language = 'en'", -1, &stmt, NULL)); // TODO language selection
	sqlite_check(db, sqlite3_bind_int(stmt, 1, theme_id));
	for (v = sqlite3_step(stmt); v == SQLITE_ROW; v = sqlite3_step(stmt)) {
		ThemeContentItem tci = {
		  .name = strdup((const char*)sqlite3_column_text(stmt, 0)),
		  .value = strdup((const char*)sqlite3_column_text(stmt, 1)),
		};
		da_push(pl.content_items, tci);
	}
	if (v != SQLITE_DONE) {
		sqlite_check(db, v);
	}
	sqlite_check(db, sqlite3_finalize(stmt));

	// Fetch navigation data
	sqlite_check(db, sqlite3_prepare_v2(db, 
				"select p.relative_path, pc.title "
				"from server s "
				"join page p "
				  "on p.server_id = s.id "
				"join page_content pc "
				  "on pc.page_id = p.id "
				"where s.id = "
				  "(select max(id) "
				    "from server s2 "
				    "where (s2.hostname = ? or s2.is_default)) "
				"and pc.language = 'en'", -1, &stmt, NULL)); // TODO language selection
	sqlite_check(db, sqlite3_bind_text(stmt, 1, path, -1, NULL));
	for (v = sqlite3_step(stmt); v == SQLITE_ROW; v = sqlite3_step(stmt)) {
		NavItem nn = {
		  .url = strdup((const char*)sqlite3_column_text(stmt, 0)),
		  .title = strdup((const char*)sqlite3_column_text(stmt, 1)),
		};
		da_push(pl.nav_items, nn);
	}
	if (v != SQLITE_DONE) {
		sqlite_check(db, v);
	}
	sqlite3_finalize(stmt);
	return pl;
}

void free_page_data(PageData pld) {
	free(pld.content);
	free(pld.title);
	free(pld.template);
	for (int i=0; i<da_count(pld.nav_items); i++) {
		NavItem ni = da_get(pld.nav_items, i);
		free(ni.title);
		free(ni.url);
	}
	da_free(pld.nav_items);
	free(pld.language);
	for (int i=0; i<da_count(pld.content_items); i++) {
		ThemeContentItem tci = da_get(pld.content_items, i);
		free(tci.name);
		free(tci.value);
	}
	da_free(pld.content_items);
}

/* 
 * Imlementation for fetching mustach template values from the PageData.
 * tags 'title' and 'content' are supported, along with any user defined theme content items.
 * iterable '#nav' is supported, with 'title' and 'url' members to provide a site navigation.
 */
int mustache_get(void* cls, const char* name, struct mustach_sbuf* sbuf) {
	PageData* pld = (PageData*)cls;
	sbuf->value = "";
	sbuf->freecb = NULL;
	if (pld->isnav) {
		// If we're iterating inside a #nav block, 
		// then title and url are valid names
		NavItem ni = da_get(pld->nav_items, pld->navix);
		if (strcmp(name, "title") == 0) {
			sbuf->value = ni.title;
		} else if (strcmp(name, "url") == 0) {
			sbuf->value = ni.url;
		} 
		// TODO highlighting the currently selected nav item
	} else if (strcmp(name, "title") == 0) {
		sbuf->value = pld->title;
	} else if (strcmp(name, "content") == 0) {
		sbuf->value = pld->content;
	} else if (strcmp(name, "language") == 0) {
		sbuf->value = pld->language;
	} else {
		// Fall back to looking among the theme content items
		for (int i=0; i<da_count(pld->content_items); i++) {
			ThemeContentItem tci = da_get(pld->content_items, i);
			if (strcmp(name, tci.name) == 0) {
				sbuf->value = tci.value;
			}
		}
	}
	return MUSTACH_OK;
}
int mustache_enter(void *closure, const char *name) {
	PageData* pld = (PageData*)closure;
	if (strcmp(name, "nav") == 0) {
		pld->isnav = 1;
		pld->navix = 0;
		return 1;
	} 
	return 0;	
}
int mustache_next(void *closure) {
	PageData* pld = (PageData*)closure;
	if (pld->isnav) {
		if (pld->navix < (da_count(pld->nav_items)-1)) {
			pld->navix++;
			return 1;
		}
	} 
	return 0;
}
int mustache_leave(void *closure) {
	PageData* pld = (PageData*)closure;
	pld->isnav = 0;
	return MUSTACH_OK;	
}
static struct mustach_itf mustache_page_data_interface = {
	.enter = mustache_enter,
	.next = mustache_next,
	.leave = mustache_leave,
	.get = mustache_get,
};
char* mustache_render(PageData pld) {
	char* result;
	size_t discard;
	int v = mustach(pld.template, &mustache_page_data_interface, &pld, &result, &discard);
	if (v != MUSTACH_OK) {
		printf("Error with mustache %d\n", v);
		raise(SIGTERM);
	}
	return result;
}

/*
 * Find some static content from static_resource table.
 * Static resources are served from the /static path, 
 * and are attached to a particular virtual host.
 */
StaticResource find_static_resource(const char* host, const char* subpath) {
	printf("Looking for static resource host %s subpath %s\n", host, subpath);
	sqlite3_stmt* stmt;
	sqlite_check(db, sqlite3_prepare_v2(db, 
		"select sr.key, sr.value, sr.content_type "
		"from static_resources sr "
		"join server s on s.id = sr.server_id "
		"where sr.key = ? "
		"and s.hostname = ? ", -1, &stmt, NULL));
	sqlite_check(db, sqlite3_bind_text(stmt, 1, subpath, -1, NULL));
	sqlite_check(db, sqlite3_bind_text(stmt, 2, host, -1, NULL));
	StaticResource r = {
		.key = NULL,
		.value = NULL,
		.value_size = 0,
		.content_type = NULL,
		.isnotfound = true,
	};
	int v = sqlite3_step(stmt);
	if (v == SQLITE_DONE) {
		sqlite3_finalize(stmt);
		return r;
	}
	r.key = strdup((const char*)sqlite3_column_text(stmt, 0));
	const void* vz = sqlite3_column_blob(stmt, 1);
	int sz = sqlite3_column_bytes(stmt, 1);
	r.value = malloc(sz);
	memcpy(r.value, vz, sz);
	r.value_size = sz;
	r.content_type = strdup((const char*)sqlite3_column_text(stmt, 2));
	r.isnotfound = false;
	sqlite3_finalize(stmt);
	return r;
}

void free_static_resource(StaticResource r) {
	free(r.key);
	free(r.value);
	free(r.content_type);
}

//////////// API ///////////

Servers get_servers() {
	sqlite3_stmt* stmt;
	sqlite_check(db, sqlite3_prepare_v2(db, 
				"select id, hostname, theme_id, is_default "
				"from server", -1, &stmt, NULL));
	int v;
	Servers s = {0};
	for (v = sqlite3_step(stmt); v == SQLITE_ROW; v = sqlite3_step(stmt)) {
		Server sv = {
			.id = sqlite3_column_int(stmt, 0),
			.hostname = strdup((const char*)sqlite3_column_text(stmt, 1)),
			.theme_id = sqlite3_column_int(stmt, 2),
			.is_default = sqlite3_column_int(stmt, 3),
		};
		da_push(s, sv);
	}
	if (v != SQLITE_DONE) {
		sqlite_check(db, v);
	}
	sqlite3_finalize(stmt);
	return s;
}

void free_server(Server s) {
	free(s.hostname);
}

void free_servers(Servers s) {
	for (int i=0;i<da_count(s); i++) {
		free_server(da_get(s, i));
	}
	da_free(s);
}

NewServer parse_new_server(struct json_object* o) {
	NewServer s = {
		.valid = 0,
		.hostname = NULL,
		.theme_id = -1,
	};
	if (o == NULL || !json_object_is_type(o, json_type_object)) {
		return s;
	}
	struct json_object* hno = json_object_object_get(o, "hostname");
	if (hno == NULL || !json_object_is_type(hno, json_type_string)) {
		return s;
	}
	struct json_object* to = json_object_object_get(o, "theme_id");
	if (to == NULL || !json_object_is_type(to, json_type_int)) {
		return s;
	}
	s.valid = true;
	s.hostname = strdup(json_object_get_string(hno));
	s.theme_id = json_object_get_int(to);
	return s;
}

void free_new_server(NewServer s) {
	if (s.hostname != NULL)
		free(s.hostname);
}

NewServerResponse create_server(NewServer ns) {
	NewServerResponse r = {
		.success = false,
		.error_message = NULL
	};
	sqlite3_stmt* stmt;
	sqlite_check(db, sqlite3_prepare_v2(db, 
			"insert into server (hostname, theme_id, default_language) "
			"values (?, ?, ?)", -1, &stmt, NULL));
	sqlite_check(db, sqlite3_bind_text(stmt, 1, ns.hostname, -1, NULL));
	sqlite_check(db, sqlite3_bind_int(stmt, 2, ns.theme_id));
	sqlite_check(db, sqlite3_bind_text(stmt, 3, "en", -1, NULL));
	int v = sqlite3_step(stmt);
	if (v == SQLITE_DONE) {
		Server s = {
		  .id = sqlite3_last_insert_rowid(db),
		  .hostname = strdup(ns.hostname),
		};
		r.success = true;
		r.server = s;
	} else {
		r.error_message = strdup(sqlite3_errmsg(db));
	}
	sqlite3_finalize(stmt);
	return r;
}

void free_new_server_response(NewServerResponse n) {
	if (n.error_message != NULL) 
		free(n.error_message);
	if (n.success)
		free_server(n.server);
}

struct json_object* server_to_json(Server s) {
	struct json_object* o = json_object_new_object();
	json_object_object_add(o, "id", json_object_new_int(s.id));
	json_object_object_add(o, "hostname", json_object_new_string(s.hostname));
	json_object_object_add(o, "theme_id", json_object_new_int(s.theme_id));
	json_object_object_add(o, "is_default", json_object_new_boolean(s.is_default));
	return o;
}

struct json_object* servers_to_json(Servers s) {
	struct json_object* v = json_object_new_array();
	for (int i=0; i<da_count(s); i++) {
		Server sv = da_get(s, i);
		json_object_array_add(v, server_to_json(sv));
	}
	return v;
}

HttpResponse handle_api_server_path( 
		const char* method,
		const char* body) {
	HttpResponse r = {
		.status_code = 400,
		.content = NULL,
		.content_length = -1,
		.content_type = NULL,
	};
	if (strcmp("GET", method) == 0) {
		Servers servers = get_servers();
		struct json_object* v = servers_to_json(servers);
		r = http_json_response(v, 200);
		json_object_put(v);
		free_servers(servers);
	} else if (strcmp("POST", method) == 0) {
		struct json_object* json_body = json_tokener_parse(body);
		NewServer ns = parse_new_server(json_body);
		json_object_put(json_body);
		if (ns.valid) {
			NewServerResponse nsr = create_server(ns);
			if (nsr.success) {
				struct json_object* vv = server_to_json(nsr.server);
				r = http_json_response(vv, 200);
				json_object_put(vv);
			} else {
				r = http_error_response(nsr.error_message, 400);
			}
			free_new_server_response(nsr);
		} else {
			r = http_error_response("Invalid data supplied!", 400);
		}
		free_new_server(ns);
	}
	return r;
}

Pages get_pages() {
	Pages p = {0};
	sqlite3_stmt* stmt;
	sqlite_check(db, sqlite3_prepare_v2(db, "select id, server_id, parent_page_id, relative_path from page", -1, &stmt, NULL));
	int v;
	for (v = sqlite3_step(stmt); v != SQLITE_DONE; v = sqlite3_step(stmt)) {
		Page pp = {
			.id = sqlite3_column_int(stmt, 0),
			.server_id = sqlite3_column_int(stmt, 1),
			.parent_page_id = sqlite3_column_int(stmt, 2),
			.relative_path = strdup((const char*)sqlite3_column_text(stmt, 3)),
		};
		da_push(p, pp);
	}
	if (v != SQLITE_DONE) {
		sqlite_check(db, v);
	}
	sqlite3_finalize(stmt);
	return p;
}

void free_page(Page page) {
	free(page.relative_path);
}

void free_pages(Pages pages) {
	for (int i=0; i<da_count(pages); i++) 
		free_page(da_get(pages, i));
	da_free(pages);
}

struct json_object* page_to_json(Page p) {
	struct json_object* v = json_object_new_object();
	json_object_object_add(v, "id", json_object_new_int(p.id));
	json_object_object_add(v, "server_id", json_object_new_int(p.server_id));
	if (p.parent_page_id > -1)
		json_object_object_add(v, "parent_page_id", json_object_new_int(p.parent_page_id));
	json_object_object_add(v, "relative_path", json_object_new_string(p.relative_path));
	return v;
}

struct json_object* pages_to_json(Pages p) {
	struct json_object* v = json_object_new_array();
	for (int i=0;i<da_count(p); i++) {
		Page pp = da_get(p, i);
		struct json_object* pj = page_to_json(pp);
		json_object_array_add(v, pj);
	}
	return v;
}

NewPage parse_new_page(struct json_object* v) {
	NewPage p = {
		.valid = false,
		.server_id = -1,
		.parent_page_id = -1,
		.relative_path = NULL,
	};
	struct json_object* sio = NULL;
	struct json_object* rio = NULL;
	if (!json_object_object_get_ex(v, "server_id", &sio)) {
		return p;
	}
	if (!json_object_is_type(sio, json_type_int)) {
		return p;
	}
	if (!json_object_object_get_ex(v, "relative_path", &rio)) {
		return p;
	}
	if (!json_object_is_type(rio, json_type_string)) {
		return p;
	}
	p.valid = true;
	p.server_id = json_object_get_int(sio);
	p.parent_page_id = -1; // not supported yet
	p.relative_path = strdup(json_object_get_string(rio));
	return p;
}

NewPageResponse create_page(NewPage np) {
	NewPageResponse r = {
		.success = false,
		.error_message = NULL
	};
	sqlite3_stmt* stmt;
	sqlite_check(db, sqlite3_prepare_v2(db, "insert into page (server_id, parent_page_id, relative_path) "
				"values (?,?,?)", -1, &stmt, NULL));
	sqlite_check(db, sqlite3_bind_int(stmt, 1, np.server_id));
	if (np.parent_page_id > -1) {
		sqlite_check(db, sqlite3_bind_int(stmt, 2, np.parent_page_id));
	} else {
		sqlite_check(db, sqlite3_bind_null(stmt, 2));
	}
	sqlite_check(db, sqlite3_bind_text(stmt, 3, np.relative_path, -1, NULL));
	int v = sqlite3_step(stmt);
	if (v == SQLITE_DONE) {
		Page p = {
			.id = sqlite3_last_insert_rowid(db),
			.server_id = np.server_id,
			.parent_page_id = np.parent_page_id,
			.relative_path = strdup(np.relative_path),
		};
		r.success = true;
		r.page = p;
	} else {
		r.error_message = strdup(sqlite3_errmsg(db));
	}
	sqlite3_finalize(stmt);
	return  r;
}

void free_new_page(NewPage p) {
	free(p.relative_path);
}

void free_new_page_response(NewPageResponse r) {
	if (r.error_message != NULL) 
		free(r.error_message);
	if (r.success) 
		free_page(r.page);
}

HttpResponse handle_api_page_path(
		const char* method,
		const char* body) {
	HttpResponse r = {
		.status_code = 400,
		.content = NULL,
		.content_length = 0,
		.content_type = NULL,
	};
	if (strcmp("GET", method) == 0) {
		Pages pages = get_pages();
		struct json_object* json = pages_to_json(pages);
		r = http_json_response(json, 200);
		json_object_put(json);
		free_pages(pages);
	} else if (strcmp("POST", method) == 0) {
		struct json_object* v = json_tokener_parse(body);
		if (v != NULL) {
			NewPage np = parse_new_page(v);
			if (np.valid) {
				NewPageResponse npr = create_page(np);
				if (npr.success) {
					struct json_object* page = page_to_json(npr.page);
					r = http_json_response(page, 200);
					json_object_put(page);
				} else {
					r = http_error_response(npr.error_message, 400);
				}
				free_new_page_response(npr);
			} else {
				r = http_error_response("supplied new_server is not valid", 400);
			}
			free_new_page(np);
		} else {
			r = http_error_response("Invalid JSON supplied", 400);
		}
	}
	return r;
}

PageContents get_page_contents() {
	PageContents r = {0};
	sqlite3_stmt* stmt;
	sqlite_check(db, sqlite3_prepare_v2(db, "select id, page_id, language, title, content "
				"from page_content", -1, &stmt, NULL));
	int v;
	for (v = sqlite3_step(stmt); v != SQLITE_DONE; v = sqlite3_step(stmt)) {
		PageContent pc = {
			.id = sqlite3_column_int(stmt, 0),
			.page_id = sqlite3_column_int(stmt, 1),
			.language = strdup((const char*)sqlite3_column_text(stmt, 2)),
			.title = strdup((const char*)sqlite3_column_text(stmt, 3)),
			.content = strdup((const char*)sqlite3_column_text(stmt, 4)),
		};
		da_push(r, pc);
	}
	sqlite3_finalize(stmt);
	return r;
}

void free_page_content(PageContent c) {
	free(c.content);
	free(c.language);
	free(c.title);
}

void free_page_contents(PageContents c) {
	for (int i=0; i<da_count(c); i++) {
		free_page_content(da_get(c, i));
	}
	da_free(c);
}

struct json_object* page_content_to_json(PageContent c) {
	struct json_object* o = json_object_new_object();
	json_object_object_add(o, "id", json_object_new_int(c.id));
	json_object_object_add(o, "page_id", json_object_new_int(c.page_id));
	json_object_object_add(o, "language", json_object_new_string(c.language));
	json_object_object_add(o, "title", json_object_new_string(c.title));
	json_object_object_add(o, "content", json_object_new_string(c.content));
	return o;
}

struct json_object* page_contents_to_json(PageContents c) {
	struct json_object* v = json_object_new_array();
	for (int i=0; i<da_count(c); i++) {
		struct json_object* cc = page_content_to_json(da_get(c,i));
		json_object_array_add(v, cc);
	}
	return v;
}

NewPageContent parse_new_page_content(struct json_object* v) {
	NewPageContent r = {
		.valid = false,
		.page_id = -1,
		.language = NULL,
		.title = NULL,
		.content = NULL,
	};
	if (v == NULL || !json_object_is_type(v, json_type_object)) {
		return r;
	}
	struct json_object* pio = json_object_object_get(v, "page_id");
	if (pio == NULL || !json_object_is_type(pio, json_type_int)) {
		return r;
	}
	struct json_object* lo = json_object_object_get(v, "language");
	if (lo == NULL || !json_object_is_type(lo, json_type_string)) {
		return r;
	}
	struct json_object* to = json_object_object_get(v, "title");
	if (to == NULL || !json_object_is_type(to, json_type_string)) {
		return r;
	}
	struct json_object* co = json_object_object_get(v, "content");
	if (co == NULL || !json_object_is_type(co, json_type_string)) {
		return r;
	}
	r.valid = true;
	r.page_id = json_object_get_int(pio);
	r.language = strdup(json_object_get_string(lo));
	r.title = strdup(json_object_get_string(to));
	r.content = strdup(json_object_get_string(co));
	return r;
}

void free_new_page_content(NewPageContent p) {
	if (p.language != NULL)
		free(p.language);
	if (p.title != NULL)
		free(p.title);
	if (p.content != NULL)
		free(p.content);
}

NewPageContentResponse create_page_content(NewPageContent npc) {
	NewPageContentResponse r = {
		.success = false,
		.error_message = NULL
	};
	sqlite3_stmt* stmt;
	sqlite_check(db, sqlite3_prepare_v2(db, 
				"insert into page_content (page_id, language, title, content) "
				"values (?,?,?,?)", -1, &stmt, NULL));
	sqlite_check(db, sqlite3_bind_int(stmt, 1, npc.page_id));
	sqlite_check(db, sqlite3_bind_text(stmt, 2, npc.language, -1, NULL));
	sqlite_check(db, sqlite3_bind_text(stmt, 3, npc.title, -1, NULL));
	sqlite_check(db, sqlite3_bind_text(stmt, 4, npc.content, -1, NULL));
	int v = sqlite3_step(stmt);
	if (v == SQLITE_DONE) {
		r.success = true;
		PageContent pc = {
			.id = sqlite3_last_insert_rowid(db),
			.page_id = npc.page_id,
			.language = strdup(npc.language),
			.title = strdup(npc.title),
			.content = strdup(npc.content),
		};
		r.content = pc;
	} else {
		r.error_message = strdup(sqlite3_errmsg(db));
	}
	sqlite3_finalize(stmt);
	return r;
}

void free_new_page_content_response(NewPageContentResponse r) {
	if (r.success)
		free_page_content(r.content);
}

PatchPageContent parse_patch_page_content(struct json_object* v, int id) {
	PatchPageContent r = {
		.valid = false,
		.id = id,
		.title = NULL,
		.content = NULL,
	};
	if (v != NULL && json_object_is_type(v, json_type_object)) {
		r.valid = true;
		struct json_object* to = json_object_object_get(v, "title");
		if (to != NULL && json_object_is_type(to, json_type_string)) {
			r.title = strdup(json_object_get_string(to));
		}
		struct json_object* co = json_object_object_get(v, "title");
		if (co != NULL && json_object_is_type(co, json_type_string)) {
			r.content = strdup(json_object_get_string(co));
		}
	}
	return r;
}

void free_patch_page_content(PatchPageContent ppc) {
	if (ppc.content != NULL)
		free(ppc.content);
	if (ppc.title != NULL)
		free(ppc.title);
}

PatchPageContentResponse update_page_content(PatchPageContent ppc) {
	PatchPageContentResponse r = {
		.success = false,
		.error_message= NULL
	};
	const char* sql;
	if (ppc.content != NULL && ppc.title != NULL) {
		sql = "update page_content set title = ?, content = ? where id = ?";
	} else if (ppc.content != NULL) {
		sql = "update page_content set content = ? where id = ?";
	} else if (ppc.title != NULL) {
		sql = "update page_content set title = ? where id = ?";
	} else {
		r.success = true;
		return r;
	}
	 
	sqlite3_stmt* stmt;
	sqlite_check(db, sqlite3_prepare_v2(db, sql, -1, &stmt, NULL));
	if (ppc.content != NULL && ppc.title != NULL) {
		sqlite_check(db, sqlite3_bind_text(stmt, 1, ppc.title, -1, NULL));
		sqlite_check(db, sqlite3_bind_text(stmt, 2, ppc.content, -1, NULL));
		sqlite_check(db, sqlite3_bind_int(stmt, 3, ppc.id));
	} else if (ppc.content != NULL) {
		sqlite_check(db, sqlite3_bind_text(stmt, 1, ppc.content, -1, NULL));
		sqlite_check(db, sqlite3_bind_int(stmt, 2, ppc.id));
	} else if (ppc.title != NULL) {
		sqlite_check(db, sqlite3_bind_text(stmt, 1, ppc.title, -1, NULL));
		sqlite_check(db, sqlite3_bind_int(stmt, 2, ppc.id));
	} else {
		fprintf(stderr, "Fatal error, should have done early return way before now\n");
		raise(SIGTERM);
	}
	int v = sqlite3_step(stmt);
	if (v == SQLITE_DONE) {
		r.success= true;
	} else {
		r.error_message = strdup(sqlite3_errmsg(db));
	}
	sqlite3_finalize(stmt);
	return r;
}

void free_patch_page_content_response(PatchPageContentResponse ppcr) {
	if (ppcr.error_message != NULL)
		free(ppcr.error_message);
}

HttpResponse handle_editor() {
	char* editor_html = null_terminated_resource(src_editor_html);
	HttpResponse r = {
		.content = editor_html,
		.content_length = strlen(editor_html),
		.content_type = strdup("text/html"),
		.status_code = 200,
	};
	return r;
}

HttpResponse handle_api_page_content_path(const char* method,
		const char* body,
		Strings path_elements) {
	HttpResponse r = {
		.status_code = 400,
		.content = NULL,
		.content_length = 0,
		.content_type = NULL,
	};
	if (strcmp("GET", method) == 0) {
		PageContents page_contents = get_page_contents();
		struct json_object* json = page_contents_to_json(page_contents);
		r = http_json_response(json, 200);
		json_object_put(json);
		free_page_contents(page_contents);
	} else if (strcmp("POST", method) == 0) {
		struct json_object* v = json_tokener_parse(body);
		if (v != NULL) {
			NewPageContent np = parse_new_page_content(v);
			if (np.valid) {
				NewPageContentResponse npr = create_page_content(np);
				if (npr.success) {
					struct json_object* page_content = page_content_to_json(npr.content);
					r = http_json_response(page_content, 200);
					json_object_put(page_content);
				} else {
					r = http_error_response(npr.error_message, 400);
				}
				free_new_page_content_response(npr);
			} else {
				r = http_error_response("supplied new_page_content is not valid", 400);
			}
			free_new_page_content(np);
		} else {
			r = http_error_response("Invalid JSON supplied", 400);
		}
		json_object_put(v);
	} else if (strcmp("PATCH", method) == 0) {
		if (!da_empty(path_elements)) {
			char* first = da_get(path_elements, 0);
			atoi(first);
			struct json_object* v = json_tokener_parse(body);
			if (v != NULL) {

				PatchPageContent ppc = parse_patch_page_content(v, 

			}
			json_object_put(v);
		} else {
		}
	}
	return r;
}

HttpResponse handle_api(Strings path_elements, 
		const char* method,
		const char* body) {
	char* resource = da_get(path_elements, 0);
	if (strcmp("page", resource) == 0) {
		return handle_api_page_path(method, body);
	} else if (strcmp("page_content", resource) == 0) {
		return handle_api_page_content_path(method, body);
	} else if (strcmp("server", resource) == 0) {
		return handle_api_server_path(method, body);
	} else {
		return http_error_response("Not found", 404);
	}
}

HttpResponse handle_static_resources(const char* host, 
		const char* subpath) {
	// Ignore leading forward slashes, just one is permissable
	if (subpath[0] == '/') {
		subpath = subpath + 1;
	}
	printf("looking for static resource host %s subpath %s\n", host, subpath);
	HttpResponse r = {
		.content = NULL,
		.content_length = 0,
		.content_type = NULL,
		.status_code = 404,
	};
	
	StaticResource sr = find_static_resource(host, subpath);
	if (sr.isnotfound) {
		free_static_resource(sr);
		return r;
	}
	void* cc = (char*)malloc(sr.value_size);
	memcpy(cc, sr.value, sr.value_size);
	r.content = (char*)cc;
	r.content_length = sr.value_size;
	r.content_type = strdup(sr.content_type);
	r.status_code = 200;
	return r;
}

/*
 * Handle a request for content page
 */
HttpResponse handle_content(const char* host, 
		const char* path) {
	PageData pd = find_page_data(host, path, "en");
	char* content = mustache_render(pd);
	HttpResponse r = {
		.content = strdup(content),
		.content_length = strlen(content),
		.content_type = strdup("text/html"),
		.status_code = pd.isnotfound ? 404: 200,
	};
	free_page_data(pd);
	return r;
}

/*
 * Split path by URL path element delimeter (/)
 * and return the path elements in a list. 
 * Caller is responsible for freeing the result.
 */
Strings get_path_elements(const char* path) {
	// strtok is destructive, take a copy before operating
	char* path_dup = strdup(path);
	// Tokenize and push into a list
	Strings path_elements = {0};
	char* sv;
	char* tok = strtok_r(path_dup, "/", &sv);
	while (tok != NULL) {
		// Copy again to push it into the array
		da_push(path_elements, strdup(tok));
		tok = strtok_r(NULL, "/", &sv);
	}
	// Get rid of the shredded string
	free(path_dup);
	// Empty path / is allowed, just treat is as an empty string 
	// not a NULL
	if (da_count(path_elements) == 0) {
		da_push(path_elements, strdup(""));
	}
	return path_elements;
}

/*
 * Join path elements with URL path separator (/)
 * Caller is responsible for freeing the result.
 */
char* join_path_elements(Strings path_elements) {
	size_t req = 0;
	for (int i=0; i<da_count(path_elements); i++) {
		char* pe = da_get(path_elements, i);
		req += strlen(pe);
		req += 1;
	}
	char* ret = malloc(req);
	memset(ret, '\0', req);
	size_t ix = 0;
	for (int i=0; i<da_count(path_elements); i++) {
		char* pe = da_get(path_elements, i);
		unsigned long element_len = strlen(pe);
		memcpy(ret+ix, pe, element_len);
		ix += element_len;
		if (i == (da_count(path_elements)-1)) {
			ret[ix] = '\0';
		} else {
			ret[ix] = '/';
		}
		ix += 1;
	}
	printf("joined %s\n", ret);
	return ret;
}

void free_strings(Strings s) {
	for (int i=0; i<da_count(s); i++) {
		free(da_get(s, i));
	}
	da_free(s);
}

// HTTP handler function
// Responsible for routing
enum MHD_Result handle_http(void* cls, 
		struct MHD_Connection* connection,
                const char* path,
                const char* method, 
		const char* version,
                const char* upload_data,
                long unsigned int* upload_data_size, 
		void** con_cls) {
	Strings path_elements = get_path_elements(path);
	char* first_path_element = da_get(path_elements, 0);

	printf("Handling connection path %s method %s version %s\n", path, method, version);
	const char* host = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_HOST);
	HttpResponse r = {0};
	if (strcmp("editor.html", first_path_element) == 0) {
		r = handle_editor();
	} else if (strcmp("api", first_path_element) == 0) {
		da_delete(path_elements, 0);
		r = handle_api(path_elements, method, upload_data);
	} else if (strcmp("static", first_path_element) == 0) {
		da_delete(path_elements, 0);
		char* subpath = join_path_elements(path_elements);
		r = handle_static_resources(host, subpath);
		free(subpath);
	} else {
		r = handle_content(host, path);
	}
	free_strings(path_elements);
	
	struct MHD_Response* response = 
		MHD_create_response_from_buffer(r.content_length, 
			r.content, 
			MHD_RESPMEM_MUST_FREE);
	MHD_add_response_header(response, "Content-Type", r.content_type);
	int ret = MHD_queue_response(connection, r.status_code, response);
	MHD_destroy_response(response);
	// Free the content type, don't free the content as MHD will do that 
	// for us once it's actually sent.
	free(r.content_type);
	return ret;
}

/*
 * Signal handling, clean up resources we're using 
 * before allowing the program to terminate
 */
static volatile sig_atomic_t sigint_in_progress = 0;
void handle_term(int sig) {
	// Handle recursive signals
	if (sigint_in_progress) {
		raise(sig);
	}
	sigint_in_progress = 1;
	// Clean up resources we're holding
	if (http_server_daemon != NULL) {
		MHD_stop_daemon(http_server_daemon);
	}
	if (db != NULL) {
		sqlite3_close(db);
	}
	// Remove the signal handler and re-raise
	signal(sig, SIG_DFL);
	raise(sig);
}

int main(int argc, char** argv) {
	// Set globals before we setup any signal handling
	http_server_daemon = NULL;
	db = NULL;
	// Setup termination signal handling
	signal(SIGINT, handle_term);
	signal(SIGTERM, handle_term);
	// Open the database
	initialize_database("ccms.db");
	// Start the http server
	http_server_daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, 
		  8000, 
		  NULL, 
		  NULL, 
		  handle_http, 
		  NULL, 
		  MHD_OPTION_END);
	// Pause until exit
	pause();
	return 0;
}

