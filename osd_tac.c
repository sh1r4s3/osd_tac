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

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

// Program name:
#define PROG_NAME "osd_tac"

// Defaults:
#define DEFAULT_TEXT "PROGRESS"
#define DEFAULT_FONT "-*-terminal-bold-*-*-*-18-140-*-*-*-*-*-*"
#define DEFAULT_COLOR "LawnGreen"
#define SOCKET_FILE "/tmp/" PROG_NAME ".socket"

// Logging macros.
#define ERR(format, ...) \
    do { \
      fprintf(stderr, __FILE__ ":%d  ERROR: " format "\n", __LINE__, ##__VA_ARGS__); \
      exit(EXIT_FAILURE); \
    } while (0)

static int sockfd = 0;
static char *text = NULL;
static char *font = NULL;
static char *color = NULL;
static xosd *osd;
static pthread_t draw_hdlr;
static int verbose = 0;
static int progress = 0;
static int timeout = 2; // 2 seconds

#define SHOW_PROGRESS_ON  1
#define SHOW_PROGRESS_OFF 0
static int show_progress = SHOW_PROGRESS_OFF;
#define PROGRESS_STR_LEN 13 // should be enough for +-2^31 + '%' + '\0'
static char progress_str[PROGRESS_STR_LEN] = {0};

static int run = 0;
static pthread_mutex_t run_mutex = PTHREAD_MUTEX_INITIALIZER;

void *draw_thread(void *) {
  do {
    osd = xosd_create(2 + show_progress);

    xosd_set_font(osd, font);
    xosd_set_colour(osd, color);
    xosd_set_shadow_offset(osd, 5);
    xosd_set_align(osd, XOSD_center);
    xosd_set_pos(osd, XOSD_middle);

    xosd_display(osd, 0, XOSD_string, text);
    xosd_display(osd, 1, XOSD_percentage, progress);
    if (show_progress) {
      snprintf(progress_str, PROGRESS_STR_LEN, "%d%%", progress);
      xosd_display(osd, 2, XOSD_string, progress_str);
    }

    xosd_set_timeout(osd, timeout);
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
  const char help_str[] = \
PROG_NAME " [options]\n"\
"Options:\n"\
"-h             help\n"\
"-p <progress>  progress to show (0..100)\n"\
"-f <font>      select font\n"\
"-t <text>      text above the progress bar\n"\
"-c <color>     color of the text and progress bar\n"\
"-T <timeout>   timeout for OSD in seconds\n"\
"-P             show percentage progress under the progress bar\n";
  puts(help_str);
}

int main(int argc, char **argv) {
  int opt;
  const char optstr[] = "hvt:p:f:c:T:P";
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
        text = strdup(optarg);
        if (!text) ERR("Can't duplicate string (%d)", errno);
        break;
      case 'f':
        font = strdup(optarg);
        if (!font) ERR("Can't duplicate string (%d)", errno);
        break;
      case 'c':
        color = strdup(optarg);
        if (!color) ERR("Can't duplicate string (%d)", errno);
        break;
      case 'T':
        timeout = atoi(optarg);
        break;
      case 'P':
        show_progress = 1;
        break;
      default:
        ERR("Unknown option: %c", opt);
        break;
    }
  }

  if (!text) {
    text = strdup(DEFAULT_TEXT);
    if (!text) ERR("Can't duplicate string (%d)", errno);
  }
  if (!font) {
    font = strdup(DEFAULT_FONT);
    if (!font) ERR("Can't duplicate string (%d)", errno);
  }

  if (!color) {
    color = strdup(DEFAULT_COLOR);
    if (!color) ERR("Can't duplicate string (%d)", errno);
  }

  int ret = pthread_create(&draw_hdlr, NULL, draw_thread, NULL);

  struct stat st;
  int exists = stat(SOCKET_FILE, &st);

  sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un addr = {
    AF_UNIX,
    SOCKET_FILE
  };

  if (exists == 0) {
    connect(sockfd, (const struct sockaddr *)&addr, sizeof(addr));
    write(sockfd, &progress, sizeof(progress));
    return EXIT_SUCCESS;
  }

  bind(sockfd, (const struct sockaddr *)&addr, sizeof(addr));
  while (1) {
    int ret = listen(sockfd, 16);
    int conn = accept(sockfd, NULL, NULL);
    if (conn < 0) {
      if (!run) break;
    }
    int nb = read(conn, &progress, sizeof(progress));
    if (nb != sizeof(progress)) {
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
  unlink(SOCKET_FILE);
  free(text);
  free(font);
  free(color);

  return EXIT_SUCCESS;
}
