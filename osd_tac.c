/*
 * Copyright (c) 2025 Nikita (sh1r4s3) Ermakov <sh1r4s3@mail.si-head.nl>
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <xosd.h>
#include <pthread.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <signal.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

// Program name:
#define PROG_NAME "osd_tac"

// Defaults:
#define DEFAULT_TEXT "PROGRESS"
#define DEFAULT_FONT "-*-terminal-bold-*-*-*-18-140-*-*-*-*-*-*"
#define DEFAULT_COLOR "LawnGreen"
#define DEFAULT_SOCKET_FILE "/tmp/" PROG_NAME ".socket"

// Logging macros.
#define ERR(format, ...) \
    do { \
      fprintf(stderr, __FILE__ ":%d  ERROR: " format "\n", __LINE__, ##__VA_ARGS__); \
      exit(EXIT_FAILURE); \
    } while (0)

enum EPROGRESS: int {
  HIDE_PROGRESS = 0,
  SHOW_PROGRESS
};

enum EMODE: int {
  SERVER,
  CLIENT
};

static int sockfd = 0;
static char *socket_file = NULL;
static xosd *osd;
static pthread_t draw_hdlr;
static int verbose = 0;
static enum EMODE mode = SERVER;
static struct sigaction custom_sigaction = {0};
static const int sig2handle[] = {SIGTERM, SIGSEGV, SIGINT, SIGABRT};

static struct __attribute__((packed)) TDATA {
  int progress;
  int timeout; // seconds
  enum EPROGRESS show_progress;
  int text_sz;
  int font_sz;
  int color_sz;
  char opts[0]; // text, font, color strings
} *data;

#define GET_TEXT(x) x->opts
#define GET_FONT(x) x->opts + x->text_sz
#define GET_COLOR(x) x->opts + x->text_sz + x->font_sz

#define PROGRESS_STR_LEN 13 // should be enough for +-2^31 + '%' + '\0'
static char progress_str[PROGRESS_STR_LEN] = {0};

static int run = 0;
static pthread_mutex_t run_mutex = PTHREAD_MUTEX_INITIALIZER;

void *draw_thread(void *) {
  do {
    osd = xosd_create(2 + data->show_progress /* an extra line for progress percentage */);

    xosd_set_font(osd, GET_FONT(data));
    xosd_set_colour(osd, GET_COLOR(data));
    xosd_set_shadow_offset(osd, 5);
    xosd_set_align(osd, XOSD_center);
    xosd_set_pos(osd, XOSD_middle);

    xosd_display(osd, 0, XOSD_string, GET_TEXT(data));
    xosd_display(osd, 1, XOSD_percentage, data->progress);
    if (data->show_progress) {
      snprintf(progress_str, PROGRESS_STR_LEN, "%d%%", data->progress);
      xosd_display(osd, 2, XOSD_string, progress_str);
    }

    xosd_set_timeout(osd, data->timeout);
    xosd_wait_until_no_display(osd);
    xosd_destroy(osd);

    pthread_mutex_lock(&run_mutex);
    if (!run) {
      pthread_mutex_unlock(&run_mutex);
      break;
    }
    run = 0;
    pthread_mutex_unlock(&run_mutex);
  } while (1);
  pthread_mutex_lock(&run_mutex);
  puts("exiting");
  run = 0;
  shutdown(sockfd, SHUT_RD);
  pthread_mutex_unlock(&run_mutex);
}

void print_help() {
  const char help_str[] =                                               \
PROG_NAME " [options]\n"                                                \
"Options:\n"                                                            \
"-h                  help\n"                                            \
"-p <progress>       progress to show (0..100)\n"                       \
"-f <font>           select font\n"                                     \
"-t <text>           text above the progress bar\n"                     \
"-c <color>          color of the text and progress bar\n"              \
"-T <timeout>        timeout for OSD in seconds\n"                      \
"-P                  show percentage progress under the progress bar\n" \
"-s <socket file>    path to the socket file\n";
  puts(help_str);
}

void free_resources() {
  unlink(socket_file);
  free(data);
  free(socket_file);
}

void sig_handler(int sig) {
  puts("sig handler");
  signal(sig, SIG_DFL);
  raise(sig);
  free_resources();
}

