#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <wordexp.h>

#include "misc.h"

int write_all(int fd, const void* buf, size_t len)
{
	ssize_t status;
	size_t written = 0;

	while (written < len) {
		status = write(fd, buf + written, len - written);
		if (status < 0) {
			if (status != EINTR)
				return -1;
		} else {
			written += status;
		}
	}

	return 0;
}

int read_all(int fd, void* buf, size_t len)
{
	ssize_t status;
	size_t done = 0;

	while (done < len) {
		status = read(fd, buf + done, len - done);
		if (status < 0) {
			if (status != EINTR)
				return -1;
		} else if (status == 0) {
			return -1;
		} else {
			done += status;
		}
	}

	return 0;
}

void set_fd_nonblock(int fd, int nb)
{
	int flags = fcntl(fd, F_GETFL);

	if (flags == -1) {
		perror("fcntl");
		abort();
	}

	if (nb)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;

	if (fcntl(fd, F_SETFL, flags)) {
		perror("fcntl");
		abort();
	}
}

void set_fd_cloexec(int fd, int ce)
{
	int flags = fcntl(fd, F_GETFD);

	if (flags == -1) {
		perror("fcntl");
		abort();
	}

	if (ce)
		flags |= FD_CLOEXEC;
	else
		flags &= ~FD_CLOEXEC;

	if (fcntl(fd, F_SETFD, flags)) {
		perror("fcntl");
		abort();
	}
}

char* expand_word(const char* wd)
{
	char* ret;
	wordexp_t exp;

	/*
	 * OSX's wordexp(3) sadly just ignores these flags, but I guess we
	 * might as well try...
	 */
	if (wordexp(wd, &exp, WRDE_NOCMD|WRDE_UNDEF) || exp.we_wordc != 1)
		return NULL;

	ret = xstrdup(exp.we_wordv[0]);

	wordfree(&exp);

	return ret;
}
