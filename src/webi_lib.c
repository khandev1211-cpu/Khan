// ===========================================================================
// webi_lib.c — Native HTTP server backend for the webi Khan package
//
// Provides:
//   http_serve(port, app)  — blocking server loop, calls Khan's webi_handle()
//
// Also provides the v1.1 requests extensions:
//   http_get_h / http_post_h / http_put_h / http_put_json
//   http_patch / http_patch_json / http_delete_h / http_head
//
// Platform support:
//   POSIX (Linux/macOS) — raw BSD sockets + curl subprocess for outbound
//   Windows            — WinHTTP for outbound; Winsock for server
// ===========================================================================

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <winhttp.h>
#  pragma comment(lib, "ws2_32.lib")
#  pragma comment(lib, "winhttp.lib")
typedef SOCKET wb_sock_t;
#else
#  define _POSIX_C_SOURCE 200809L
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
typedef int wb_sock_t;
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "webi_lib.h"
/* Bridge: call Khan function by name from C */
extern Value khan_call_fn(Interpreter *interp, Environment *env,
                          const char *fn_name, int argc, Value *argv);
/* Global env pointer set by http_serve so the accept loop can call Khan */
static Environment *g_webi_env = NULL;

// ---------------------------------------------------------------------------
// Shared buffer helpers (same pattern as requests_lib.c)
// ---------------------------------------------------------------------------
static void wb_buf_append(char **buf, int *len, int *cap, const char *s, int slen) {
    while (*len + slen + 1 > *cap) {
        *cap = *cap ? *cap * 2 : 512;
        *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, slen);
    *len += slen;
    (*buf)[*len] = '\0';
}
static void wb_buf_str(char **buf, int *len, int *cap, const char *s) {
    wb_buf_append(buf, len, cap, s, (int)strlen(s));
}
static void wb_buf_char(char **buf, int *len, int *cap, char c) {
    wb_buf_append(buf, len, cap, &c, 1);
}

// ---------------------------------------------------------------------------
// Inline JSON encoder (mirrors requests_lib.c — no cross-module dependency)
// ---------------------------------------------------------------------------
static void wb_json_string(const char *s, char **buf, int *len, int *cap) {
    wb_buf_char(buf, len, cap, '"');
    for (const char *p = s; *p; p++) {
        if      (*p == '"')  { wb_buf_append(buf, len, cap, "\\\"", 2); }
        else if (*p == '\\') { wb_buf_append(buf, len, cap, "\\\\", 2); }
        else if (*p == '\n') { wb_buf_append(buf, len, cap, "\\n",  2); }
        else if (*p == '\r') { wb_buf_append(buf, len, cap, "\\r",  2); }
        else if (*p == '\t') { wb_buf_append(buf, len, cap, "\\t",  2); }
        else wb_buf_char(buf, len, cap, *p);
    }
    wb_buf_char(buf, len, cap, '"');
}

static void wb_json_encode(Value v, char **buf, int *len, int *cap) {
    char tmp[64];
    switch (v.type) {
        case VAL_NIL:
            wb_buf_append(buf, len, cap, "null", 4); break;
        case VAL_BOOL:
            if (v.as.boolean) wb_buf_append(buf, len, cap, "true", 4);
            else              wb_buf_append(buf, len, cap, "false", 5);
            break;
        case VAL_NUMBER:
            if (v.as.number == (long long)v.as.number)
                snprintf(tmp, sizeof(tmp), "%lld", (long long)v.as.number);
            else
                snprintf(tmp, sizeof(tmp), "%.17g", v.as.number);
            wb_buf_append(buf, len, cap, tmp, (int)strlen(tmp)); break;
        case VAL_STRING:
            wb_json_string(v.as.string, buf, len, cap); break;
        case VAL_ARRAY:
            wb_buf_char(buf, len, cap, '[');
            for (int i = 0; i < v.as.array.count; i++) {
                if (i) wb_buf_append(buf, len, cap, ", ", 2);
                wb_json_encode(v.as.array.items[i], buf, len, cap);
            }
            wb_buf_char(buf, len, cap, ']'); break;
        case VAL_MAP:
            wb_buf_char(buf, len, cap, '{');
            for (int i = 0; i < v.as.map.count; i++) {
                if (i) wb_buf_append(buf, len, cap, ", ", 2);
                wb_json_string(v.as.map.entries[i].key, buf, len, cap);
                wb_buf_append(buf, len, cap, ": ", 2);
                wb_json_encode(v.as.map.entries[i].value, buf, len, cap);
            }
            wb_buf_char(buf, len, cap, '}'); break;
        default:
            wb_buf_append(buf, len, cap, "null", 4); break;
    }
}