int main(int argc, char **argv) {
  int opt;
  char *text = NULL, *font = NULL, *color = NULL;
  int progress = 0;
  int timeout = 2; // seconds
  enum EPROGRESS show_progress = HIDE_PROGRESS;
  const char optstr[] = "hvt:p:f:c:T:Ps:";
  while ((opt = getopt(argc, argv, optstr)) != -1) {
    switch (opt) {
      case 'h':
        print_help();
        return EXIT_SUCCESS;
        break;
      case 'v':
        verbose = 1;
        break;
      case 'p':
        progress = atoi(optarg);
        break;
      case 't':
        text = optarg;
        break;
      case 'f':
        font = optarg;
        break;
      case 'c':
        color = optarg;
        break;
      case 'T':
        timeout = atoi(optarg);
        break;
      case 'P':
        show_progress = SHOW_PROGRESS;
        break;
      case 's':
        socket_file = strdup(optarg);
        if (!socket_file) ERR("Can't duplicate string (%d)", errno);
        break;
      default:
        ERR("Unknown option: %c", opt);
        break;
    }
  }

  if (!text) text = DEFAULT_TEXT;
  if (!font) font = DEFAULT_FONT;
  if (!color) color = DEFAULT_COLOR;

  int text_sz = strlen(text) + 1;
  int font_sz = strlen(font) + 1;
  int color_sz = strlen(color) + 1;
  data = malloc(sizeof(struct TDATA) + text_sz + font_sz + color_sz);
  if (!data) ERR("Can't allocate memory for data struct");

  data->progress = progress;
  data->show_progress = show_progress;
  data->timeout = timeout;
  data->text_sz = text_sz;
  data->font_sz = font_sz;
  data->color_sz = color_sz;
  strcpy(GET_TEXT(data), text);
  strcpy(GET_FONT(data), font);
  strcpy(GET_COLOR(data), color);

  if (!socket_file) {
    socket_file = strdup(DEFAULT_SOCKET_FILE);
    if (!socket_file) ERR("Can't duplicate string (%d)", errno);
  }

  int ret = pthread_create(&draw_hdlr, NULL, draw_thread, NULL);

  struct stat st;
  mode = stat(socket_file, &st) ? SERVER : CLIENT;

  sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un addr = {
    AF_UNIX,
    0
  };
  if (sizeof(addr.sun_path) < strlen(socket_file)) {
    ERR("socket pathname is too long");
  }
  strncpy(addr.sun_path, socket_file, sizeof(addr.sun_path) - 1);

  if (mode == CLIENT) {
    connect(sockfd, (const struct sockaddr *)&addr, sizeof(addr));
    write(sockfd, data, sizeof(struct TDATA) + data->text_sz + data->font_sz + data->color_sz);
    return EXIT_SUCCESS;
  }

  bind(sockfd, (const struct sockaddr *)&addr, sizeof(addr));

  // handle signals to cleanup the resources
  custom_sigaction.sa_handler = sig_handler;
  for (int isig = 0; isig < sizeof(sig2handle)/sizeof(int); ++isig) {
    if (sigaction(sig2handle[isig], &custom_sigaction, NULL) < 0) {
      ERR("Can't set signal handler for %d", sig2handle[isig]);
    }
  }

  while (1) {
    int ret = listen(sockfd, 16);
    int conn = accept(sockfd, NULL, NULL);
    if (conn < 0) {
      if (!run) break;
    }
    free(data);
    struct TDATA header = {0};
    int nb = read(conn, &header, sizeof(struct TDATA));
    data = malloc(sizeof(struct TDATA) + header.text_sz + header.font_sz + header.color_sz);
    memcpy(data, &header, sizeof(struct TDATA));
    nb += read(conn, data->opts, header.text_sz + header.font_sz + header.color_sz);
    if (nb != sizeof(struct TDATA) + data->text_sz + data->font_sz + data->color_sz) {
      ERR("wtf");
    }
    pthread_mutex_lock(&run_mutex);
    run = 1;
    pthread_mutex_unlock(&run_mutex);
    xosd_hide(osd);
    close(conn);
  }

  close(sockfd);
  pthread_join(draw_hdlr, NULL);
  free_resources();

  return EXIT_SUCCESS;
}
