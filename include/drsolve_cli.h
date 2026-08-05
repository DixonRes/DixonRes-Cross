#ifndef DRSOLVE_CLI_H
#define DRSOLVE_CLI_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <flint/flint.h>
#include <flint/ulong_extras.h>
#include <flint/fmpz.h>
#include <flint/fmpz_factor.h>
#include <flint/fq_nmod.h>
#include <ctype.h>
#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#ifdef _OPENMP
#include <omp.h>
#endif

#include "dixon_flint.h"
#include "dixon_interface_flint.h"
#include "fq_mvpoly.h"
#include "fq_unified_interface.h"
#include "fq_multivariate_interpolation.h"
#include "unified_mpoly_resultant.h"
#include "dixon_with_ideal_reduction.h"
#include "dixon_complexity.h"
#include "large_prime_system_solver.h"
#include "polynomial_system_solver.h"
#include "complex_solver.h"
#include "rational_system_solver.h"
#include "dixon_test.h"
#include "fmpq_acb_roots.h"

#ifdef _WIN32
#define DIXON_NULL_DEVICE "NUL"
#else
#define DIXON_NULL_DEVICE "/dev/null"
#endif

#define DEFAULT_OUTPUT_DIR "out"

void drsolve_cli_print_version(void);
void drsolve_cli_print_usage(const char *prog_name);
int drsolve_cli_main(int argc, char *argv[], const char *prog_name);

#endif