// ---------------------------------------------------------------------------
// Arg checking helpers
// ---------------------------------------------------------------------------
static int wb_check(Interpreter *interp, const char *fn, int min, int actual) {
    if (actual < min) {
        fprintf(stderr, "Runtime error: %s() expects at least %d argument(s), got %d\n",
                fn, min, actual);
        interp->had_runtime_error = 1;
        return 0;
    }
    return 1;
}

static int wb_str_arg(Interpreter *interp, const char *fn, int idx,
                       Value v, const char **out) {
    if (v.type != VAL_STRING) {
        fprintf(stderr, "Runtime error: %s() argument %d must be a string\n", fn, idx+1);
        interp->had_runtime_error = 1;
        return 0;
    }
    *out = v.as.string;
    return 1;
}

// ===========================================================================
// SHARED HTTP PARSING / RESPONSE HELPERS (portable — used by both the POSIX
// and Windows server loops below). These only use recv() with an int-like fd
// (works for both POSIX fd and Winsock SOCKET-as-int) and plain string/buffer
// manipulation, so they compile and run unchanged on every platform.
// ===========================================================================

// ---------------------------------------------------------------------------
// Internal: read a full HTTP request from a socket fd
// Returns malloc'd raw request string, caller frees.
// ---------------------------------------------------------------------------
static char *wb_read_request(wb_sock_t fd) {
    char *buf = NULL;
    int len = 0, cap = 0;
    char tmp[4096];
    ssize_t n;

    // Read until we have headers (double CRLF)
    while ((n = recv(fd, tmp, sizeof(tmp), 0)) > 0) {
        if (len + n + 1 > cap) {
            cap = cap ? (cap + (int)n) * 2 : 8192;
            buf = realloc(buf, cap);
        }
        memcpy(buf + len, tmp, n);
        len += n;
        buf[len] = '\0';

        // Check if we have the header terminator
        if (strstr(buf, "\r\n\r\n")) break;
    }
    if (!buf) buf = strdup("");
    return buf;
}

// ---------------------------------------------------------------------------
// Internal: parse the first line of an HTTP request
// Sets method, path, query (all must point to caller-allocated char[512])
// ---------------------------------------------------------------------------
static void wb_parse_request_line(const char *raw,
                                    char *method, char *path, char *query) {
    method[0] = path[0] = query[0] = '\0';
    sscanf(raw, "%511s %511s", method, path);

    // Split path into path + query string
    char *q = strchr(path, '?');
    if (q) {
        strncpy(query, q + 1, 511); query[511] = '\0';
        *q = '\0';
    }
}

// ---------------------------------------------------------------------------
// Internal: extract all headers from raw request into a single string
// "Key: Value\r\nKey2: Value2\r\n..."
// ---------------------------------------------------------------------------
static char *wb_extract_headers(const char *raw) {
    const char *start = strstr(raw, "\r\n");
    if (!start) return strdup("");
    start += 2; // skip first line
    const char *end = strstr(start, "\r\n\r\n");
    if (!end) end = start + strlen(start);
    int len = (int)(end - start);
    char *h = malloc(len + 1);
    memcpy(h, start, len);
    h[len] = '\0';
    return h;
}

// ---------------------------------------------------------------------------
// Internal: extract Content-Length from headers, read body from socket
// ---------------------------------------------------------------------------
static char *wb_read_body(wb_sock_t fd, const char *raw) {
    const char *cl = strstr(raw, "Content-Length:");
    if (!cl) cl = strstr(raw, "content-length:");
    if (!cl) return strdup("");

    int content_length = 0;
    sscanf(cl + 15, "%d", &content_length);
    if (content_length <= 0) return strdup("");

    // Body starts after \r\n\r\n
    const char *body_start = strstr(raw, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        int already = (int)strlen(body_start);
        if (already >= content_length) {
            char *b = malloc(content_length + 1);
            memcpy(b, body_start, content_length);
            b[content_length] = '\0';
            return b;
        }
    }

    // Need to read more from socket
    char *body = malloc(content_length + 1);
    int received = 0;
    if (body_start) {
        int already = (int)strlen(body_start);
        if (already > 0) {
            memcpy(body, body_start, already);
            received = already;
        }
    }
    while (received < content_length) {
        ssize_t n = recv(fd, body + received, content_length - received, 0);
        if (n <= 0) break;
        received += n;
    }
    body[received] = '\0';
    return body;
}

