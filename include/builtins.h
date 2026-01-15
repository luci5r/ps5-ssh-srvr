#pragma once
#include <stddef.h>
#include <sys/types.h>

typedef int (*builtin_fn)(int argc, char** argv);

typedef struct builtin {
  const char* name;
  builtin_fn  fn;
  const char* help;
} builtin_t;

const builtin_t* builtin_table(size_t* count);
int builtin_is_pipeline_safe(const char* name);
int run_builtin_in_current(int argc, char** argv, pid_t listener_pid);