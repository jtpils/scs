#include "private.h"

static timer linsysTimer;
static pfloat totalSolveTime;

void BLAS(potrf)(char *uplo, blasint *n, pfloat *a, blasint * lda, blasint *info);
void BLAS(trsv)(char *uplo, char *trans, char *diag, blasint *n, pfloat *a, blasint *lda, pfloat *x, blasint *incx);

char * getLinSysMethod(Data * d, Priv * p) {
	char * tmp = scs_malloc(sizeof(char) * 32);
	sprintf(tmp, "dense-direct");
	return tmp;
}

char * getLinSysSummary(Priv * p, Info * info) {
	char * str = scs_malloc(sizeof(char) * 64);
	sprintf(str, "\tLin-sys: avg solve time: %1.2es\n", totalSolveTime / (info->iter + 1) / 1e3);
	totalSolveTime = 0;
	return str;
}

Priv * initPriv(Data * d) {
	blasint n = (blasint) d->n, m = (blasint) d->m, info;
	pfloat zero = 0.0, onef = 1.0;
	idxint k, j;
	pfloat * A = d->A->x;
	Priv * p = scs_malloc(sizeof(Priv));
	p->L = scs_calloc(n * n, sizeof(pfloat));

	BLAS(gemm)("Transpose", "NoTranspose", &n, &n, &m, &onef, A, &m, A, &m, &zero, p->L, &n);

	for (j = 0; j < n; j++) {
		p->L[j * n + j] += d->RHO_X;
	}
	BLAS(potrf)("Lower", &n, p->L, &n, &info);
	if (info != 0) {
		scs_free(p->L);
		scs_free(p);
		return NULL;
	}
	/* copy L into top half for faster solve steps */
	for (k = 0; k < n; ++k) {
		for (j = k + 1; j < n; ++j) {
			p->L[k + j * n] = p->L[j + k * n];
		}
	}
	totalSolveTime = 0.0;
	return p;
}

void freePriv(Priv * p) {
	scs_free(p->L);
	scs_free(p);
}

void solveLinSys(Data * d, Priv * p, pfloat * b, const pfloat * s, idxint iter) {
	/* returns solution to linear system */
	/* Ax = b with solution stored in b */
	pfloat * A = d->A->x;
	pfloat * L = p->L;
	blasint m = (blasint) d->m, n = (blasint) d->n, one = 1;
	pfloat onef = 1.0, negOnef = -1.0;
#ifdef EXTRAVERBOSE
	scs_printf("solving lin sys\n");
#endif
	tic(&linsysTimer);

	BLAS(gemv)("Transpose", &m, &n, &onef, A, &m, &(b[d->n]), &one, &onef, b, &one);

	/* Solve using forward-substitution, L c = b */
	BLAS(trsv)("Lower", "NoTranspose", "NonUnit", &n, L, &n, b, &one);
	/* Perform back-substitution, U x = c */
	BLAS(trsv)("Upper", "NoTranspose", "NonUnit", &n, L, &n, b, &one);

	BLAS(gemv)("NoTranspose", &m, &n, &onef, A, &m, b, &one, &negOnef, &(b[d->n]), &one);
	totalSolveTime += tocq(&linsysTimer);
#ifdef EXTRAVERBOSE
	scs_printf("finished solving lin sys\n");
#endif
}