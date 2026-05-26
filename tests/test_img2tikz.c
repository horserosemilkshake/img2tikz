#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#if defined(__GNUC__) || defined(__clang__)
#define MAYBE_UNUSED __attribute__((unused))
#else
#define MAYBE_UNUSED
#endif

static int g_fail_malloc = 0;
static int g_malloc_calls = 0;

static int g_force_fprintf_fail = 0;
static int g_fprintf_fail_at_call = -1;
static int g_fprintf_call_count = 0;

static int g_mock_mkstemp_fail = 0;
static int g_mock_close_rc = 0;
static int g_mock_fork_rc = 1;
static int g_mock_waitpid_mode = 0;
static int g_mock_waitpid_calls = 0;
static int g_mock_remove_calls = 0;
static int g_mock_mkstemp_missing_png = 0;

int img2tikz_test_force_can_load_any = -1;
int img2tikz_test_force_svg_zero_size = 0;
int img2tikz_test_force_svg_malloc_fail = 0;
int img2tikz_test_force_svg_rasterizer_fail = 0;
int img2tikz_test_force_downscale_fail = 0;

static jmp_buf g_exit_jmp;
static int g_expect_exit = 0;
static int g_exit_code = -1;

enum {
    WAITPID_EXIT_0 = 0,
    WAITPID_EXIT_7,
    WAITPID_SIGNAL_9,
    WAITPID_EINTR_THEN_EXIT_0,
    WAITPID_ERROR,
    WAITPID_STOPPED,
};

static void reset_mocks(void) {
    g_fail_malloc = 0;
    g_malloc_calls = 0;
    g_force_fprintf_fail = 0;
    g_fprintf_fail_at_call = -1;
    g_fprintf_call_count = 0;
    g_mock_mkstemp_fail = 0;
    g_mock_close_rc = 0;
    g_mock_fork_rc = 1;
    g_mock_waitpid_mode = WAITPID_EXIT_0;
    g_mock_waitpid_calls = 0;
    g_mock_remove_calls = 0;
    g_mock_mkstemp_missing_png = 0;
    g_expect_exit = 0;
    g_exit_code = -1;
    img2tikz_test_force_can_load_any = -1;
    img2tikz_test_force_svg_zero_size = 0;
    img2tikz_test_force_svg_malloc_fail = 0;
    img2tikz_test_force_svg_rasterizer_fail = 0;
    img2tikz_test_force_downscale_fail = 0;
}

static void *test_malloc(size_t n) {
    ++g_malloc_calls;
    if (g_fail_malloc) {
        return NULL;
    }
    return calloc(1, n);
}

static int test_fprintf(FILE *stream, const char *fmt, ...) {
    ++g_fprintf_call_count;
    if (g_force_fprintf_fail ||
        (g_fprintf_fail_at_call > 0 && g_fprintf_call_count == g_fprintf_fail_at_call)) {
        return -1;
    }

    va_list ap;
    va_start(ap, fmt);
    int rc = vfprintf(stream, fmt, ap);
    va_end(ap);
    return rc;
}

static int MAYBE_UNUSED test_mkstemp(char *tpl) {
    if (g_mock_mkstemp_fail) {
        errno = EACCES;
        return -1;
    }

    if (g_mock_mkstemp_missing_png) {
        strcpy(tpl, "/tmp/m.png");
    } else {
        strcpy(tpl, "/tmp/w.png");
    }
    return 10;
}

static int MAYBE_UNUSED test_close(int fd) {
    (void)fd;
    return g_mock_close_rc;
}

static pid_t MAYBE_UNUSED test_fork(void) {
    return (pid_t)g_mock_fork_rc;
}

static int MAYBE_UNUSED test_execlp(const char *file, const char *arg, ...) {
    (void)file;
    (void)arg;
    errno = ENOENT;
    return -1;
}

static void MAYBE_UNUSED test__exit(int status) {
    if (g_expect_exit) {
        g_exit_code = status;
        longjmp(g_exit_jmp, 1);
    }
    abort();
}

static pid_t MAYBE_UNUSED test_waitpid(pid_t pid, int *status, int options) {
    (void)options;
    ++g_mock_waitpid_calls;

    if (g_mock_waitpid_mode == WAITPID_EINTR_THEN_EXIT_0 && g_mock_waitpid_calls == 1) {
        errno = EINTR;
        return -1;
    }

    if (g_mock_waitpid_mode == WAITPID_ERROR) {
        errno = ECHILD;
        return -1;
    }

    if (g_mock_waitpid_mode == WAITPID_EXIT_7) {
        *status = 7 << 8;
        return pid;
    }

    if (g_mock_waitpid_mode == WAITPID_SIGNAL_9) {
        *status = 9;
        return pid;
    }

    if (g_mock_waitpid_mode == WAITPID_STOPPED) {
        *status = 0x7f;
        return pid;
    }

    *status = 0;
    return pid;
}

