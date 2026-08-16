/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FQ_SPARSE_INTERPOLATION_H
#define FQ_SPARSE_INTERPOLATION_H

#include <flint/flint.h>
#include <flint/nmod.h>
#include <flint/nmod_poly.h>
#include <flint/nmod_mpoly.h>
#include <flint/nmod_mat.h>
#include <flint/nmod_vec.h>
#include <flint/nmod_poly_factor.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <gmp.h>
#include <limits.h>
#include <stdarg.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include <nmod_vec_extra.h>
#include "gf2n_field.h"

#define SPARSE_BASE_RECOVERY_ATTEMPTS 24
#define SPARSE_DEFAULT_START_BUDGET 32
#define SPARSE_ESTIMATE_CAP ((slong) (LONG_MAX / 4))
#define SPARSE_FAST_ROOT_THRESHOLD 64
#define SPARSE_FAST_ROOT_SPLIT_ATTEMPTS 12
#define SPARSE_BM_HIST_BINS 11

#define DEBUG 0
#define TIMING 1
#define DETAILED_TIMING 0

typedef struct {
    clock_t start;
    double elapsed;
} my_timer_t;

typedef struct {
    nmod_mpoly_struct** entries;
    slong rows;
    slong cols;
} poly_mat_t;

void timer_start(my_timer_t* t);
void timer_stop(my_timer_t* t);
void timer_print(my_timer_t* t, const char* label);

void poly_mat_init(poly_mat_t* mat, slong rows, slong cols, const nmod_mpoly_ctx_t ctx);
void poly_mat_clear(poly_mat_t* mat, const nmod_mpoly_ctx_t ctx);
void poly_mat_entry_set(poly_mat_t* mat, slong i, slong j, const nmod_mpoly_t poly, const nmod_mpoly_ctx_t ctx);
void poly_mat_det_2x2(nmod_mpoly_t det, const poly_mat_t* mat, const nmod_mpoly_ctx_t ctx);
void poly_mat_det(nmod_mpoly_t det, const poly_mat_t* mat, const nmod_mpoly_ctx_t ctx);

slong poly_max_total_degree(const nmod_mpoly_t poly, const nmod_mpoly_ctx_t ctx);
void poly_max_degrees_per_var(slong* max_degs, const nmod_mpoly_t poly, const nmod_mpoly_ctx_t ctx);

void BM(nmod_poly_t C, const mp_limb_t* s, slong N, nmod_t mod);
void Vinvert(mp_limb_t* c1, const nmod_poly_t c, const mp_limb_t* v,
             const mp_limb_t* a, slong n, nmod_t mod);

int DerivativeSparseInterpolatePolynomial(nmod_mpoly_t result,
                                         const nmod_mpoly_t f,
                                         slong n,
                                         nmod_t mod,
                                         const slong* max_degs_per_var,
                                         const nmod_mpoly_ctx_t mctx);

void ComputePolyMatrixDet(nmod_mpoly_t det_poly,
                         const poly_mat_t* A,
                         slong nvars,
                         mp_limb_t p,
                         const nmod_mpoly_ctx_t mctx);

void myrandpoly(nmod_mpoly_t f, slong n, slong T, slong D,
                nmod_t mod, const nmod_mpoly_ctx_t mctx);

#endif
