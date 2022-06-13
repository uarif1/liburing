/* SPDX-License-Identifier: MIT */
/*
 * Simple test case showing using msgsnd and msgrcv through io_uring
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "liburing.h"

#define MAX_MSG	128

static const char str[] = "This is a test of msgsnd and msgrcv over io_uring!";

struct mymsgbuf {
	long mtype;
	char mtext[MAX_MSG];
};

static int msgsend(int qid, int msgtype)
{
	int ret;
	struct io_uring ring;
	struct io_uring_cqe *cqe;
	struct io_uring_sqe *sqe;
	struct mymsgbuf msg;

	msg.mtype = msgtype;
	snprintf(msg.mtext, sizeof(str), "%s", str);

	ret = io_uring_queue_init(1, &ring, 0);
	if (ret) {
		fprintf(stderr, "queue init failed: %d\n", ret);
		return 1;
	}

	sqe = io_uring_get_sqe(&ring);
	io_uring_prep_msgsnd(sqe, qid, &msg, sizeof(msg.mtext), 0);

	ret = io_uring_submit(&ring);
	if (ret <= 0) {
		fprintf(stderr, "submit failed: %d\n", ret);
		return 1;
	}

	ret = io_uring_wait_cqe(&ring, &cqe);
	if (cqe->res < 0) {
		fprintf(stderr, "%s: failed cqe: %d\n", __func__, cqe->res);
		return 1;
	}
	ret = cqe->res;
	io_uring_cqe_seen(&ring, cqe);

	return 0;
}

static int msgrecv(int qid, int msgtype, struct mymsgbuf *msg,
		   struct io_uring *recv_ring)
{
	int ret;
	struct io_uring_sqe *sqe;

	ret = io_uring_queue_init(1, recv_ring, 0);
	if (ret) {
		fprintf(stderr, "queue init failed: %d\n", ret);
		return 1;
	}

	sqe = io_uring_get_sqe(recv_ring);
	io_uring_prep_msgrcv(sqe, qid, msg, sizeof(msg->mtext), msgtype, 0);

	ret = io_uring_submit(recv_ring);
	if (ret <= 0) {
		fprintf(stderr, "submit failed: %d\n", ret);
		return 1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int qid, ret;
	struct mymsgbuf recv_msg;
	struct io_uring recv_ring;
	struct io_uring_cqe *recv_cqe;

	int msgtype = 1; /* random msgtype */
	int msgkey = 123; /* random msgkey */

	qid = msgget(msgkey, IPC_CREAT | 0666);
	if (qid == -1) {
		perror("msgget");
		return 1;
	}

	ret = msgrecv(qid, msgtype, &recv_msg, &recv_ring);
	if (ret)
		return ret;

	ret = msgsend(qid, msgtype);
	if (ret)
		return ret;

	ret = io_uring_wait_cqe(&recv_ring, &recv_cqe);
	if (recv_cqe->res < 0) {
		fprintf(stderr, "%s: failed cqe: %d\n", __func__, recv_cqe->res);
		return 1;
	}
	ret = recv_cqe->res;
	io_uring_cqe_seen(&recv_ring, recv_cqe);

	if (strncmp(str, recv_msg.mtext, MAX_MSG)) {
		fprintf(stderr, "string mismatch\n expected: %s\n received: %s\n",
			str, recv_msg.mtext);
		return 1;
	}

	return 0;
}