// ---------------------------------------------------------------------------
// Internal: build HTTP response string from a webi response map
// ---------------------------------------------------------------------------
static char *wb_build_http_response(Value *res_map) {
    Value *status_v = map_get(res_map, "status");
    Value *body_v   = map_get(res_map, "body");
    Value *ct_v     = map_get(res_map, "content_type");
    Value *hdrs_v   = map_get(res_map, "headers");

    long   status = status_v ? (long)status_v->as.number : 200;
    const char *body    = (body_v && body_v->type == VAL_STRING) ? body_v->as.string : "";
    const char *ct      = (ct_v  && ct_v->type  == VAL_STRING)  ? ct_v->as.string   : "text/plain; charset=utf-8";

    // Map status code to reason phrase
    const char *reason = "OK";
    if      (status == 200) reason = "OK";
    else if (status == 201) reason = "Created";
    else if (status == 204) reason = "No Content";
    else if (status == 301) reason = "Moved Permanently";
    else if (status == 302) reason = "Found";
    else if (status == 400) reason = "Bad Request";
    else if (status == 401) reason = "Unauthorized";
    else if (status == 403) reason = "Forbidden";
    else if (status == 404) reason = "Not Found";
    else if (status == 405) reason = "Method Not Allowed";
    else if (status == 500) reason = "Internal Server Error";

    int body_len = (int)strlen(body);

    char *out = NULL; int olen = 0, ocap = 0;
    char line[256];

    // Status line
    snprintf(line, sizeof(line), "HTTP/1.1 %ld %s\r\n", status, reason);
    wb_buf_str(&out, &olen, &ocap, line);

    // Standard headers
    wb_buf_str(&out, &olen, &ocap, "Server: webi/1.0.0 (Khan)\r\n");
    wb_buf_str(&out, &olen, &ocap, "Connection: close\r\n");
    snprintf(line, sizeof(line), "Content-Type: %s\r\n", ct);
    wb_buf_str(&out, &olen, &ocap, line);
    snprintf(line, sizeof(line), "Content-Length: %d\r\n", body_len);
    wb_buf_str(&out, &olen, &ocap, line);

    // Extra headers from res["headers"] map
    if (hdrs_v && hdrs_v->type == VAL_MAP) {
        for (int i = 0; i < hdrs_v->as.map.count; i++) {
            const char *hk = hdrs_v->as.map.entries[i].key;
            Value hv = hdrs_v->as.map.entries[i].value;
            if (hv.type == VAL_STRING) {
                snprintf(line, sizeof(line), "%s: %s\r\n", hk, hv.as.string);
                wb_buf_str(&out, &olen, &ocap, line);
            }
        }
    }

    wb_buf_str(&out, &olen, &ocap, "\r\n");
    if (body_len > 0) wb_buf_str(&out, &olen, &ocap, body);

    return out;
}


// ===========================================================================
// POSIX OUTBOUND REQUESTS (curl subprocess, same strategy as requests_lib.c)
// ===========================================================================
#ifndef _WIN32

static char *wb_run_curl(const char **args, int nargs) {
    size_t cmd_len = 16;
    for (int i = 0; i < nargs; i++)
        cmd_len += strlen(args[i]) * 2 + 4;
    char *cmd = malloc(cmd_len);
    strcpy(cmd, "curl -s");
    for (int i = 0; i < nargs; i++) {
        strcat(cmd, " '");
        char *p = cmd + strlen(cmd);
        for (const char *q = args[i]; *q; q++) {
            if (*q == '\'') { *p++ = '\''; *p++ = '\\'; *p++ = '\''; *p++ = '\''; }
            else { *p++ = *q; }
        }
        *p++ = '\''; *p = '\0';
    }
    FILE *fp = popen(cmd, "r");
    free(cmd);
    if (!fp) return strdup("");
    char *out = NULL; size_t out_len = 0, cap = 0;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (out_len + n + 1 > cap) { cap = cap ? (cap + n) * 2 : 4096; out = realloc(out, cap); }
        memcpy(out + out_len, buf, n); out_len += n;
    }
    pclose(fp);
    if (!out) return strdup("");
    out[out_len] = '\0';
    return out;
}

