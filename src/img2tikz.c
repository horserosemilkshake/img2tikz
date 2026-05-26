#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#define NANOSVG_IMPLEMENTATION
#include "../third_party/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../third_party/nanosvgrast.h"

typedef struct {
    int width;
    int height;
    unsigned char *rgba;
} Image;

typedef struct {
    float cell_pt;
    int max_side;
    int quant_step;
} Options;

typedef int (*CanLoadFn)(const char *path);
typedef int (*LoadImageFn)(const char *path, Image *img);

typedef struct {
    const char *name;
    CanLoadFn can_load;
    LoadImageFn load;
} ImageLoader;

#ifdef IMG2TIKZ_TEST_HOOKS
extern int img2tikz_test_force_can_load_any;
extern int img2tikz_test_force_svg_zero_size;
extern int img2tikz_test_force_svg_malloc_fail;
extern int img2tikz_test_force_svg_rasterizer_fail;
extern int img2tikz_test_force_downscale_fail;
#endif

#define MIN_INT(a, b) (((a) < (b)) ? (a) : (b))
#define MAX_INT(a, b) (((a) > (b)) ? (a) : (b))

#define ARG_IS(s) (strcmp(arg, (s)) == 0)
#define REQUIRE_OPTION_VALUE(flag)                                                     \
    do {                                                                               \
        if (i + 1 >= argc) {                                                           \
            fprintf(stderr, "Missing value for %s\n", (flag));                        \
            usage(argv[0]);                                                            \
            return 2;                                                                  \
        }                                                                              \
        ++i;                                                                           \
    } while (0)
#define CLOSE_OUTPUT_IF_NEEDED()                                                       \
    do {                                                                               \
        if (output_path) {                                                             \
            fclose(out);                                                               \
        }                                                                              \
    } while (0)

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [--cell <pt>] [--max-side <px>] [--quant-step <1..64>] <input_image> [output.tex]\n"
            "\n"
            "Supports: JPEG, PNG, WEBP, SVG\n"
            "  WEBP decode requires ffmpeg available in PATH.\n"
            "Defaults:\n"
            "  --cell 1.0       (TikZ unit in pt for each source pixel)\n"
            "  --max-side 256   (largest side after optional downscaling; 0 disables)\n"
            "  --quant-step 1   (color quantization step; higher means fewer colors)\n",
            prog);
}

static int str_ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static int has_ext(const char *path, const char *ext) {
    const char *dot = strrchr(path, '.');
    if (!dot || dot[1] == '\0') {
        return 0;
    }
    return str_ieq(dot + 1, ext);
}

static void free_image(Image *img) {
    if (img->rgba) {
        free(img->rgba);
    }
    img->rgba = NULL;
    img->width = 0;
    img->height = 0;
}

static int load_raster_image(const char *path, Image *img) {
    int w = 0;
    int h = 0;
    unsigned char *rgba = stbi_load(path, &w, &h, NULL, 4);
    if (!rgba) {
        fprintf(stderr, "Failed to read raster image '%s': %s\n", path, stbi_failure_reason());
        return 0;
    }
    img->width = w;
    img->height = h;
    img->rgba = rgba;
    return 1;
}

static int can_load_svg(const char *path) {
    return has_ext(path, "svg");
}

static int can_load_webp(const char *path) {
    return has_ext(path, "webp");
}

static int can_load_any(const char *path) {
    (void)path;
#ifdef IMG2TIKZ_TEST_HOOKS
    if (img2tikz_test_force_can_load_any >= 0) {
        return img2tikz_test_force_can_load_any;
    }
#endif
    return 1;
}

