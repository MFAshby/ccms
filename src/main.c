#include <mongoose.h>
#include <sqlite3.h>
#include <string.h>
#include <alloca.h>
#include <mustach.h>

static const char* http_port = "8000";
static const char* database_path = "ccms.db";

// Exits the program with an error message if the error code isn't OK
#define cc_sqlite3_check(db, function_call) {\
  int error_code = function_call; \
  if (error_code != SQLITE_OK) { \
    printf("sqlite3 error: %s:%d\n" \
	   "%s", __FILE__, __LINE__, sqlite3_errmsg(db)); \
    exit(1); \
  } \
}


// Copies a resource embedded into the binary with ld into 
// the stack, and adds a null terminator. 
// The value is allocated with alloca, so is automatically
// freed when the current function returns. Don't pass
// this pointer around!
// Implemented as a macro, so that just the sanitized 
// resource path is required.
#define cc_nt_resource(resource) ({\
  char* res; \
  do { \
    extern char _binary_##resource ##_start[]; \
    extern char _binary_##resource##_end[]; \
    int len = _binary_##resource##_end - _binary_##resource##_start; \
    res = alloca(len + 1); \
    memcpy(res,  _binary_##resource##_start, len); \
    res[len] = '\0'; \
  } while(0); \
  res; \
})

// Open the database and run any migration scripts that are required
sqlite3* cc_initialize_database() {
  // Open the database
  sqlite3* db;
  int error_code = sqlite3_open(database_path, &db);
  if (error_code != SQLITE_OK) {
    printf("sqlite3 error: %s", sqlite3_errmsg(db));
    exit(1);
  }

  // Run the initialization script
  cc_sqlite3_check(db, sqlite3_exec(db, cc_nt_resource(src_initial_sql), NULL, NULL, NULL));
  // All done
  return db;
}

// Pulls a header or returns the default value.
struct mg_str cc_header_or_default(struct http_message* hm, 
		const char* header, 
		const char* dfvalue) {
	struct mg_str* value = mg_get_http_header(hm, header);
	if (value) {
		return *value;
	}
	return mg_mk_str(dfvalue);
}

typedef struct _NavItem {
	char* title;
	char* url;
} NavItem;

// Structure and function for pulling data required for content page
// rendering.
typedef struct _PageData {
	char* template_html;
	char* page_title;
	char* page_content;
	int isnotfound;
	NavItem* nav;
	int nav_count;

	// Iterator for mustach
	int isnav;
	int navix;
} PageData;