static Value wb_curl_request(const char *method, const char *url,
                               const char *body, const char *ctype,
                               const char *extra_headers) {
#define WB_MAX_ARGS 32
    const char *args[WB_MAX_ARGS];
    int n = 0;
    args[n++] = "-w"; args[n++] = "\n__WB_STATUS__%{http_code}";
    if (method && strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
        args[n++] = "-X"; args[n++] = method;
    }
    if (strcmp(method, "HEAD") == 0) {
        args[n++] = "-I";
    }
    if (body) { args[n++] = "-d"; args[n++] = body; }
    if (ctype) { args[n++] = "-H"; args[n++] = ctype; }
    // Extra headers: split on \r\n and add each as -H
    if (extra_headers && strlen(extra_headers) > 0) {
        // We pass a single combined header string; curl handles multiple -H flags
        // Make a mutable copy and split on \r\n
        char *hcopy = strdup(extra_headers);
        char *line = strtok(hcopy, "\r\n");
        while (line && n < WB_MAX_ARGS - 2) {
            if (strlen(line) > 3) { args[n++] = "-H"; args[n++] = line; }
            line = strtok(NULL, "\r\n");
        }
        // Note: hcopy leaks here for simplicity; fix with arena allocator later
    }
    args[n++] = url;

    char *raw = wb_run_curl(args, n);
    const char *marker = strstr(raw, "\n__WB_STATUS__");
    long status = 0; char *body_str;
    if (marker) {
        int blen = (int)(marker - raw);
        body_str = malloc(blen + 1);
        memcpy(body_str, raw, blen); body_str[blen] = '\0';
        status = atol(marker + (int)strlen("\n__WB_STATUS__"));
    } else {
        body_str = strdup(raw);
    }
    free(raw);

    Value m = value_map_empty();
    map_set(&m, "status",  value_number((double)status));
    map_set(&m, "body",    value_string(body_str));
    map_set(&m, "success", value_bool(status >= 200 && status < 300));
    free(body_str);
    return m;
}

// ---------------------------------------------------------------------------
// v1.1 Extended request natives — POSIX
// ---------------------------------------------------------------------------

static void fn_http_get_h(Value *result, Interpreter *interp, int argc, Value *args) {
    if (!wb_check(interp, "http_get_h", 2, argc)) { *result = value_nil(); return; }
    const char *url, *headers;
    if (!wb_str_arg(interp, "http_get_h", 0, args[0], &url)) { *result = value_nil(); return; }
    if (!wb_str_arg(interp, "http_get_h", 1, args[1], &headers)) { *result = value_nil(); return; }
    *result = wb_curl_request("GET", url, NULL, NULL, headers);
}

static void fn_http_post_h(Value *result, Interpreter *interp, int argc, Value *args) {
    if (!wb_check(interp, "http_post_h", 3, argc)) { *result = value_nil(); return; }
    const char *url, *body, *headers;
    if (!wb_str_arg(interp, "http_post_h", 0, args[0], &url))     { *result = value_nil(); return; }
    if (!wb_str_arg(interp, "http_post_h", 1, args[1], &body))    { *result = value_nil(); return; }
    if (!wb_str_arg(interp, "http_post_h", 2, args[2], &headers)) { *result = value_nil(); return; }
    *result = wb_curl_request("POST", url, body, NULL, headers);
}

static void fn_http_put_h(Value *result, Interpreter *interp, int argc, Value *args) {
    if (!wb_check(interp, "http_put_h", 3, argc)) { *result = value_nil(); return; }
    const char *url, *body, *headers;
    if (!wb_str_arg(interp, "http_put_h", 0, args[0], &url))     { *result = value_nil(); return; }
    if (!wb_str_arg(interp, "http_put_h", 1, args[1], &body))    { *result = value_nil(); return; }
    if (!wb_str_arg(interp, "http_put_h", 2, args[2], &headers)) { *result = value_nil(); return; }
    *result = wb_curl_request("PUT", url, body, NULL, headers);
}

static void fn_http_put_json(Value *result, Interpreter *interp, int argc, Value *args) {
    if (!wb_check(interp, "http_put_json", 2, argc)) { *result = value_nil(); return; }
    const char *url;
    if (!wb_str_arg(interp, "http_put_json", 0, args[0], &url)) { *result = value_nil(); return; }
    char *json_body = NULL; int jlen = 0, jcap = 0;
    wb_json_encode(args[1], &json_body, &jlen, &jcap);
    *result = wb_curl_request("PUT", url, json_body ? json_body : "{}", "Content-Type: application/json", NULL);
    free(json_body);
}

