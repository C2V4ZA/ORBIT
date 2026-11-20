/* phone upload page: token-only access, page escaping, size limit, one picture, clean stop */
#include "image.h"
#include "orbit.h"
#include "upload.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int port;
static char resp[8192];
static int ask(const char *head, const uint8_t *body, size_t n) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(!connect(s, (struct sockaddr *)&a, sizeof(a)));
    send(s, head, strlen(head), MSG_NOSIGNAL);
    if (body)
        send(s, body, n, MSG_NOSIGNAL);
    int got = 0, k;
    while (got < (int)sizeof(resp) - 1 && (k = (int)recv(s, resp + got, sizeof(resp) - 1 - got, 0)) > 0)
        got += k;
    resp[got] = 0;
    close(s);
    int status = 0;
    sscanf(resp, "HTTP/1.1 %d", &status);
    return status;
}

static int can_connect(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int ok = !connect(s, (struct sockaddr *)&a, sizeof(a));
    close(s);
    return ok;
}

int main(void) {
    OrbitUpload *u = upload_start(0, "Profile <picture>", "Choose a photo", "Sent! It's on the PS3 now.",
                                  "Did not work");
    assert(u);
    port = upload_port(u);
    const char *tok = upload_token(u);
    assert(strlen(tok) == 32);
    char head[512];
    snprintf(head, sizeof(head), "GET /%s HTTP/1.1\r\nHost: x\r\n\r\n", tok);
    assert(ask(head, NULL, 0) == 200);
    assert(strstr(resp, "Profile &lt;picture&gt;") && !strstr(resp, "<picture>"));
    assert(strstr(resp, "It\xE2\x80\x99s on the PS3")); /* no raw quote inside the script */
    assert(ask("GET / HTTP/1.1\r\n\r\n", NULL, 0) == 404);
    assert(ask("GET /0123456789abcdef0123456789abcdef HTTP/1.1\r\n\r\n", NULL, 0) == 404);
    snprintf(head, sizeof(head), "GET /%s/../etc HTTP/1.1\r\n\r\n", tok);
    assert(ask(head, NULL, 0) == 404);
    snprintf(head, sizeof(head), "POST /%s/upload HTTP/1.1\r\nContent-Length: 999999999\r\n\r\n", tok);
    assert(ask(head, NULL, 0) == 413);
    uint8_t *data;
    size_t n;
    assert(!upload_take(u, &data, &n) && upload_state(u) == UPLOAD_WAITING);

    FILE *f = fopen("tests/fixtures/base420.jpg", "rb");
    static uint8_t jpg[1 << 16];
    size_t jn = fread(jpg, 1, sizeof(jpg), f);
    fclose(f);
    snprintf(head, sizeof(head), "POST /%s/upload HTTP/1.1\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
             tok, jn);
    assert(ask(head, jpg, jn) == 200);
    for (int i = 0; i < 50 && upload_state(u) != UPLOAD_DONE; i++)
        usleep(20000);
    assert(upload_state(u) == UPLOAD_DONE && upload_take(u, &data, &n) && n == jn);
    OrbitImage img;
    assert(image_decode_any(data, n, &img) == IMG_OK && img.w == 100);
    image_free(&img);
    free(data);
    /* single use: after one picture the token answers nothing any more */
    for (int i = 0; i < 50 && can_connect(); i++)
        usleep(20000);
    assert(!can_connect());
    upload_stop(u);
    /* expiry: an unused page stops by itself */
    u = upload_start(0, "a", "b", "c", "d");
    assert(u);
    port = upload_port(u);
    snprintf(head, sizeof(head), "GET /%s HTTP/1.1\r\nHost: x\r\n\r\n", upload_token(u));
    assert(ask(head, NULL, 0) == 200);
    upload_set_lifetime(u, 0);
    for (int i = 0; i < 100 && upload_state(u) != UPLOAD_STOPPED; i++)
        usleep(20000);
    assert(upload_state(u) == UPLOAD_STOPPED && !can_connect() && !upload_take(u, &data, &n));
    upload_stop(u);
    printf("PASS: phone upload: page only with the token, escaped texts, 404 elsewhere, size limit, one "
           "picture received and decoded, token single use, expiry, clean stop\n");
    return 0;
}