PageData cc_find_pagedata(sqlite3* db, 
		struct mg_str host,
		struct mg_str lang,
		struct mg_str path) {
	printf("Looking for page with host %.*s lang %.*s path %.*s\n",
			(int)host.len, host.p, 
			(int)lang.len, lang.p,
			(int)path.len, path.p);
	sqlite3_stmt* stmt;
	cc_sqlite3_check(db, sqlite3_prepare_v2(db, 
			"select t.html, pc.title, pc.content "
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
	cc_sqlite3_check(db, sqlite3_bind_text(stmt, 1, path.p, path.len, NULL));
	cc_sqlite3_check(db, sqlite3_bind_text(stmt, 2, host.p, host.len, NULL));
	int v  = sqlite3_step(stmt);
	if (v == SQLITE_DONE) {
		printf("Couldn't find PageData!\n");
		exit(1);
	} else if (v != SQLITE_ROW) {
		cc_sqlite3_check(db, v);
	}
	const char* pt = (const char*)sqlite3_column_text(stmt, 1);
	const char* pc = (const char*)sqlite3_column_text(stmt, 2);
	PageData pl = {
		.template_html = strdup((const char*)sqlite3_column_text(stmt, 0)),
		.page_title = strdup(pt != NULL ? pt : "Not Found!"),
		.page_content = strdup(pc != NULL ? pc : "Not Found!"),
		.isnotfound = pc == NULL,
		.nav = NULL,
		.nav_count = 0,

		.navix = 0,
		.isnav = 0,
	};
	cc_sqlite3_check(db, sqlite3_finalize(stmt));

	// Populate navigation data
	cc_sqlite3_check(db, sqlite3_prepare_v2(db, 
				"select p.relative_path, pc.title "
				"from server s "
				"join page p "
				  "on p.server_id = s.id "
				"join page_content pc "
				  "on pc.page_id = p.id "
				"where s.id = "
				  "(select max(id) "
				  "from server s2 "
				  "where (s2.hostname = ? or s2.is_default))", -1, &stmt, NULL));
	cc_sqlite3_check(db, sqlite3_bind_text(stmt, 1, path.p, path.len, NULL));
	pl.nav = NULL;
	pl.nav_count = 0;
	for (v = sqlite3_step(stmt); v == SQLITE_ROW; v = sqlite3_step(stmt)) {
		pl.nav = realloc(pl.nav, sizeof(NavItem) * (pl.nav_count+1));
		NavItem nn = {
		  .url = strdup((const char*)sqlite3_column_text(stmt, 0)),
		  .title = strdup((const char*)sqlite3_column_text(stmt, 1)),
		};
		pl.nav[pl.nav_count] = nn;
		pl.nav_count ++;
	}
	if (v != SQLITE_DONE) {
		cc_sqlite3_check(db, v);
	}
	
	return pl;
}

void cc_free_pageloaddata(PageData pld) {
	free(pld.page_content);
	free(pld.page_title);
	free(pld.template_html);
}

// Imlementation for fetching template values from the PageData and
// rendering them.
int cc_mst_get(void* cls, const char* name, struct mustach_sbuf* sbuf) {
	PageData* pld = (PageData*)cls;
	sbuf->value = "";
	sbuf->freecb = NULL;
	if (pld->isnav) {
		if (strcmp(name, "title") == 0) {
			sbuf->value = pld->nav[pld->navix].title;
		} else if (strcmp(name, "url") == 0) {
			sbuf->value = pld->nav[pld->navix].url;
		} 
	}
	else if (strcmp(name, "title") == 0) {
		sbuf->value = pld->page_title;
	} else if (strcmp(name, "content") == 0) {
		sbuf->value = pld->page_content;
	} 
	return MUSTACH_OK;
}
int cc_mst_enter(void *closure, const char *name) {
	PageData* pld = (PageData*)closure;
	if (strcmp(name, "nav") == 0) {
		pld->isnav = 1;
		pld->navix = 0;
		return 1;
	} 
	return 0;	
}
int cc_mst_next(void *closure) {
	PageData* pld = (PageData*)closure;
	if (!pld->isnav) {
		return 0;
	} else if (pld->navix == (pld->nav_count-1)) {
		return 0;
	}
	pld->navix++;
	return 1;
}
int cc_mst_leave(void *closure) {
	PageData* pld = (PageData*)closure;
	pld->isnav = 0;
	return MUSTACH_OK;	
}
static struct mustach_itf pageloaddata_itf = {
	.enter = cc_mst_enter,
	.next = cc_mst_next,
	.leave = cc_mst_leave,
	.get = cc_mst_get,
};
struct mg_str cc_render(PageData pld) {
	char* result;
	size_t res_size;
	int v = mustach(pld.template_html, &pageloaddata_itf, &pld, &result, &res_size);
	if (v != MUSTACH_OK) {
		printf("Error with mustache %d\n", v);
		exit(1);
	}
	return mg_mk_str_n(result, res_size);
}

// The main event loop handler
// Copes with routing, rendering, and returning 
// to the client
static void ev_handler(struct mg_connection* c, int ev, void* p) {
  switch (ev) {
  case MG_EV_HTTP_REQUEST: {
    struct http_message* hm = (struct http_message*)p;
    sqlite3* db = (sqlite3*)c->user_data;

    struct mg_str host = cc_header_or_default(hm, "Host", "localhost:8000");
    struct mg_str path = hm->uri;
    struct mg_str language = cc_header_or_default(hm, "Accept-Language", "en");

    PageData pld = cc_find_pagedata(db, host, language, path);
    struct mg_str payload = cc_render(pld);
    
    mg_send_head(c, pld.isnotfound ? 404 : 200, payload.len, "Content-Type: text/html");
    mg_send(c, payload.p, payload.len);
    mg_strfree(&payload);
    cc_free_pageloaddata(pld);
  }
  }
}

int main(void) {
  // Initialize the database
  sqlite3* db = cc_initialize_database();

  // Set up http server
  struct mg_mgr mgr;
  mg_mgr_init(&mgr, NULL);
  struct mg_bind_opts opts;
  memset(&opts, 0, sizeof(struct mg_bind_opts));
  opts.user_data = db;
  struct mg_connection* c = mg_bind_opt(&mgr, http_port, ev_handler, opts);

  mg_set_protocol_http_websocket(c);

  // Main loop
  for (;;) {
    mg_mgr_poll(&mgr, 1000);
  }
  mg_mgr_free(&mgr);
  cc_sqlite3_check(db, sqlite3_close(db));
  return 0;
}

