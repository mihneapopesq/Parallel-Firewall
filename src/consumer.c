// SPDX-License-Identifier: BSD-3-Clause

#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>

#include "consumer.h"
#include "ring_buffer.h"
#include "packet.h"
#include "utils.h"

void write_to_log(sem_t *log_semaphore, int log_fd, const char *format,
				  const char *decision, unsigned long hash, unsigned long timestamp)
{
	sem_wait(log_semaphore);
	dprintf(log_fd, format, decision, hash, timestamp);
	sem_post(log_semaphore);
}

void *consumer_thread(void *arg)
{
	so_consumer_ctx_t *ctx = (so_consumer_ctx_t *)arg;
	so_packet_t packet;
	int log_fd = fileno((FILE *)ctx->log_file);

	if (log_fd == -1) {
		perror("Failed to get file descriptor from log file");
		return NULL;
	}

	const char *log_format = "%s %016lx %lu\n";

	while (1) {
		ssize_t result = ring_buffer_dequeue(ctx->producer_rb, &packet, sizeof(packet));

		if (result == 0)
			break;

		so_action_t decision = process_packet(&packet);

		unsigned long hash = packet_hash(&packet);

		write_to_log(&ctx->log_semaphore, log_fd, log_format, RES_TO_STR(decision), hash, packet.hdr.timestamp);
	}

	return NULL;
}

static int setup_consumer_environment(FILE **log_file, sem_t *log_semaphore, const char *out_filename)
{
	*log_file = fopen(out_filename, "w");
	if (!*log_file) {
		perror("Failed to open output file");
		return -1;
	}

	if (sem_init(log_semaphore, 0, 1) != 0) {
		perror("Failed to initialize log semaphore");
		fclose(*log_file);
		return -1;
	}

	return 0;
}

int create_consumers(pthread_t *tids,
					 int num_consumers,
					 struct so_ring_buffer_t *rb,
					 const char *out_filename)
{
	FILE *log_file = NULL;
	sem_t log_semaphore;

	if (setup_consumer_environment(&log_file, &log_semaphore, out_filename) != 0)
		return -1;

	so_consumer_ctx_t *contexts = malloc(num_consumers * sizeof(so_consumer_ctx_t));

	if (!contexts) {
		perror("Failed to allocate memory for consumer contexts");
		fclose(log_file);
		sem_destroy(&log_semaphore);
		return -1;
	}

	for (int i = 0; i < num_consumers; i++) {
		contexts[i].producer_rb = rb;
		contexts[i].log_file = (const char *)log_file;
		contexts[i].log_semaphore = log_semaphore;

		if (pthread_create(&tids[i], NULL, consumer_thread, &contexts[i]) != 0) {
			perror("Failed to create thread");
			for (int j = 0; j < i; j++)
				pthread_cancel(tids[j]);
			free(contexts);
			fclose(log_file);
			sem_destroy(&log_semaphore);
			return -1;
		}
	}

	return num_consumers;
}
