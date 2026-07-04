#ifndef LOGZILLA_H
#define LOGZILLA_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  LOGZILLA_INFO,
  LOGZILLA_WARN,
  LOGZILLA_ERROR,
  LOGZILLA_FATAL
} LogzillaLevel;

void logzilla_init(const char *filepath);
void logzilla_log(LogzillaLevel level, const char *file, int line,
                  const char *fmt, ...);
void logzilla_shutdown(void);

#define LOGZILLA_INFO(...)                                                     \
  logzilla_log(LOGZILLA_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOGZILLA_WARN(...)                                                     \
  logzilla_log(LOGZILLA_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOGZILLA_ERROR(...)                                                    \
  logzilla_log(LOGZILLA_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOGZILLA_FATAL(...)                                                    \
  logzilla_log(LOGZILLA_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // LOGZILLA_H
