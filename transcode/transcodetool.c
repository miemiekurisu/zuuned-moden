/*
 * transcodetool — CLI harness for the Zuuned transcode layer (Phase 2 gate)
 *
 * Exercises every exported function against real media without the app:
 *
 *   transcodetool audio <in>                 → MP3 320 CBR (prints path)
 *   transcodetool video <in> <profile 0-3>   → Zune video (prints path)
 *   transcodetool probe <in>                 → duration + resolution
 *   transcodetool art   <in> <out.jpg>       → embedded album art
 *   transcodetool frame <in> <sec> <out.jpg> → video frame grab
 *   transcodetool retag <in.mp3> <albumartist> → ID3v2.3 rewrite (prints path)
 *
 * ZUUNED_HW_DECODE=0|1|2|3 picks the decode path a transcode uses (0 sw,
 * 1 auto → NVDEC then VAAPI then sw, 2 NVDEC only, 3 VAAPI only), which is the
 * same knob Settings' transcodeAccel drives in the app.  A gate has to reach both
 * the hardware path and the software fallback on one machine; the library default
 * is auto and shows it only one of them.
 */

#include "libav_transcode.h"
#include "id3_rewrite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>

/* The CRT hands main() its argv in the ANSI code page, so on a machine whose
 * ACP cannot spell the name every argument arrives as question marks and probe,
 * art and frame all fail on a file the application opens happily.  The library
 * is called with UTF-8 by the app itself (FrameThumbnailService,
 * LibraryService, AudioCopyFingerprint all pass toUtf8()), so for this tool to
 * gate what the app does its arguments have to be the same bytes: rebuild argv
 * from the wide command line as UTF-8, which is what QCoreApplication does for
 * the Qt-side tools. */
static char **utf8_argv(int *count)
{
    int wide_argc = 0;
    LPWSTR *wide = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (!wide || wide_argc <= 0)
        return NULL;

    char **vec = calloc((size_t)wide_argc + 1, sizeof(char *));
    if (!vec) {
        LocalFree(wide);
        return NULL;
    }
    for (int i = 0; i < wide_argc; i++) {
        const int need = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1,
                                             NULL, 0, NULL, NULL);
        if (need <= 0)
            goto fail;
        char *utf8 = malloc((size_t)need);
        if (!utf8)
            goto fail;
        if (WideCharToMultiByte(CP_UTF8, 0, wide[i], -1,
                                utf8, need, NULL, NULL) <= 0) {
            free(utf8);
            goto fail;
        }
        vec[i] = utf8;
    }
    vec[wide_argc] = NULL;
    LocalFree(wide);
    *count = wide_argc;
    return vec;

fail:
    for (int i = 0; i < wide_argc; i++)
        free(vec[i]);
    free(vec);
    LocalFree(wide);
    return NULL;
}
#endif

static void progress_cb(float fraction, void *userdata)
{
    (void)userdata;
    static int last_pct = -1;
    int pct = (int)(fraction * 100.0f);
    if (pct / 10 != last_pct / 10 || pct >= 100) {
        fprintf(stderr, "[progress] %d%%\n", pct);
        last_pct = pct;
    }
}

static int usage(void)
{
    fprintf(stderr,
        "usage:\n"
        "  transcodetool audio <in>\n"
        "  transcodetool video <in> <profile 0-3>\n"
        "  transcodetool probe <in>\n"
        "  transcodetool art <in> <out.jpg>\n"
        "  transcodetool frame <in> <seconds> <out.jpg>\n"
        "  transcodetool retag <in.mp3> <albumartist>\n");
    return 2;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    {
        int utf8_argc = 0;
        char **wide_argv = utf8_argv(&utf8_argc);
        if (wide_argv) {
            argv = wide_argv;
            argc = utf8_argc;
        }
    }
#endif
    if (argc < 3) return usage();
    const char *cmd = argv[1];
    const char *in = argv[2];

    const char *hw = getenv("ZUUNED_HW_DECODE");
    if (hw && *hw)
        zuuned_transcode_set_hw_decode(atoi(hw));

    if (strcmp(cmd, "audio") == 0) {
        char *out = zuuned_transcode_audio(in, NULL, progress_cb, NULL);
        if (!out) { fprintf(stderr, "FAIL: audio transcode\n"); return 1; }
        printf("%s\n", out);
        free(out);
        return 0;
    }

    if (strcmp(cmd, "video") == 0) {
        if (argc < 4) return usage();
        int profile = atoi(argv[3]);
        char *out = zuuned_transcode_video(in, profile, NULL, 0, 0, NULL,
                                           progress_cb, NULL);
        if (!out) { fprintf(stderr, "FAIL: video transcode (profile %d)\n", profile); return 1; }
        printf("%s\n", out);
        free(out);
        return 0;
    }

    if (strcmp(cmd, "probe") == 0) {
        double dur = 0;
        int w = 0, h = 0;
        int vi = zuuned_probe_video_info(in, &dur, &w, &h);
        double d2 = 0;
        int di = zuuned_probe_duration(in, &d2);
        if (vi != 0 && di != 0) { fprintf(stderr, "FAIL: probe\n"); return 1; }
        printf("duration=%.3f\n", dur > 0 ? dur : d2);
        if (w > 0) printf("video=%dx%d\n", w, h);
        return 0;
    }

    if (strcmp(cmd, "art") == 0) {
        if (argc < 4) return usage();
        if (zuuned_extract_art(in, argv[3], 400) != 0) {
            fprintf(stderr, "FAIL: no embedded art\n");
            return 1;
        }
        printf("%s\n", argv[3]);
        return 0;
    }

    if (strcmp(cmd, "frame") == 0) {
        if (argc < 5) return usage();
        if (zuuned_extract_frame(in, atof(argv[3]), argv[4], 400) != 0) {
            fprintf(stderr, "FAIL: frame extract\n");
            return 1;
        }
        printf("%s\n", argv[4]);
        return 0;
    }

    if (strcmp(cmd, "retag") == 0) {
        if (argc < 4) return usage();
        char *out = zuuned_retag_mp3(in, argv[3]);
        if (!out) { fprintf(stderr, "FAIL: retag\n"); return 1; }
        printf("%s\n", out);
        free(out);
        return 0;
    }

    return usage();
}
