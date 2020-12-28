#include <mongoose.h>
#include <sqlite3.h>
#include <string.h>
#include <alloca.h>
#include <mustach.h>

// TODO Should be configuration options
static const char* http_port = "8000";
static const char* database_path = "ccms.db";

// Globals I can't avoid
static sqlite3* db;

// Exits the program with an error message if the error code isn't OK
void cc_sqlite3_check(int error_code, char* error_msg) {
  if (error_code != SQLITE_OK) {
    printf("sqlite3 error: %s", sqlite3_errstr(error_code));
    if (error_msg != NULL) {
      printf("error_message: %s", error_msg);
    }
    exit(1);
  }
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
  cc_sqlite3_check(sqlite3_open(database_path, &db), NULL);

  // Run the initialization script
  char* errmsg;
  cc_sqlite3_check(sqlite3_exec(db, cc_nt_resource(src_initial_sql), NULL, NULL, &errmsg), errmsg);
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

// Structure and function for pulling data required for content page
// rendering.
typedef struct _PageData {
	char* template_html;
	char* page_title;
	char* page_content;
	int isnotfound;
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
	cc_sqlite3_check(sqlite3_prepare_v2(db, "select t.html, pc.title, pc.content "
			"from server s "
			"left outer join page p "
				"on s.id = p.server_id "
				"and p.relative_path = ? "
			"left outer join page_content pc "
				"on pc.page_id = p.id "
				"and pc.language = 'en' "// TODO language
			"join theme t on t.id = s.theme_id "
			"where (s.hostname = ? or s.is_default) "
			"order by s.is_default ", -1, &stmt, NULL), NULL);
	cc_sqlite3_check(sqlite3_bind_text(stmt, 1, path.p, path.len, NULL), NULL);
	cc_sqlite3_check(sqlite3_bind_text(stmt, 2, host.p, host.len, NULL), NULL);
	
	int v  = sqlite3_step(stmt);
	PageData pl = {};
	if (v == SQLITE_ROW) {
		pl.template_html = strdup((const char*)sqlite3_column_text(stmt, 0));
		const char* pt = (const char*)sqlite3_column_text(stmt, 1);
		pl.page_title = strdup(pt != NULL ? pt : "Not Found!");
		const char* pc = (const char*)sqlite3_column_text(stmt, 2);
		pl.page_content = strdup(pc != NULL ? pc : "Not Found!");
		pl.isnotfound = pc == NULL;
	} else if (v == SQLITE_DONE) {
		printf("Couldn't find PageData!");
		exit(1);
	} else {
		cc_sqlite3_check(v, NULL);
	}
	cc_sqlite3_check(sqlite3_finalize(stmt), NULL);
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
	if (strcmp(name, "title") == 0) {
		sbuf->value = pld->page_title;
		sbuf->freecb = NULL;
	} else if (strcmp(name, "content") == 0) {
		sbuf->value = pld->page_content;
		sbuf->freecb = NULL;
	} 
	return MUSTACH_OK;
}
int cc_mst_enter(void *closure, const char *name) {
	return MUSTACH_OK;	
}
int cc_mst_next(void *closure) {
	return MUSTACH_OK;	
}
int cc_mst_leave(void *closure) {
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
    struct mg_str host = cc_header_or_default(hm, "Host", "localhost:8000");
    struct mg_str path = hm->uri;
    // TODO Server default language?
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
  struct mg_connection* c = mg_bind(&mgr, http_port, ev_handler);
  mg_set_protocol_http_websocket(c);

  // Main loop
  for (;;) {
    mg_mgr_poll(&mgr, 1000);
  }
  mg_mgr_free(&mgr);
  cc_sqlite3_check(sqlite3_close(db), NULL);
  return 0;
}

