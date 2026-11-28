/* Game covers: the download part (kept apart so the PC preview renderer needs no network code). */
#include "covers.h"
#include "update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fetch_one(const char *url, const char *dir, const char *id) {
    uint8_t *buf = malloc(COVER_MAX_FILE);
    if (!buf)
        return COVER_E_NET;
    size_t n = 0;
    int rc = http_get(url, (char *)buf, COVER_MAX_FILE, &n);
    if (rc) {
        free(buf);
        return rc == UPD_E_HTTP ? COVER_E_NONE : rc == UPD_E_SIZE ? COVER_E_IMAGE : COVER_E_NET;
    }
    OrbitImage img;
    rc = covers_decode(buf, n, &img);
    if (!rc) {
        image_free(&img);
        rc = covers_store(dir, id, buf, n);
    }
    free(buf);
    return rc;
}
int covers_fetch(const CoverConfig *cfg, const char *dir, const char *id, const char *pref) {
    char url[COVER_URL_MAX + 32];
    int rc = covers_url(cfg, id, url, sizeof(url));
    if (rc)
        return rc;
    const char *regions[COVER_TRIES];
    int n = strstr(cfg->url, "{REGION}") ? covers_regions(id, pref, regions, COVER_TRIES) : 0;
    if (!n)
        rc = fetch_one(url, dir, id);
    else
        for (int i = 0; i < n; i++) {
            if (covers_url_region(cfg, id, regions[i], url, sizeof(url)))
                continue;
            rc = fetch_one(url, dir, id);
            if (rc != COVER_E_NONE && rc != COVER_E_IMAGE)
                break; /* got it, or no network / disk: stop */
        }
    if (!rc) {
        char p[512];
        covers_nil_path(dir, id, p, sizeof(p));
        remove(p);
        covers_trim_disk(dir, COVER_DISK_MAX);
    } else if (rc != COVER_E_NET) /* no network: try again next start; no cover: wait a day */
        covers_mark_missing(dir, id);
    return rc;
}