static void fn_http_patch(Value *result, Interpreter *interp, int argc, Value *args) {
    if (!wb_check(interp, "http_patch", 2, argc)) { *result = value_nil(); return; }
    const char *url, *body;
    if (!wb_str_arg(interp, "http_patch", 0, args[0], &url))  { *result = value_nil(); return; }
    if (!wb_str_arg(interp, "http_patch", 1, args[1], &body)) { *result = value_nil(); return; }
    *result = wb_curl_request("PATCH", url, body, "Content-Type: application/x-www-form-urlencoded", NULL);
}

static void fn_http_patch_json(Value *result, Interpreter *interp, int argc, Value *args) {
    if (!wb_check(interp, "http_patch_json", 2, argc)) { *result = value_nil(); return; }
    const char *url;
    if (!wb_str_arg(interp, "http_patch_json", 0, args[0], &url)) { *result = value_nil(); return; }
    char *json_body = NULL; int jlen = 0, jcap = 0;
    wb_json_encode(args[1], &json_body, &jlen, &jcap);
    *result = wb_curl_request("PATCH", url, json_body ? json_body : "{}", "Content-Type: application/json", NULL);
    free(json_body);
}

static void fn_http_delete_h(Value *result, Interpreter *interp, int argc, Value *args) {
    if (!wb_check(interp, "http_delete_h", 2, argc)) { *result = value_nil(); return; }
    const char *url, *headers;
    if (!wb_str_arg(interp, "http_delete_h", 0, args[0], &url))     { *result = value_nil(); return; }
    if (!wb_str_arg(interp, "http_delete_h", 1, args[1], &headers)) { *result = value_nil(); return; }
    *result = wb_curl_request("DELETE", url, NULL, NULL, headers);
}

static void fn_http_head(Value *result, Interpreter *interp, int argc, Value *args) {
    if (!wb_check(interp, "http_head", 1, argc)) { *result = value_nil(); return; }
    const char *url;
    if (!wb_str_arg(interp, "http_head", 0, args[0], &url)) { *result = value_nil(); return; }
    *result = wb_curl_request("HEAD", url, NULL, NULL, NULL);
}

// ===========================================================================
// HTTP SERVER — POSIX (raw sockets, single-threaded, HTTP/1.1)
// ===========================================================================

// ---------------------------------------------------------------------------
// fn_http_serve — the main server loop
// Signature (Khan): http_serve(port, app)
//
// The server:
//   1. Binds to 0.0.0.0:port
//   2. Accepts connections in a loop
//   3. Parses each HTTP request
//   4. Calls Khan's webi_handle(app, method, path, query, headers, body)
//   5. Builds the HTTP response from the returned map
//   6. Sends it back and closes the connection
// ---------------------------------------------------------------------------
static void fn_http_serve(Value *result, Interpreter *interp, int argc, Value *args) {
    if (!wb_check(interp, "http_serve", 2, argc)) { *result = value_nil(); return; }

    if (args[0].type != VAL_NUMBER) {
        fprintf(stderr, "Runtime error: http_serve(port, app) — port must be a number\n");
        interp->had_runtime_error = 1; *result = value_nil(); return;
    }
    int port = (int)args[0].as.number;
    Value app = value_copy(args[1]);   // save the app map

    // Look up webi_handle in the current environment
    // We'll call it directly through the interpreter

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[webi] socket()");
        interp->had_runtime_error = 1; *result = value_nil(); return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[webi] bind()");
        close(server_fd);
        interp->had_runtime_error = 1; *result = value_nil(); return;
    }
    if (listen(server_fd, 64) < 0) {
        perror("[webi] listen()");
        close(server_fd);
        interp->had_runtime_error = 1; *result = value_nil(); return;
    }

    g_webi_env = interp->base_env;  /* store env for accept loop */
    printf("[webi] Listening on port %d\n", port);
    fflush(stdout);

    // Accept loop
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("[webi] accept()");
            break;
        }

        // Read request
        char *raw = wb_read_request(client_fd);
        char method[512], path[512], query[512];
        wb_parse_request_line(raw, method, path, query);
        char *headers_str = wb_extract_headers(raw);
        char *body_str    = wb_read_body(client_fd, raw);
        char  client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

        // Call webi_handle(app, method, path, query, headers, body) in Khan
        // We need to look up the function from the interpreter environment.
        // We pass it via a synthetic AstNode call using interpreter_execute.
        // Simpler approach: look up "webi_handle" as a global native or Khan fn
        // and call its underlying C NativeFn / Khan fn body directly.

        // Build args array for webi_handle
        Value call_args[6];
        call_args[0] = value_copy(app);
        call_args[1] = value_string(method);
        call_args[2] = value_string(path);
        call_args[3] = value_string(query);
        call_args[4] = value_string(headers_str);
        call_args[5] = value_string(body_str);

        /* Call Khan webi_handle(app, method, path, query, headers, body) */
        Value res_map;
        if (g_webi_env) {
            res_map = khan_call_fn(interp, g_webi_env, "webi_handle", 6, call_args);
        } else {
            res_map = value_map_empty();
            map_set(&res_map, "status",       value_number(500));
            map_set(&res_map, "body",         value_string("[webi] env not set"));
            map_set(&res_map, "content_type", value_string("text/plain"));
            map_set(&res_map, "headers",      value_map_empty());
        }
        char *http_response = wb_build_http_response(&res_map);
        send(client_fd, http_response, strlen(http_response), 0);

        // Cleanup
        // NOTE: call_args are NOT freed here — khan_call_fn() binds them as
        // parameters in the callee's environment, and env_free() (called
        // internally by khan_call_fn after the function returns) already
        // owns and frees each parameter Value. Freeing them again here
        // would be a double-free.
        free(http_response);
        free(raw); free(headers_str); free(body_str);
        value_free(res_map);
        close(client_fd);

        if (interp->had_runtime_error) break;
    }

    close(server_fd);
    value_free(app);
    *result = value_nil();
}