static int test_remove(const char *path) {
    (void)path;
    ++g_mock_remove_calls;
    return 0;
}

#define malloc test_malloc
#define fprintf test_fprintf
#define mkstemp test_mkstemp
#define close test_close
#define fork test_fork
#define execlp test_execlp
#define _exit test__exit
#define waitpid test_waitpid
#define remove test_remove
#define main img2tikz_program_main
#define IMG2TIKZ_TEST_HOOKS 1
#include "../src/img2tikz.c"
#undef main
#undef remove
#undef waitpid
#undef _exit
#undef execlp
#undef fork
#undef close
#undef mkstemp
#undef fprintf
#undef malloc

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                                  \
        }                                                                              \
    } while (0)

static int write_bytes(const char *path, const unsigned char *buf, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    size_t w = fwrite(buf, 1, n, f);
    fclose(f);
    return w == n;
}

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    size_t n = strlen(text);
    size_t w = fwrite(text, 1, n, f);
    fclose(f);
    return w == n;
}

static int test_small_helpers(void) {
    CHECK(str_ieq("PNG", "png") == 1);
    CHECK(str_ieq("abc", "ab") == 0);
    CHECK(str_ieq("ab", "abc") == 0);
    CHECK(has_ext("a.PNG", "png") == 1);
    CHECK(has_ext("a", "png") == 0);
    CHECK(has_ext("a.", "png") == 0);
    CHECK(can_load_svg("x.svg") == 1);
    CHECK(can_load_svg("x.png") == 0);
    CHECK(can_load_webp("x.webp") == 1);
    CHECK(can_load_webp("x.png") == 0);
    CHECK(can_load_any("anything") == 1);
    return 0;
}

static int test_free_and_quantize_and_composite(void) {
    Image img;
    unsigned char *p = malloc(16);
    CHECK(p != NULL);
    img.width = 2;
    img.height = 2;
    img.rgba = p;
    free_image(&img);
    CHECK(img.rgba == NULL);
    CHECK(img.width == 0);
    CHECK(img.height == 0);

    img.width = 1;
    img.height = 1;
    img.rgba = NULL;
    free_image(&img);
    CHECK(img.rgba == NULL);

    CHECK(quantize_channel(42, 1) == 42);
    CHECK(quantize_channel(42, 0) == 42);
    CHECK(quantize_channel(250, 16) == 255);
    CHECK(quantize_channel(17, 16) == 16);

    unsigned char px[4] = {0, 0, 0, 0};
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    composite_over_white(px, &r, &g, &b);
    CHECK(r == 255 && g == 255 && b == 255);

    unsigned char px2[4] = {100, 50, 200, 255};
    composite_over_white(px2, &r, &g, &b);
    CHECK(r == 100 && g == 50 && b == 200);
    return 0;
}

static int test_resize_and_downscale(void) {
    unsigned char src[16] = {
        10, 20, 30, 255,
        11, 21, 31, 255,
        12, 22, 32, 255,
        13, 23, 33, 255,
    };

    unsigned char *same = resize_nearest_rgba(src, 2, 2, 2, 2);
    CHECK(same != NULL);
    CHECK(same[0] == 10 && same[4] == 11 && same[8] == 12 && same[12] == 13);
    free(same);

    Image img;
    img.width = 2;
    img.height = 2;
    img.rgba = malloc(16);
    CHECK(img.rgba != NULL);
    memcpy(img.rgba, src, 16);

    CHECK(maybe_downscale(&img, 0) == 1);
    CHECK(img.width == 2 && img.height == 2);
    CHECK(maybe_downscale(&img, 2) == 1);
    CHECK(img.width == 2 && img.height == 2);

    CHECK(maybe_downscale(&img, 1) == 1);
    CHECK(img.width == 1 && img.height == 1);

    g_fail_malloc = 1;
    img.width = 4;
    img.height = 4;
    img.rgba = (unsigned char *)calloc(1, 4u * 4u * 4u);
    CHECK(img.rgba != NULL);
    CHECK(maybe_downscale(&img, 2) == 0);
    g_fail_malloc = 0;

    free(img.rgba);
    img.rgba = NULL;
    return 0;
}

