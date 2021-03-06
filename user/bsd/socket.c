/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "priv.h"

Rock *_sock_rock;

Rock*
_sock_findrock(int fd, struct stat *dp)
{
	Rock *r;
	struct stat d;

	if(dp == 0)
		dp = &d;
	fstat(fd, dp);
	for(r = _sock_rock; r; r = r->next){
		if(r->inode == dp->st_ino
		&& r->dev == dp->st_dev)
			break;
	}
	return r;
}

Rock*
_sock_newrock(int fd)
{
	Rock *r;
	struct stat d;

	r = _sock_findrock(fd, &d);
	if(r == 0){
		r = malloc(sizeof(Rock));
		if(r == 0)
			return 0;
		r->dev = d.st_dev;
		r->inode = d.st_ino;
		r->other = -1;
		/* TODO: this is not thread-safe! */
		r->next = _sock_rock;
		_sock_rock = r;
	}
	memset(&r->raddr, 0, sizeof(r->raddr));
	memset(&r->addr, 0, sizeof(r->addr));
	r->reserved = 0;
	r->dev = d.st_dev;
	r->inode = d.st_ino;
	r->other = -1;
	return r;
}

/* For a ctlfd and a few other settings, it opens and returns the corresponding
 * datafd.  This will close cfd for you. */
int
_sock_data(int cfd, char *net, int domain, int stype, int protocol, Rock **rp)
{
	int n, fd;
	Rock *r;
	char name[Ctlsize];

	/* get the data file name */
	n = read(cfd, name, sizeof(name)-1);
	if(n < 0){
		close(cfd);
		errno = ENOBUFS;
		return -1;
	}
	name[n] = 0;
	n = strtoul(name, 0, 0);
	snprintf(name, sizeof name, "/net/%s/%d/data", net, n);

	/* open data file */
	fd = open(name, O_RDWR);
	close(cfd); /* close this no matter what */
	if(fd < 0){
		errno = ENOBUFS;
		return -1;
	}

	/* hide stuff under the rock */
	snprintf(name, sizeof name, "/net/%s/%d/ctl", net, n);
	r = _sock_newrock(fd);
	if(r == 0){
		errno = ENOBUFS;
		close(fd);
		return -1;
	}
	if(rp)
		*rp = r;
	memset(&r->raddr, 0, sizeof(r->raddr));
	memset(&r->addr, 0, sizeof(r->addr));
	r->domain = domain;
	r->stype = stype;
	r->protocol = protocol;
	strcpy(r->ctl, name);
	return fd;
}

int
socket(int domain, int stype, int protocol)
{
	Rock *r;
	int cfd, fd, n;
	int pfd[2];
	char *net;
	char msg[128];

	switch(domain){
	case PF_INET:
		/* get a free network directory */
		switch(stype){
		case SOCK_DGRAM:
			net = "udp";
			cfd = open("/net/udp/clone", O_RDWR);
			/* All BSD UDP sockets are in 'headers' mode, where each packet has
			 * the remote addr:port, local addr:port and other info. */
			if (!(cfd < 0)) {
				n = snprintf(msg, sizeof(msg), "headers");
				n = write(cfd, msg, n);
				if (n < 0) {
					perror("UDP socket headers failed");
					return -1;
				}
			}
			break;
		case SOCK_STREAM:
			net = "tcp";
			cfd = open("/net/tcp/clone", O_RDWR);
			break;
		default:
			errno = EPROTONOSUPPORT;
			return -1;
		}
		if(cfd < 0){
			return -1;
		}
		return _sock_data(cfd, net, domain, stype, protocol, 0);
	case PF_UNIX:
		if(pipe(pfd) < 0){
			return -1;
		}
		r = _sock_newrock(pfd[0]);
		r->domain = domain;
		r->stype = stype;
		r->protocol = protocol;
		r->other = pfd[1];
		return pfd[0];
	default:
		errno = EPROTONOSUPPORT;
		return -1;
	}
}

int
issocket(int fd)
{
	Rock *r;

	r = _sock_findrock(fd, 0);
	return (r != 0);
}

/*
 * probably should do better than this
 */
int getsockopt (int __fd, int __level, int __optname,
		       void *__restrict __optval,
		       socklen_t *__restrict __optlen)
{
	return -1;
}

int setsockopt (int __fd, int __level, int __optname,
		       __const void *__optval, socklen_t __optlen)
{
	return 0;
}

