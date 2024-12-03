// SPDX-License-Identifier: BSD-3-Clause

#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

#include "consumer.h"
#include "ring_buffer.h"
#include "packet.h"
#include "utils.h"

void consumer_thread(so_consumer_ctx_t *ctx)
{
	so_packet_t packet;

	while (1) {
		ssize_t result = ring_buffer_dequeue(ctx->producer_rb, &packet, sizeof(packet));
		if (result == 0) {
			break;
		}

		so_action_t decision = process_packet(&packet);

		unsigned long hash = packet_hash(&packet);
		char log_entry[256];
		snprintf(log_entry, sizeof(log_entry), "%s %016lx %lu\n", RES_TO_STR(decision), hash,
				packet.hdr.timestamp);

		pthread_mutex_lock(&ctx->log_mutex);
		fputs(log_entry, ctx->log_file);
		pthread_mutex_unlock(&ctx->log_mutex);
	}

	return NULL;
}

static int setup_consumer_environment(FILE **log_file, pthread_mutex_t *log_mutex, const char *out_filename)
{
	*log_file = fopen(out_filename, "w");
	if (!*log_file) {
		perror("Failed to open output file");
		return -1;
	}

	if (pthread_mutex_init(log_mutex, NULL) != 0) {
		perror("Failed to initialize log mutex");
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
	pthread_mutex_t log_mutex;

	if (setup_consumer_environment(&log_file, &log_mutex, out_filename) != 0) {
		return -1;
	}

	so_consumer_ctx_t *contexts = malloc(num_consumers * sizeof(so_consumer_ctx_t));
	if (!contexts) {
		perror("Failed to allocate memory for consumer contexts");
		fclose(log_file);
		pthread_mutex_destroy(&log_mutex);
		return -1;
	}

	for (int i = 0; i < num_consumers; i++) {
		contexts[i].producer_rb = rb;
		contexts[i].log_file = log_file;
		contexts[i].log_mutex = log_mutex;

		if (pthread_create(&tids[i], NULL, (void *(*)(void *))consumer_thread, &contexts[i]) != 0) {
			perror("Failed to create thread");
			for (int j = 0; j < i; j++)
				pthread_cancel(tids[j]);
			free(contexts);
			fclose(log_file);
			pthread_mutex_destroy(&log_mutex);
			return -1;
		}
	}

	return num_consumers;
}