static int test_write_tikz_paths(void) {
    unsigned char rgba[16] = {
        0, 0, 0, 255,
        255, 255, 255, 255,
        255, 0, 0, 128,
        255, 0, 0, 128,
    };
    Image img;
    img.width = 2;
    img.height = 2;
    img.rgba = rgba;

    FILE *f = tmpfile();
    CHECK(f != NULL);
    CHECK(write_tikz(f, &img, 1.0f, 8) == 1);
    fclose(f);

    g_force_fprintf_fail = 1;
    f = tmpfile();
    CHECK(f != NULL);
    CHECK(write_tikz(f, &img, 1.0f, 1) == 0);
    fclose(f);
    g_force_fprintf_fail = 0;
    return 0;
}

static int test_load_raster_and_svg_and_dispatch(void) {
    static const unsigned char tiny_png[] = {
        137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82,
        0, 0, 0, 1, 0, 0, 0, 1, 8, 4, 0, 0, 0, 181, 28, 12,
        2, 0, 0, 0, 11, 73, 68, 65, 84, 120, 218, 99, 252, 255, 31,
        0, 3, 3, 2, 0, 238, 254, 230, 132, 0, 0, 0, 0, 73, 69, 78,
        68, 174, 66, 96, 130,
    };

    CHECK(write_bytes("/tmp/tiny.png", tiny_png, sizeof(tiny_png)) == 1);
    CHECK(write_text("/tmp/tiny.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"1\"><rect width=\"1\" height=\"1\" fill=\"#00ff00\"/></svg>") == 1);
    CHECK(write_text("/tmp/tiny-h0.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"0\"><rect width=\"1\" height=\"0\" fill=\"#00ff00\"/></svg>") == 1);

    Image img;
    img.width = 0;
    img.height = 0;
    img.rgba = NULL;
    CHECK(load_raster_image("/tmp/tiny.png", &img) == 1);
    CHECK(img.width == 1 && img.height == 1);
    free_image(&img);
    CHECK(load_raster_image("/tmp/not-found.png", &img) == 0);

    CHECK(load_svg_image("/tmp/tiny.svg", &img) == 1);
    CHECK(img.width == 1 && img.height == 1);
    free_image(&img);
    CHECK(load_svg_image("/tmp/definitely-not-existing.svg", &img) == 0);

    img2tikz_test_force_svg_zero_size = 1;
    CHECK(load_svg_image("/tmp/tiny.svg", &img) == 1);
    CHECK(img.width == 512 && img.height == 512);
    free_image(&img);
    img2tikz_test_force_svg_zero_size = 0;

    CHECK(load_svg_image("/tmp/tiny-h0.svg", &img) == 1);
    CHECK(img.width == 512 && img.height == 512);
    free_image(&img);

    img2tikz_test_force_svg_malloc_fail = 1;
    CHECK(load_svg_image("/tmp/tiny.svg", &img) == 0);
    img2tikz_test_force_svg_malloc_fail = 0;

    img2tikz_test_force_svg_rasterizer_fail = 1;
    CHECK(load_svg_image("/tmp/tiny.svg", &img) == 0);
    img2tikz_test_force_svg_rasterizer_fail = 0;

    CHECK(load_image_any("/tmp/tiny.svg", &img) == 1);
    free_image(&img);
    CHECK(load_image_any("/tmp/tiny.png", &img) == 1);
    free_image(&img);

    img2tikz_test_force_can_load_any = 0;
    CHECK(load_image_any("/tmp/no-loader.xyz", &img) == 0);
    img2tikz_test_force_can_load_any = -1;
    return 0;
}

