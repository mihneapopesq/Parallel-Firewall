// SPDX-License-Identifier: BSD-3-Clause

#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include "ring_buffer.h"
#include "utils.h"

static void rb_set_zero(so_ring_buffer_t *ring, size_t cap);
static void copy_data_ring(so_ring_buffer_t *ring, void *data, size_t size, int is_write);


int ring_buffer_init(so_ring_buffer_t *ring, size_t cap)
{
    ring->data = malloc(cap);
    DIE(ring->data == NULL, "Memory allocation failed");

    rb_set_zero(ring, cap);

    DIE(pthread_mutex_init(&ring->mutex, NULL) != 0, "Failed to initialize mutex");
    DIE(pthread_cond_init(&ring->not_empty, NULL) != 0, "Failed to initialize not_empty condition");
    DIE(pthread_cond_init(&ring->not_full, NULL) != 0, "Failed to initialize not_full condition");

    return 0;
}


ssize_t ring_buffer_enqueue(so_ring_buffer_t *ring, void *data, size_t size)
{
    DIE(pthread_mutex_lock(&ring->mutex) != 0, "Failed to lock mutex");

    while ((ring->cap - ring->len) < size && !ring->stop) {
        DIE(pthread_cond_wait(&ring->not_full, &ring->mutex) != 0, "Failed to wait on not_full condition");
    }

    if (ring->stop) {
        DIE(pthread_mutex_unlock(&ring->mutex) != 0, "Failed to unlock mutex");
        return -1;
    }

	if(size){
    	copy_data_ring(ring, data, size, 1);
		ring->write_pos = (ring->write_pos + size) % ring->cap;
		ring->len += size;
	}

    DIE(pthread_cond_signal(&ring->not_empty) != 0, "Failed to signal not_empty condition");
    DIE(pthread_mutex_unlock(&ring->mutex) != 0, "Failed to unlock mutex");

    return size;
}

ssize_t ring_buffer_dequeue(so_ring_buffer_t *ring, void *data, size_t size)
{
    DIE(pthread_mutex_lock(&ring->mutex) != 0, "Failed to lock mutex");

    while (size && ring->len < size && !ring->stop) {
        DIE(pthread_cond_wait(&ring->not_empty, &ring->mutex) != 0, "Failed to wait on not_empty condition");
    }

    if (ring->stop && ring->len == 0) {
        DIE(pthread_mutex_unlock(&ring->mutex) != 0, "Failed to unlock mutex");
        return 0;
    }

	if(size){
    copy_data_ring(ring, data, size, 0);
    ring->read_pos = (ring->read_pos + size) % ring->cap;
    ring->len -= size;
	}

    DIE(pthread_cond_signal(&ring->not_full) != 0, "Failed to signal not_full condition");
    DIE(pthread_mutex_unlock(&ring->mutex) != 0, "Failed to unlock mutex");

    return size;
}


void ring_buffer_destroy(so_ring_buffer_t *ring)
{
    free(ring->data);

    DIE(pthread_mutex_destroy(&ring->mutex) != 0, "Failed to destroy mutex");
    DIE(pthread_cond_destroy(&ring->not_empty) != 0, "Failed to destroy not_empty condition");
    DIE(pthread_cond_destroy(&ring->not_full) != 0, "Failed to destroy not_full condition");
}

void ring_buffer_stop(so_ring_buffer_t *ring)
{
    DIE(pthread_mutex_lock(&ring->mutex) != 0, "Failed to lock mutex");

    ring->stop = 1;

    DIE(pthread_cond_broadcast(&ring->not_empty) != 0, "Failed to broadcast not_empty condition");
    DIE(pthread_cond_broadcast(&ring->not_full) != 0, "Failed to broadcast not_full condition");

    DIE(pthread_mutex_unlock(&ring->mutex) != 0, "Failed to unlock mutex");
}


static void rb_set_zero(so_ring_buffer_t *ring, size_t cap)
{
    ring->cap = cap;
    ring->len = 0;
    ring->read_pos = 0;
    ring->write_pos = 0;
    ring->stop = 0;
}

static void copy_data_ring(so_ring_buffer_t *ring, void *data, size_t size, int is_write)
{
    size_t space;
    size_t rb_data;

    if (is_write)
        space = ring->cap - ring->write_pos;
    else
        space = ring->cap - ring->read_pos;

    if (size > space)
        rb_data = space;
    else
        rb_data = size;
    

    if (is_write)
        memcpy(ring->data + ring->write_pos, data, rb_data);
    else
        memcpy(data, ring->data + ring->read_pos, rb_data);
}

