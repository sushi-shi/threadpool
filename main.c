#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "threadpool.h"
#include "queue.h"

	/*	Prototypes	*/

void read_lines(FILE *handle, char ***buffer, int *buf_len);

int compare(const void *a, const void *b);
void to_thread(void *args);

void mergesort(char** first, int first_len, char** second, int second_len, char** result);

int split_lines(const char* path, char ***buffer, int *buf_len);
int sort_lines(char **lines, size_t lines_num, int thread_num);
void print_lines(char **lines, int lines_num);
void merge_lines(char **lines, int lines_num, int chanks_num);


typedef struct __params {
	void *base;
	size_t num;
} t_params, *p_params;


void read_lines(FILE *handle, char ***buffer, int *buf_len)
{
	int i = 0;
	while (1) {
		if (i * (int)sizeof(char *) >= *buf_len) {
			*buf_len *= 2;
			*buffer = realloc(*buffer, *buf_len);
		}
		char *str = NULL;
		int len = 0;

		if (getline(&str, &len, handle) <= 0)
			break;

		(*buffer)[i] = str;
		i++;
	}
	*buffer = realloc(*buffer, i * sizeof(char *));
	*buf_len = i;
}

int compare(const void *a, const void *b) {
    const char *sa = *(const char**)a;
    const char *sb = *(const char**)b;

    return strcmp(sa, sb);
}



void to_thread(void *args)
{
	p_params params = (p_params)args;
	qsort(params->base, params->num, sizeof(char *), compare);
	free(args);
}

void mergesort(char** first, int first_len, char** second, int second_len, char** result)
{
	int i = 0;
	int j = 0;
	int k = 0;
	while (i < first_len && j < second_len)
		if (strcmp(first[i], second[j]) <= 0)
			result[k++] = first[i++];
		else
			result[k++] = second[j++];

	if (i == first_len)
		while (j < second_len)
			result[k++] = second[j++];
	else 
		while (i < first_len)
			result[k++] = first[i++];
}

int split_lines(const char* path, char ***buffer, int *buf_len)
{
	FILE *handle = fopen(path, "r");
	if (handle == NULL) {
		fprintf(stderr, "fopen: Couldn't open a file");
		return -1;
	}

	(*buf_len) = 5000;
	(*buffer) = malloc(*buf_len * sizeof(char *));
	read_lines(handle, buffer, buf_len);
	if (*buf_len == -1) {
		fprintf(stderr, "read_lines: Nothing to read\n");
		return -1;
	}
	fclose(handle);
	return 0;
}

int sort_lines(char **lines, size_t lines_num, int thread_num)
{
	threadpool tp = pool_create(thread_num);
	int per_thread = lines_num / thread_num;

	for (int i = 0; i < thread_num - 1 && per_thread != 0; i++)
	{
		p_params params = malloc(sizeof(t_params));
		if (params == NULL) {
			fprintf(stderr, "malloc: NULL\n");
			return -1;
		}

		params->base = &lines[i * per_thread];
		params->num = per_thread;

		pool_add_task(tp, to_thread, (void *)params);
		params = NULL;
	}
	// the last one has to get the residue
	p_params params = malloc(sizeof(t_params));
	if (params == NULL) {
		fprintf(stderr, "malloc: NULL\n");
		return -1;
	}
	params->base = &lines[per_thread * (thread_num - 1)];
	params->num = per_thread + lines_num % thread_num;
	pool_add_task(tp, to_thread, (void *)params);

	pool_wait(tp);
	pool_destroy(tp);
	return 0;

}

void print_lines(char **lines, int lines_num)
{
	for (int i = 0; i < lines_num; i++)
		printf("%s", lines[i]);
}

void merge_lines(char **lines, int lines_num, int chanks_num)
{
	if (chanks_num == 1)
		return;
	
	char **buffer = malloc(sizeof(char *) * lines_num);

	// can be done smarter, but I don't want to go into that
	int per_chank = lines_num / chanks_num;
	for (int i = 1; i < chanks_num; i++) {
		mergesort(lines, i * per_chank, &lines[i * per_chank], per_chank, buffer);

		for (int j = 0; j < (i + 1) * per_chank; j++)
			lines[j] = buffer[j];
	}
	int residue = lines_num % chanks_num;
	mergesort(lines, lines_num - residue,  &lines[lines_num - residue], residue, buffer);
	
	for (int j = 0; j < lines_num; j++)
		lines[j] = buffer[j];

	free(buffer);

}

int main(void)
{
	char **buffer = NULL;
	int buf_len = 0;
	int threads_num = 10;
	const char* path = "E:\\concurrency.hs";

	// blissfully ignoring all possible errors
	split_lines(path, &buffer, &buf_len);
	sort_lines(buffer, buf_len, threads_num);
	merge_lines(buffer, buf_len, threads_num);
	print_lines(buffer, buf_len);

	free(buffer);
	return 0;
}