static int test_webp_loader_branches(void) {
    static const unsigned char tiny_png[] = {
        137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82,
        0, 0, 0, 1, 0, 0, 0, 1, 8, 4, 0, 0, 0, 181, 28, 12,
        2, 0, 0, 0, 11, 73, 68, 65, 84, 120, 218, 99, 252, 255, 31,
        0, 3, 3, 2, 0, 238, 254, 230, 132, 0, 0, 0, 0, 73, 69, 78,
        68, 174, 66, 96, 130,
    };

    CHECK(write_bytes("/tmp/w.png", tiny_png, sizeof(tiny_png)) == 1);

    Image img;
    img.width = 0;
    img.height = 0;
    img.rgba = NULL;

    reset_mocks();
    g_mock_mkstemp_fail = 1;
    CHECK(load_webp_via_ffmpeg("/tmp/a.webp", &img) == 0);

    reset_mocks();
    g_mock_fork_rc = -1;
    CHECK(load_webp_via_ffmpeg("/tmp/a.webp", &img) == 0);

    reset_mocks();
    g_expect_exit = 1;
    g_mock_fork_rc = 0;
    if (setjmp(g_exit_jmp) == 0) {
        (void)load_webp_via_ffmpeg("/tmp/a.webp", &img);
        CHECK(0 && "child path should longjmp through test__exit");
    }
    CHECK(g_exit_code == 127);

    reset_mocks();
    g_mock_waitpid_mode = WAITPID_ERROR;
    CHECK(load_webp_via_ffmpeg("/tmp/a.webp", &img) == 0);

    reset_mocks();
    g_mock_waitpid_mode = WAITPID_EINTR_THEN_EXIT_0;
    CHECK(load_webp_via_ffmpeg("/tmp/a.webp", &img) == 1);
    free_image(&img);

    reset_mocks();
    g_mock_waitpid_mode = WAITPID_EXIT_0;
    g_mock_mkstemp_missing_png = 1;
    CHECK(load_webp_via_ffmpeg("/tmp/a.webp", &img) == 0);

    reset_mocks();
    g_mock_waitpid_mode = WAITPID_EXIT_7;
    CHECK(load_webp_via_ffmpeg("/tmp/a.webp", &img) == 0);

    reset_mocks();
    g_mock_waitpid_mode = WAITPID_SIGNAL_9;
    CHECK(load_webp_via_ffmpeg("/tmp/a.webp", &img) == 0);

    reset_mocks();
    g_mock_waitpid_mode = WAITPID_STOPPED;
    CHECK(load_webp_via_ffmpeg("/tmp/a.webp", &img) == 0);

    return 0;
}

static int test_convert_image_to_tikz_paths(void) {
    CHECK(write_text("/tmp/ok.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"1\"><rect width=\"2\" height=\"1\" fill=\"#112233\"/></svg>") == 1);

    Options opt;
    opt.cell_pt = 1.0f;
    opt.max_side = 0;
    opt.quant_step = 1;

    FILE *f = tmpfile();
    CHECK(f != NULL);
    CHECK(convert_image_to_tikz(f, "/tmp/ok.svg", &opt) == 1);
    fclose(f);

    CHECK(convert_image_to_tikz(stdout, "/tmp/does-not-exist.png", &opt) == 0);

    img2tikz_test_force_downscale_fail = 1;
    opt.max_side = 1;
    CHECK(convert_image_to_tikz(stdout, "/tmp/ok.svg", &opt) == 0);
    img2tikz_test_force_downscale_fail = 0;
    opt.max_side = 0;

    g_force_fprintf_fail = 1;
    f = tmpfile();
    CHECK(f != NULL);
    CHECK(convert_image_to_tikz(f, "/tmp/ok.svg", &opt) == 0);
    fclose(f);
    g_force_fprintf_fail = 0;
    return 0;
}

static int test_main_argument_and_io_paths(void) {
    char *argv0[] = {"img2tikz", NULL};
    CHECK(img2tikz_program_main(1, argv0) == 2);

    char *argv1[] = {"img2tikz", "--cell", NULL};
    CHECK(img2tikz_program_main(2, argv1) == 2);

    char *argv2[] = {"img2tikz", "--cell", "0", "x.svg", NULL};
    CHECK(img2tikz_program_main(4, argv2) == 2);

    char *argv3[] = {"img2tikz", "--max-side", "-1", "x.svg", NULL};
    CHECK(img2tikz_program_main(4, argv3) == 2);

    char *argv3b[] = {"img2tikz", "--max-side", "20001", "x.svg", NULL};
    CHECK(img2tikz_program_main(4, argv3b) == 2);

    char *argv3c[] = {"img2tikz", "--max-side", "x", "x.svg", NULL};
    CHECK(img2tikz_program_main(4, argv3c) == 2);

    char *argv3d[] = {"img2tikz", "--max-side", "12x", "x.svg", NULL};
    CHECK(img2tikz_program_main(4, argv3d) == 2);

    char *argv4[] = {"img2tikz", "--quant-step", "65", "x.svg", NULL};
    CHECK(img2tikz_program_main(4, argv4) == 2);

    char *argv4d[] = {"img2tikz", "--quant-step", "0", "x.svg", NULL};
    CHECK(img2tikz_program_main(4, argv4d) == 2);

    char *argv4e[] = {"img2tikz", "--quant-step", "x", "x.svg", NULL};
    CHECK(img2tikz_program_main(4, argv4e) == 2);

    char *argv4f[] = {"img2tikz", "--quant-step", "8x", "x.svg", NULL};
    CHECK(img2tikz_program_main(4, argv4f) == 2);

    char *argv4b[] = {"img2tikz", "--max-side", NULL};
    CHECK(img2tikz_program_main(2, argv4b) == 2);

    char *argv4c[] = {"img2tikz", "--quant-step", NULL};
    CHECK(img2tikz_program_main(2, argv4c) == 2);

    char *argv5[] = {"img2tikz", "a", "b", "c", NULL};
    CHECK(img2tikz_program_main(4, argv5) == 2);

    CHECK(write_text("/tmp/main.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"1\"><rect width=\"1\" height=\"1\" fill=\"#000\"/></svg>") == 1);

    char *argv6[] = {"img2tikz", "/tmp/main.svg", "/definitely/no/such/dir/out.tex", NULL};
    CHECK(img2tikz_program_main(3, argv6) == 1);

    char *argv7[] = {"img2tikz", "--cell", "1.25", "--max-side", "16", "--quant-step", "8", "/tmp/main.svg", "/tmp/main_out.tex", NULL};
    CHECK(img2tikz_program_main(9, argv7) == 0);

    char *argv8[] = {"img2tikz", "/tmp/main.svg", NULL};
    CHECK(img2tikz_program_main(2, argv8) == 0);

    char *argv9[] = {"img2tikz", "/tmp/does-not-exist.png", "/tmp/main_fail_out.tex", NULL};
    CHECK(img2tikz_program_main(3, argv9) == 1);

    char *argv10[] = {"img2tikz", "/tmp/does-not-exist.png", NULL};
    CHECK(img2tikz_program_main(2, argv10) == 1);

    return 0;
}

