/* LICENSE
 * Copyright 2017 Eric Melville
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * DESCRIPTION
 * portbounce - redirect traffic between ports
 * 
 * Networks and applications often restrict traffic. Sometimes it is useful
 * to accept traffic on an alternative port or from a wider set of addresses
 * and redirect it to the original target. This tool was originally used for
 * VNC debug on remote VM servers, but built in a generic way with other uses
 * in mind.
 * 
 * HISTORY
 * 0.1 - Initial version, listen on one port and shuttle data to the other
 * 
 * TODO
 * Handle partial sends
 * Clean up error reporting
 * Smooth out duplicated code between network directions
 * Reduce exit paths
 */

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>

#include <sys/time.h>
#include <sys/types.h>

#define BACKLOG 10
#define BUFFERSIZE 4096

void bouncetraffic(int sock, int target);

int
main(int argc, char *argv[])
{
	struct sockaddr_in srva, cli;
	socklen_t len;
	int lnum, tnum;
	int lsock, ssock;
	int pid;

	if (argc != 3) {
		printf("%s: need a listen port and target port\n", argv[0]);
		return 1;
	}

	lnum = atoi(argv[1]);
	tnum = atoi(argv[2]);

	if (lnum == 0 || tnum == 0) {
		printf("port numbers look bogus\n");
		return 1;
	}

	lsock = socket(AF_INET, SOCK_STREAM, 0);
	if (lsock < 0) {
		printf("socket allocation error\n");
		return 1;
	}

	bzero((char *) &srva, sizeof(srva));
	srva.sin_family = AF_INET;
	srva.sin_addr.s_addr = INADDR_ANY;
	srva.sin_port = htons(lnum);

	if (bind(lsock, (struct sockaddr *) &srva, sizeof(srva)) < 0) {
		printf("bind error\n");
		return 1;
	}

	listen(lsock, BACKLOG);
	len = sizeof(cli);

	for (;;) {
		ssock = accept(lsock, (struct sockaddr *) &cli, &len);

		if (ssock < 0) {
			printf("socket accept error\n");
			return 1;
		}

		pid = fork();
		if (pid < 0) {
			printf("fork error\n");
			return 1;
		}

		if (pid == 0) {
			/* child call */
			close(lsock);
			bouncetraffic(ssock, tnum);
			return 0;
		} else {
			close(ssock);
		}
	}

	return 0;
}

void
bouncetraffic(int sock, int target)
{
	char buf[BUFFERSIZE];
	struct sockaddr_in dst;
	struct hostent *h;
	fd_set fds, reset;
	int maxfd, outfd, ret, sent;

	outfd = socket(AF_INET, SOCK_STREAM, 0);
	if (outfd < 0) {
		printf("socket allocation error\n");
		return;
	}

	h = gethostbyname("localhost");
	bzero((char *) &dst, sizeof(dst));
	dst.sin_family = AF_INET;
	bcopy((char *)h->h_addr, (char *)&dst.sin_addr.s_addr, h->h_length);
	dst.sin_port = htons(target);

	ret = connect(outfd, (struct sockaddr *) &dst, sizeof(dst));
	if (ret < 0) {
		printf("got connection but nothing listening on other end\n");
		return;
	}

	if (sock > outfd)
		maxfd = sock + 1;
	else
		maxfd = outfd + 1;

	FD_ZERO(&reset);
	FD_SET(sock, &reset);
	FD_SET(outfd, &reset);

	for (;;) {
		fds = reset;
		ret = select(maxfd, &fds, NULL, NULL, NULL);
		if (ret == -1) {
			printf("error with select\n");
			return;
		}

		if (FD_ISSET(sock, &fds)) {
			bzero(buf, sizeof(buf));
			ret = recv(sock, buf, sizeof(buf), 0);
			if (ret > 0) {
				sent = send(outfd, buf, ret, 0);
				if (sent != ret) {
					printf("warning partial send\n");
				}
			} else {
				return;
			}
		}

		if (FD_ISSET(outfd, &fds)) {
			bzero(buf, sizeof(buf));
			ret = recv(outfd, buf, sizeof(buf), 0);
			if (ret > 0) {
				sent = send(sock, buf, ret, 0);
				if (sent != ret) {
					printf("warning partial send\n");
				}
			} else {
				return;
			}
		}

	}
}
