#include <unistd.h>
#include <errno.h>

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