static int test_write_tikz_remaining_error_branches(void) {
    unsigned char rgba_non_white[4] = {10, 20, 30, 255};
    Image img_non_white;
    img_non_white.width = 1;
    img_non_white.height = 1;
    img_non_white.rgba = rgba_non_white;

    unsigned char rgba_white[4] = {255, 255, 255, 255};
    Image img_white;
    img_white.width = 1;
    img_white.height = 1;
    img_white.rgba = rgba_white;

    FILE *f = tmpfile();
    CHECK(f != NULL);
    g_force_fprintf_fail = 0;
    g_fprintf_fail_at_call = 2;
    g_fprintf_call_count = 0;
    CHECK(write_tikz(f, &img_non_white, 1.0f, 1) == 0);
    fclose(f);

    f = tmpfile();
    CHECK(f != NULL);
    g_force_fprintf_fail = 0;
    g_fprintf_fail_at_call = 2;
    g_fprintf_call_count = 0;
    CHECK(write_tikz(f, &img_white, 1.0f, 1) == 0);
    fclose(f);

    g_fprintf_fail_at_call = -1;

    unsigned char rgba_gdiff[8] = {10, 20, 30, 255, 10, 21, 30, 255};
    Image img_gdiff;
    img_gdiff.width = 2;
    img_gdiff.height = 1;
    img_gdiff.rgba = rgba_gdiff;
    f = tmpfile();
    CHECK(f != NULL);
    CHECK(write_tikz(f, &img_gdiff, 1.0f, 1) == 1);
    fclose(f);

    unsigned char rgba_bdiff[8] = {10, 20, 30, 255, 10, 20, 31, 255};
    Image img_bdiff;
    img_bdiff.width = 2;
    img_bdiff.height = 1;
    img_bdiff.rgba = rgba_bdiff;
    f = tmpfile();
    CHECK(f != NULL);
    CHECK(write_tikz(f, &img_bdiff, 1.0f, 1) == 1);
    fclose(f);

    unsigned char rgba_almost_white[4] = {255, 255, 254, 255};
    Image img_almost_white;
    img_almost_white.width = 1;
    img_almost_white.height = 1;
    img_almost_white.rgba = rgba_almost_white;
    f = tmpfile();
    CHECK(f != NULL);
    CHECK(write_tikz(f, &img_almost_white, 1.0f, 1) == 1);
    fclose(f);

    return 0;
}

int main(void) {
    reset_mocks();

    if (test_small_helpers()) return 1;
    if (test_free_and_quantize_and_composite()) return 1;
    if (test_resize_and_downscale()) return 1;
    if (test_write_tikz_paths()) return 1;
    if (test_load_raster_and_svg_and_dispatch()) return 1;
    if (test_webp_loader_branches()) return 1;
    if (test_convert_image_to_tikz_paths()) return 1;
    if (test_main_argument_and_io_paths()) return 1;
    if (test_write_tikz_remaining_error_branches()) return 1;

    puts("All img2tikz tests passed.");
    return 0;
}