#include "stdio.h"
#include "time.h"
#include "stdlib.h"
#include "assert.h"

#ifdef SNIPER_TEST
#include "sim_api.h"
#endif

#ifdef PAPI_TEST
#include "papi.h"
#include "../perftool.h"
#endif

#ifdef GEM5_TEST
#include "gem5/m5ops.h"
#endif

#ifndef NUM1
#define NUM1 100
#endif
#ifndef NUM2
#define NUM2 100
#endif
#ifndef ITER1
#define ITER1 100
#endif
#ifndef ITER2
#define ITER2 100
#endif

#define ABS(x) ((x) < 0 ? -(x) : (x))


// inline void clflush(volatile void *p)
// {
// 	__asm__ __volatile__ ("clflush (%0)" :: "r"(p) : );
// }

double **matrix_alloc(int size)
{
	double **ptr = (double **)malloc(sizeof(double *) * size);
	int i;
	for (i = 0; i < size; i++)
	{
		ptr[i] = (double *)malloc(sizeof(double) * size);
	}
	return ptr;
}

void matrix_free(double **ptr, int size)
{
	int i;
	for (i = 0; i < size; i++)
		free(ptr[i]);
	free(ptr);
}

void matrix_print(double **ptr, int size)
{
	int i, j;
	for(i = 0; i < size; i++) {
		for (j = 0; j < size; j++)
			printf("%lf\t", ptr[i][j]);
		printf("\n");
	}
}

void transpose(double **arr, int size)
{
	int i, j;
	for (i = 0; i < size; i++)
	{
		for (j = 0; j < i; j++)
		{
			double temp = arr[i][j];
			arr[i][j] = arr[j][i];
			arr[j][i] = temp;
		}
	}
}

void inverse(double **arr, int size)
{
	int p[size];
	int i, j, k;
	for (i = 0; i < size; i++)
	{
		p[i] = i;
	}
	// in-place LUP decomposition
	double **lu = matrix_alloc(size);
	for (i = 0; i < size; i++)
	{
		for (j = 0; j < size; j++)
			lu[i][j] = arr[i][j];
	}

	for (k = 0; k < size; k++)
	{
		double maxabs = 0;
		int maxidx = 0;
		for (i = k; i < size; i++)
		{
			if (ABS(lu[i][k]) > maxabs)
			{
				maxabs = ABS(lu[i][k]);
				maxidx = i;
			}
		}
		if (maxabs == 0)
		{
			assert(0 && "singular matrix\n");
		}
		int temp = p[k];
		p[k] = p[maxidx];
		p[maxidx] = temp;
		for (i = 0; i < size; i++)
		{
			double dtemp = lu[k][i];
			lu[k][i] = lu[maxidx][i];
			lu[maxidx][i] = dtemp;
		}
		for (i = k + 1; i < size; i++)
		{
			lu[i][k] /= lu[k][k];
			for (j = k + 1; j < size; j++)
			{
				lu[i][j] -= (lu[i][k] * lu[k][j]);
			}
		}
	}

	// LUP solve
	double *b = (double *)malloc(size * sizeof(double));
	for (i = 0; i < size; i++)
	{
		b[i] = 0;
	}
	b[0] = 1;
	double *y = (double *)malloc(size * sizeof(double));
	for (k = 0; k < size; k++)
	{
		if (k > 0)
		{
			b[k - 1] = 0;
			b[k] = 1;
		}
		for (i = 0; i < size; i++)
		{
			y[i] = b[p[i]];
			for (j = 0; j < i; j++)
			{
				y[i] -= (lu[i][j] * y[j]);
			}
		}
		for (i = size - 1; i >= 0; i--)
		{
			arr[i][k] = y[i];
			for (j = i + 1; j < size; j++)
			{
				arr[i][k] -= (lu[i][j] * arr[j][k]);
			}
			arr[i][k] /= lu[i][i];
		}
	}
	free(y);
	free(b);
	matrix_free(lu, size);
}

double **matrix_mul(double **result, double **x, double **y, int size)
{
	int i, j, k;
	for (i = 0; i < size; i++) {
		for (j = 0; j < size; j++) {
			result[i][j] = 0;
			for (k = 0; k < size; k++) {
				result[i][j] += x[i][k] * y[k][j];
			}
		}
	}
	return result;
}

int main()
{
	srand(0);
	double **arr = matrix_alloc(NUM1);
	double **c = matrix_alloc(NUM1);
	int i, j;
	for(i = 0; i < NUM1; i++) {
		for(j = 0; j < NUM1; j++) {
			arr[i][j] = rand()%20 + 1;
		}
	}
	for(i = 0; i < NUM1; i++) {
		for(j = 0; j < NUM1; j++) {
			c[i][j] = rand()%20 + 1;
		}
	}


#ifdef PAPI_TEST
	papi_event papi;
	papi = papi_start_all_count();
#endif
	// transpose the entire arr
	for (i = 0; i < ITER1; i++)
		transpose(arr, NUM2);
	// matrix_print(arr, NUM1);
#ifdef PAPI_TEST
	papi_get_all_count(papi);
#endif


	inverse(arr, NUM2);



#ifdef PAPI_TEST
	papi = papi_start_all_count();
#endif

	// matrix multiply
	double **result = matrix_alloc(NUM1);
	for (i = 0; i < ITER2; i++) {
		if (i % 2 == 0)
			matrix_mul(result, arr, c, NUM2);
		else
			matrix_mul(arr, result, c, NUM2);
	}
	// printf("result \n");
	// matrix_print(result, NUM1);

#ifdef PAPI_TEST
	papi_get_all_count(papi);
#endif

	return 0;
}
