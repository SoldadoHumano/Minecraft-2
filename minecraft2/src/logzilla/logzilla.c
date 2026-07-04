#include "logzilla.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE *g_log_file = NULL;

void logzilla_init(const char *filepath) {
  if (filepath) {
#ifdef _WIN32
    if (fopen_s(&g_log_file, filepath, "w") != 0) {
      fprintf(stderr, "[Logzilla] Failed to open log file: %s\n", filepath);
    }
#else
    g_log_file = fopen(filepath, "w");
    if (!g_log_file) {
      fprintf(stderr, "[Logzilla] Failed to open log file: %s\n", filepath);
    }
#endif
  }
}

void logzilla_log(LogzillaLevel level, const char *file, int line,
                  const char *fmt, ...) {
  time_t t = time(NULL);
  struct tm tm_info;
#ifdef _WIN32
  localtime_s(&tm_info, &t);
#else
  localtime_r(&t, &tm_info);
#endif

  char time_buf[26];
  strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_info);

  const char *level_str = "INFO";
  switch (level) {
  case LOGZILLA_INFO:
    level_str = "INFO";
    break;
  case LOGZILLA_WARN:
    level_str = "WARN";
    break;
  case LOGZILLA_ERROR:
    level_str = "ERROR";
    break;
  case LOGZILLA_FATAL:
    level_str = "FATAL";
    break;
  }

  // Write to standard output
  fprintf(stdout, "[%s] [%s] %s:%d - ", time_buf, level_str, file, line);
  va_list args;
  va_start(args, fmt);
  vfprintf(stdout, fmt, args);
  va_end(args);
  fprintf(stdout, "\n");
  fflush(stdout);

  // Write to file if initialized
  if (g_log_file) {
    fprintf(g_log_file, "[%s] [%s] %s:%d - ", time_buf, level_str, file, line);
    va_start(args, fmt);
    vfprintf(g_log_file, fmt, args);
    va_end(args);
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
  }
}

void logzilla_shutdown(void) {
  if (g_log_file) {
    fclose(g_log_file);
    g_log_file = NULL;
  }
}