#endif // !_WIN32

// ===========================================================================
// WINDOWS — WinHTTP outbound + Winsock server
// ===========================================================================
#ifdef _WIN32

static Value wh_request(const char *method, const char *url,
                          const char *body, const char *ctype,
                          const char *extra_headers) {
    const char *hs = url;
    if      (strncmp(url, "https://", 8) == 0) hs = url + 8;
    else if (strncmp(url, "http://",  7) == 0) hs = url + 7;
    const char *slash = strchr(hs, '/');
    char *host; const char *path;
    if (slash) {
        int hl = (int)(slash - hs);
        host = malloc(hl + 1); strncpy(host, hs, hl); host[hl] = '\0';
        path = slash;
    } else { host = strdup(hs); path = "/"; }

    BOOL use_https = (strncmp(url, "https", 5) == 0);
    INTERNET_PORT port = use_https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;

    HINTERNET hSess = WinHttpOpen(L"Khan-webi/1.1",
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    Value result = value_map_empty();
    map_set(&result, "status",  value_number(0));
    map_set(&result, "body",    value_string(""));
    map_set(&result, "success", value_bool(0));
    if (!hSess) { free(host); return result; }

    int wl = MultiByteToWideChar(CP_UTF8, 0, host, -1, NULL, 0);
    wchar_t *whost = malloc(wl * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, wl);

    int pl = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    wchar_t *wpath = malloc(pl * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, pl);

    int ml = MultiByteToWideChar(CP_UTF8, 0, method, -1, NULL, 0);
    wchar_t *wmethod = malloc(ml * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, method, -1, wmethod, ml);

    HINTERNET hConn = WinHttpConnect(hSess, whost, port, 0);
    if (hConn) {
        HINTERNET hReq = WinHttpOpenRequest(hConn, wmethod, wpath, NULL, NULL, NULL,
                                             use_https ? WINHTTP_FLAG_SECURE : 0);
        if (hReq) {
            if (ctype) {
                int cl = MultiByteToWideChar(CP_UTF8, 0, ctype, -1, NULL, 0);
                wchar_t *wct = malloc(cl * sizeof(wchar_t));
                MultiByteToWideChar(CP_UTF8, 0, ctype, -1, wct, cl);
                WinHttpAddRequestHeaders(hReq, wct, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
                free(wct);
            }
            if (extra_headers) {
                int el = MultiByteToWideChar(CP_UTF8, 0, extra_headers, -1, NULL, 0);
                wchar_t *weh = malloc(el * sizeof(wchar_t));
                MultiByteToWideChar(CP_UTF8, 0, extra_headers, -1, weh, el);
                WinHttpAddRequestHeaders(hReq, weh, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
                free(weh);
            }
            DWORD blen = body ? (DWORD)strlen(body) : 0;
            if (WinHttpSendRequest(hReq, NULL, 0, (LPVOID)body, blen, blen, 0) &&
                WinHttpReceiveResponse(hReq, NULL)) {
                DWORD sc = 0, ssz = sizeof(sc);
                WinHttpQueryHeaders(hReq,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    NULL, &sc, &ssz, NULL);
                char *rbuf = NULL; DWORD tot = 0, br = 0, av = 0;
                while (WinHttpQueryDataAvailable(hReq, &av) && av > 0) {
                    rbuf = realloc(rbuf, tot + av + 1);
                    WinHttpReadData(hReq, rbuf + tot, av, &br);
                    tot += br; rbuf[tot] = '\0';
                }
                if (!rbuf) rbuf = strdup("");
                map_set(&result, "status",  value_number((double)sc));
                map_set(&result, "body",    value_string(rbuf));
                map_set(&result, "success", value_bool(sc >= 200 && sc < 300));
                free(rbuf);
            }
            WinHttpCloseHandle(hReq);
        }
        WinHttpCloseHandle(hConn);
    }
    free(whost); free(wpath); free(wmethod);
    WinHttpCloseHandle(hSess); free(host);
    return result;
}

static void fn_http_get_h(Value *r, Interpreter *i, int argc, Value *args) {
    if (!wb_check(i, "http_get_h", 2, argc)) { *r = value_nil(); return; }
    const char *url, *h;
    if (!wb_str_arg(i, "http_get_h", 0, args[0], &url)) { *r = value_nil(); return; }
    if (!wb_str_arg(i, "http_get_h", 1, args[1], &h))   { *r = value_nil(); return; }
    *r = wh_request("GET", url, NULL, NULL, h);
}
static void fn_http_post_h(Value *r, Interpreter *i, int argc, Value *args) {
    if (!wb_check(i, "http_post_h", 3, argc)) { *r = value_nil(); return; }
    const char *url, *body, *h;
    if (!wb_str_arg(i, "http_post_h", 0, args[0], &url))  { *r = value_nil(); return; }
    if (!wb_str_arg(i, "http_post_h", 1, args[1], &body)) { *r = value_nil(); return; }
    if (!wb_str_arg(i, "http_post_h", 2, args[2], &h))    { *r = value_nil(); return; }
    *r = wh_request("POST", url, body, NULL, h);
}
static void fn_http_put_h(Value *r, Interpreter *i, int argc, Value *args) {
    if (!wb_check(i, "http_put_h", 3, argc)) { *r = value_nil(); return; }
    const char *url, *body, *h;
    if (!wb_str_arg(i, "http_put_h", 0, args[0], &url))  { *r = value_nil(); return; }
    if (!wb_str_arg(i, "http_put_h", 1, args[1], &body)) { *r = value_nil(); return; }
    if (!wb_str_arg(i, "http_put_h", 2, args[2], &h))    { *r = value_nil(); return; }
    *r = wh_request("PUT", url, body, NULL, h);
}
static void fn_http_put_json(Value *r, Interpreter *i, int argc, Value *args) {
    if (!wb_check(i, "http_put_json", 2, argc)) { *r = value_nil(); return; }
    const char *url;
    if (!wb_str_arg(i, "http_put_json", 0, args[0], &url)) { *r = value_nil(); return; }
    char *j = NULL; int jl = 0, jc = 0;
    wb_json_encode(args[1], &j, &jl, &jc);
    *r = wh_request("PUT", url, j ? j : "{}", "Content-Type: application/json", NULL);
    free(j);
}
static void fn_http_patch(Value *r, Interpreter *i, int argc, Value *args) {
    if (!wb_check(i, "http_patch", 2, argc)) { *r = value_nil(); return; }
    const char *url, *body;
    if (!wb_str_arg(i, "http_patch", 0, args[0], &url))  { *r = value_nil(); return; }
    if (!wb_str_arg(i, "http_patch", 1, args[1], &body)) { *r = value_nil(); return; }
    *r = wh_request("PATCH", url, body, "Content-Type: application/x-www-form-urlencoded", NULL);
}
static void fn_http_patch_json(Value *r, Interpreter *i, int argc, Value *args) {
    if (!wb_check(i, "http_patch_json", 2, argc)) { *r = value_nil(); return; }
    const char *url;
    if (!wb_str_arg(i, "http_patch_json", 0, args[0], &url)) { *r = value_nil(); return; }
    char *j = NULL; int jl = 0, jc = 0;
    wb_json_encode(args[1], &j, &jl, &jc);
    *r = wh_request("PATCH", url, j ? j : "{}", "Content-Type: application/json", NULL);
    free(j);
}
static void fn_http_delete_h(Value *r, Interpreter *i, int argc, Value *args) {
    if (!wb_check(i, "http_delete_h", 2, argc)) { *r = value_nil(); return; }
    const char *url, *h;
    if (!wb_str_arg(i, "http_delete_h", 0, args[0], &url)) { *r = value_nil(); return; }
    if (!wb_str_arg(i, "http_delete_h", 1, args[1], &h))   { *r = value_nil(); return; }
    *r = wh_request("DELETE", url, NULL, NULL, h);
}
static void fn_http_head(Value *r, Interpreter *i, int argc, Value *args) {
    if (!wb_check(i, "http_head", 1, argc)) { *r = value_nil(); return; }
    const char *url;
    if (!wb_str_arg(i, "http_head", 0, args[0], &url)) { *r = value_nil(); return; }
    *r = wh_request("HEAD", url, NULL, NULL, NULL);
}

// Windows server — Winsock + real webi_handle call
static void fn_http_serve(Value *result, Interpreter *interp, int argc, Value *args) {
    if (!wb_check(interp, "http_serve", 2, argc)) { *result = value_nil(); return; }
    if (args[0].type != VAL_NUMBER) { *result = value_nil(); return; }
    int port = (int)args[0].as.number;
    Value app = value_copy(args[1]);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) {
        fprintf(stderr, "[webi] socket() failed: %d\n", WSAGetLastError());
        WSACleanup(); *result = value_nil(); return;
    }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((u_short)port);
    bind(srv, (struct sockaddr *)&addr, sizeof(addr));
    listen(srv, 64);

    g_webi_env = interp->base_env;
    printf("[webi] Listening on port %d\n", port);
    fflush(stdout);

    while (1) {
        SOCKET cli = accept(srv, NULL, NULL);
        if (cli == INVALID_SOCKET) continue;

        /* Read raw request */
        char *raw = NULL;
        int rlen = 0, rcap = 0;
        char rbuf[4096];
        int n;
        while ((n = recv(cli, rbuf, sizeof(rbuf), 0)) > 0) {
            if (rlen + n + 1 > rcap) {
                rcap = rcap ? rcap * 2 : 8192;
                raw = realloc(raw, rcap);
            }
            memcpy(raw + rlen, rbuf, n);
            rlen += n;
            raw[rlen] = '\0';
            if (strstr(raw, "\r\n\r\n")) break;
        }
        if (!raw) { closesocket(cli); continue; }

        char method[512], path[512], query[512];
        wb_parse_request_line(raw, method, path, query);
        char *headers_str = wb_extract_headers(raw);
        char *body_str    = wb_read_body(cli, raw);

        Value call_args[6];
        call_args[0] = value_copy(app);
        call_args[1] = value_string(method);
        call_args[2] = value_string(path);
        call_args[3] = value_string(query);
        call_args[4] = value_string(headers_str ? headers_str : "");
        call_args[5] = value_string(body_str    ? body_str    : "");

        Value res_map;
        if (g_webi_env) {
            res_map = khan_call_fn(interp, g_webi_env, "webi_handle", 6, call_args);
        } else {
            res_map = value_map_empty();
            map_set(&res_map, "status",       value_number(500));
            map_set(&res_map, "body",         value_string("[webi] env not set"));
            map_set(&res_map, "content_type", value_string("text/plain"));
            map_set(&res_map, "headers",      value_map_empty());
        }

        char *http_response = wb_build_http_response(&res_map);
        send(cli, http_response, (int)strlen(http_response), 0);

        // NOTE: call_args are NOT freed here — see POSIX loop comment above;
        // khan_call_fn's internal env_free already owns/frees each param.
        free(http_response);
        free(raw);
        if (headers_str) free(headers_str);
        if (body_str)    free(body_str);
        value_free(res_map);
        closesocket(cli);

        if (interp->had_runtime_error) break;
    }
    closesocket(srv);
    WSACleanup();
    value_free(app);
    *result = value_nil();
}
#endif // _WIN32

// ===========================================================================
// Registration
// ===========================================================================
void webi_register_all(Environment *env) {
    // v1.1 extended request natives
    env_define(env, "http_get_h",      value_native("http_get_h",      fn_http_get_h));
    env_define(env, "http_post_h",     value_native("http_post_h",     fn_http_post_h));
    env_define(env, "http_put_h",      value_native("http_put_h",      fn_http_put_h));
    env_define(env, "http_put_json",   value_native("http_put_json",   fn_http_put_json));
    env_define(env, "http_patch",      value_native("http_patch",      fn_http_patch));
    env_define(env, "http_patch_json", value_native("http_patch_json", fn_http_patch_json));
    env_define(env, "http_delete_h",   value_native("http_delete_h",   fn_http_delete_h));
    env_define(env, "http_head",       value_native("http_head",       fn_http_head));

    // webi server
    env_define(env, "http_serve",      value_native("http_serve",      fn_http_serve));
}