static int load_webp_via_ffmpeg(const char *path, Image *img) {
#ifdef _WIN32
    char temp_dir[MAX_PATH];
    char temp_stem[MAX_PATH];
    char temp_png[MAX_PATH];

    DWORD dir_len = GetTempPathA((DWORD)sizeof(temp_dir), temp_dir);
    if (dir_len == 0 || dir_len >= sizeof(temp_dir)) {
        fprintf(stderr, "Failed to locate temporary directory for WebP decode\n");
        return 0;
    }

    if (GetTempFileNameA(temp_dir, "i2t", 0, temp_stem) == 0) {
        fprintf(stderr, "Failed to create temporary file for WebP decode\n");
        return 0;
    }

    remove(temp_stem);
    if ((size_t)snprintf(temp_png, sizeof(temp_png), "%s.png", temp_stem) >= sizeof(temp_png)) {
        fprintf(stderr, "Temporary path too long for WebP decode\n");
        return 0;
    }

    char cmd[4096];
    if ((size_t)snprintf(cmd, sizeof(cmd),
                         "ffmpeg -hide_banner -loglevel error -y -i \"%s\" -frames:v 1 -f image2 -vcodec png \"%s\"",
                         path, temp_png) >= sizeof(cmd)) {
        fprintf(stderr, "ffmpeg command too long for WebP decode\n");
        remove(temp_png);
        return 0;
    }

    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr,
                "Failed to decode WebP '%s' with ffmpeg (exit code %d). "
                "Install ffmpeg or convert WebP to PNG/JPEG first.\n",
                path, rc);
        remove(temp_png);
        return 0;
    }

    int ok = load_raster_image(temp_png, img);
    remove(temp_png);
    if (!ok) {
        fprintf(stderr, "Temporary PNG decode failed after ffmpeg conversion\n");
        return 0;
    }
    return 1;
#else
    char temp_path[] = "/tmp/img2tikz-webp-XXXXXX";
    int fd = mkstemp(temp_path);
    if (fd < 0) {
        fprintf(stderr, "Failed to create temporary file for WebP decode: %s\n", strerror(errno));
        return 0;
    }
    close(fd);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Failed to start ffmpeg for WebP decode: %s\n", strerror(errno));
        remove(temp_path);
        return 0;
    }

    if (pid == 0) {
        execlp("ffmpeg", "ffmpeg",
               "-hide_banner", "-loglevel", "error", "-y",
               "-i", path,
               "-frames:v", "1",
               "-f", "image2",
               "-vcodec", "png",
               temp_path,
               (char *)NULL);
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        fprintf(stderr, "Failed while waiting for ffmpeg: %s\n", strerror(errno));
        remove(temp_path);
        return 0;
    }

    int rc = 1;
    if (WIFEXITED(status)) {
        rc = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        rc = 128 + WTERMSIG(status);
    }

    if (rc != 0) {
        fprintf(stderr,
                "Failed to decode WebP '%s' with ffmpeg (exit code %d). "
                "Install ffmpeg or convert WebP to PNG/JPEG first.\n",
                path, rc);
        remove(temp_path);
        return 0;
    }

    int ok = load_raster_image(temp_path, img);
    remove(temp_path);
    if (!ok) {
        fprintf(stderr, "Temporary PNG decode failed after ffmpeg conversion\n");
        return 0;
    }
    return 1;
#endif
}

