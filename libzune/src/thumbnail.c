/*
 * libzune — thumbnail.c
 * Album art, video poster, and photo thumbnail operations.
 *
 * Uses native PTP/MTP stack (mtp.h) instead of vendored libmtp.
 */

#include "zune_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Method 2 parks the full download in the temporary directory.  POSIX uses /tmp
 * directly; the Windows compatibility layer hands back a writable directory with
 * a trailing separator and the macro is a function call, so it goes in via "%s". */
#ifndef ZUUNED_TMPDIR
#define ZUUNED_TMPDIR "/tmp/"
#endif

#ifdef _WIN32
/* Method 2 asks system() to run a command, and system() runs whatever shell the
 * platform keeps behind it: /bin/sh on POSIX, cmd.exe on Windows.  cmd.exe does
 * not treat a single quote as quoting — it is an ordinary character, so a path
 * with a space in it splits into several arguments — and "2>/dev/null" is not a
 * redirect there either, it is a file named "dev\null" created under whatever the
 * current directory happens to be.  A cmd line built with POSIX syntax therefore
 * fails every time.
 *
 * cmd.exe quotes with double quotes and doubles an embedded one.  Refuse rather
 * than truncate: a path that does not fit is a path the resize cannot be
 * trusted with, and the caller's answer is the same "no thumbnail" it gets for
 * any other Method 2 failure. */
static int shell_quote_path(char *out, size_t out_len, const char *path) {
    size_t o = 0;
    if (out_len < 3) return -1;
    out[o++] = '"';
    for (const char *p = path; *p; p++) {
        if (*p == '"' && o + 3 > out_len) return -1;
        if (*p == '"') out[o++] = '"';
        if (o + 2 >= out_len) return -1;
        out[o++] = *p;
    }
    if (o + 2 > out_len) return -1;
    out[o++] = '"';
    out[o] = '\0';
    return 0;
}
#endif

/* ---- Album art (set) ---- */

int zune_brand(ZuneDevice *dev, uint32_t item_id,
                        const uint8_t *jpeg_data, size_t jpeg_len) {
    if (!dev || !jpeg_data || jpeg_len == 0) return -1;

    int ret = mtp_send_representative_sample(&dev->ptp, item_id,
                                             MTP_OFC_JPEG,
                                             0, 0,   /* width/height: let device handle it */
                                             jpeg_data, (uint32_t)jpeg_len);
    if (ret != 0) {
        fprintf(stderr, "[libzune] set_album_art failed for item %u\n", item_id);
        return -1;
    }
    fprintf(stderr, "[libzune] album art set for item %u (%zu bytes)\n",
            item_id, jpeg_len);
    return 0;
}

/* ---- Thumbnail fetch (no full download) ---- */

int zune_grab_thumb(ZuneDevice *dev, uint32_t item_id,
                        const char *cache_path) {
    if (!dev || !cache_path) return -1;

    /* Already cached? */
    struct stat st;
    if (stat(cache_path, &st) == 0 && st.st_size > 0)
        return 0;

    /* Get representative sample (uses GetThumb 0x100A) */
    uint8_t *data = NULL;
    uint32_t size = 0;
    int ret = mtp_get_representative_sample(&dev->ptp, item_id, &data, &size);
    if (ret == 0 && data && size > 0) {
        FILE *fp = fopen(cache_path, "wb");
        if (fp) {
            fwrite(data, 1, size, fp);
            fclose(fp);
            fprintf(stderr, "[libzune] got thumbnail for %u (%u bytes)\n",
                    item_id, size);
            free(data);
            return 0;
        }
    }
    free(data);

    /* No full download fallback — too slow for album art / video posters */
    fprintf(stderr, "[libzune] all thumbnail methods failed for %u\n", item_id);
    return -1;
}

/* ---- Photo thumbnail with full fallback chain ---- */

int zune_grab_photo_thumb(ZuneDevice *dev, uint32_t item_id,
                              const char *cache_path) {
    if (!dev || !cache_path) return -1;

    /* Already cached? */
    struct stat st;
    if (stat(cache_path, &st) == 0 && st.st_size > 0)
        return 0;

    /* Method 1: Representative sample (GetThumb 0x100A) */
    uint8_t *data = NULL;
    uint32_t size = 0;
    int ret = mtp_get_representative_sample(&dev->ptp, item_id, &data, &size);
    if (ret == 0 && data && size > 0) {
        FILE *fp = fopen(cache_path, "wb");
        if (fp) {
            fwrite(data, 1, size, fp);
            fclose(fp);
            fprintf(stderr, "[libzune] got representative sample for %u (%u bytes)\n",
                    item_id, size);
            free(data);
            return 0;
        }
    }
    free(data);

    /* Method 2: Download full file + resize to 200x200 */
    /* ZUUNED_TMPDIR alone can run past the CRT limit, and Method 2 also has to
     * leave room for the resize command built on top of it. */
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%szunelinux-photo-%u", ZUUNED_TMPDIR, item_id);
    ret = mtp_get_object_to_file(&dev->ptp, item_id, tmp);
    if (ret == 0) {
        char cmd[1024];
#ifdef __APPLE__
        /* macOS: sips — preserve aspect ratio, fit within 200x200 */
        snprintf(cmd, sizeof(cmd),
                 "sips --resampleHeightWidthMax 200 --setProperty format jpeg '%s' --out '%s' 2>/dev/null",
                 tmp, cache_path);
#elif defined(_WIN32)
        /* Windows: same ffmpeg command, spelled for cmd.exe.  ffmpeg must be
         * reachable on PATH — the app bundles no copy, so on a machine without
         * one Method 2 fails the same way it always did and only the
         * representative sample (Method 1) produces a thumbnail. */
        char in_q[512], out_q[512];
        if (shell_quote_path(in_q, sizeof(in_q), tmp) != 0
            || shell_quote_path(out_q, sizeof(out_q), cache_path) != 0) {
            fprintf(stderr,
                    "[libzune] thumbnail path too long for cmd line %u\n", item_id);
            remove(tmp);
            return -1;
        }
        int used = snprintf(cmd, sizeof(cmd),
                            "ffmpeg -y -i %s -vf "
                            "scale=200:200:force_original_aspect_ratio=decrease "
                            "%s >nul 2>&1",
                            in_q, out_q);
        /* A cmd.exe command line has a hard ceiling of its own; a truncated one
         * would launch ffmpeg with half an argument list and report a resize
         * failure that is really a path length. */
        if (used < 0 || (size_t)used >= sizeof(cmd)) {
            fprintf(stderr, "[libzune] thumbnail command too long for %u\n", item_id);
            remove(tmp);
            return -1;
        }
#else
        /* Linux: use ffmpeg */
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -y -i '%s' -vf "
                 "scale=200:200:force_original_aspect_ratio=decrease "
                 "'%s' 2>/dev/null",
                 tmp, cache_path);
#endif
        ret = system(cmd);
        remove(tmp);

        if (ret == 0) {
            fprintf(stderr, "[libzune] resized full file for thumbnail %u\n",
                    item_id);
            return 0;
        }
    } else {
        remove(tmp);
    }

    fprintf(stderr, "[libzune] all photo thumbnail methods failed for %u\n",
            item_id);
    return -1;
}
