#include <mongoose.h>
#include <sqlite3.h>
#include <string.h>

static const char* http_port = "8000";
static const char* database_path = "ccms.db";

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
// the heap, and adds a null terminator. The caller must 
// free the memory when done. 
// Implemented as a macro, so that just the sanitized 
// resource path is required.
#define cc_nt_resource(resource) ({\
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

// Open the database and run any migration scripts that are required
sqlite3* cc_initialize_database() {
  sqlite3* db;

  // Open the database
  cc_sqlite3_check(sqlite3_open(database_path, &db), NULL);

  // Run the initialization script
  char* script = cc_nt_resource(src_initial_sql);
  char* errmsg;
  cc_sqlite3_check(sqlite3_exec(db, script, NULL, NULL, &errmsg), errmsg);
  free(script);
  // All done
  return db;
}


static void ev_handler(struct mg_connection *c, int ev, void *p) {
  if (ev == MG_EV_HTTP_REQUEST) {
    struct http_message *hm = (struct http_message *) p;

    // We have received an HTTP request. Parsed request is contained in `hm`.
    // Send HTTP reply to the client which shows full original request.
    mg_send_head(c, 200, hm->message.len, "Content-Type: text/plain");
    mg_printf(c, "%.*s", (int)hm->message.len, hm->message.p);
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