static int load_svg_image(const char *path, Image *img) {
    NSVGimage *svg = nsvgParseFromFile(path, "px", 96.0f);
    if (!svg) {
        fprintf(stderr, "Failed to parse SVG '%s'\n", path);
        return 0;
    }

    int w = (int)(svg->width + 0.5f);
    int h = (int)(svg->height + 0.5f);
#ifdef IMG2TIKZ_TEST_HOOKS
    if (img2tikz_test_force_svg_zero_size) {
        w = 0;
        h = 0;
    }
#endif
    if (w <= 0 || h <= 0) {
        w = 512;
        h = 512;
    }

    unsigned char *rgba = NULL;
#ifdef IMG2TIKZ_TEST_HOOKS
    if (!img2tikz_test_force_svg_malloc_fail) {
        rgba = (unsigned char *)malloc((size_t)w * (size_t)h * 4u);
    }
#else
    rgba = (unsigned char *)malloc((size_t)w * (size_t)h * 4u);
#endif
    if (!rgba) {
        fprintf(stderr, "Out of memory while allocating SVG raster (%dx%d)\n", w, h);
        nsvgDelete(svg);
        return 0;
    }

    NSVGrasterizer *rast = NULL;
#ifdef IMG2TIKZ_TEST_HOOKS
    if (!img2tikz_test_force_svg_rasterizer_fail) {
        rast = nsvgCreateRasterizer();
    }
#else
    rast = nsvgCreateRasterizer();
#endif
    if (!rast) {
        fprintf(stderr, "Failed to create SVG rasterizer\n");
        free(rgba);
        nsvgDelete(svg);
        return 0;
    }

    nsvgRasterize(rast, svg, 0.0f, 0.0f, 1.0f, rgba, w, h, w * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(svg);

    img->width = w;
    img->height = h;
    img->rgba = rgba;
    return 1;
}

static int load_image_any(const char *path, Image *img) {
    static const ImageLoader loaders[] = {
        {"svg", can_load_svg, load_svg_image},
        {"webp", can_load_webp, load_webp_via_ffmpeg},
        {"raster", can_load_any, load_raster_image},
    };

    for (size_t i = 0; i < sizeof(loaders) / sizeof(loaders[0]); ++i) {
        if (loaders[i].can_load(path)) {
            return loaders[i].load(path, img);
        }
    }

    fprintf(stderr, "No loader available for input: %s\n", path);
    return 0;
}

static unsigned char *resize_nearest_rgba(const unsigned char *src, int sw, int sh, int dw, int dh) {
    unsigned char *dst = (unsigned char *)malloc((size_t)dw * (size_t)dh * 4u);
    if (!dst) {
        return NULL;
    }
    for (int y = 0; y < dh; ++y) {
        int sy = MIN_INT((int)((long long)y * sh / dh), sh - 1);
        for (int x = 0; x < dw; ++x) {
            int sx = MIN_INT((int)((long long)x * sw / dw), sw - 1);
            const unsigned char *sp = src + ((size_t)sy * (size_t)sw + (size_t)sx) * 4u;
            unsigned char *dp = dst + ((size_t)y * (size_t)dw + (size_t)x) * 4u;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }
    return dst;
}

static int maybe_downscale(Image *img, int max_side) {
#ifdef IMG2TIKZ_TEST_HOOKS
    if (img2tikz_test_force_downscale_fail) {
        return 0;
    }
#endif
    if (max_side <= 0) {
        return 1;
    }

    int w = img->width;
    int h = img->height;
    int longest = MAX_INT(w, h);
    if (longest <= max_side) {
        return 1;
    }

    double scale = (double)max_side / (double)longest;
    int nw = MAX_INT((int)(w * scale + 0.5), 1);
    int nh = MAX_INT((int)(h * scale + 0.5), 1);

    unsigned char *resized = resize_nearest_rgba(img->rgba, w, h, nw, nh);
    if (!resized) {
        fprintf(stderr, "Out of memory while resizing to %dx%d\n", nw, nh);
        return 0;
    }

    free(img->rgba);
    img->rgba = resized;
    img->width = nw;
    img->height = nh;
    return 1;
}

static void composite_over_white(const unsigned char *px, unsigned char *r, unsigned char *g, unsigned char *b) {
    int a = px[3];
    int rr = px[0];
    int gg = px[1];
    int bb = px[2];
    *r = (unsigned char)((rr * a + 255 * (255 - a) + 127) / 255);
    *g = (unsigned char)((gg * a + 255 * (255 - a) + 127) / 255);
    *b = (unsigned char)((bb * a + 255 * (255 - a) + 127) / 255);
}

static unsigned char quantize_channel(unsigned char v, int step) {
    if (step <= 1) {
        return v;
    }
    int q = ((int)v + step / 2) / step;
    q *= step;
    if (q > 255) {
        q = 255;
    }
    return (unsigned char)q;
}

static int write_tikz(FILE *out, const Image *img, float cell_pt, int quant_step) {
    int w = img->width;
    int h = img->height;
    const unsigned char *rgba = img->rgba;

    if (fprintf(out,
                "%% Auto-generated by img2tikz\n"
                "\\documentclass{article}\n"
                "\\usepackage[margin=0pt]{geometry}\n"
                "\\usepackage{tikz}\n"
                "\\pagestyle{empty}\n"
                "\\begin{document}\n"
                "\\noindent\n"
                "\\begin{tikzpicture}[x=%.6gpt,y=%.6gpt]\n"
                "  \\path[fill=white,draw=none] (0,0) rectangle (%d,%d);\n"
                "  \\begin{scope}[yscale=-1,yshift={-%d}]\n",
                cell_pt, cell_pt, w, h, h) < 0) {
        return 0;
    }

    for (int y = 0; y < h; ++y) {
        int x = 0;
        while (x < w) {
            const unsigned char *p0 = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            unsigned char r0 = 0;
            unsigned char g0 = 0;
            unsigned char b0 = 0;
            composite_over_white(p0, &r0, &g0, &b0);
            r0 = quantize_channel(r0, quant_step);
            g0 = quantize_channel(g0, quant_step);
            b0 = quantize_channel(b0, quant_step);

            int run_end = x + 1;
            while (run_end < w) {
                const unsigned char *pk = rgba + ((size_t)y * (size_t)w + (size_t)run_end) * 4u;
                unsigned char rk = 0;
                unsigned char gk = 0;
                unsigned char bk = 0;
                composite_over_white(pk, &rk, &gk, &bk);
                rk = quantize_channel(rk, quant_step);
                gk = quantize_channel(gk, quant_step);
                bk = quantize_channel(bk, quant_step);
                if (rk != r0 || gk != g0 || bk != b0) {
                    break;
                }
                ++run_end;
            }

            if (!(r0 == 255 && g0 == 255 && b0 == 255)) {
                if (fprintf(out,
                            "    \\path[fill={rgb,255:red,%u;green,%u;blue,%u},draw=none] (%d,%d) rectangle (%d,%d);\n",
                            (unsigned)r0, (unsigned)g0, (unsigned)b0,
                            x, y, run_end, y + 1) < 0) {
                    return 0;
                }
            }
            x = run_end;
        }
    }

    if (fprintf(out,
                "  \\end{scope}\n"
                "\\end{tikzpicture}\n"
                "\\end{document}\n") < 0) {
        return 0;
    }

    return 1;
}

static int convert_image_to_tikz(FILE *out, const char *input_path, const Options *opt) {
    Image img;
    img.width = 0;
    img.height = 0;
    img.rgba = NULL;

    if (!load_image_any(input_path, &img)) {
        return 0;
    }

    if (!maybe_downscale(&img, opt->max_side)) {
        free_image(&img);
        return 0;
    }

    if (!write_tikz(out, &img, opt->cell_pt, opt->quant_step)) {
        fprintf(stderr, "Failed while writing TikZ output\n");
        free_image(&img);
        return 0;
    }

    free_image(&img);
    return 1;
}

int main(int argc, char **argv) {
    Options opt;
    opt.cell_pt = 1.0f;
    opt.max_side = 256;
    opt.quant_step = 1;

    const char *input_path = NULL;
    const char *output_path = NULL;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (ARG_IS("--cell")) {
            REQUIRE_OPTION_VALUE("--cell");
            opt.cell_pt = (float)strtod(argv[i], NULL);
            if (!(opt.cell_pt > 0.0f)) {
                fprintf(stderr, "Invalid --cell value: %s\n", argv[i]);
                return 2;
            }
            continue;
        }
        if (ARG_IS("--max-side")) {
            REQUIRE_OPTION_VALUE("--max-side");
            char *end = NULL;
            long v = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || v < 0 || v > 20000) {
                fprintf(stderr, "Invalid --max-side value: %s\n", argv[i]);
                return 2;
            }
            opt.max_side = (int)v;
            continue;
        }
        if (ARG_IS("--quant-step")) {
            REQUIRE_OPTION_VALUE("--quant-step");
            char *end = NULL;
            long v = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || v < 1 || v > 64) {
                fprintf(stderr, "Invalid --quant-step value: %s\n", argv[i]);
                return 2;
            }
            opt.quant_step = (int)v;
            continue;
        }

        if (!input_path) {
            input_path = arg;
        } else if (!output_path) {
            output_path = arg;
        } else {
            fprintf(stderr, "Unexpected argument: %s\n", arg);
            usage(argv[0]);
            return 2;
        }
    }

    if (!input_path) {
        usage(argv[0]);
        return 2;
    }

    FILE *out = stdout;
    if (output_path) {
        out = fopen(output_path, "wb");
        if (!out) {
            fprintf(stderr, "Failed to open output file '%s': %s\n", output_path, strerror(errno));
            return 1;
        }
    }

    if (!convert_image_to_tikz(out, input_path, &opt)) {
        CLOSE_OUTPUT_IF_NEEDED();
        return 1;
    }

    CLOSE_OUTPUT_IF_NEEDED();
    return 0;
}
